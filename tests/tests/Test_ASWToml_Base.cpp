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
#include <filesystem>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <vector>
//---------------------------------------------------------------------------
#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWToml_Base.h"
#include "ASWToml_Document.h"
#include "ASWToml_File.h"
#include "ASWToml_Interface.h"
#include "ASWToml_Options.h"
#include "ASWToml_Path.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
#include "ASWToml_Version.h"
//---------------------------------------------------------------------------
#include "UT_Helper_FailingAlloc.h"
#include "UT_Helper_TempDir.h"
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
using ASWToml::PathToUtf8;
using ASWToml::TASWBaseToml;
using ASWToml::TASWTomlDocument;
using ASWToml::TASWTomlLogCallback;
using ASWToml::TASWTomlMessage;
using ASWToml::TASWTomlOptions;
using ASWToml::TASWTomlPath;
using ASWToml::TASWTomlResult;
using ASWToml::TASWTomlSavingOptions;
using ASWToml::TASWTomlValue;
using ASWToml::TMessageVector;
using ASWToml::TStringVector;
using ASWToml::Utf8ToPath;

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

// A text whose bytes a text-mode read or write, or a writer that normalizes, would change: a byte order mark, CRLF and
// a lone LF, tabs, odd spacing, TOML 1.1's escapes, time without seconds and multi-line inline table, and no line break
// at the end
constexpr char const* TrickyText =
    "\xEF\xBB\xBF# Settings\r\n"
    "\r\n"
    "name   =   'literal'\t# Spaced\r\n"
    "escaped = \"\\e[0m \\x41\"\r\n"
    "hex = 0xDEAD_beef\r\n"
    "when = 1979-05-27 07:32\r\n"
    "inline = {\r\n"
    "  a = 1,\r\n"
    "  b = [ 1, 2, ],\r\n"
    "}\r\n"
    "text = \"\"\"\r\n"
    "line one\r\n"
    "line two\"\"\"\r\n"
    "\r\n"
    "[ table . sub ]  # Odd spacing\r\n"
    "key=\"no spaces\"\n"
    "\r\n"
    "[[servers]]\r\n"
    "host = \"a\"\r\n"
    "# A comment at the end, without a line break";

// How long a test waits for another thread before it counts as stuck
constexpr std::chrono::seconds ThreadTimeout(10);

// The edit TEditingWriter makes while the file is written
enum class EEditDuringSave
{
    SetKey,
    RemoveKey,
    RemoveTable,
    FailedEdit,
};

// The operation TOverlapSeam starts on another thread while it reads or writes the file
enum class EOverlapStart
{
    None,
    Load,
    SaveAs,
};


/////////////////////////////////////////////////////////////////////////////
// TReadCall
//
// The arguments of a call of TSeamToml::ReadFile()
/////////////////////////////////////////////////////////////////////////////
struct TReadCall
{
    std::string const& FilePath;
    std::size_t MaxSize;
    std::string& Contents;
};


/////////////////////////////////////////////////////////////////////////////
// TWriteCall
//
// The arguments of a call of TSeamToml::WriteFile()
/////////////////////////////////////////////////////////////////////////////
struct TWriteCall
{
    std::string const& FilePath;
    std::string_view Contents;
    TASWTomlSavingOptions const& Options;
    TMessageVector& Warnings;
};


/////////////////////////////////////////////////////////////////////////////
// TSeamToml
//
// A TASWBaseToml whose ReadFile() and WriteFile() call OnRead and OnWrite instead of the defaults when they are set (to
// one of the seam classes below), so a test can see their arguments, call the object while a file is read or written,
// or make them fail
/////////////////////////////////////////////////////////////////////////////
class TSeamToml : public TASWBaseToml
{
public:
    std::function<TASWTomlResult (TReadCall const& call)> OnRead;
    std::function<TASWTomlResult (TWriteCall const& call)> OnWrite;

protected:
    TASWTomlResult ReadFile(std::string const& filePath, std::size_t maxSize, std::string& contents) override;
    TASWTomlResult WriteFile(std::string const& filePath, std::string_view contents,
        TASWTomlSavingOptions const& options, TMessageVector& warnings) override;

public:
    // TASWBaseToml's WriteFile() with the arguments of 'call', which writes the file
    TASWTomlResult DefaultWrite(TWriteCall const& call);
};


/////////////////////////////////////////////////////////////////////////////
// TSeamArguments
//
// The arguments TRecordingSeam recorded
/////////////////////////////////////////////////////////////////////////////
struct TSeamArguments
{
    std::string ReadPath;
    std::size_t ReadMaxSize = 0;
    std::string WritePath;
    std::string WriteText;
    TASWTomlSavingOptions WriteOptions;
};


/////////////////////////////////////////////////////////////////////////////
// TCountingSeam
//
// A seam (TSeamToml's OnRead or OnWrite) that counts its calls and succeeds, reading nothing
/////////////////////////////////////////////////////////////////////////////
class TCountingSeam
{
private:
    int* m_Calls;

public:
    explicit TCountingSeam(int& calls);

    TASWTomlResult operator()(TReadCall const& call) const;
    TASWTomlResult operator()(TWriteCall const& call) const;
};


/////////////////////////////////////////////////////////////////////////////
// TEditingReader
//
// A read seam that, while the file is read, gets window.width from the object and sets it to 800, then reads the text
// "loaded = 1\n"
/////////////////////////////////////////////////////////////////////////////
class TEditingReader
{
private:
    TASWBaseToml* m_Toml;
    int* m_Width;
    TASWTomlResult* m_Edit;

public:
    TEditingReader(TASWBaseToml& toml, int& width, TASWTomlResult& edit);

    TASWTomlResult operator()(TReadCall const& call) const;
};


/////////////////////////////////////////////////////////////////////////////
// TEditingWriter
//
// A write seam that makes the edit an EEditDuringSave names before it writes the file, so the file doesn't have it
/////////////////////////////////////////////////////////////////////////////
class TEditingWriter
{
private:
    TSeamToml* m_Toml;
    EEditDuringSave const* m_Edit;

public:
    TEditingWriter(TSeamToml& toml, EEditDuringSave const& edit);

    TASWTomlResult operator()(TWriteCall const& call) const;
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
// TOverlap
//
// What FileOperations_DontOverlap's seams share: the object, how many of them are reading or writing, whether two
// ever did at once, the operation to start on another thread from the next one, and that thread
/////////////////////////////////////////////////////////////////////////////
struct TOverlap
{
    TSeamToml* Toml;
    std::atomic<int> Inside{ 0 };
    std::atomic<bool> Overlapped{ false };
    EOverlapStart Start = EOverlapStart::None;
    std::thread Other;

    explicit TOverlap(TSeamToml& toml);
};


/////////////////////////////////////////////////////////////////////////////
// TOverlapSeam
//
// A seam that counts itself in TOverlap's Inside while it runs (noting when another seam is already inside) and starts
// TOverlap's Start operation on another thread, giving it 100 ms to get to its own seam; the read gives SampleText
/////////////////////////////////////////////////////////////////////////////
class TOverlapSeam
{
private:
    TOverlap* m_Overlap;

private:
    // What both calls do
    void Access() const;

public:
    explicit TOverlapSeam(TOverlap& overlap);

    TASWTomlResult operator()(TReadCall const& call) const;
    TASWTomlResult operator()(TWriteCall const& call) const;
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
// TRecordingSeam
//
// A seam that records the arguments of each call in a TSeamArguments and succeeds; the read gives SampleText
/////////////////////////////////////////////////////////////////////////////
class TRecordingSeam
{
private:
    TSeamArguments* m_Arguments;

public:
    explicit TRecordingSeam(TSeamArguments& arguments);

