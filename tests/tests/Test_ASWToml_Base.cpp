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
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
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
#include "ASWToml_Converters.h"
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
#include "UT_Helper_Threads.h"
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

using TResultVector = std::vector<TASWTomlResult>;
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

// The file operation TFileReentrant calls for the next message
enum class EFileCall
{
    None,
    Load,
    Save,
};

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
// TFileReentrant
//
// A log callback that adds each message to a vector and then makes the file call an EFileCall names (once, setting it
// to None): Load() of its file or Save() of the object it is the callback of, adding the result to another vector
/////////////////////////////////////////////////////////////////////////////
class TFileReentrant
{
private:
    TASWBaseToml* m_Toml;
    std::string m_FilePath;
    EFileCall* m_Next;
    TMessageVector* m_Messages;
    TResultVector* m_Results;

public:
    TFileReentrant(TASWBaseToml& toml, std::string const& filePath, EFileCall& next, TMessageVector& messages,
        TResultVector& results);

    void operator()(TASWTomlMessage const& message) const;
};


/////////////////////////////////////////////////////////////////////////////
// TGenerationServer
//
// An element of the array of tables "servers" in the thread tests: a writer sets all of them to one generation in one
// edit, so a reader that sees two generations in one read has seen a document halfway through an edit
/////////////////////////////////////////////////////////////////////////////
struct TGenerationServer
{
    std::string Host;
    int Port = 0;
    int Generation = 0;
};

// Its fields, as an application declares them (here, since the converter's return type is deduced)
auto ASWTomlFields(TGenerationServer const&)
{
    return std::tuple{ ASWToml::Field("host", &TGenerationServer::Host),
        ASWToml::Field("port", &TGenerationServer::Port),
        ASWToml::Field("generation", &TGenerationServer::Generation) };
}


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
// TThreadTest
//
// What the threads of a thread test share: the object, the problems they find, and how much work each does. Writer
// threads count WriterPath(index) up from 1, to at least Iterations and at most Largest; threads that go on until
// others are done wait for Left to reach 0.
/////////////////////////////////////////////////////////////////////////////
struct TThreadTest
{
    TSeamToml Toml;
    TThreadFailures Failures;
    std::size_t Writers = 0;
    int Iterations = 0;
    int Largest = 0;
    int FileOperations = 0; // The loads or saves of each file thread
    std::atomic<int> Left{ 0 }; // The threads still running that the others go on for
    std::string FilePath; // The file the loads and saves use
    std::atomic<int> FirstCount{ 0 }; // The messages First and Second counted
    std::atomic<int> SecondCount{ 0 };
    TASWTomlLogCallback First; // The callbacks SwitchCallbacks() switches between
    TASWTomlLogCallback Second;
    TASWTomlOptions Hexadecimal; // The options SwitchOptions() switches to and from the default ones
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


/////////////////////////////////////////////////////////////////////////////
// TWholeFileChecker
//
// A write seam of a thread test that writes the file, then reads it back and checks it has the text that was saved,
// which reads back as it is with each writer's count in range (see ValuesProblem())
/////////////////////////////////////////////////////////////////////////////
class TWholeFileChecker
{
private:
    TThreadTest* m_Test;

public:
    explicit TWholeFileChecker(TThreadTest& test);

