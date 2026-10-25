/* **************************************************************************
Test_ASWToml_Base.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "Test_ASWToml_Base.h"
//---------------------------------------------------------------------------
#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWToml_Base.h"
#include "ASWToml_Document.h"
#include "ASWToml_Interface.h"
#include "ASWToml_Options.h"
#include "ASWToml_Path.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
#include "ASWToml_Version.h"
//---------------------------------------------------------------------------
#include "UT_Helper_FailingAlloc.h"
//---------------------------------------------------------------------------

// Neither copyable nor movable
static_assert(!std::is_copy_constructible_v<ASWToml::TASWBaseToml>, "TASWBaseToml shouldn't be copyable");
static_assert(!std::is_move_constructible_v<ASWToml::TASWBaseToml>, "TASWBaseToml shouldn't be movable");
static_assert(!std::is_copy_constructible_v<ASWToml::Detail::TMutex>, "TMutex shouldn't be copyable");
static_assert(!std::is_copy_constructible_v<ASWToml::Detail::TSharedMutex>, "TSharedMutex shouldn't be copyable");

// LoadText() swaps a document in after the point where an exception would make it change nothing
static_assert(std::is_nothrow_move_assignable_v<ASWToml::TASWTomlDocument>,
    "a TASWTomlDocument should move without throwing");

namespace ASWUnitTests
{

namespace
{

using ASWToml::EIntegerBase;
using ASWToml::ESeverity;
using ASWToml::ETomlCode;
using ASWToml::TASWBaseToml;
using ASWToml::TASWTomlDocument;
using ASWToml::TASWTomlLogCallback;
using ASWToml::TASWTomlMessage;
using ASWToml::TASWTomlOptions;
using ASWToml::TASWTomlPath;
using ASWToml::TASWTomlResult;
using ASWToml::TASWTomlValue;
using ASWToml::TMessageVector;
using ASWToml::TStringVector;

using TTable = TASWTomlValue::TKeyValueVector;

// A text with a file comment, keys with comments, a table and an array of tables
constexpr char const* SampleText =
    "# Settings of the app\n"
    "\n"
    "name = \"app\"  # The name\n"
    "port = 80\n"
    "\n"
    "# The window\n"
    "[window]\n"
    "width = 640\n"
    "# In pixels\n"
    "height = 480\n"
    "\n"
    "[[servers]]\n"
    "host = \"a\"\n"
    "port = 1\n";

// A text with a syntax error on line 2
constexpr char const* InvalidText =
    "[window]\n"
    "width = = 1\n";

// How long a test waits for another thread before it counts as stuck
constexpr std::chrono::seconds ThreadTimeout(10);


/////////////////////////////////////////////////////////////////////////////
// TFilelessToml
//
// A TASWBaseToml without files: Load(), Save() and SaveAs() fail
/////////////////////////////////////////////////////////////////////////////
class TFilelessToml : public TASWBaseToml
{
private:
    // The failure of Load(), Save() and SaveAs()
    static TASWTomlResult NoFiles();

public:
    TASWTomlResult Load(std::string_view filePath) override;
    TASWTomlResult Save() override;
    TASWTomlResult SaveAs(std::string_view filePath) override;
};


/////////////////////////////////////////////////////////////////////////////
// TLockCounters
//
// What the threads of Locks_WorkLikeStandardMutexes share: the locks, and the counters they guard
/////////////////////////////////////////////////////////////////////////////
struct TLockCounters
{
    ASWToml::Detail::TSharedMutex Shared;
    ASWToml::Detail::TMutex Plain;
    long long First = 0; // Guarded by Shared, as Second is
    long long Second = 0;
    long long PlainCount = 0; // Guarded by Plain
    std::atomic<long long> Unequal{ 0 }; // How often a reader saw First and Second differ
};


/////////////////////////////////////////////////////////////////////////////
// TRecorder
//
// A log callback that adds each message to a vector
/////////////////////////////////////////////////////////////////////////////
class TRecorder
{
private:
    TMessageVector* m_Messages;

public:
    explicit TRecorder(TMessageVector& messages);

    void operator()(TASWTomlMessage const& message) const;
};


/////////////////////////////////////////////////////////////////////////////
// TReentrant
//
// A log callback that, for the first message, calls the object it is the callback of: a GetValue() of a value that
// doesn't convert (whose Warning comes back to the callback while it runs), an edit, SetOptions() and ToText(). It
// adds each message's text, the value GetValue() gave and the text to a vector.
/////////////////////////////////////////////////////////////////////////////
class TReentrant
{
private:
    TASWBaseToml* m_Toml;
    TStringVector* m_Seen;

public:
    TReentrant(TASWBaseToml& toml, TStringVector& seen);

    void operator()(TASWTomlMessage const& message) const;
};


/////////////////////////////////////////////////////////////////////////////
// TReplacing
//
// A log callback that counts its calls and replaces itself with a TRecorder
/////////////////////////////////////////////////////////////////////////////
class TReplacing
{
private:
    TASWBaseToml* m_Toml;
    TMessageVector* m_Recorded;
    std::size_t* m_Calls;

public:
    TReplacing(TASWBaseToml& toml, TMessageVector& recorded, std::size_t& calls);

    void operator()(TASWTomlMessage const& message) const;
};


/////////////////////////////////////////////////////////////////////////////
// TThrowingRecorder
//
// A log callback that adds each message to a vector, then throws: a std::runtime_error for the first, an int after it
/////////////////////////////////////////////////////////////////////////////
class TThrowingRecorder
{
private:
    TMessageVector* m_Messages;

public:
    explicit TThrowingRecorder(TMessageVector& messages);

    void operator()(TASWTomlMessage const& message) const;
};

void LockReader(TLockCounters& counters, int iterations);
void LockWriter(TLockCounters& counters, int iterations);
TASWTomlDocument ParsedDocument(std::string_view text);
bool SameResult(TASWTomlResult const& first, TASWTomlResult const& second);
void SharedReader(ASWToml::Detail::TSharedMutex& shared, std::atomic<bool>& inside);

//---------------------------------------------------------------------------
/*
    LockReader

    Checks 'iterations' times, under the shared lock, that the two counters the writers increment together are equal.
*/
void LockReader(TLockCounters& counters, int iterations)
{
    for (int i = 0; i < iterations; ++i)
    {
        std::shared_lock<ASWToml::Detail::TSharedMutex> lock(counters.Shared);
        if (counters.First != counters.Second)
            ++counters.Unequal;
    }
}
//---------------------------------------------------------------------------
/*
    LockWriter

    Increments, 'iterations' times, the two counters under the exclusive lock and the third under the mutex, each with
    a yield between reading and writing it, so without the lock other writers' increments in between would be lost.
*/
void LockWriter(TLockCounters& counters, int iterations)
{
    for (int i = 0; i < iterations; ++i)
    {
        {
            std::lock_guard<ASWToml::Detail::TSharedMutex> lock(counters.Shared);
            long long const value = counters.First;
            std::this_thread::yield();
            counters.First = value + 1;
            std::this_thread::yield();
            ++counters.Second;
        }

        std::lock_guard<ASWToml::Detail::TMutex> lock(counters.Plain);
        long long const count = counters.PlainCount;
        std::this_thread::yield();
        counters.PlainCount = count + 1;
    }
}
//---------------------------------------------------------------------------
/*
    ParsedDocument

    A TASWTomlDocument that has parsed 'text' with the default options.
*/
TASWTomlDocument ParsedDocument(std::string_view text)
{
    TASWTomlDocument document;
    static_cast<void>(document.Parse(text));

    return document;
}
//---------------------------------------------------------------------------
/*
    SameResult

    Whether 'first' and 'second' have the same code, message and path.
*/
bool SameResult(TASWTomlResult const& first, TASWTomlResult const& second)
{
    return first.Code == second.Code && first.Message == second.Message && first.Path == second.Path;
}
//---------------------------------------------------------------------------
/*
    SharedReader

    Takes the shared lock 'shared' and sets 'inside' while it holds it.
*/
void SharedReader(ASWToml::Detail::TSharedMutex& shared, std::atomic<bool>& inside)
{
    std::shared_lock<ASWToml::Detail::TSharedMutex> lock(shared);
    inside = true;
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TFilelessToml
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TFilelessToml::Load(std::string_view /*filePath*/)
{
    return NoFiles();
}
//---------------------------------------------------------------------------
TASWTomlResult TFilelessToml::NoFiles()
{
    TASWTomlResult result;
    result.Code = ETomlCode::InvalidArgument;
    result.Message = "This test object has no files.";

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TFilelessToml::Save()
{
    return NoFiles();
}
//---------------------------------------------------------------------------
TASWTomlResult TFilelessToml::SaveAs(std::string_view /*filePath*/)
{
    return NoFiles();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TRecorder
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TRecorder::TRecorder(TMessageVector& messages)
    : m_Messages(&messages)
{
}
//---------------------------------------------------------------------------
void TRecorder::operator()(TASWTomlMessage const& message) const
{
    m_Messages->push_back(message);
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TReentrant
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TReentrant::TReentrant(TASWBaseToml& toml, TStringVector& seen)
    : m_Toml(&toml),
      m_Seen(&seen)
{
}
//---------------------------------------------------------------------------
void TReentrant::operator()(TASWTomlMessage const& message) const
{
    m_Seen->push_back(message.Text);
    if (m_Seen->size() > 1)
        return;

    std::string const value = m_Toml->GetValue("port", std::string("none"));
    m_Seen->push_back(value);
    static_cast<void>(m_Toml->SetValue("called", true));
    static_cast<void>(m_Toml->SetOptions(m_Toml->GetOptions()));
    m_Seen->push_back(m_Toml->ToText());
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TReplacing
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TReplacing::TReplacing(TASWBaseToml& toml, TMessageVector& recorded, std::size_t& calls)
    : m_Toml(&toml),
      m_Recorded(&recorded),
      m_Calls(&calls)
{
}
//---------------------------------------------------------------------------
void TReplacing::operator()(TASWTomlMessage const& /*message*/) const
{
    ++*m_Calls;
    m_Toml->SetLogCallback(TRecorder(*m_Recorded));
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TThrowingRecorder
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TThrowingRecorder::TThrowingRecorder(TMessageVector& messages)
    : m_Messages(&messages)
{
}
//---------------------------------------------------------------------------
void TThrowingRecorder::operator()(TASWTomlMessage const& message) const
{
    m_Messages->push_back(message);
    if (m_Messages->size() == 1)
        throw std::runtime_error("The callback failed.");

    throw 42;
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_BaseAccess
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_BaseAccess::TTest_ASWToml_BaseAccess()
    : inherited("ASWToml_BaseAccess_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_BaseAccess::~TTest_ASWToml_BaseAccess()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_BaseAccess::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_BaseAccess::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_BaseAccess::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_BaseAccess::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseAccess, Edits_FailureChangesNothing)
{
    // Arrange
    TFilelessToml toml;
    static_cast<void>(toml.LoadText(SampleText));
    TASWTomlDocument document = ParsedDocument(SampleText);

    // Act
    TASWTomlResult const renameMissing = toml.Rename("window.depth", "z");
    TASWTomlResult const renameExisting = toml.Rename("window.width", "height");
    TASWTomlResult const comment = toml.SetComment("missing", "Text");
    TASWTomlResult const trailing = toml.SetTrailingComment("name", "Two\nlines");
    TASWTomlResult const setValue = toml.SetValue("port.x", 1);
    TASWTomlResult const append = toml.Append("port", 1);

    // Assert
    CheckTrue(SameResult(document.Rename("window.depth", "z"), renameMissing) &&
        renameMissing.Code == ETomlCode::NotFound, "a missing key should fail as the document's Rename() does");
    CheckTrue(SameResult(document.Rename("window.width", "height"), renameExisting) &&
        renameExisting.Code == ETomlCode::AlreadyExists, "an existing name too");
    CheckTrue(SameResult(document.SetComment("missing", "Text"), comment) && comment.Code == ETomlCode::NotFound,
        "a comment of a missing key as the document's SetComment() does");
    CheckTrue(SameResult(document.SetTrailingComment("name", "Two\nlines"), trailing) &&
        trailing.Code == ETomlCode::InvalidValue, "a trailing comment with a line break as SetTrailingComment() does");
    CheckTrue(SameResult(document.SetValue("port.x", 1), setValue) && setValue.Code == ETomlCode::InvalidArgument,
        "a key under a value that isn't a table as SetValue() does");
    CheckTrue(SameResult(document.Append("port", 1), append) && append.Code == ETomlCode::InvalidArgument,
        "an append to a value that isn't an array as Append() does");
    CheckEquals(std::string(SampleText), toml.ToText(), "the text should be unchanged");
    CheckFalse(toml.IsModified(), "the object shouldn't be modified");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseAccess, Edits_MatchTheDocument)
{
    // Arrange
    TFilelessToml toml;
    static_cast<void>(toml.LoadText(SampleText));
    TASWTomlDocument document = ParsedDocument(SampleText);
    TASWTomlValue const server = TTable{ { "host", "b" }, { "port", 2 } };
    std::vector<TASWTomlResult> results;
    std::vector<TASWTomlResult> expected;

    // Act
    results.push_back(toml.SetValue("port", 8080));
    results.push_back(toml.SetValue("window.left", 10));
    results.push_back(toml.SetValue("new.key", "value"));
    results.push_back(toml.Append("servers", server));
    results.push_back(toml.Rename("window.height", "tall"));
    results.push_back(toml.SetComment("window", "The main window"));
    results.push_back(toml.SetTrailingComment("port", "TCP"));
    results.push_back(toml.SetComment("", "New file comment"));
    bool const removed = toml.Remove("servers[0]");
    expected.push_back(document.SetValue("port", 8080));
    expected.push_back(document.SetValue("window.left", 10));
    expected.push_back(document.SetValue("new.key", "value"));
    expected.push_back(document.Append("servers", server));
    expected.push_back(document.Rename("window.height", "tall"));
    expected.push_back(document.SetComment("window", "The main window"));
    expected.push_back(document.SetTrailingComment("port", "TCP"));
    expected.push_back(document.SetComment("", "New file comment"));
    bool const expectedRemoved = document.Remove("servers[0]");

    // Assert
    for (std::size_t i = 0; i < results.size(); ++i)
        CheckTrue(results[i].IsOk() && expected[i].IsOk(), "edit " + std::to_string(i) + " should succeed");

    CheckTrue(removed && expectedRemoved, "Remove() should do what the document's does");
    CheckEquals(document.ToText(), toml.ToText(), "the edits should change the text as they do the document's");
    CheckTrue(toml.IsModified(), "the object should be modified");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseAccess, GetDocument_IsACopy)
{
    // Arrange
    TFilelessToml toml;
    static_cast<void>(toml.LoadText(SampleText));

    // Act
    TASWTomlDocument document = toml.GetDocument();
    TASWTomlResult const result = document.SetValue("window.width", 1);

    // Assert
    CheckTrue(result.IsOk(), "the copy should be edited");
    CheckEquals(std::string(SampleText), toml.GetDocument().ToText(), "the copy should have the object's text");
    CheckEquals(640, toml.GetValue("window.width", 0), "editing the copy shouldn't change the object");
    CheckFalse(toml.IsModified(), "the object shouldn't be modified");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseAccess, LoadText_FailureChangesNothing)
{
    // Arrange
    TFilelessToml toml;
    static_cast<void>(toml.LoadText(SampleText));
    static_cast<void>(toml.SetValue("port", 8080));
    std::string const before = toml.ToText();

    // Act
    TASWTomlResult const syntax = toml.LoadText(InvalidText);
    TASWTomlResult const utf16 = toml.LoadText(std::string_view("\xFF\xFE" "a", 3));
    TASWTomlResult const duplicate = toml.LoadText("a = 1\na = 2\n");

    // Assert
    CheckEquals(static_cast<int>(ETomlCode::ParseFailed), static_cast<int>(syntax.Code),
        "a syntax error should be ParseFailed");
    CheckTrue(syntax.Line == 2 && syntax.Column != 0, "with the line and column of the error");
    CheckEquals(static_cast<int>(ETomlCode::UnsupportedEncoding), static_cast<int>(utf16.Code),
        "UTF-16 should be UnsupportedEncoding");
    CheckEquals(static_cast<int>(ETomlCode::DuplicateKey), static_cast<int>(duplicate.Code),
        "a key defined twice should be DuplicateKey");
    CheckEquals(before, toml.ToText(), "the document should be unchanged");
    CheckTrue(toml.IsModified(), "the modified flag should be unchanged");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseAccess, LoadText_ReplacesTheDocument)
{
    // Arrange
    TFilelessToml toml;
    static_cast<void>(toml.SetValue("old", 1));

    // Act
    TASWTomlResult const result = toml.LoadText(SampleText);

    // Assert
    CheckTrue(result.IsOk(), "the text should be loaded");
    CheckEquals(std::string(SampleText), toml.ToText(), "the text should be written back as it is");
    CheckFalse(toml.Contains("old"), "the old document should be gone");
    CheckFalse(toml.IsModified(), "a loaded document shouldn't be modified");
    CheckEquals(std::string(), toml.GetFilePath(), "there should still be no file path");

    // Act
    TASWTomlResult const empty = toml.LoadText("");

    // Assert
    CheckTrue(empty.IsOk(), "an empty text should be loaded");
    CheckEquals(std::string(), toml.ToText(), "an empty text should empty the document");
    CheckTrue(toml.GetKeyNames("").empty(), "with no keys");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseAccess, LoadText_UsesTheOptions)
{
    // Arrange
    TFilelessToml toml;
    TASWTomlOptions options;
    options.Reading.MaxNestingDepth = 2;
    static_cast<void>(toml.SetOptions(options));
    std::string const withBom = "\xEF\xBB\xBF" "a = [[1]]\n";

    // Act
    TASWTomlResult const tooDeep = toml.LoadText("a = [[[1]]]\n");
    TASWTomlResult const deepEnough = toml.LoadText(withBom);

    // Assert
    CheckEquals(static_cast<int>(ETomlCode::NestingTooDeep), static_cast<int>(tooDeep.Code),
        "the text should be read with the object's reading options");
    CheckTrue(deepEnough.IsOk(), "a text they allow should be loaded");
    CheckEquals(withBom, toml.ToText(), "and written with its writing options, which keep a byte order mark");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseAccess, Locks_WorkLikeStandardMutexes)
{
    // Arrange
    // Writers increment two counters under the exclusive lock, and a third under the mutex (see LockWriter()); readers
    // check under the shared lock that the two are equal. A second shared lock is taken while one is held, which an
    // exclusive lock would block.
    TLockCounters counters;
    int const iterations = 5000;
    std::atomic<bool> secondReaderIn{ false };
    bool sharedTogether = false;

    // Act
    std::vector<std::thread> threads;
    for (int i = 0; i < 6; ++i)
    {
        if (i % 2 == 0)
            threads.emplace_back(LockWriter, std::ref(counters), iterations);
        else
            threads.emplace_back(LockReader, std::ref(counters), iterations);
    }

    for (std::thread& thread : threads)
        thread.join();

    {
        std::shared_lock<ASWToml::Detail::TSharedMutex> lock(counters.Shared);
        std::thread thread(SharedReader, std::ref(counters.Shared), std::ref(secondReaderIn));
        auto const deadline = std::chrono::steady_clock::now() + ThreadTimeout;
        while (!secondReaderIn && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();

        sharedTogether = secondReaderIn;
        lock.unlock();
        thread.join();
    }

    // Assert
    CheckEquals(3LL * iterations, counters.First, "no exclusive increment should be lost");
    CheckEquals(3LL * iterations, counters.PlainCount, "no increment under the mutex should be lost");
    CheckEquals(0LL, counters.Unequal.load(), "a reader should never see a writer halfway");
    CheckTrue(sharedTogether, "two threads should hold the shared lock together");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseAccess, Lookups_MatchTheDocument)
{
    // Arrange
    TFilelessToml toml;
    static_cast<void>(toml.LoadText(SampleText));
    TASWTomlDocument const document = ParsedDocument(SampleText);
    TStringVector const rootKeys = { "name", "port", "window", "servers" };
    TStringVector const windowKeys = { "width", "height" };

    // Act & Assert
    CheckTrue(toml.Contains("") && toml.Contains("window") && toml.Contains("window.width") &&
        toml.Contains("servers[0].host"), "the root, a table, a key and a key of an element should be there");
    CheckFalse(toml.Contains("missing") || toml.Contains("WINDOW") || toml.Contains("servers[1]") ||
        toml.Contains("a..b"), "a missing key, one in another case, an index past the end or an invalid path not");
    CheckTrue(toml.GetKeyNames("") == rootKeys, "the root's keys should be in the document's order");
    CheckTrue(toml.GetKeyNames("window") == windowKeys, "a table's too");
    CheckTrue(toml.GetKeyNames("port").empty() && toml.GetKeyNames("servers").empty() &&
        toml.GetKeyNames("missing").empty(), "a value that isn't a table should have none");
    CheckEquals(std::string("Settings of the app"), toml.GetComment(""), "the file comment should be the document's");
    CheckEquals(std::string("The window"), toml.GetComment("window"), "a table's comment");
    CheckEquals(std::string("In pixels"), toml.GetComment("window.height"), "a key's comment");
    CheckEquals(std::string("The name"), toml.GetTrailingComment("name"), "a trailing comment");
    CheckTrue(toml.GetValue("window", TASWTomlValue()) == *document.Find("window"), "a value as it is");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseAccess, New_IsEmptyWithTheDefaultOptions)
{
    // Arrange & Act
    TFilelessToml toml;

    // Assert
    TASWTomlOptions const defaults;
    TASWTomlOptions const options = toml.GetOptions();
    CheckEquals(std::string(), toml.ToText(), "a new object should have no text");
    CheckEquals(std::string(), toml.GetFilePath(), "a new object should have no file path");
    CheckFalse(toml.IsModified(), "a new object shouldn't be modified");
    CheckTrue(toml.Contains(""), "the root table should always be there");
    CheckTrue(toml.GetKeyNames("").empty(), "with no keys");
    CheckTrue(options.Reading.MaxNestingDepth == defaults.Reading.MaxNestingDepth &&
        options.Writing.CommentPrefix == defaults.Writing.CommentPrefix &&
        options.Saving.AtomicSave == defaults.Saving.AtomicSave, "the options should be the defaults");
    CheckTrue(toml.GetValueOptions().IntegerBase == EIntegerBase::Auto, "the value options should be the defaults");
    CheckEquals(std::string(ASWToml::Version), std::string(toml.GetVersionStr()),
        "the version should be the library's");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseAccess, OutOfMemory_EditChangesNothing)
{
    // Arrange
    if (!CanFailAllocations())
        Skip("allocations can't be made to fail in this build (see CanFailAllocations())");

    // The path and value are made before an allocation can fail
    TASWTomlPath const path = "new.key";
    std::string const value = "A value long enough not to fit in a string's own buffer";
    std::size_t failures = 0;

    // Act & Assert
    // Each allocation of the edit fails in turn, on a new object, until the edit makes no more
    for (std::size_t number = 1; number < 10000; ++number)
    {
        TFilelessToml toml;
        static_cast<void>(toml.LoadText(SampleText));
        std::string const which = " (allocation " + std::to_string(number) + ")";
        TASWTomlResult result;
        bool failed = false;
        {
            TFailingAlloc failingAlloc(number);
            result = toml.SetValue(path, value);
            failed = failingAlloc.HasFailed();
        }

        if (result.IsOk())
        {
            CheckTrue(toml.GetValue(path, std::string()) == value && toml.IsModified(),
                "an edit that succeeded should be complete" + which);
        }
        else
        {
            ++failures;
            CheckTrue(failed, "the edit should only fail when an allocation fails" + which);
            CheckEquals(static_cast<int>(ETomlCode::UnexpectedException), static_cast<int>(result.Code),
                "a failed allocation should be UnexpectedException" + which);
            CheckStartsWith(result.Message, std::string("An unexpected exception was thrown: "),
                "the message should say what was thrown" + which);
            CheckEquals(std::string("new.key"), result.Path, "the result should have the path" + which);
            CheckEquals(std::string(SampleText), toml.ToText(), "a failed edit should change nothing" + which);
            CheckFalse(toml.IsModified(), "a failed edit shouldn't set the modified flag" + which);
        }

        if (!failed)
            break;
    }

    CheckGreaterThan(failures, std::size_t(0), "at least one allocation should have failed");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseAccess, OutOfMemory_LoadTextChangesNothing)
{
    // Arrange
    if (!CanFailAllocations())
        Skip("allocations can't be made to fail in this build (see CanFailAllocations())");

    std::size_t failures = 0;

    // Act & Assert
    // As OutOfMemory_EditChangesNothing does
    for (std::size_t number = 1; number < 100000; ++number)
    {
        TFilelessToml toml;
        static_cast<void>(toml.SetValue("old", 1));
        std::string const before = toml.ToText();
        std::string const which = " (allocation " + std::to_string(number) + ")";
        TASWTomlResult result;
        bool failed = false;
        {
            TFailingAlloc failingAlloc(number);
            result = toml.LoadText(SampleText);
            failed = failingAlloc.HasFailed();
        }

        if (result.IsOk())
        {
            CheckEquals(std::string(SampleText), toml.ToText(), "a load that succeeded should have loaded the text" +
                which);
            CheckFalse(toml.IsModified(), "a loaded document shouldn't be modified" + which);
        }
        else
        {
            ++failures;
            CheckTrue(failed, "the load should only fail when an allocation fails" + which);
            CheckEquals(static_cast<int>(ETomlCode::UnexpectedException), static_cast<int>(result.Code),
                "a failed allocation should be UnexpectedException" + which);
            CheckEquals(before, toml.ToText(), "a failed load should change nothing" + which);
            CheckTrue(toml.IsModified(), "a failed load should keep the modified flag" + which);
        }

        if (!failed)
            break;
    }

    CheckGreaterThan(failures, std::size_t(0), "at least one allocation should have failed");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseAccess, OutOfMemory_SetOptionsChangesNothing)
{
    // Arrange
    if (!CanFailAllocations())
        Skip("allocations can't be made to fail in this build (see CanFailAllocations())");

    TASWTomlOptions options;
    options.Values.IntegerBase = EIntegerBase::Hexadecimal;
    std::size_t failures = 0;

    // Act & Assert
    // As OutOfMemory_EditChangesNothing does
    for (std::size_t number = 1; number < 100000; ++number)
    {
        TFilelessToml toml;
        static_cast<void>(toml.LoadText(SampleText));
        std::string const which = " (allocation " + std::to_string(number) + ")";
        TASWTomlResult result;
        bool failed = false;
        {
            TFailingAlloc failingAlloc(number);
            result = toml.SetOptions(options);
            failed = failingAlloc.HasFailed();
        }

        bool const hexadecimal = toml.GetValueOptions().IntegerBase == EIntegerBase::Hexadecimal;
        if (result.IsOk())
        {
            CheckTrue(hexadecimal, "SetOptions() that succeeded should have set the options" + which);
        }
        else
        {
            ++failures;
            CheckTrue(failed, "SetOptions() should only fail when an allocation fails" + which);
            CheckEquals(static_cast<int>(ETomlCode::UnexpectedException), static_cast<int>(result.Code),
                "a failed allocation should be UnexpectedException" + which);
            CheckFalse(hexadecimal, "a failed SetOptions() should keep the options" + which);
        }

        CheckEquals(std::string(SampleText), toml.ToText(), "the text should be unchanged" + which);
        CheckEquals(640, toml.GetValue("window.width", 0), "and read as it was" + which);
        if (!failed)
            break;
    }

    CheckGreaterThan(failures, std::size_t(0), "at least one allocation should have failed");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseAccess, Remove_ReturnsWhetherAnythingWasRemoved)
{
    // Arrange
    TFilelessToml toml;
    static_cast<void>(toml.LoadText(SampleText));

    // Act
    bool const removedKey = toml.Remove("window.width");
    bool const removedAgain = toml.Remove("window.width");
    bool const removedArray = toml.Remove("servers");
    bool const removedRoot = toml.Remove("");
    bool const removedInvalid = toml.Remove("a..b");

    // Assert
    CheckTrue(removedKey, "an existing key should be removed");
    CheckFalse(removedAgain, "a removed key shouldn't be removed again");
    CheckTrue(removedArray, "an array of tables should be removed");
    CheckFalse(removedRoot || removedInvalid, "the root or an invalid path shouldn't be removed");
    CheckFalse(toml.Contains("window.width") || toml.Contains("servers"), "the key and the array should be gone");
    CheckTrue(toml.IsModified(), "the object should be modified");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseAccess, SetOptions_InvalidOptionsChangeNothing)
{
    // Arrange
    TFilelessToml toml;
    static_cast<void>(toml.LoadText(SampleText));
    TASWTomlOptions options;
    options.Values.IntegerBase = EIntegerBase::Hexadecimal;
    options.Writing.CommentPrefix = "//";

    // Act
    TASWTomlResult const result = toml.SetOptions(options);

    // Assert
    CheckEquals(static_cast<int>(ETomlCode::InvalidOptions), static_cast<int>(result.Code),
        "invalid options should be InvalidOptions");
    CheckEquals(TASWTomlOptions().Writing.CommentPrefix, toml.GetOptions().Writing.CommentPrefix,
        "the options should be unchanged");
    CheckTrue(toml.GetValueOptions().IntegerBase == EIntegerBase::Auto, "all of them");
    CheckEquals(std::string(SampleText), toml.ToText(), "the text should be unchanged");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseAccess, SetOptions_ReadsTheTextAgain)
{
    // Arrange
    TFilelessToml toml;
    static_cast<void>(toml.LoadText("a = [[1]]\nb = 1\n"));
    static_cast<void>(toml.SetValue("b", 2));
    std::string const before = toml.ToText();
    TASWTomlOptions shallow;
    shallow.Reading.MaxNestingDepth = 1;
    TASWTomlOptions options;
    options.Reading.MaxNestingDepth = 2;
    options.Values.IntegerBase = EIntegerBase::Hexadecimal;

    // Act
    TASWTomlResult const refused = toml.SetOptions(shallow);
    std::size_t const depthAfterRefused = toml.GetOptions().Reading.MaxNestingDepth;
    TASWTomlResult const result = toml.SetOptions(options);

    // Assert
    CheckEquals(static_cast<int>(ETomlCode::NestingTooDeep), static_cast<int>(refused.Code),
        "options whose reading the text fails should be refused");
    CheckEquals(TASWTomlOptions().Reading.MaxNestingDepth, depthAfterRefused, "and leave the options as they were");
    CheckTrue(result.IsOk(), "options the text reads with should be set");
    CheckEquals(std::size_t(2), toml.GetOptions().Reading.MaxNestingDepth, "the object should have them");
    CheckTrue(toml.GetValueOptions().IntegerBase == EIntegerBase::Hexadecimal, "the value options too");
    CheckEquals(before, toml.ToText(), "the text should be unchanged");
    CheckEquals(2, toml.GetValue("b", 0), "and read as it is");
    CheckTrue(toml.IsModified(), "the modified flag should be kept");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseAccess, TypedValues_UseTheValueOptions)
{
    // Arrange
    TFilelessToml object;
    ASWToml::IASWToml& toml = object;
    static_cast<void>(toml.LoadText(SampleText));
    TASWTomlOptions options;
    options.Values.IntegerBase = EIntegerBase::Hexadecimal;
    static_cast<void>(toml.SetOptions(options));

    // Act
    TASWTomlResult const result = toml.SetValue("window.mask", 255);
    int const mask = toml.GetValue("window.mask", 0);
    int const width = toml.GetValue("window.width", 0);

    // Assert
    CheckTrue(result.IsOk(), "the value should be set");
    CheckContains(toml.ToText(), std::string("mask = 0xFF\n"), "an integer should be written as the object's value "
        "options say");
    CheckEquals(255, mask, "and read back");
    CheckEquals(640, width, "an int should be read");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_BaseCallback
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_BaseCallback::TTest_ASWToml_BaseCallback()
    : inherited("ASWToml_BaseCallback_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_BaseCallback::~TTest_ASWToml_BaseCallback()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_BaseCallback::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_BaseCallback::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_BaseCallback::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_BaseCallback::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseCallback, Callback_ExceptionIsDropped)
{
    // Arrange
    TFilelessToml toml;
    TMessageVector messages;
    toml.SetLogCallback(TThrowingRecorder(messages));

    // Act
    TASWTomlResult const first = toml.Rename("missing", "x");
    TASWTomlResult const second = toml.SetComment("missing", "x");

    // Assert
    CheckTrue(first.Code == ETomlCode::NotFound && second.Code == ETomlCode::NotFound,
        "the operations should fail as they would without the callback");
    CheckEquals(std::size_t(2), messages.size(), "each failure should be delivered, after the callback threw");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseCallback, Callback_MayCallTheObject)
{
    // Arrange
    // A lock held while the callback runs would make these calls deadlock (the CI's test timeout turns that into a
    // failure). The GetValue() it calls reports a Warning, to the callback, while it is still running.
    TFilelessToml toml;
    static_cast<void>(toml.LoadText("port = 80\n"));
    TStringVector seen;
    toml.SetLogCallback(TReentrant(toml, seen));

    // Act
    TASWTomlResult const failed = toml.Rename("missing", "x");

    // Assert
    CheckEquals(static_cast<int>(ETomlCode::NotFound), static_cast<int>(failed.Code), "the rename should fail");
    CheckEquals(std::size_t(4), seen.size(), "the callback should have run twice");
    if (seen.size() == 4)
    {
        CheckEquals(failed.Message, seen[0], "the failed rename should be reported");
        CheckEquals(std::string("The value 80 is an integer, not a string."), seen[1],
            "the callback's GetValue() should report its Warning, inside the callback");
        CheckEquals(std::string("none"), seen[2], "and give the default");
        CheckEquals(std::string("port = 80\ncalled = true\n"), seen[3],
            "the callback's edit should be in the document");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseCallback, Callback_MayReplaceItself)
{
    // Arrange
    TFilelessToml toml;
    TMessageVector recorded;
    std::size_t calls = 0;
    toml.SetLogCallback(TReplacing(toml, recorded, calls));

    // Act
    static_cast<void>(toml.Rename("missing", "x"));
    static_cast<void>(toml.Rename("missing", "y"));

    // Assert
    CheckEquals(std::size_t(1), calls, "the first callback should get the first message");
    CheckEquals(std::size_t(1), recorded.size(), "the callback it set should get the next one");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseCallback, FailedEdit_IsReportedAsAnError)
{
    // Arrange
    TFilelessToml toml;
    static_cast<void>(toml.LoadText(SampleText));
    TMessageVector messages;
    toml.SetLogCallback(TRecorder(messages));

    // Act
    TASWTomlResult const ok = toml.SetValue("window.width", 1);
    TASWTomlResult const results[] = {
        toml.Rename("window.depth", "z"), toml.Rename("window.width", "height"), toml.SetComment("missing", "Text"),
        toml.SetTrailingComment("name", "Two\nlines"), toml.SetValue("port.x", 1), toml.Append("port", 1),
        toml.SetValue("a..b", 1),
    };

    // Assert
    CheckTrue(ok.IsOk(), "the first edit should succeed");
    std::size_t const count = sizeof(results) / sizeof(results[0]);
    CheckEquals(count, messages.size(), "each failed edit, and only those, should be reported");
    for (std::size_t i = 0; i < count && i < messages.size(); ++i)
    {
        std::string const which = " (edit " + std::to_string(i) + ")";
        CheckFalse(results[i].IsOk(), "the edit should fail" + which);
        CheckTrue(messages[i].Severity == ESeverity::Error, "the message should be an Error" + which);
        CheckTrue(messages[i].Code == results[i].Code, "with the result's code" + which);
        CheckEquals(results[i].Message, messages[i].Text, "text" + which);
        CheckEquals(results[i].Path, messages[i].Path, "and path" + which);
        CheckEquals(std::string(), messages[i].FilePath, "without a file path, since there is none" + which);
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseCallback, GetValue_WarningIsReported)
{
    // Arrange
    TFilelessToml toml;
    static_cast<void>(toml.LoadText(SampleText));
    TMessageVector messages;
    toml.SetLogCallback(TRecorder(messages));

    // Act
    std::string const width = toml.GetValue("window.width", std::string("none"));

    // Assert
    CheckEquals(std::string("none"), width, "a value that doesn't convert should give the default");
    CheckEquals(std::size_t(1), messages.size(), "the value should be reported");
    if (messages.size() == 1)
    {
        CheckTrue(messages[0].Severity == ESeverity::Warning, "as a Warning");
        CheckTrue(messages[0].Code == ETomlCode::TypeMismatch, "with the converter's code");
        CheckEquals(std::string("The value 640 is an integer, not a string."), messages[0].Text, "and text");
        CheckEquals(std::string("window.width"), messages[0].Path, "and the path");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseCallback, LoadText_FailureIsReportedAsAnError)
{
    // Arrange
    TFilelessToml toml;
    TMessageVector messages;
    toml.SetLogCallback(TRecorder(messages));

    // Act
    TASWTomlResult const loaded = toml.LoadText(SampleText);
    TASWTomlResult const failed = toml.LoadText(InvalidText);

    // Assert
    CheckTrue(loaded.IsOk(), "the first text should be loaded");
    CheckEquals(std::size_t(1), messages.size(), "only the failure should be reported");
    if (messages.size() == 1)
    {
        CheckTrue(messages[0].Severity == ESeverity::Error && messages[0].Code == ETomlCode::ParseFailed,
            "as an Error with the result's code");
        CheckEquals(failed.Message, messages[0].Text, "its text");
        CheckTrue(messages[0].Line == 2 && messages[0].Column == failed.Column, "and its line and column");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseCallback, Lookups_ReportNothing)
{
    // Arrange
    TFilelessToml toml;
    static_cast<void>(toml.LoadText(SampleText));
    TMessageVector messages;
    toml.SetLogCallback(TRecorder(messages));
    std::string text;

    // Act
    static_cast<void>(toml.Contains("missing"));
    static_cast<void>(toml.GetKeyNames("missing"));
    static_cast<void>(toml.GetComment("missing"));
    static_cast<void>(toml.GetTrailingComment("missing"));
    static_cast<void>(toml.GetValue("missing", 1));
    static_cast<void>(toml.GetDocument());
    static_cast<void>(toml.ToText());
    TASWTomlResult const tryGet = toml.TryGetValue("window.width", text);
    bool const removed = toml.Remove("missing");

    // Assert
    CheckEquals(static_cast<int>(ETomlCode::TypeMismatch), static_cast<int>(tryGet.Code), "TryGetValue() should fail");
    CheckFalse(removed, "nothing should be removed");
    CheckEquals(std::size_t(0), messages.size(), "lookups, TryGetValue() and removals should report nothing");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseCallback, OutOfMemory_IsReportedAsAnError)
{
    // Arrange
    if (!CanFailAllocations())
        Skip("allocations can't be made to fail in this build (see CanFailAllocations())");

    TFilelessToml toml;
    TMessageVector messages;
    toml.SetLogCallback(TRecorder(messages));
    TASWTomlPath const path = "section.key";

    // Act
    TASWTomlResult result;
    TASWTomlResult loaded;
    {
        TFailingAlloc failingAlloc(1);
        result = toml.SetValue(path, "A value long enough not to fit in a string's own buffer");
    }

    {
        TFailingAlloc failingAlloc(1);
        loaded = toml.LoadText(SampleText);
    }

    // Assert
    CheckEquals(static_cast<int>(ETomlCode::UnexpectedException), static_cast<int>(result.Code),
        "a failed allocation should be UnexpectedException");
    CheckEquals(static_cast<int>(ETomlCode::UnexpectedException), static_cast<int>(loaded.Code), "in a load too");
    CheckEquals(std::size_t(2), messages.size(), "each failure should be reported");
    if (messages.size() == 2)
    {
        CheckTrue(messages[0].Severity == ESeverity::Error, "as an Error");
        CheckEquals(result.Message, messages[0].Text, "with the result's text");
        CheckEquals(std::string("section.key"), messages[0].Path, "and path");
        CheckTrue(messages[1].Severity == ESeverity::Error && messages[1].Text == loaded.Message, "each");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseCallback, SetLogCallback_EmptyTurnsReportingOff)
{
    // Arrange
    TFilelessToml toml;
    TMessageVector messages;
    toml.SetLogCallback(TRecorder(messages));
    static_cast<void>(toml.Rename("missing", "x"));

    // Act
    toml.SetLogCallback(TASWTomlLogCallback());
    TASWTomlResult const result = toml.Rename("missing", "x");
    static_cast<void>(toml.LoadText(InvalidText));

    // Assert
    CheckEquals(static_cast<int>(ETomlCode::NotFound), static_cast<int>(result.Code), "the rename should still fail");
    CheckEquals(std::size_t(1), messages.size(), "only the failure before the callback was removed should be reported");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseCallback, SetOptions_FailureIsReportedAsAnError)
{
    // Arrange
    TFilelessToml toml;
    static_cast<void>(toml.LoadText("a = [[1]]\n"));
    TMessageVector messages;
    toml.SetLogCallback(TRecorder(messages));
    TASWTomlOptions invalid;
    invalid.Writing.CommentPrefix = "//";
    TASWTomlOptions shallow;
    shallow.Reading.MaxNestingDepth = 1;

    // Act
    TASWTomlResult const invalidResult = toml.SetOptions(invalid);
    TASWTomlResult const shallowResult = toml.SetOptions(shallow);

    // Assert
    CheckEquals(std::size_t(2), messages.size(), "each failure should be reported");
    if (messages.size() == 2)
    {
        CheckTrue(messages[0].Severity == ESeverity::Error && messages[0].Code == ETomlCode::InvalidOptions &&
            messages[0].Text == invalidResult.Message, "invalid options as an Error with the result's code and text");
        CheckTrue(messages[1].Severity == ESeverity::Error && messages[1].Code == ETomlCode::NestingTooDeep &&
            messages[1].Text == shallowResult.Message, "options the text can't be read with too");
    }
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_BaseAccess, "Base")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_BaseCallback, "Base")