    TASWTomlResult operator()(TReadCall const& call) const;
    TASWTomlResult operator()(TWriteCall const& call) const;
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
// TResultSeam
//
// A seam that returns the result a test set last; the read gives a text too (which isn't used when that fails)
/////////////////////////////////////////////////////////////////////////////
class TResultSeam
{
private:
    TASWTomlResult const* m_Next;
    char const* m_Text;

public:
    TResultSeam(TASWTomlResult const& next, char const* text);

    TASWTomlResult operator()(TReadCall const& call) const;
    TASWTomlResult operator()(TWriteCall const& call) const;
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


/////////////////////////////////////////////////////////////////////////////
// TThrowingSeam
//
// A seam that throws: a std::runtime_error from a read, an int from a write
/////////////////////////////////////////////////////////////////////////////
class TThrowingSeam
{
public:
    TASWTomlResult operator()(TReadCall const& call) const;
    TASWTomlResult operator()(TWriteCall const& call) const;
};


/////////////////////////////////////////////////////////////////////////////
// TWarningWriter
//
// A write seam that adds a TempFileCleanupFailed warning that names no file, then writes the file
/////////////////////////////////////////////////////////////////////////////
class TWarningWriter
{
private:
    TSeamToml* m_Toml;

public:
    explicit TWarningWriter(TSeamToml& toml);

    TASWTomlResult operator()(TWriteCall const& call) const;
};

void LoadAppFile(TASWBaseToml* toml);
void LockReader(TLockCounters& counters, int iterations);
void LockWriter(TLockCounters& counters, int iterations);
TASWTomlDocument ParsedDocument(std::string_view text);
bool SameResult(TASWTomlResult const& first, TASWTomlResult const& second);
void SaveAsAppFile(TASWBaseToml* toml);
TASWTomlResult SeamFailure(ETomlCode code, std::string const& filePath, int systemError);
void SharedReader(ASWToml::Detail::TSharedMutex& shared, std::atomic<bool>& inside);

//---------------------------------------------------------------------------
/*
    LoadAppFile

    Loads "app.toml" into 'toml' (a thread of FileOperations_DontOverlap).
*/
void LoadAppFile(TASWBaseToml* toml)
{
    static_cast<void>(toml->Load("app.toml"));
}

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
    SaveAsAppFile