    TASWTomlResult operator()(TWriteCall const& call) const;
};

void CountUp(TThreadTest& test, std::size_t index);
void FailEdits(TThreadTest& test, std::size_t index);
std::vector<TGenerationServer> GenerationServers(int generation);
void LoadAppFile(TASWBaseToml* toml);
void LoadRepeatedly(TThreadTest& test, std::size_t index);
void LockReader(TLockCounters& counters, int iterations);
void LockWriter(TLockCounters& counters, int iterations);
TASWTomlDocument ParsedDocument(std::string_view text);
void ReadCounts(TThreadTest& test, std::size_t index);
void ReadDuringFileOperations(TThreadTest& test, std::size_t index);
void ReadWithOptions(TThreadTest& test, std::size_t index);
bool SameResult(TASWTomlResult const& first, TASWTomlResult const& second);
void SaveAsAppFile(TASWBaseToml* toml);
void SaveRepeatedly(TThreadTest& test, std::size_t index);
TASWTomlResult SeamFailure(ETomlCode code, std::string const& filePath, int systemError);
std::string ServersProblem(std::vector<TGenerationServer> const& servers);
void SetComments(TThreadTest& test, std::size_t index);
void SetCountAndMask(TThreadTest& test, std::size_t index);
void SharedReader(ASWToml::Detail::TSharedMutex& shared, std::atomic<bool>& inside);
void SwitchCallbacks(TThreadTest& test, std::size_t index);
void SwitchOptions(TThreadTest& test, std::size_t index);
void UseScratchTable(TThreadTest& test, std::size_t index);
std::string ValuesProblem(TASWTomlDocument const& document, std::size_t writers, int largest);
void WriteServers(TThreadTest& test, std::size_t index);

//---------------------------------------------------------------------------
/*
    CountUp

    A writer thread: sets WriterPath(index) to 1, 2 and so on, to at least test.Iterations, and on until test.Left
    is 0, at most to test.Largest.
*/
void CountUp(TThreadTest& test, std::size_t index)
{
    std::mt19937 generator(ThreadsSeed + static_cast<std::uint32_t>(index));
    for (int value = 1; value <= test.Largest; ++value)
    {
        TASWTomlResult const result = test.Toml.SetValue(WriterPath(index), value);
        if (!result.IsOk())
            test.Failures.Add("SetValue() should succeed: " + result.Message);

        if (value >= test.Iterations && test.Left == 0)
            break;

        MaybeYield(generator);
    }
}
//---------------------------------------------------------------------------
/*
    FailEdits

    Renames a missing key test.Iterations times, each a NotFound failure that is reported, then counts itself out of
    test.Left.
*/
void FailEdits(TThreadTest& test, std::size_t index)
{
    std::mt19937 generator(ThreadsSeed + static_cast<std::uint32_t>(index));
    for (int i = 0; i < test.Iterations; ++i)
    {
        TASWTomlResult const result = test.Toml.Rename("missing", "other");
        if (result.Code != ETomlCode::NotFound)
            test.Failures.Add("renaming a missing key should be NotFound: " + result.Message);

        MaybeYield(generator);
    }

    --test.Left;
}
//---------------------------------------------------------------------------
/*
    GenerationServers

    The three elements of "servers" of generation 'generation': hosts h0 to h2, ports 1000 to 1002.
*/
std::vector<TGenerationServer> GenerationServers(int generation)
{
    std::vector<TGenerationServer> servers;
    for (int i = 0; i < 3; ++i)
        servers.push_back(TGenerationServer{ "h" + std::to_string(i), 1000 + i, generation });

    return servers;
}
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
    LoadRepeatedly