    Saves 'toml' as "app.toml" (a thread of FileOperations_DontOverlap).
*/
void SaveAsAppFile(TASWBaseToml* toml)
{
    static_cast<void>(toml->SaveAs("app.toml"));
}
//---------------------------------------------------------------------------
/*
    SeamFailure

    A failed result with 'code', 'filePath' and 'systemError', as a seam may return it.
*/
TASWTomlResult SeamFailure(ETomlCode code, std::string const& filePath, int systemError)
{
    TASWTomlResult result;
    result.Code = code;
    result.Message = "The seam failed.";
    result.FilePath = filePath;
    result.SystemError = systemError;

    return result;
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
// TSeamToml
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TSeamToml::DefaultWrite(TWriteCall const& call)
{
    return TASWBaseToml::WriteFile(call.FilePath, call.Contents, call.Options, call.Warnings);
}
//---------------------------------------------------------------------------
TASWTomlResult TSeamToml::ReadFile(std::string const& filePath, std::size_t maxSize, std::string& contents)
{
    TReadCall const call = { filePath, maxSize, contents };
    if (OnRead)
        return OnRead(call);

    return TASWBaseToml::ReadFile(filePath, maxSize, contents);
}
//---------------------------------------------------------------------------
TASWTomlResult TSeamToml::WriteFile(std::string const& filePath, std::string_view contents,
    TASWTomlSavingOptions const& options, TMessageVector& warnings)
{
    TWriteCall const call = { filePath, contents, options, warnings };
    if (OnWrite)
        return OnWrite(call);

    return DefaultWrite(call);
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TCountingSeam
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TCountingSeam::TCountingSeam(int& calls)
    : m_Calls(&calls)
{
}
//---------------------------------------------------------------------------
TASWTomlResult TCountingSeam::operator()(TReadCall const& /*call*/) const
{
    ++*m_Calls;

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult TCountingSeam::operator()(TWriteCall const& /*call*/) const
{
    ++*m_Calls;

    return TASWTomlResult();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TEditingReader
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TEditingReader::TEditingReader(TASWBaseToml& toml, int& width, TASWTomlResult& edit)
    : m_Toml(&toml),
      m_Width(&width),
      m_Edit(&edit)
{
}
//---------------------------------------------------------------------------
TASWTomlResult TEditingReader::operator()(TReadCall const& call) const
{
    *m_Width = m_Toml->GetValue("window.width", 0);
    *m_Edit = m_Toml->SetValue("window.width", 800);
    call.Contents = "loaded = 1\n";

    return TASWTomlResult();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TEditingWriter
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TEditingWriter::TEditingWriter(TSeamToml& toml, EEditDuringSave const& edit)
    : m_Toml(&toml),
      m_Edit(&edit)
{
}
//---------------------------------------------------------------------------
TASWTomlResult TEditingWriter::operator()(TWriteCall const& call) const
{
    switch (*m_Edit)
    {
        case EEditDuringSave::SetKey:
            static_cast<void>(m_Toml->SetValue("window.left", 10));
            break;

        case EEditDuringSave::RemoveKey:
            static_cast<void>(m_Toml->Remove("window.height"));
            break;

        case EEditDuringSave::RemoveTable:
            static_cast<void>(m_Toml->Remove("servers"));
            break;

        case EEditDuringSave::FailedEdit:
            static_cast<void>(m_Toml->Rename("missing", "other"));
            break;
    }

    return m_Toml->DefaultWrite(call);
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TOverlap
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TOverlap::TOverlap(TSeamToml& toml)
    : Toml(&toml)
{
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TOverlapSeam
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TOverlapSeam::TOverlapSeam(TOverlap& overlap)
    : m_Overlap(&overlap)
{
}
//---------------------------------------------------------------------------
void TOverlapSeam::Access() const
{
    if (m_Overlap->Inside.fetch_add(1) != 0)
        m_Overlap->Overlapped = true;

    EOverlapStart const start = m_Overlap->Start;
    m_Overlap->Start = EOverlapStart::None;
    if (start == EOverlapStart::Load)
        m_Overlap->Other = std::thread(LoadAppFile, m_Overlap->Toml);
    else if (start == EOverlapStart::SaveAs)
        m_Overlap->Other = std::thread(SaveAsAppFile, m_Overlap->Toml);

    if (start != EOverlapStart::None)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    --m_Overlap->Inside;
}
//---------------------------------------------------------------------------
TASWTomlResult TOverlapSeam::operator()(TReadCall const& call) const
{
    Access();
    call.Contents = SampleText;

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult TOverlapSeam::operator()(TWriteCall const& /*call*/) const
{
    Access();

    return TASWTomlResult();
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
// TRecordingSeam
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TRecordingSeam::TRecordingSeam(TSeamArguments& arguments)
    : m_Arguments(&arguments)
{
}
//---------------------------------------------------------------------------
TASWTomlResult TRecordingSeam::operator()(TReadCall const& call) const
{
    m_Arguments->ReadPath = call.FilePath;
    m_Arguments->ReadMaxSize = call.MaxSize;
    call.Contents = SampleText;

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult TRecordingSeam::operator()(TWriteCall const& call) const
{
    m_Arguments->WritePath = call.FilePath;
    m_Arguments->WriteText = std::string(call.Contents);
    m_Arguments->WriteOptions = call.Options;

    return TASWTomlResult();
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
// TResultSeam
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TResultSeam::TResultSeam(TASWTomlResult const& next, char const* text)
    : m_Next(&next),
      m_Text(text)
{
}
//---------------------------------------------------------------------------
TASWTomlResult TResultSeam::operator()(TReadCall const& call) const
{
    call.Contents = m_Text;

    return *m_Next;
}
//---------------------------------------------------------------------------
TASWTomlResult TResultSeam::operator()(TWriteCall const& /*call*/) const
{
    return *m_Next;
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


/////////////////////////////////////////////////////////////////////////////
// TThrowingSeam
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TThrowingSeam::operator()(TReadCall const& /*call*/) const
{
    throw std::runtime_error("The disk is gone.");
}
//---------------------------------------------------------------------------
TASWTomlResult TThrowingSeam::operator()(TWriteCall const& /*call*/) const
{
    throw 42;
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TWarningWriter
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TWarningWriter::TWarningWriter(TSeamToml& toml)
    : m_Toml(&toml)
{
}
//---------------------------------------------------------------------------
TASWTomlResult TWarningWriter::operator()(TWriteCall const& call) const
{
    TASWTomlMessage warning;
    warning.Severity = ESeverity::Warning;
    warning.Code = ETomlCode::TempFileCleanupFailed;
    warning.Text = "A warning that names no file.";
    call.Warnings.push_back(warning);

    return m_Toml->DefaultWrite(call);
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
    TASWBaseToml toml;
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
    TASWBaseToml toml;
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
    TASWBaseToml toml;
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
    TASWBaseToml toml;
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
    TASWBaseToml toml;
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
    TASWBaseToml toml;
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
    TASWBaseToml toml;
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
    TASWBaseToml toml;

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
        TASWBaseToml toml;
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
        TASWBaseToml toml;
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
        TASWBaseToml toml;
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
    TASWBaseToml toml;
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
    TASWBaseToml toml;
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
    TASWBaseToml toml;
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
    TASWBaseToml object;
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
    TASWBaseToml toml;
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
    TASWBaseToml toml;
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
    TASWBaseToml toml;
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
    TASWBaseToml toml;
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
    TASWBaseToml toml;
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
    TASWBaseToml toml;
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
    TASWBaseToml toml;
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

    TASWBaseToml toml;
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
    TASWBaseToml toml;
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
    TASWBaseToml toml;
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


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_BaseFile
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_BaseFile::TTest_ASWToml_BaseFile()
    : inherited("ASWToml_BaseFile_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_BaseFile::~TTest_ASWToml_BaseFile()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_BaseFile::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_BaseFile::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_BaseFile::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_BaseFile::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseFile, EmptyPath_IsInvalidArgument)
{
    // Arrange
    TSeamToml toml;
    static_cast<void>(toml.LoadText(SampleText));
    static_cast<void>(toml.SetValue("port", 8080));
    std::string const before = toml.ToText();
    TMessageVector messages;
    toml.SetLogCallback(TRecorder(messages));
    int seamCalls = 0;
    toml.OnRead = TCountingSeam(seamCalls);
    toml.OnWrite = TCountingSeam(seamCalls);

    // Act
    TASWTomlResult const load = toml.Load("");
    TASWTomlResult const saveAs = toml.SaveAs("");

    // Assert
    CheckEquals(static_cast<int>(ETomlCode::InvalidArgument), static_cast<int>(load.Code),
        "Load() of an empty path should be InvalidArgument");
    CheckEquals(static_cast<int>(ETomlCode::InvalidArgument), static_cast<int>(saveAs.Code),
        "SaveAs() of an empty path too");
    CheckEquals(0, seamCalls, "no file should be read or written");
    CheckEquals(before, toml.ToText(), "the document should be unchanged");
    CheckTrue(toml.IsModified(), "the modified flag should be unchanged");
    CheckEquals(std::string(), toml.GetFilePath(), "there should still be no file path");
    CheckEquals(std::size_t(2), messages.size(), "both failures should be reported");
    for (TASWTomlMessage const& message : messages)
        CheckTrue(message.Severity == ESeverity::Error, "a failure should be an Error");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseFile, FileOperations_DontOverlap)
{
    // Arrange
    // While a load reads (or a save writes) the file, the seam starts a save (or a load) on another thread and gives it
    // time to get to its own file access, which has to wait until the first operation is done
    TSeamToml toml;
    TOverlap overlap(toml);
    toml.OnRead = TOverlapSeam(overlap);
    toml.OnWrite = TOverlapSeam(overlap);

    // Act
    overlap.Start = EOverlapStart::SaveAs;
    TASWTomlResult const loaded = toml.Load("app.toml");
    overlap.Other.join();
    bool const saveWaited = !overlap.Overlapped;
    overlap.Start = EOverlapStart::Load;
    TASWTomlResult const saved = toml.Save();
    overlap.Other.join();
    bool const loadWaited = !overlap.Overlapped;

    // Assert
    CheckTrue(loaded.IsOk() && saved.IsOk(), "the file should be loaded and saved");
    CheckTrue(saveWaited, "a save should wait for a load to finish");
    CheckTrue(loadWaited, "a load should wait for a save to finish");
    CheckEquals(std::string("app.toml"), toml.GetFilePath(), "the file path should be set");
    CheckEquals(std::string(SampleText), toml.ToText(), "the file should be loaded");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseFile, Load_FileErrorsChangeNothing)
{
    // Arrange
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const large = folder.Path() / "large.toml";
    std::filesystem::path const utf16 = folder.Path() / "utf16.toml";
    AssertTrue(WriteTestFile(large, std::string(101, '#')), "the large file should be written");
    AssertTrue(WriteTestFile(utf16, std::string("\xFF\xFE" "a\0=\0" "1\0", 8)), "the UTF-16 file should be written");
    TASWBaseToml toml;
    TASWTomlOptions options;
    options.Reading.MaxFileSize = 100;
    static_cast<void>(toml.SetOptions(options));
    static_cast<void>(toml.LoadText(SampleText));
    static_cast<void>(toml.SetValue("port", 8080));
    std::string const before = toml.ToText();
    TMessageVector messages;
    toml.SetLogCallback(TRecorder(messages));

    // Act
    TASWTomlResult const results[] = {
        toml.Load(PathToUtf8(large)), toml.Load(PathToUtf8(folder.Path())), toml.Load(PathToUtf8(utf16)),
    };

    // Assert
    // Windows can't open a folder as a file (AccessDenied); elsewhere it opens, but isn't a file (OpenFailed)
    CheckEquals(static_cast<int>(ETomlCode::FileTooLarge), static_cast<int>(results[0].Code),
        "a file larger than MaxFileSize should be FileTooLarge");
    CheckTrue(results[1].Code == ETomlCode::AccessDenied || results[1].Code == ETomlCode::OpenFailed,
        "a folder should be AccessDenied or OpenFailed");
    CheckEquals(static_cast<int>(ETomlCode::UnsupportedEncoding), static_cast<int>(results[2].Code),
        "a UTF-16 file should be UnsupportedEncoding");
    CheckEquals(PathToUtf8(utf16), results[2].FilePath, "the failure to read the text should name the file");
    CheckEquals(before, toml.ToText(), "the document should be unchanged");
    CheckTrue(toml.IsModified(), "the modified flag should be unchanged");
    CheckEquals(std::string(), toml.GetFilePath(), "there should still be no file path");
    CheckEquals(std::size_t(3), messages.size(), "each failure should be reported");
    for (std::size_t i = 0; i < messages.size() && i < 3; ++i)
    {
        std::string const which = " (load " + std::to_string(i) + ")";
        CheckTrue(messages[i].Severity == ESeverity::Error, "the failure should be an Error" + which);
        CheckTrue(messages[i].Code == results[i].Code, "the message should have the code" + which);
        CheckNotEmpty(messages[i].FilePath, "the message should name the file" + which);
        CheckEquals(results[i].FilePath, messages[i].FilePath, "the message should have the result's file path" + which);
        CheckEquals(results[i].SystemError, messages[i].SystemError,
            "the message should have the result's system error" + which);
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseFile, Load_MissingFileEmptiesTheDocument)
{
    // Arrange
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::string const path = PathToUtf8(folder.Path() / "app.toml");
    TASWBaseToml toml;
    TASWTomlOptions options;
    options.Values.IntegerBase = EIntegerBase::Hexadecimal;
    static_cast<void>(toml.SetOptions(options));
    static_cast<void>(toml.LoadText(SampleText));
    static_cast<void>(toml.SetValue("port", 8080));
    TMessageVector messages;
    toml.SetLogCallback(TRecorder(messages));

    // Act
    TASWTomlResult const result = toml.Load(path);

    // Assert
    CheckEquals(static_cast<int>(ETomlCode::FileNotFound), static_cast<int>(result.Code),
        "a missing file should be FileNotFound");
    CheckNotEquals(0, result.SystemError, "the system's error code should be kept");
    CheckEquals(path, result.FilePath, "the result should name the file");
    CheckEquals(std::string(), toml.ToText(), "the document should be emptied");
    CheckFalse(toml.IsModified(), "the emptied document shouldn't be modified");
    CheckEquals(path, toml.GetFilePath(), "the missing file should become the file path");
    CheckTrue(toml.GetValueOptions().IntegerBase == EIntegerBase::Hexadecimal, "the options should be kept");
    CheckEquals(std::size_t(1), messages.size(), "the missing file should be reported once");
    if (messages.size() == 1)
    {
        CheckTrue(messages[0].Severity == ESeverity::Warning, "a missing file should be a Warning");
        CheckTrue(messages[0].Code == ETomlCode::FileNotFound, "the warning should be FileNotFound");
        CheckEquals(result.Message, messages[0].Text, "the warning should have the result's text");
        CheckEquals(path, messages[0].FilePath, "the warning should name the file");
        CheckEquals(result.SystemError, messages[0].SystemError, "the warning should have the system error");
    }

    // Act
    static_cast<void>(toml.SetValue("port", 255));
    TASWTomlResult const saved = toml.Save();

    // Assert
    std::string contents;
    CheckTrue(saved.IsOk(), "Save() should create the file: " + saved.Message);
    CheckTrue(ReadTestFile(folder.Path() / "app.toml", contents), "the file should exist");
    CheckEquals(toml.ToText(), contents, "the file should have the document's text");
    CheckStartsWith(contents, std::string("port = 0xFF"), "written with the options");
    CheckEquals(std::size_t(1), messages.size(), "the save should report nothing");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseFile, Load_MissingFolderIsCreatedBySave)
{
    // Arrange
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const file = folder.Path() / "Company" / "App" / "app.toml";
    TASWBaseToml toml;

    // Act
    TASWTomlResult const loaded = toml.Load(PathToUtf8(file));
    static_cast<void>(toml.SetValue("name", "My App"));
    TASWTomlResult const saved = toml.Save();

    // Assert
    std::string contents;
    CheckEquals(static_cast<int>(ETomlCode::FileNotFound), static_cast<int>(loaded.Code),
        "a missing folder should be FileNotFound");
    CheckTrue(saved.IsOk(), "Save() should create the folders and the file: " + saved.Message);
    CheckTrue(ReadTestFile(file, contents), "the file should exist");
    CheckEquals(toml.ToText(), contents, "the file should have the document's text");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseFile, Load_ReadsTheFile)
{
    // Arrange
    // The path as an application may give it, with '/' on Windows too
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    AssertTrue(WriteTestFile(folder.Path() / "app.toml", SampleText), "the test file should be written");
    std::string const path = PathToUtf8(folder.Path()) + "/app.toml";
    TASWBaseToml toml;
    static_cast<void>(toml.SetValue("old", 1));
    TMessageVector messages;
    toml.SetLogCallback(TRecorder(messages));

    // Act
    TASWTomlResult const result = toml.Load(path);
    TASWTomlResult const failedEdit = toml.Rename("missing", "other");

    // Assert
    CheckTrue(result.IsOk(), "the file should be loaded: " + result.Message);
    CheckEquals(std::string(), result.FilePath, "a result that is Ok should be only that");
    CheckEquals(std::string(SampleText), toml.ToText(), "the document should have the file's text");
    CheckFalse(toml.Contains("old"), "the old document should be gone");
    CheckFalse(toml.IsModified(), "a loaded document shouldn't be modified");
    CheckEquals(path, toml.GetFilePath(), "the file path should be the path as it was given");
    CheckEquals(static_cast<int>(ETomlCode::NotFound), static_cast<int>(failedEdit.Code), "the rename should fail");
    CheckEquals(std::size_t(1), messages.size(), "the load should report nothing, and the failed edit be reported");
    if (messages.size() == 1)
    {
        CheckTrue(messages[0].Severity == ESeverity::Error, "the failed edit should be an Error");
        CheckEquals(path, messages[0].FilePath, "and name the file");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseFile, Load_ReadsWithoutTheLock)
{
    // Arrange
    // A lookup or an edit made while the file is read would wait forever if the document's lock were held (the CI's
    // test timeout turns that into a failure)
    TSeamToml toml;
    static_cast<void>(toml.LoadText(SampleText));
    int widthDuringLoad = 0;
    TASWTomlResult editDuringLoad;
    toml.OnRead = TEditingReader(toml, widthDuringLoad, editDuringLoad);

    // Act
    TASWTomlResult const result = toml.Load("app.toml");

    // Assert
    CheckTrue(result.IsOk(), "the file should be loaded: " + result.Message);
    CheckEquals(640, widthDuringLoad, "a lookup during the load should see the old document");
    CheckTrue(editDuringLoad.IsOk(), "an edit during the load should succeed");
    CheckEquals(std::string("loaded = 1\n"), toml.ToText(), "the loaded file should replace the edited document");
    CheckFalse(toml.IsModified(), "a loaded document shouldn't be modified");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseFile, Load_SyntaxErrorChangesNothing)
{
    // Arrange
    // The object has loaded one file and has an edit; another file has a syntax error
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const good = folder.Path() / "good.toml";
    std::filesystem::path const bad = folder.Path() / "bad.toml";
    AssertTrue(WriteTestFile(good, SampleText), "the good file should be written");
    AssertTrue(WriteTestFile(bad, InvalidText), "the bad file should be written");
    TASWBaseToml toml;
    static_cast<void>(toml.Load(PathToUtf8(good)));
    static_cast<void>(toml.SetValue("port", 8080));
    std::string const before = toml.ToText();
    TMessageVector messages;
    toml.SetLogCallback(TRecorder(messages));

    // Act
    TASWTomlResult const failed = toml.Load(PathToUtf8(bad));
    std::string const textAfterFailure = toml.ToText();
    bool const modifiedAfterFailure = toml.IsModified();
    std::string const pathAfterFailure = toml.GetFilePath();
    TASWTomlResult const saved = toml.Save();

    // Assert
    std::string goodContents;
    std::string badContents;
    CheckEquals(static_cast<int>(ETomlCode::ParseFailed), static_cast<int>(failed.Code),
        "a syntax error should be ParseFailed");
    CheckTrue(failed.Line == 2 && failed.Column != 0, "with the line and column of the error");
    CheckEquals(PathToUtf8(bad), failed.FilePath, "and the file");
    CheckEquals(before, textAfterFailure, "the document should be unchanged");
    CheckTrue(modifiedAfterFailure, "the modified flag should be unchanged");
    CheckEquals(PathToUtf8(good), pathAfterFailure, "the file path should be unchanged");
    CheckEquals(std::size_t(1), messages.size(), "the failure should be reported, and the save nothing");
    if (messages.size() == 1)
    {
        CheckTrue(messages[0].Severity == ESeverity::Error && messages[0].Code == ETomlCode::ParseFailed,
            "as an Error with the result's code");
        CheckEquals(failed.Message, messages[0].Text, "its text");
        CheckTrue(messages[0].Line == failed.Line && messages[0].Column == failed.Column, "its line and column");
        CheckEquals(PathToUtf8(bad), messages[0].FilePath, "and the file with the error");
    }

    CheckTrue(saved.IsOk(), "Save() should write the file path: " + saved.Message);
    CheckTrue(ReadTestFile(good, goodContents) && ReadTestFile(bad, badContents), "both files should be readable");
    CheckEquals(before, goodContents, "Save() should write the document to the file it was loaded from");
    CheckEquals(std::string(InvalidText), badContents, "and leave the file with the error as it was");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseFile, OutOfMemory_LoadChangesNothing)
{
    // Arrange
    if (!CanFailAllocations())
        Skip("allocations can't be made to fail in this build (see CanFailAllocations())");

    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    AssertTrue(WriteTestFile(folder.Path() / "app.toml", SampleText), "the test file should be written");
    std::string const paths[] = { PathToUtf8(folder.Path() / "app.toml"), PathToUtf8(folder.Path() / "missing.toml") };

    // Act & Assert
    // Each allocation of the load fails in turn, on a new object, until the load makes no more: of a file, and of a
    // missing file, which empties the document
    for (std::size_t file = 0; file < 2; ++file)
    {
        std::string const& path = paths[file];
        bool const missing = file == 1;
        std::size_t failures = 0;
        for (std::size_t number = 1; number < 100000; ++number)
        {
            TASWBaseToml toml;
            static_cast<void>(toml.SetValue("old", 1));
            std::string const before = toml.ToText();
            std::string const which = " (" + path + ", allocation " + std::to_string(number) + ")";
            TASWTomlResult result;
            bool failed = false;
            {
                TFailingAlloc failingAlloc(number);
                result = toml.Load(path);
                failed = failingAlloc.HasFailed();
            }

            bool const loaded = missing ? result.Code == ETomlCode::FileNotFound : result.IsOk();
            if (loaded)
            {
                CheckEquals(std::string(missing ? "" : SampleText), toml.ToText(),
                    "a load that succeeded should have loaded the file, or emptied the document" + which);
                CheckEquals(path, toml.GetFilePath(), "and set the file path" + which);
                CheckFalse(toml.IsModified(), "a loaded document shouldn't be modified" + which);
            }
            else
            {
                ++failures;
                CheckTrue(failed, "the load should only fail when an allocation fails" + which);
                CheckEquals(static_cast<int>(ETomlCode::UnexpectedException), static_cast<int>(result.Code),
                    "a failed allocation should be UnexpectedException" + which);
                CheckEquals(path, result.FilePath, "the failure should name the file" + which);
                CheckEquals(before, toml.ToText(), "a failed load should change nothing" + which);
                CheckEquals(std::string(), toml.GetFilePath(), "a failed load should keep the file path" + which);
                CheckTrue(toml.IsModified(), "a failed load should keep the modified flag" + which);
            }

            if (!failed)
                break;
        }

        CheckGreaterThan(failures, std::size_t(0), "at least one allocation should have failed (" + path + ")");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseFile, OutOfMemory_SaveChangesNothing)
{
    // Arrange
    if (!CanFailAllocations())
        Skip("allocations can't be made to fail in this build (see CanFailAllocations())");

    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const file = folder.Path() / "app.toml";
    std::filesystem::path const copy = folder.Path() / "copy.toml";
    std::string const filePath = PathToUtf8(file);
    std::string const copyPath = PathToUtf8(copy);

    // Act & Assert
    // As OutOfMemory_LoadChangesNothing does, for Save() and for SaveAs() to a new file. The save is atomic, so a failed
    // one leaves the file as it was, and no temporary file.
    for (int operation = 0; operation < 2; ++operation)
    {
        bool const saveAs = operation == 1;
        std::size_t failures = 0;
        for (std::size_t number = 1; number < 100000; ++number)
        {
            AssertTrue(WriteTestFile(file, SampleText), "the test file should be written");
            std::error_code error;
            std::filesystem::remove(copy, error);
            TASWBaseToml toml;
            static_cast<void>(toml.Load(filePath));
            static_cast<void>(toml.SetValue("window.width", 800));
            std::string const text = toml.ToText();
            std::string const which = std::string(saveAs ? " (SaveAs()" : " (Save()") + ", allocation " +
                std::to_string(number) + ")";
            TASWTomlResult result;
            bool failed = false;
            {
                TFailingAlloc failingAlloc(number);
                result = saveAs ? toml.SaveAs(copyPath) : toml.Save();
                failed = failingAlloc.HasFailed();
            }

            std::string contents;
            std::string written;
            CheckTrue(ReadTestFile(file, contents), "the file should be readable" + which);
            bool const wasWritten = ReadTestFile(saveAs ? copy : file, written);
            if (result.IsOk())
            {
                CheckTrue(wasWritten && written == text, "a save that succeeded should write the text" + which);
                CheckEquals(saveAs ? copyPath : filePath, toml.GetFilePath(), "and set the file path" + which);
                CheckFalse(toml.IsModified(), "a saved document shouldn't be modified" + which);
            }
            else
            {
                ++failures;
                CheckTrue(failed, "the save should only fail when an allocation fails" + which);
                CheckEquals(static_cast<int>(ETomlCode::UnexpectedException), static_cast<int>(result.Code),
                    "a failed allocation should be UnexpectedException" + which);
                if (saveAs)
                    CheckEquals(copyPath, result.FilePath, "the failure should name the file" + which);

                CheckEquals(std::string(SampleText), contents, "a failed save should leave the file as it was" + which);
                CheckEquals(std::size_t(1), CountEntries(folder.Path()), "and write no other file" + which);
                CheckEquals(filePath, toml.GetFilePath(), "a failed save should keep the file path" + which);
                CheckTrue(toml.IsModified(), "a failed save should keep the modified flag" + which);
            }

            if (!failed)
                break;
        }

        CheckGreaterThan(failures, std::size_t(0), std::string("at least one allocation should have failed") +
            (saveAs ? " (SaveAs())" : " (Save())"));
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseFile, SaveAs_SetsTheFilePath)
{
    // Arrange
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::string const first = PathToUtf8(folder.Path() / "first.toml");
    std::string const inMissingFolder = PathToUtf8(folder.Path() / "No" / "second.toml");
    TASWBaseToml toml;
    TASWTomlOptions options;
    options.Saving.CreateFolders = false;
    static_cast<void>(toml.SetOptions(options));
    static_cast<void>(toml.LoadText(SampleText));
    TMessageVector messages;
    toml.SetLogCallback(TRecorder(messages));

    // Act
    TASWTomlResult const saved = toml.SaveAs(first);
    std::string const pathAfterSave = toml.GetFilePath();
    static_cast<void>(toml.SetValue("port", 8080));
    TASWTomlResult const failed = toml.SaveAs(inMissingFolder);
    std::string const pathAfterFailure = toml.GetFilePath();
    bool const modifiedAfterFailure = toml.IsModified();
    TASWTomlResult const savedAgain = toml.Save();

    // Assert
    std::string contents;
    CheckTrue(saved.IsOk(), "SaveAs() should write the file: " + saved.Message);
    CheckEquals(first, pathAfterSave, "the file should become the file path");
    CheckEquals(static_cast<int>(ETomlCode::FileNotFound), static_cast<int>(failed.Code),
        "a missing folder without CreateFolders should be FileNotFound");
    CheckEquals(first, pathAfterFailure, "a failed SaveAs() should keep the file path");
    CheckTrue(modifiedAfterFailure, "a failed SaveAs() should keep the modified flag");
    CheckTrue(savedAgain.IsOk(), "Save() should write the file path: " + savedAgain.Message);
    CheckFalse(toml.IsModified(), "a saved document shouldn't be modified");
    CheckTrue(ReadTestFile(folder.Path() / "first.toml", contents), "the file should exist");
    CheckEquals(toml.ToText(), contents, "the file should have the document's text");
    CheckEquals(std::size_t(1), messages.size(), "only the failure should be reported");
    if (messages.size() == 1)
    {
        CheckTrue(messages[0].Severity == ESeverity::Error, "the failure should be an Error");
        CheckEquals(failed.FilePath, messages[0].FilePath, "the failure should name the file it couldn't write");
        CheckNotEquals(first, messages[0].FilePath, "the failure shouldn't name the object's file path");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseFile, SaveAs_UsesUtf8Paths)
{
    // Arrange
    // A missing folder "cafe" with U+00E9, and the Greek lambda U+03BB, given to the object as UTF-8 by PathToUtf8()
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const subFolder = folder.Path() / Utf8ToPath("caf\xC3\xA9");
    std::filesystem::path const file = subFolder / Utf8ToPath("\xCE\xBB.toml");
    std::string const path = PathToUtf8(file);
    std::string const missingPath = PathToUtf8(subFolder / Utf8ToPath("\xCE\xBB-missing.toml"));
    TASWBaseToml toml;
    static_cast<void>(toml.LoadText(SampleText));
    TASWBaseToml loader;
    TASWBaseToml missingLoader;
    TMessageVector messages;
    missingLoader.SetLogCallback(TRecorder(messages));

    // Act
    TASWTomlResult const saved = toml.SaveAs(path);
    TASWTomlResult const loaded = loader.Load(path);
    TASWTomlResult const missing = missingLoader.Load(missingPath);

    // Assert
    std::string contents;
    CheckTrue(saved.IsOk(), "SaveAs() should write the file: " + saved.Message);
    CheckEquals(path, toml.GetFilePath(), "the UTF-8 path should become the file path");
    CheckTrue(Utf8ToPath(toml.GetFilePath()) == file, "the file path should convert back to the path");
    CheckTrue(ReadTestFile(file, contents), "the file should exist at the path");
    CheckEquals(toml.ToText(), contents, "the file should have the document's text");
    CheckTrue(loaded.IsOk(), "Load() should read the file: " + loaded.Message);
    CheckEquals(toml.ToText(), loader.ToText(), "the file should load as it was saved");
    CheckEquals(static_cast<int>(ETomlCode::FileNotFound), static_cast<int>(missing.Code),
        "a missing file should be FileNotFound");
    CheckEquals(missingPath, missing.FilePath, "the result should name the file as UTF-8");
    CheckEquals(std::size_t(1), messages.size(), "the missing file should be reported");
    if (messages.size() == 1)
        CheckEquals(missingPath, messages[0].FilePath, "the message should name the file as UTF-8");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseFile, Save_EditDuringSaveKeepsTheModifiedFlag)
{
    // Arrange
    // WriteFile() makes an edit before it writes the text Save() made, so the file doesn't have the edit
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const file = folder.Path() / "app.toml";
    TSeamToml toml;
    static_cast<void>(toml.LoadText(SampleText));
    static_cast<void>(toml.SetValue("port", 8080));
    EEditDuringSave edit = EEditDuringSave::SetKey;
    toml.OnWrite = TEditingWriter(toml, edit);
    std::string contents;

    // Act
    std::string const textOfFirstSave = toml.ToText();
    TASWTomlResult const first = toml.SaveAs(PathToUtf8(file));

    // Assert
    CheckTrue(first.IsOk(), "the file should be saved: " + first.Message);
    CheckTrue(toml.IsModified(), "a key set during the save should keep the modified flag");
    CheckTrue(ReadTestFile(file, contents), "the file should exist");
    CheckEquals(textOfFirstSave, contents, "the file should have the text before the edit");
    CheckEquals(10, toml.GetValue("window.left", 0), "the document should have the edit");

    // Act
    edit = EEditDuringSave::RemoveKey;
    std::string const textOfSecondSave = toml.ToText();
    TASWTomlResult const second = toml.Save();

    // Assert
    CheckTrue(second.IsOk(), "the file should be saved: " + second.Message);
    CheckTrue(toml.IsModified(), "a key removed during the save should keep the modified flag");
    CheckTrue(ReadTestFile(file, contents), "the file should exist");
    CheckEquals(textOfSecondSave, contents, "the file should have the text before the removal");

    // Act
    edit = EEditDuringSave::RemoveTable;
    TASWTomlResult const third = toml.Save();

    // Assert
    CheckTrue(third.IsOk(), "the file should be saved: " + third.Message);
    CheckTrue(toml.IsModified(), "an array of tables removed during the save should keep the modified flag");

    // Act
    edit = EEditDuringSave::FailedEdit;
    TASWTomlResult const fourth = toml.Save();

    // Assert
    CheckTrue(fourth.IsOk(), "the file should be saved: " + fourth.Message);
    CheckFalse(toml.IsModified(), "an edit that failed during the save changed nothing, so the flag should be cleared");
    CheckTrue(ReadTestFile(file, contents), "the file should exist");
    CheckEquals(toml.ToText(), contents, "the file should have the document's text");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseFile, Save_NoFilePathFails)
{
    // Arrange
    TASWBaseToml toml;
    static_cast<void>(toml.LoadText(SampleText));
    static_cast<void>(toml.SetValue("port", 8080));
    TMessageVector messages;
    toml.SetLogCallback(TRecorder(messages));

    // Act
    TASWTomlResult const result = toml.Save();

    // Assert
    CheckEquals(static_cast<int>(ETomlCode::NoFilePath), static_cast<int>(result.Code),
        "Save() without a file path should fail");
    CheckTrue(toml.IsModified(), "the modified flag should be unchanged");
    CheckEquals(std::size_t(1), messages.size(), "the failure should be reported");
    if (messages.size() == 1)
    {
        CheckTrue(messages[0].Severity == ESeverity::Error, "the failure should be an Error");
        CheckTrue(messages[0].Code == ETomlCode::NoFilePath, "the message should be NoFilePath");
        CheckEquals(result.Message, messages[0].Text, "the message should have the result's text");
        CheckEquals(std::string(), messages[0].FilePath, "the message should name no file");
    }
}
//---------------------------------------------------------------------------
#if defined(_WIN32)
ASW_TEST(TTest_ASWToml_BaseFile, Save_ReadOnlyFileChangesNothing)
{
    // Arrange
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const file = folder.Path() / "readonly.toml";
    AssertTrue(WriteTestFile(file, SampleText), "the test file should be written");
    std::string const path = PathToUtf8(file);
    TASWBaseToml toml;
    static_cast<void>(toml.Load(path));
    static_cast<void>(toml.SetValue("port", 8080));
    AssertTrue(::SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_READONLY) != 0, "the file should be made read-only");
    TMessageVector messages;
    toml.SetLogCallback(TRecorder(messages));

    // Act
    TASWTomlResult const result = toml.Save();
    std::string contents;
    bool const read = ReadTestFile(file, contents);

    // So the folder can be deleted
    ::SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL);

    // Assert
    CheckEquals(static_cast<int>(ETomlCode::ReplaceFailed), static_cast<int>(result.Code),
        "the code should be ReplaceFailed");
    CheckEquals(static_cast<int>(ERROR_ACCESS_DENIED), result.SystemError, "the system error should be access denied");
    CheckTrue(read, "the file should still be readable");
    CheckEquals(std::string(SampleText), contents, "the file should be unchanged");
    CheckTrue(toml.IsModified(), "the modified flag should be unchanged");
    CheckEquals(path, toml.GetFilePath(), "the file path should be unchanged");
    CheckEquals(std::size_t(1), messages.size(), "the failure should be reported");
    if (messages.size() == 1)
    {
        CheckTrue(messages[0].Severity == ESeverity::Error, "the failure should be an Error");
        CheckEquals(path, messages[0].FilePath, "the message should name the file");
        CheckEquals(result.SystemError, messages[0].SystemError, "the message should have the system error");
    }
}
#endif
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseFile, Save_RoundTripsByteForByte)
{
    // Arrange
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const source = folder.Path() / "source.toml";
    std::filesystem::path const copy = folder.Path() / "copy.toml";
    AssertTrue(WriteTestFile(source, TrickyText), "the test file should be written");
    TASWBaseToml toml;

    // Act
    TASWTomlResult const loaded = toml.Load(PathToUtf8(source));
    TASWTomlResult const saved = toml.Save();
    TASWTomlResult const savedAs = toml.SaveAs(PathToUtf8(copy));

    // Assert
    std::string sourceContents;
    std::string copyContents;
    CheckTrue(loaded.IsOk(), "the file should be loaded: " + loaded.Message);
    CheckTrue(saved.IsOk(), "the file should be saved: " + saved.Message);
    CheckTrue(savedAs.IsOk(), "the copy should be saved: " + savedAs.Message);
    CheckTrue(ReadTestFile(source, sourceContents), "the file should be readable");
    CheckTrue(ReadTestFile(copy, copyContents), "the copy should be readable");
    CheckEquals(std::string(TrickyText), sourceContents, "the saved file should have the bytes it was loaded from");
    CheckEquals(std::string(TrickyText), copyContents, "the copy should have the bytes the file was loaded from");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseFile, Save_WarningsAreReported)
{
    // Arrange
    // A folder where the backup should go, so the backup fails; and WriteFile() adds a warning that names no file,
    // which a SaveAs() that fails (its folder is missing) reports too
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const file = folder.Path() / "app.toml";
    AssertTrue(WriteTestFile(file, SampleText), "the test file should be written");
    std::error_code error;
    AssertTrue(std::filesystem::create_directory(folder.Path() / "app.toml.bak", error),
        "the folder where the backup should go should be created");
    std::string const path = PathToUtf8(file);
    std::string const inMissingFolder = PathToUtf8(folder.Path() / "No" / "other.toml");
    TSeamToml toml;
    TASWTomlOptions options;
    options.Saving.Backup = true;
    options.Saving.CreateFolders = false;
    static_cast<void>(toml.SetOptions(options));
    static_cast<void>(toml.Load(path));
    TMessageVector messages;
    toml.SetLogCallback(TRecorder(messages));

    // Act
    static_cast<void>(toml.SetValue("port", 8080));
    TASWTomlResult const saved = toml.Save();
    std::size_t const messagesOfFirstSave = messages.size();
    toml.OnWrite = TWarningWriter(toml);
    TASWTomlResult const savedAgain = toml.Save();
    TASWTomlResult const failed = toml.SaveAs(inMissingFolder);

    // Assert
    std::string contents;
    CheckTrue(saved.IsOk(), "a failed backup shouldn't fail the save: " + saved.Message);
    CheckTrue(savedAgain.IsOk(), "the second save should succeed: " + savedAgain.Message);
    CheckEquals(static_cast<int>(ETomlCode::FileNotFound), static_cast<int>(failed.Code),
        "the SaveAs() to a missing folder should fail");
    CheckFalse(toml.IsModified(), "a saved document shouldn't be modified");
    CheckTrue(ReadTestFile(file, contents), "the file should be readable");
    CheckEquals(toml.ToText(), contents, "the file should have the document's text");
    CheckEquals(std::size_t(1), messagesOfFirstSave, "the failed backup should be reported");
    CheckEquals(std::size_t(5), messages.size(), "the second save should report its two warnings, and the SaveAs() its "
        "warning and failure");
    if (messages.size() == 5)
    {
        CheckTrue(messages[0].Severity == ESeverity::Warning, "the backup should be a Warning");
        CheckTrue(messages[0].Code == ETomlCode::BackupFailed, "the warning should be BackupFailed");
        CheckEquals(path + ".bak", messages[0].FilePath, "the warning should keep the file it names, the backup");
        CheckTrue(messages[1].Code == ETomlCode::TempFileCleanupFailed, "WriteFile()'s warning should be reported in "
            "order");
        CheckEquals(path, messages[1].FilePath, "a warning that names no file should name the saved file");
        CheckTrue(messages[2].Code == ETomlCode::BackupFailed, "the second failed backup should be reported");
        CheckTrue(messages[3].Code == ETomlCode::TempFileCleanupFailed, "the SaveAs()'s warning should be reported");
        CheckEquals(inMissingFolder, messages[3].FilePath, "and name the file it was about, not the file path");
        CheckTrue(messages[4].Severity == ESeverity::Error && messages[4].Code == ETomlCode::FileNotFound,
            "then its failure, as an Error");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseFile, Save_WritesAndClearsTheModifiedFlag)
{
    // Arrange
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const file = folder.Path() / "app.toml";
    AssertTrue(WriteTestFile(file, SampleText), "the test file should be written");
    TASWBaseToml toml;
    static_cast<void>(toml.Load(PathToUtf8(file)));
    static_cast<void>(toml.SetValue("port", 8080));
    std::string contents;

    // Act
    TASWTomlResult const saved = toml.Save();

    // Assert
    CheckTrue(saved.IsOk(), "the file should be saved: " + saved.Message);
    CheckEquals(std::string(), saved.FilePath, "a result that is Ok should be only that");
    CheckFalse(toml.IsModified(), "a saved document shouldn't be modified");
    CheckTrue(ReadTestFile(file, contents), "the file should be readable");
    CheckEquals(toml.ToText(), contents, "the file should have the document's text");
    CheckContains(contents, std::string("port = 8080\n"), "the file should have the edit");

    // Act
    // A document that isn't modified is saved too
    AssertTrue(WriteTestFile(file, "Changed by another program"), "the file should be changed");
    TASWTomlResult const savedAgain = toml.Save();

    // Assert
    CheckTrue(savedAgain.IsOk(), "the file should be saved again: " + savedAgain.Message);
    CheckTrue(ReadTestFile(file, contents), "the file should be readable");
    CheckEquals(toml.ToText(), contents, "the file should have the document's text again");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseFile, Seams_ExceptionIsUnexpectedException)
{
    // Arrange
    TSeamToml toml;
    static_cast<void>(toml.LoadText(SampleText));
    static_cast<void>(toml.SetValue("port", 8080));
    std::string const before = toml.ToText();
    TMessageVector messages;
    toml.SetLogCallback(TRecorder(messages));
    toml.OnRead = TThrowingSeam();
    toml.OnWrite = TThrowingSeam();

    // Act
    TASWTomlResult const load = toml.Load("app.toml");
    TASWTomlResult const saveAs = toml.SaveAs("other.toml");

    // Assert
    CheckEquals(static_cast<int>(ETomlCode::UnexpectedException), static_cast<int>(load.Code),
        "an exception from ReadFile() should be UnexpectedException");
    CheckEquals(std::string("An unexpected exception was thrown: The disk is gone."), load.Message,
        "the message should say what was thrown");
    CheckEquals(std::string("app.toml"), load.FilePath, "the result should name the file Load() was given");
    CheckEquals(static_cast<int>(ETomlCode::UnexpectedException), static_cast<int>(saveAs.Code),
        "an exception from WriteFile() should be UnexpectedException");
    CheckEquals(std::string("An exception that isn't a std::exception was thrown."), saveAs.Message,
        "the message should say what was thrown");
    CheckEquals(std::string("other.toml"), saveAs.FilePath, "the result should name the file SaveAs() was given");
    CheckEquals(before, toml.ToText(), "the document should be unchanged");
    CheckTrue(toml.IsModified(), "the modified flag should be unchanged");
    CheckEquals(std::string(), toml.GetFilePath(), "there should still be no file path");
    CheckEquals(std::size_t(2), messages.size(), "both failures should be reported");
    if (messages.size() == 2)
    {
        CheckTrue(messages[0].Severity == ESeverity::Error && messages[1].Severity == ESeverity::Error,
            "as Errors");
        CheckEquals(std::string("app.toml"), messages[0].FilePath, "each with the file it was about");
        CheckEquals(std::string("other.toml"), messages[1].FilePath, "the second too");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseFile, Seams_FailuresChangeNothing)
{
    // Arrange
    // The seams return 'next'; a failure without a file path gets the path of the file the operation was about
    TSeamToml toml;
    TASWTomlResult next;
    toml.OnRead = TResultSeam(next, "new = 1\n");
    toml.OnWrite = TResultSeam(next, "");
    static_cast<void>(toml.LoadText(SampleText));
    static_cast<void>(toml.SaveAs("app.toml"));
    static_cast<void>(toml.SetValue("port", 8080));
    std::string const before = toml.ToText();
    TMessageVector messages;
    toml.SetLogCallback(TRecorder(messages));

    // Act
    next = SeamFailure(ETomlCode::WriteFailed, "", 0);
    TASWTomlResult const writeFailed = toml.Save();
    next = SeamFailure(ETomlCode::ReplaceFailed, "app.toml.tmp", 5);
    TASWTomlResult const replaceFailed = toml.SaveAs("other.toml");
    next = SeamFailure(ETomlCode::ReadFailed, "", 0);
    TASWTomlResult const readFailed = toml.Load("other.toml");

    // Assert
    CheckEquals(static_cast<int>(ETomlCode::WriteFailed), static_cast<int>(writeFailed.Code),
        "Save() should fail as WriteFile() did");
    CheckEquals(std::string("app.toml"), writeFailed.FilePath, "the failed Save() should name the file path");
    CheckEquals(static_cast<int>(ETomlCode::ReplaceFailed), static_cast<int>(replaceFailed.Code),
        "SaveAs() should fail as WriteFile() did");
    CheckEquals(std::string("app.toml.tmp"), replaceFailed.FilePath, "a failure that names a file should keep it");
    CheckEquals(static_cast<int>(ETomlCode::ReadFailed), static_cast<int>(readFailed.Code),
        "Load() should fail as ReadFile() did");
    CheckEquals(std::string("other.toml"), readFailed.FilePath, "the failed Load() should name the file it read");
    CheckEquals(before, toml.ToText(), "the document should be unchanged");
    CheckTrue(toml.IsModified(), "the modified flag should be unchanged");
    CheckEquals(std::string("app.toml"), toml.GetFilePath(), "the file path should be unchanged");
    CheckEquals(std::size_t(3), messages.size(), "each failure should be reported");
    if (messages.size() == 3)
    {
        CheckEquals(std::string("app.toml"), messages[0].FilePath,
            "the failed Save() should be reported with the file path");
        CheckEquals(std::string("app.toml.tmp"), messages[1].FilePath,
            "the failed SaveAs() should be reported with the file it names");
        CheckEquals(5, messages[1].SystemError, "the system error should be reported");
        CheckEquals(std::string("other.toml"), messages[2].FilePath,
            "the failed Load() should be reported with the file it read, not the file path");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseFile, Seams_FileNotFoundIsAMissingFile)
{
    // Arrange
    // ReadFile() fails with FileNotFound for a file it names itself, and gives a text that isn't used
    TSeamToml toml;
    static_cast<void>(toml.LoadText(SampleText));
    TMessageVector messages;
    toml.SetLogCallback(TRecorder(messages));
    TASWTomlResult const notFound = SeamFailure(ETomlCode::FileNotFound, "elsewhere.toml", 2);
    toml.OnRead = TResultSeam(notFound, "unused = 1\n");

    // Act
    TASWTomlResult const result = toml.Load("app.toml");

    // Assert
    CheckEquals(static_cast<int>(ETomlCode::FileNotFound), static_cast<int>(result.Code),
        "Load() should fail as ReadFile() did");
    CheckEquals(std::string(), toml.ToText(), "the document should be emptied");
    CheckEquals(std::string("app.toml"), toml.GetFilePath(), "the file Load() was given should become the file path");
    CheckEquals(std::size_t(1), messages.size(), "the missing file should be reported once");
    if (messages.size() == 1)
    {
        CheckTrue(messages[0].Severity == ESeverity::Warning, "a missing file should be a Warning");
        CheckEquals(std::string("elsewhere.toml"), messages[0].FilePath,
            "the warning should name the file ReadFile() named");
        CheckEquals(2, messages[0].SystemError, "the warning should have the system error");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseFile, Seams_GetTheirArguments)
{
    // Arrange
    TSeamToml toml;
    TASWTomlOptions options;
    options.Reading.MaxFileSize = 1000;
    options.Saving.AtomicSave = false;
    options.Saving.Backup = true;
    options.Saving.BackupSuffix = ".old";
    static_cast<void>(toml.SetOptions(options));
    TSeamArguments arguments;
    toml.OnRead = TRecordingSeam(arguments);
    toml.OnWrite = TRecordingSeam(arguments);

    // Act
    TASWTomlResult const loaded = toml.Load("Folder/app.toml");
    static_cast<void>(toml.SetValue("port", 8080));
    TASWTomlResult const saved = toml.Save();

    // Assert
    CheckTrue(loaded.IsOk() && saved.IsOk(), "the file should be loaded and saved");
    CheckEquals(std::string("Folder/app.toml"), arguments.ReadPath, "ReadFile() should get the path as given");
    CheckEquals(std::size_t(1000), arguments.ReadMaxSize, "ReadFile() should get MaxFileSize");
    CheckEquals(std::string("Folder/app.toml"), arguments.WritePath, "WriteFile() should get the file path");
    CheckEquals(toml.ToText(), arguments.WriteText, "WriteFile() should get the document's text");
    CheckTrue(arguments.WriteOptions.Backup && !arguments.WriteOptions.AtomicSave &&
        arguments.WriteOptions.BackupSuffix == ".old", "WriteFile() should get the saving options");
    CheckFalse(toml.IsModified(), "a saved document shouldn't be modified");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_BaseAccess, "Base")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_BaseCallback, "Base")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_BaseFile, "Base")