    A file thread: test.FileOperations times, loads test.FilePath, and every other time a text with each writer's
    count 0 (LoadText(), which keeps the file path), then counts itself out of test.Left.
*/
void LoadRepeatedly(TThreadTest& test, std::size_t index)
{
    std::mt19937 generator(ThreadsSeed + static_cast<std::uint32_t>(index));
    std::string text;
    for (std::size_t i = 0; i < test.Writers; ++i)
        text += "[writer" + std::to_string(i) + "]\ncount = 0\n";

    for (int i = 0; i < test.FileOperations; ++i)
    {
        TASWTomlResult const result = i % 2 == 0 ? test.Toml.Load(test.FilePath) : test.Toml.LoadText(text);
        if (!result.IsOk())
            test.Failures.Add("Load() and LoadText() should succeed: " + result.Message);

        MaybeYield(generator);
    }

    --test.Left;
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
    ReadCounts

    A reader thread of Edits_LookupsNeverSeeAValueGoBack: test.Iterations times, reads a writer's count or the servers,
    checking that the count converts and never goes back, and that the servers are all of one generation that never
    goes back; every 50 reads, checks a copy of the document (see ValuesProblem()).
*/
void ReadCounts(TThreadTest& test, std::size_t index)
{
    std::mt19937 generator(ThreadsSeed + static_cast<std::uint32_t>(index));
    std::vector<int> last(test.Writers + 1, 0);
    for (int i = 0; i < test.Iterations; ++i)
    {
        std::size_t const key = generator() % (test.Writers + 1);
        if (key < test.Writers)
        {
            int value = -1;
            TASWTomlResult const result = test.Toml.TryGetValue(WriterPath(key), value);
            if (!result.IsOk())
                test.Failures.Add("each count should convert: " + result.Message);
            else if (value < last[key] || value > test.Iterations)
                test.Failures.Add(WriterPath(key) + " shouldn't go back: " + std::to_string(value) + " after " +
                    std::to_string(last[key]));
            else
                last[key] = value;
        }
        else
        {
            std::vector<TGenerationServer> servers;
            TASWTomlResult const result = test.Toml.TryGetValue("servers", servers);
            std::string const problem = result.IsOk() ? ServersProblem(servers) : result.Message;
            if (!problem.empty())
                test.Failures.Add("the servers should be read whole: " + problem);
            else if (servers[0].Generation < last[key])
                test.Failures.Add("the servers' generation shouldn't go back");
            else
                last[key] = servers[0].Generation;
        }

        if (i % 50 == 0)
        {
            std::string const problem = ValuesProblem(test.Toml.GetDocument(), test.Writers, test.Iterations);
            if (!problem.empty())
                test.Failures.Add("in a copy of the document, " + problem);
        }

        MaybeYield(generator);
    }
}
//---------------------------------------------------------------------------
/*
    ReadDuringFileOperations

    A reader thread of LoadsAndSaves_EverySavedFileIsWhole: reads a writer's count, which a load may set back but is
    always a number in range, and checks the file path stays; at least test.Iterations times, and on until test.Left
    is 0, at most test.Largest times.
*/
void ReadDuringFileOperations(TThreadTest& test, std::size_t index)
{
    std::mt19937 generator(ThreadsSeed + static_cast<std::uint32_t>(index));
    for (int i = 0; i < test.Largest; ++i)
    {
        std::size_t const key = generator() % test.Writers;
        int value = -1;
        TASWTomlResult const result = test.Toml.TryGetValue(WriterPath(key), value);
        if (!result.IsOk() || value < 0 || value > test.Largest)
            test.Failures.Add(WriterPath(key) + " should be a number in range: " + result.Message);

        if (test.Toml.GetFilePath() != test.FilePath)
            test.Failures.Add("the file path should stay the same");

        if (i >= test.Iterations && test.Left == 0)
            break;

        MaybeYield(generator);
    }
}
//---------------------------------------------------------------------------
/*
    ReadWithOptions

    A reader thread of SetOptions_LookupsSeeTheOldOrTheNewOptions: test.Iterations times, checks that writer 0's count
    converts and never goes back and that its mask (written in either base) converts; every 50 reads, checks a copy of
    the document (see ValuesProblem()).
*/
void ReadWithOptions(TThreadTest& test, std::size_t index)
{
    std::mt19937 generator(ThreadsSeed + static_cast<std::uint32_t>(index));
    int last = 0;
    for (int i = 0; i < test.Iterations; ++i)
    {
        int value = -1;
        TASWTomlResult const count = test.Toml.TryGetValue(WriterPath(0), value);
        if (!count.IsOk() || value < last || value > test.Iterations)
            test.Failures.Add("the count should convert and never go back: " + std::to_string(value) + " after " +
                std::to_string(last));
        else
            last = value;

        int mask = -1;
        TASWTomlResult const maskResult = test.Toml.TryGetValue("writer0.mask", mask);
        if (!maskResult.IsOk())
            test.Failures.Add("the mask should convert: " + maskResult.Message);

        if (i % 50 == 0)
        {
            std::string const problem = ValuesProblem(test.Toml.GetDocument(), 1, test.Iterations);
            if (!problem.empty())
                test.Failures.Add("in a copy of the document, " + problem);
        }

        MaybeYield(generator);
    }
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
    SaveRepeatedly

    A file thread: saves the object test.FileOperations times, then counts itself out of test.Left.
*/
void SaveRepeatedly(TThreadTest& test, std::size_t index)
{
    std::mt19937 generator(ThreadsSeed + static_cast<std::uint32_t>(index));
    for (int i = 0; i < test.FileOperations; ++i)
    {
        TASWTomlResult const result = test.Toml.Save();
        if (!result.IsOk())
            test.Failures.Add("Save() should succeed: " + result.Message);

        MaybeYield(generator);
    }

    --test.Left;
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
    ServersProblem

    What is wrong with 'servers', the servers read in one read: "" when they are GenerationServers() of one generation.
*/
std::string ServersProblem(std::vector<TGenerationServer> const& servers)
{
    if (servers.empty())
        return "there should be servers";

    std::vector<TGenerationServer> const expected = GenerationServers(servers[0].Generation);
    if (servers.size() != expected.size())
        return "there should be " + std::to_string(expected.size()) + " servers, not " + std::to_string(servers.size());

    for (std::size_t i = 0; i < servers.size(); ++i)
    {
        TGenerationServer const& server = servers[i];
        if (server.Host != expected[i].Host || server.Port != expected[i].Port ||
            server.Generation != expected[i].Generation)
        {
            return "server " + std::to_string(i) + " should be " + expected[i].Host + " of generation " +
                std::to_string(expected[i].Generation) + ", not " + server.Host + " of generation " +
                std::to_string(server.Generation);
        }
    }

    return std::string();
}
//---------------------------------------------------------------------------
/*
    SetComments

    test.Iterations times, sets the file comment, the comment of a writer's table or the trailing comment of its
    count.
*/
void SetComments(TThreadTest& test, std::size_t index)
{
    std::mt19937 generator(ThreadsSeed + static_cast<std::uint32_t>(index));
    for (int i = 0; i < test.Iterations; ++i)
    {
        std::string const text = "Comment " + std::to_string(i);
        std::size_t const which = generator() % 3;
        std::size_t const writer = generator() % test.Writers;
        TASWTomlResult result;
        switch (which)
        {
            case 0:
                result = test.Toml.SetComment("", text);
                break;

            case 1:
                result = test.Toml.SetComment("writer" + std::to_string(writer), text);
                break;

            default:
                result = test.Toml.SetTrailingComment(WriterPath(writer), text);
                break;
        }

        if (!result.IsOk())
            test.Failures.Add("setting a comment should succeed: " + result.Message);

        MaybeYield(generator);
    }
}
//---------------------------------------------------------------------------
/*
    SetCountAndMask

    The writer thread of SetOptions_LookupsSeeTheOldOrTheNewOptions: sets writer 0's count and its mask to 1, 2 and so
    on to test.Iterations, so each is written with the options of the moment.
*/
void SetCountAndMask(TThreadTest& test, std::size_t index)
{
    std::mt19937 generator(ThreadsSeed + static_cast<std::uint32_t>(index));
    for (int value = 1; value <= test.Iterations; ++value)
    {
        TASWTomlResult const count = test.Toml.SetValue(WriterPath(0), value);
        TASWTomlResult const mask = test.Toml.SetValue("writer0.mask", value);
        if (!count.IsOk() || !mask.IsOk())
            test.Failures.Add("SetValue() should succeed: " + count.Message + mask.Message);

        MaybeYield(generator);
    }
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
/*
    SwitchCallbacks

    Sets the log callback to test.Second and test.First in turn, at least test.Iterations times, and on until test.Left
    is 0, at most test.Largest times.
*/
void SwitchCallbacks(TThreadTest& test, std::size_t index)
{
    std::mt19937 generator(ThreadsSeed + static_cast<std::uint32_t>(index));
    for (int i = 0; i < test.Largest; ++i)
    {
        test.Toml.SetLogCallback(i % 2 == 0 ? test.Second : test.First);
        if (i >= test.Iterations && test.Left == 0)
            break;

        MaybeYield(generator);
    }
}
//---------------------------------------------------------------------------
/*
    SwitchOptions

    test.Iterations times, gives the object test.Hexadecimal and the default options in turn (the default ones last,
    for an even count).
*/
void SwitchOptions(TThreadTest& test, std::size_t index)
{
    std::mt19937 generator(ThreadsSeed + static_cast<std::uint32_t>(index));
    TASWTomlOptions const defaults;
    for (int i = 0; i < test.Iterations; ++i)
    {
        TASWTomlResult const result = test.Toml.SetOptions(i % 2 == 0 ? test.Hexadecimal : defaults);
        if (!result.IsOk())
            test.Failures.Add("SetOptions() should succeed: " + result.Message);

        MaybeYield(generator);
    }
}
//---------------------------------------------------------------------------
/*
    UseScratchTable

    test.Iterations times, sets a key of the table "scratch" (adding the table), renames the key, and removes the
    table.
*/
void UseScratchTable(TThreadTest& test, std::size_t index)
{
    std::mt19937 generator(ThreadsSeed + static_cast<std::uint32_t>(index));
    for (int i = 0; i < test.Iterations; ++i)
    {
        TASWTomlResult const set = test.Toml.SetValue("scratch.round", i);
        TASWTomlResult const renamed = test.Toml.Rename("scratch.round", "done");
        bool const removed = test.Toml.Remove("scratch");
        if (!set.IsOk() || !renamed.IsOk() || !removed)
            test.Failures.Add("the scratch table should be set, renamed and removed: " + set.Message + renamed.Message);

        MaybeYield(generator);
    }
}
//---------------------------------------------------------------------------
/*
    ValuesProblem

    What is wrong with 'document', a copy of a thread test's object or a file it saved: "" when its text reads back as
    it is, each of WriterPath(0) to WriterPath(writers - 1) is an int from 0 to 'largest', and the servers, if there
    are any, are all of one generation (see ServersProblem()).
*/
std::string ValuesProblem(TASWTomlDocument const& document, std::size_t writers, int largest)
{
    std::string const text = document.ToText();
    TASWTomlDocument reread;
    TASWTomlResult const parsed = reread.Parse(text, document.GetOptions());
    if (!parsed.IsOk() || reread.ToText() != text)
        return "the text should read back as it is: " + parsed.Message;

    for (std::size_t i = 0; i < writers; ++i)
    {
        TASWTomlValue const* const value = document.Find(WriterPath(i));
        int count = -1;
        bool converts = false;
        if (value != nullptr)
            converts = ASWToml::TASWTomlConverter<int>::FromValue(*value, count, document.GetOptions().Values).IsOk();

        if (!converts || count < 0 || count > largest)
            return WriterPath(i) + " should be a number from 0 to " + std::to_string(largest);
    }

    TASWTomlValue const* const value = document.Find("servers");
    if (value == nullptr)
        return std::string();

    std::vector<TGenerationServer> servers;
    TASWTomlResult const converted = ASWToml::TASWTomlConverter<std::vector<TGenerationServer> >::FromValue(*value,
        servers, document.GetOptions().Values);
    if (!converted.IsOk())
        return "the servers should convert: " + converted.Message;

    std::string const problem = ServersProblem(servers);

    return problem.empty() ? std::string() : "in the servers, " + problem;
}
//---------------------------------------------------------------------------
/*
    WriteServers

    test.Iterations times, sets all the servers to the next generation, in one edit (a merge of the array of tables).
*/
void WriteServers(TThreadTest& test, std::size_t index)
{
    std::mt19937 generator(ThreadsSeed + static_cast<std::uint32_t>(index));
    for (int generation = 1; generation <= test.Iterations; ++generation)
    {
        TASWTomlResult const result = test.Toml.SetValue("servers", GenerationServers(generation));
        if (!result.IsOk())
            test.Failures.Add("setting the servers should succeed: " + result.Message);

        MaybeYield(generator);
    }
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
// TFileReentrant
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TFileReentrant::TFileReentrant(TASWBaseToml& toml, std::string const& filePath, EFileCall& next,
    TMessageVector& messages, TResultVector& results)
    : m_Toml(&toml),
      m_FilePath(filePath),
      m_Next(&next),
      m_Messages(&messages),
      m_Results(&results)
{
}
//---------------------------------------------------------------------------
void TFileReentrant::operator()(TASWTomlMessage const& message) const
{
    m_Messages->push_back(message);
    EFileCall const next = *m_Next;
    *m_Next = EFileCall::None;
    if (next == EFileCall::Load)
        m_Results->push_back(m_Toml->Load(m_FilePath));
    else if (next == EFileCall::Save)
        m_Results->push_back(m_Toml->Save());
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


/////////////////////////////////////////////////////////////////////////////
// TWholeFileChecker
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TWholeFileChecker::TWholeFileChecker(TThreadTest& test)
    : m_Test(&test)
{
}
//---------------------------------------------------------------------------
TASWTomlResult TWholeFileChecker::operator()(TWriteCall const& call) const
{
    TASWTomlResult result = m_Test->Toml.DefaultWrite(call);
    if (!result.IsOk())
        return result;

    std::string contents;
    if (!ReadTestFile(Utf8ToPath(call.FilePath), contents) || contents != call.Contents)
    {
        m_Test->Failures.Add("a saved file should have the text that was saved");
    }
    else
    {
        std::string const problem = ValuesProblem(ParsedDocument(contents), m_Test->Writers, m_Test->Largest);
        if (!problem.empty())
            m_Test->Failures.Add("in a saved file, " + problem);
    }

    return result;
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
ASW_TEST(TTest_ASWToml_BaseCallback, Callback_MayLoadAndSave)
{
    // Arrange
    // The callback calls Save() for the Warning of a Load() of a missing file, and Load() for the failure of
    // SetOptions() and for a warning of Save(), which would deadlock if the file lock were held while reporting (the
    // CI's test timeout turns that into a failure)
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const file = folder.Path() / "app.toml";
    std::string const path = PathToUtf8(file);
    TSeamToml toml;
    TMessageVector messages;
    TResultVector inCallback;
    EFileCall next = EFileCall::None;
    toml.SetLogCallback(TFileReentrant(toml, path, next, messages, inCallback));
    TASWTomlOptions invalid;
    invalid.Writing.CommentPrefix = "//";

    // Act
    next = EFileCall::Save;
    TASWTomlResult const loaded = toml.Load(path);
    std::size_t const messagesOfLoad = messages.size();
    static_cast<void>(toml.SetValue("port", 80));
    next = EFileCall::Load;
    TASWTomlResult const refused = toml.SetOptions(invalid);
    bool const editKeptByLoad = toml.Contains("port");
    std::size_t const messagesOfSetOptions = messages.size() - messagesOfLoad;
    toml.OnWrite = TWarningWriter(toml);
    static_cast<void>(toml.SetValue("name", "app"));
    next = EFileCall::Load;
    TASWTomlResult const saved = toml.Save();
    std::size_t const messagesOfSave = messages.size() - messagesOfLoad - messagesOfSetOptions;

    // Assert
    std::string contents;
    CheckEquals(static_cast<int>(ETomlCode::FileNotFound), static_cast<int>(loaded.Code),
        "the missing file should be FileNotFound");
    CheckEquals(static_cast<int>(ETomlCode::InvalidOptions), static_cast<int>(refused.Code),
        "the invalid options should be refused");
    CheckTrue(saved.IsOk(), "Save() should succeed: " + saved.Message);
    CheckEquals(std::size_t(3), inCallback.size(), "the callback should have saved once and loaded twice");
    for (TASWTomlResult const& result : inCallback)
        CheckTrue(result.IsOk(), "each call in the callback should succeed: " + result.Message);

    CheckEquals(std::size_t(1), messagesOfLoad, "Load() should report the missing file, and the callback's Save() "
        "nothing");
    CheckEquals(std::size_t(1), messagesOfSetOptions, "SetOptions() should report its failure, and the callback's "
        "Load() nothing");
    CheckFalse(editKeptByLoad, "the callback's Load() should replace the edited document with the saved file");
    CheckEquals(std::size_t(1), messagesOfSave, "Save() should report its warning, and the callback's Load() nothing");
    CheckTrue(ReadTestFile(file, contents), "the file should be readable");
    CheckEquals(toml.ToText(), contents, "the callback's Load() should load the file the Save() wrote");
    CheckEquals(std::string("app"), toml.GetValue("name", ""), "with its edit");
    CheckFalse(toml.IsModified(), "the loaded document shouldn't be modified");
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


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_BaseThreads
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_BaseThreads::TTest_ASWToml_BaseThreads()
    : inherited("ASWToml_BaseThreads_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_BaseThreads::~TTest_ASWToml_BaseThreads()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_BaseThreads::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_BaseThreads::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_BaseThreads::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_BaseThreads::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseThreads, Edits_LookupsNeverSeeAValueGoBack)
{
    // Arrange
    // Writers count their own keys up and another sets all the servers (an array of tables) to the next generation in
    // one edit. Readers check that each count converts and never goes back, that the servers are all of one generation
    // that never goes back, and that a copy of the document reads back as it is, while comments are set and a scratch
    // table is added, renamed and removed.
    TThreadTest test;
    test.Writers = 4;
    test.Iterations = 1000;
    test.Largest = test.Iterations;
    std::size_t const readers = 3;
    for (std::size_t i = 0; i < test.Writers; ++i)
        static_cast<void>(test.Toml.SetValue(WriterPath(i), 0));

    static_cast<void>(test.Toml.SetValue("servers", GenerationServers(0)));
    std::atomic<int> reports{ 0 };
    test.Toml.SetLogCallback(TCountingCallback(reports));
    TWorkVector work;
    for (std::size_t i = 0; i < test.Writers; ++i)
        work.push_back(std::bind(CountUp, std::ref(test), i));

    for (std::size_t i = 0; i < readers; ++i)
        work.push_back(std::bind(ReadCounts, std::ref(test), test.Writers + i));

    std::size_t const others = test.Writers + readers;
    work.push_back(std::bind(WriteServers, std::ref(test), others));
    work.push_back(std::bind(SetComments, std::ref(test), others + 1));
    work.push_back(std::bind(UseScratchTable, std::ref(test), others + 2));

    // Act
    RunTogether(work, test.Failures);

    // Assert
    std::vector<TGenerationServer> const servers = test.Toml.GetValue("servers", std::vector<TGenerationServer>());
    CheckEquals(std::string(), test.Failures.Summary(), "no thread should find a problem");
    for (std::size_t i = 0; i < test.Writers; ++i)
        CheckEquals(test.Iterations, test.Toml.GetValue(WriterPath(i), -1),
            "each count should have its writer's last value");

    CheckEquals(std::string(), ServersProblem(servers), "the servers should be of one generation");
    CheckTrue(!servers.empty() && servers[0].Generation == test.Iterations, "the last one");
    CheckFalse(test.Toml.Contains("scratch"), "the scratch table should be removed");
    CheckEquals(std::string(), ValuesProblem(test.Toml.GetDocument(), test.Writers, test.Iterations),
        "the document should read back as it is");
    CheckEquals(0, reports.load(), "nothing should be reported");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseThreads, LoadsAndSaves_EverySavedFileIsWhole)
{
    // Arrange
    // Writers count their own keys up while other threads save the object to one file, load it from that file (or a
    // text) and look up values, and WriteFile() reads each saved file back (see TWholeFileChecker). A load replaces the
    // document, so a count may go back, but it is always a number in range. The writers and the reader go on until the
    // file threads are done, since a file operation takes much longer than an edit.
    TThreadTest test;
    test.Writers = 2;
    test.Iterations = 300;
    test.Largest = 1000000;
    test.FileOperations = 8;
    test.Left = 3;
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    test.FilePath = PathToUtf8(folder.Path() / "app.toml");
    for (std::size_t i = 0; i < test.Writers; ++i)
        static_cast<void>(test.Toml.SetValue(WriterPath(i), 0));

    AssertTrue(test.Toml.SaveAs(test.FilePath).IsOk(), "the file should be saved");
    std::atomic<int> reports{ 0 };
    test.Toml.SetLogCallback(TCountingCallback(reports));
    test.Toml.OnWrite = TWholeFileChecker(test);
    TWorkVector work;
    for (std::size_t i = 0; i < test.Writers; ++i)
        work.push_back(std::bind(CountUp, std::ref(test), i));

    work.push_back(std::bind(SaveRepeatedly, std::ref(test), test.Writers));
    work.push_back(std::bind(SaveRepeatedly, std::ref(test), test.Writers + 1));
    work.push_back(std::bind(LoadRepeatedly, std::ref(test), test.Writers + 2));
    work.push_back(std::bind(ReadDuringFileOperations, std::ref(test), test.Writers + 3));

    // Act
    // Then each count is set to a value of its own, and the object is saved and loaded again
    RunTogether(work, test.Failures);
    for (std::size_t i = 0; i < test.Writers; ++i)
        static_cast<void>(test.Toml.SetValue(WriterPath(i), static_cast<int>(i) + 10));

    TASWTomlResult const saved = test.Toml.Save();
    TASWBaseToml reloaded;
    TASWTomlResult const loaded = reloaded.Load(test.FilePath);

    // Assert
    CheckEquals(std::string(), test.Failures.Summary(), "no thread should find a problem");
    CheckTrue(saved.IsOk() && loaded.IsOk(), "the last save and load should succeed");
    CheckFalse(test.Toml.IsModified(), "the saved object shouldn't be modified");
    CheckEquals(test.Toml.ToText(), reloaded.ToText(), "the file should load as the object saved it");
    for (std::size_t i = 0; i < test.Writers; ++i)
        CheckEquals(static_cast<int>(i) + 10, reloaded.GetValue(WriterPath(i), -1),
            "each count should have its last value");

    CheckEquals(0, reports.load(), "nothing should be reported");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseThreads, SetLogCallback_EachFailureIsReportedOnce)
{
    // Arrange
    // Threads make edits that fail, each of which is reported, while another thread switches between two callbacks
    // and a writer counts its key up (both until the failing threads are done); each failure goes to one of the
    // callbacks, once, and a callback isn't destroyed while it runs (see TCountingCallback)
    TThreadTest test;
    std::size_t const failers = 3;
    std::size_t const writer = failers + 1;
    test.Iterations = 2000;
    test.Largest = 1000000;
    test.Left = static_cast<int>(failers);
    test.First = TCountingCallback(test.FirstCount);
    test.Second = TCountingCallback(test.SecondCount);
    test.Toml.SetLogCallback(test.First);
    TWorkVector work;
    for (std::size_t i = 0; i < failers; ++i)
        work.push_back(std::bind(FailEdits, std::ref(test), i));

    work.push_back(std::bind(SwitchCallbacks, std::ref(test), failers));
    work.push_back(std::bind(CountUp, std::ref(test), writer));

    // Act
    RunTogether(work, test.Failures);

    // Assert
    int const count = test.Toml.GetValue(WriterPath(writer), -1);
    CheckEquals(std::string(), test.Failures.Summary(), "no thread should find a problem");
    CheckEquals(static_cast<int>(failers) * test.Iterations, test.FirstCount.load() + test.SecondCount.load(),
        "each failure should be reported once");
    CheckTrue(count >= test.Iterations && count <= test.Largest, "the count should have the writer's last value");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BaseThreads, SetOptions_LookupsSeeTheOldOrTheNewOptions)
{
    // Arrange
    // A thread switches between options that write integers in hexadecimal and the default ones, while a writer counts
    // its key up and sets a mask (each written as the options of the moment say), and readers check that the count
    // never goes back, the mask converts, and a copy of the document reads back as it is. Many other keys make reading
    // the text again take long enough that an edit made meanwhile would be lost without the lock.
    TThreadTest test;
    test.Iterations = 1000;
    std::size_t const readers = 3;
    std::string text = "[writer0]\ncount = 0\nmask = 0\n\n[other]\n";
    for (int i = 0; i < 200; ++i)
        text += "key" + std::to_string(i) + " = " + std::to_string(i) + "\n";

    static_cast<void>(test.Toml.LoadText(text));
    test.Hexadecimal.Values.IntegerBase = EIntegerBase::Hexadecimal;
    std::atomic<int> reports{ 0 };
    test.Toml.SetLogCallback(TCountingCallback(reports));
    TWorkVector work;
    work.push_back(std::bind(SwitchOptions, std::ref(test), 0));
    work.push_back(std::bind(SetCountAndMask, std::ref(test), 1));
    for (std::size_t i = 0; i < readers; ++i)
        work.push_back(std::bind(ReadWithOptions, std::ref(test), 2 + i));

    // Act
    // The last options set are the default ones
    RunTogether(work, test.Failures);

    // Assert
    CheckEquals(std::string(), test.Failures.Summary(), "no thread should find a problem");
    CheckTrue(test.Toml.GetValueOptions().IntegerBase == EIntegerBase::Auto, "the last options set should be kept");
    CheckEquals(test.Iterations, test.Toml.GetValue(WriterPath(0), -1), "the count should have the writer's last value");
    CheckEquals(test.Iterations, test.Toml.GetValue("writer0.mask", -1), "and the mask too");
    CheckEquals(std::string(), ValuesProblem(test.Toml.GetDocument(), 1, test.Iterations),
        "the document should read back as it is");
    CheckEquals(0, reports.load(), "nothing should be reported");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_BaseAccess, "Base")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_BaseCallback, "Base")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_BaseFile, "Base")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_BaseThreads, "Base")
