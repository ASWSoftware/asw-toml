/* **************************************************************************
Test_ASWToml_Settings.cpp
Author: Anthony S. West - ASW Software

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "Test_ASWToml_Settings.h"
//---------------------------------------------------------------------------
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWToml_Base.h"
#include "ASWToml_Converters.h"
#include "ASWToml_Document.h"
#include "ASWToml_Edits.h"
#include "ASWToml_File.h"
#include "ASWToml_Options.h"
#include "ASWToml_Path.h"
#include "ASWToml_Settings.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------
#include "UT_Helper_Converters.h"
#include "UT_Helper_FailingAlloc.h"
#include "UT_Helper_TempDir.h"
#include "UT_Helper_Threads.h"
//---------------------------------------------------------------------------

// Neither copyable nor movable
static_assert(!std::is_copy_constructible_v<ASWToml::TASWTomlTable>, "TASWTomlTable shouldn't be copyable");
static_assert(!std::is_move_constructible_v<ASWToml::TASWTomlTable>, "TASWTomlTable shouldn't be movable");
static_assert(!std::is_copy_constructible_v<ASWToml::TASWTomlSetting<int> >, "a setting shouldn't be copyable");
static_assert(!std::is_move_constructible_v<ASWToml::TASWTomlSetting<int> >, "a setting shouldn't be movable");

// A range only for the types operator< compares (giving one to another type is a compile error)
static_assert(ASWToml::Detail::LessComparable<int> && ASWToml::Detail::LessComparable<std::string>,
    "numbers and strings have a range");
static_assert(ASWToml::Detail::LessComparable<ASWUnitTests::EColor>, "enums have a range");
static_assert(!ASWToml::Detail::LessComparable<ASWUnitTests::TTestServer>, "a struct without operator< has none");
static_assert(!ASWToml::Detail::LessComparable<std::vector<ASWUnitTests::TTestServer> >,
    "nor a std::vector of it");
static_assert(std::is_same_v<ASWToml::Detail::TRangeEnd<int>, std::optional<int> >, "a range end of int");
static_assert(std::is_same_v<ASWToml::Detail::TRangeEnd<ASWUnitTests::TTestWord>, ASWToml::Detail::TNoRangeEnd>,
    "no range end for a type without operator<");

namespace ASWUnitTests
{

namespace
{

using namespace std::string_literals;

using ASWToml::EIntegerBase;
using ASWToml::ESeverity;
using ASWToml::ETomlCode;
using ASWToml::TASWBaseToml;
using ASWToml::TASWTomlDocument;
using ASWToml::TASWTomlEdits;
using ASWToml::TASWTomlMessage;
using ASWToml::TASWTomlOptions;
using ASWToml::TASWTomlPath;
using ASWToml::TASWTomlResult;
using ASWToml::TASWTomlSetting;
using ASWToml::TASWTomlTable;
using ASWToml::TASWTomlValue;
using ASWToml::TASWTomlValueOptions;
using ASWToml::TMessageVector;
using ASWToml::TStringVector;

using TClusterVector = std::vector<TTestCluster>;
using TCodeVector = std::vector<ETomlCode>;
using TColorMap = std::map<EColor, int>;
using TCountMap = std::unordered_map<std::string, int>;
using THostMap = std::map<std::string, TTestServer>;
using TIntSettingVector = std::vector<std::unique_ptr<TASWTomlSetting<int> > >;
using TLimitMap = std::map<std::string, int>;
using TNameMap = std::map<int, std::string>;
using TNestedVector = std::vector<std::vector<int> >;
using TServerVector = std::vector<TTestServer>;

// A text for TTestSettings with a value for some of its settings: a key with a trailing comment, a struct with a key
// it doesn't know, an array of tables whose elements have keys the struct doesn't know and a comment, and a map
constexpr char const* BatchText =
    "[general]\n"
    "name = \"Ann\"  # Hers\n"
    "level = 1\n"
    "\n"
    "[net.server]\n"
    "host = \"a\"\n"
    "port = 1\n"
    "extra = true\n"
    "\n"
    "[[net.servers]]\n"
    "# X's comment.\n"
    "host = \"x\"\n"
    "port = 2\n"
    "note = \"X\"\n"
    "\n"
    "[[net.servers]]\n"
    "host = \"y\"\n"
    "port = 3\n"
    "note = \"Y\"\n"
    "\n"
    "[net.limits]\n"
    "a = 1\n"
    "b = 2\n";

// The text a first run of TLoadSettings writes: every setting that isn't optional with its default value and its
// comment, the tables' comments and the file comment
constexpr char const* FirstRunText =
    "# Settings of the test.\n"
    "\n"
    "# What it looks like.\n"
    "[general]\n"
    "# The name.\n"
    "name = \"Guest\"\n"
    "# From 0 to 4.\n"
    "level = 2\n"
    "\n"
    "[logging]\n"
    "# Days kept.\n"
    "days = 30\n"
    "mask = 0xFF\n";


/////////////////////////////////////////////////////////////////////////////
// TTestPaint
//
// A struct whose value-initialized Color has no name, so a new element's default value can't be written
/////////////////////////////////////////////////////////////////////////////
struct TTestPaint
{
    std::string Name;
    EColor Color{};
};

// The paints of a setting
using TPaintVector = std::vector<TTestPaint>;

auto ASWTomlFields(TTestPaint const&)
{
    return std::tuple{ ASWToml::Field("name", &TTestPaint::Name), ASWToml::Field("color", &TTestPaint::Color) };
}


/////////////////////////////////////////////////////////////////////////////
// TTestPalette
//
// A struct with a field of paints, whose new elements' default values can't be written (see TTestPaint)
/////////////////////////////////////////////////////////////////////////////
struct TTestPalette
{
    std::vector<TTestPaint> Paints;
};

// The palettes of a setting
using TPaletteVector = std::vector<TTestPalette>;

auto ASWTomlFields(TTestPalette const&)
{
    return std::tuple{ ASWToml::Field("paints", &TTestPalette::Paints) };
}

// The themes of TAppSettings, written by name
enum class ETheme
{
    Light,
    Dark,
    System,
};

auto const& ASWTomlEnumNames(ETheme)
{
    static constexpr ASWToml::TASWTomlEnumName<ETheme> names[] = {
        { ETheme::Light, "Light" }, { ETheme::Dark, "Dark" }, { ETheme::System, "System" },
    };

    return names;
}


/////////////////////////////////////////////////////////////////////////////
// TAppServer
//
// A server of TAppSettings, whose field secure the application's earlier version didn't have
/////////////////////////////////////////////////////////////////////////////
struct TAppServer
{
    std::string Host;
    std::uint16_t Port = 80;
    bool Secure = false;
};

// The servers of TAppSettings
using TAppServerVector = std::vector<TAppServer>;

auto ASWTomlFields(TAppServer const&)
{
    return std::tuple{ ASWToml::Field("host", &TAppServer::Host), ASWToml::Field("port", &TAppServer::Port),
        ASWToml::Field("secure", &TAppServer::Secure) };
}


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
// TCodeRecorder
//
// A log callback that adds the code of each message to a vector, in room made before, so it allocates nothing itself
// (for the out-of-memory tests)
/////////////////////////////////////////////////////////////////////////////
class TCodeRecorder
{
private:
    TCodeVector* m_Codes;

public:
    explicit TCodeRecorder(TCodeVector& codes);

    void operator()(TASWTomlMessage const& message) const;
};


/////////////////////////////////////////////////////////////////////////////
// TReportCounter
//
// A log callback that adds 1 to a count for each message, from any thread, at once (unlike TCountingCallback, which
// gives up its time slice first, so a load that reports a hundred messages while many threads run would take seconds)
/////////////////////////////////////////////////////////////////////////////
class TReportCounter
{
private:
    std::atomic<int>* m_Count;

public:
    explicit TReportCounter(std::atomic<int>& count);

    void operator()(TASWTomlMessage const& message) const;
};


/////////////////////////////////////////////////////////////////////////////
// TPreloaded
//
// A TOML object that is given a text before a derived class's settings are bound to it, so they aren't applied to it:
// every message it reports is in Messages, and new lines end with LF
/////////////////////////////////////////////////////////////////////////////
class TPreloaded : public TASWBaseToml
{
public:
    TMessageVector Messages;
    bool Parsed = false; // Whether the text parsed

public:
    explicit TPreloaded(std::string_view text);

    // Value options that write an integer in hexadecimal
    static TASWTomlValueOptions Hexadecimal();
};


/////////////////////////////////////////////////////////////////////////////
// TLoadSettings
//
// A settings class for what Load() does: a file comment, tables with and without a comment, an optional setting, a
// range, and value options of a setting's own (see FirstRunText)
/////////////////////////////////////////////////////////////////////////////
class TLoadSettings : public TPreloaded
{
public:
    TASWTomlTable Root{ *this, "", "Settings of the test." };
    TASWTomlTable General{ *this, "general", "What it looks like." };
    TASWTomlSetting<std::string> Name{ General, "name", "Guest", "The name." };
    TASWTomlSetting<bool> Cheats{ General, "cheats", false, "", { .Optional = true } };
    TASWTomlSetting<unsigned> Level{ General, "level", 2, "From 0 to 4.", { .Min = 0, .Max = 4 } };

    TASWTomlTable Logging{ *this, "logging" };
    TASWTomlSetting<int> Days{ Logging, "days", 30, "Days kept." };
    TASWTomlSetting<int> Mask{ Logging, "mask", 255, "", { .Values = Hexadecimal() } };

public:
    explicit TLoadSettings(std::string_view text = std::string_view());
};


/////////////////////////////////////////////////////////////////////////////
// TTestSettings
//
// A settings class with a setting of each kind, whose document is a text as it is (see TPreloaded)
/////////////////////////////////////////////////////////////////////////////
class TTestSettings : public TPreloaded
{
public:

    TASWTomlTable General{ *this, "general", "The general settings." };
    TASWTomlSetting<std::string> Name{ General, "name", "Guest", "The name.", { .Validator = NotEmpty } };
    TASWTomlSetting<bool> Cheats{ General, "cheats", false, "", { .Optional = true } };
    TASWTomlSetting<unsigned> Level{ General, "level", 2, "From 0 to 4.", { .Min = 0, .Max = 4 } };
    TASWTomlSetting<int> AtLeast{ General, "at_least", 5, "", { .Min = 1 } };
    TASWTomlSetting<int> AtMost{ General, "at_most", 5, "", { .Max = 9 } };
    TASWTomlSetting<double> Zoom{ General, "zoom", 1.0 };
    TASWTomlSetting<EColor> Color{ General, "color", EColor::Red, "The color." };
    TASWTomlSetting<EColor> Unnamed{ General, "unnamed", EColor::Unlisted };
    TASWTomlSetting<std::filesystem::path> Folder{ General, "folder", std::filesystem::path("logs") };
    TASWTomlSetting<TStringVector> Columns{ General, "columns", TStringVector{ "name", "size" } };
    TASWTomlSetting<int> Mask{ General, "mask", 255, "", { .Values = Hexadecimal() } };
    TASWTomlSetting<int> Plain{ General, "plain", 8 };
    TASWTomlSetting<TTestWord> Word{ General, "word", TTestWord{ "word" }, "", { .Validator = NotLong } };

    TASWTomlTable Net{ *this, "net" };
    TASWTomlSetting<TTestServer> Server{ Net, "server", TTestServer{ "localhost", 80, true, std::nullopt },
        "The server." };
    TASWTomlSetting<TServerVector> Servers{ Net, "servers", TServerVector(), "The servers." };
    TASWTomlSetting<TLimitMap> Limits{ Net, "limits", TLimitMap{ { "a", 1 } } };

public:
    explicit TTestSettings(std::string_view text = std::string_view());

    // The validator of a text that can't be empty
    static std::string NotEmpty(std::string const& value);

    // The validator of a word of at most 8 letters
    static std::string NotLong(TTestWord const& value);
};


/////////////////////////////////////////////////////////////////////////////
// TSaveAsOnFirst
//
// A log callback that adds each message to a vector and, on the first one, saves its TOML object as another file, so
// the object's file path is that file when the messages after it are reported
/////////////////////////////////////////////////////////////////////////////
class TSaveAsOnFirst
{
private:
    TASWBaseToml* m_Toml;
    std::string m_OtherFile;
    TMessageVector* m_Messages;

public:
    TSaveAsOnFirst(TASWBaseToml& toml, std::string otherFile, TMessageVector& messages);

    void operator()(TASWTomlMessage const& message) const;
};


/////////////////////////////////////////////////////////////////////////////
// TSetDuringSave
//
// TTestSettings whose saves write no file, but set a setting while the file would be written: Level to 3 (a key added
// with its comment) or Zoom to 2.0 (a key that is there, without a comment)
/////////////////////////////////////////////////////////////////////////////
class TSetDuringSave : public TTestSettings
{
private:
    bool m_AddKey;

public:
    // Settings of 'text' whose save sets Level when 'addKey' is set, else Zoom
    TSetDuringSave(std::string_view text, bool addKey);

protected:
    TASWTomlResult WriteFile(std::string const& filePath, std::string_view contents,
        ASWToml::TASWTomlSavingOptions const& options, TMessageVector& warnings) override;
};


/////////////////////////////////////////////////////////////////////////////
// TThrowingValidator
//
// A validator that throws std::runtime_error("boom")
/////////////////////////////////////////////////////////////////////////////
class TThrowingValidator
{
public:
    std::string operator()(int const& value) const;
};


/////////////////////////////////////////////////////////////////////////////
// TAppSettings
//
// The settings of an application, declared as the example's settings class declares them: a file comment, a
// validator, an enum by name, an optional setting, a range, a key the application's earlier version called log_days,
// a path, a vector of strings, and servers in an array of tables (see TAppServer)
/////////////////////////////////////////////////////////////////////////////
class TAppSettings : public TASWBaseToml
{
private:
    // The validator of a text that can't be empty
    static std::string NotEmpty(std::string const& value);

public:
    // The root table holds the keys before the first header; its comment is the file comment
    TASWTomlTable FileTop{ *this, "", "Settings of the ASWToml example." };

    TASWTomlTable General{ *this, "general", "What the application looks like." };
    TASWTomlSetting<std::string> UserName{ General, "user_name", "Guest", "The name in the title bar.",
        { .Validator = NotEmpty } };
    TASWTomlSetting<ETheme> Theme{ General, "theme", ETheme::System, "Light, Dark or System." };
    TASWTomlSetting<bool> EnableCheats{ General, "enable_cheats", false, "", { .Optional = true } };

    TASWTomlTable Logging{ *this, "logging" };
    TASWTomlSetting<unsigned> Level{ Logging, "level", 2, "From 0 (errors only) to 4 (everything).",
        { .Min = 0, .Max = 4 } };
    TASWTomlSetting<unsigned> DaysKept{ Logging, "days_kept", 30,
        "How many days the logs are kept; 0 keeps them forever.", { .OldKeys = { "log_days" } } };
    TASWTomlSetting<std::filesystem::path> Folder{ Logging, "folder", std::filesystem::path("logs"),
        "Relative to the folder of this file." };

    TASWTomlTable View{ *this, "view" };
    TASWTomlSetting<double> Zoom{ View, "zoom", 1.0, "From 0.25 to 4.", { .Min = 0.25, .Max = 4.0 } };
    TASWTomlSetting<TStringVector> Columns{ View, "columns", TStringVector{ "name", "size", "date" },
        "The columns shown, in order." };

    TASWTomlTable Network{ *this, "network", "Where the application connects." };
    TASWTomlSetting<int> Timeout{ Network, "timeout", 30, "In seconds.", { .Min = 1 } };
    TASWTomlSetting<TAppServerVector> Servers{ Network, "servers",
        TAppServerVector{ TAppServer{ "localhost", 8080, false } }, "The servers, tried in order." };
};


/////////////////////////////////////////////////////////////////////////////
// TBindingsTest
//
// What the threads of Bindings_ComeAndGoWhileLoading share: the object with a table of settings that stay (key0 = 0,
// key1 = 1, ..., each with a comment), the problems the threads find, and how much work each does. Binder threads
// bind settings of their own at least Iterations times, and on until Loading is false, at most Largest times; the
// loader loads Loads times.
/////////////////////////////////////////////////////////////////////////////
struct TBindingsTest
{
    TASWBaseToml Toml;
    TASWTomlTable Stays{ Toml, "stays", "Settings that stay." };
    TIntSettingVector Stayers;
    TThreadFailures Failures;
    std::size_t Binders = 0;
    int Iterations = 0;
    int Largest = 0;
    int Loads = 0;
    std::atomic<bool> Loading{ true };
};


/////////////////////////////////////////////////////////////////////////////
// TWholeSettingsTest
//
// What the threads of GetAndSet_SeeWholeSettingsWhileLoading share: the object with its tables and settings, the
// problems the threads find, and how much work each does. Writer threads count their settings up from 1, to at least
// Iterations, and threads that go on until the file threads are done wait for Left to reach 0, at most Largest times.
/////////////////////////////////////////////////////////////////////////////
struct TWholeSettingsTest
{
    TASWBaseToml Toml;
    TASWTomlTable Values{ Toml, "values", "The counts." };
    TIntSettingVector Counts; // values.count0, values.count1, ...: from 0 to Largest, each with a comment
    std::unique_ptr<TASWTomlSetting<int> > Scratch; // values.scratch, optional
    std::unique_ptr<TASWTomlSetting<TServerVector> > Servers; // values.servers, an array of tables
    TASWTomlTable Others{ Toml, "others", "Other settings." };
    TIntSettingVector Fillers; // others.key0, others.key1, ...: each with a comment
    TThreadFailures Failures;
    int Iterations = 0;
    int Largest = 0;
    int FileOperations = 0; // The loads or saves of each file thread
    std::atomic<int> Left{ 0 }; // The file threads still running
    std::string FilePath; // The file the loads and saves use
    std::string InvalidText; // A text whose values aren't valid and that lacks most keys, which a load repairs
    std::string InvalidPath; // A file the loader writes InvalidText to, then loads
};

//---------------------------------------------------------------------------

void AddRandomStep(TTestSettings& settings, TTestSettings& twin, TASWTomlEdits& edits, std::mt19937& generator,
    TASWTomlResult& failure);
void BindAndUnbind(TBindingsTest& test, std::size_t index);
TServerVector BinderServers(std::size_t index);
std::string BindingsProblem(TASWTomlDocument const& document, std::size_t stayers, std::size_t binders);
void CountSettingUp(TWholeSettingsTest& test, std::size_t index);
TTestCluster DefaultCluster();
TASWTomlOptions LfOptions();
void LoadAndCheckBindings(TBindingsTest& test, std::size_t index);
void LoadSettingsRepeatedly(TWholeSettingsTest& test, std::size_t index);
std::string MessageKinds(TMessageVector const& messages);
std::string MessagesText(TMessageVector const& messages);
std::string OddOnly(int const& value);
std::string OneServer(TServerVector const& value);
void ReadWholeSettings(TWholeSettingsTest& test, std::size_t index);
std::string RemovedFirstCluster(std::string const& first, std::string const& second);
std::string ResultProblem(TASWTomlResult const& result, TASWTomlResult const& expected);
void SaveSettingsRepeatedly(TWholeSettingsTest& test, std::size_t index);
void SetServerPorts(TWholeSettingsTest& test, std::size_t index);
void UseScratchSetting(TWholeSettingsTest& test, std::size_t index);
std::string WholeSettingsProblem(TASWTomlDocument const& document, std::size_t writers, std::size_t others);

//---------------------------------------------------------------------------
/*
    AddRandomStep

    Adds a random edit to 'edits' (a setting's Set() given them, of a random value, some of which the setting refuses,
    or an edit of a path of 'settings') and makes the same edit on 'twin' (the setting's Set(), or the object's
    operation of the edit), setting 'failure' to the first of twin's edits that fails, if it is still Ok (but for a
    value a setting refuses: its Set() given the edits adds nothing, and its Set() changes nothing)
*/
void AddRandomStep(TTestSettings& settings, TTestSettings& twin, TASWTomlEdits& edits, std::mt19937& generator,
    TASWTomlResult& failure)
{
    static char const* const names[] = { "a", "Bea", "" };
    static EColor const colors[] = { EColor::Red, EColor::Green, EColor::Blue, EColor::Unlisted };
    static char const* const hosts[] = { "x", "y", "z" };
    static char const* const removed[] = { "general.name", "net.server", "net.servers[0]", "net.limits", "general" };
    static char const* const commented[] = { "general", "general.level", "net.server" };
    int const kind = static_cast<int>(generator() % 11);
    int const number = static_cast<int>(generator() % 7);
    // A setting's Set() given the edits: when it fails, it adds nothing, and Set() changes nothing
    TASWTomlResult added;
    TASWTomlResult result;
    switch (kind)
    {
        case 0:
            added = settings.Name.Set(edits, names[number % 3]);
            result = twin.Name.Set(names[number % 3]);
            break;

        case 1:
            added = settings.Level.Set(edits, static_cast<unsigned>(number));
            result = twin.Level.Set(static_cast<unsigned>(number));
            break;

        case 2:
            added = settings.Zoom.Set(edits, number * 0.5);
            result = twin.Zoom.Set(number * 0.5);
            break;

        case 3:
            added = settings.Color.Set(edits, colors[number % 4]);
            result = twin.Color.Set(colors[number % 4]);
            break;

        case 4:
        {
            TTestServer server{ "h" + std::to_string(number), static_cast<std::uint16_t>(number), number % 2 == 0,
                std::nullopt };
            if (number % 3 == 0)
                server.Comment = "c";

            added = settings.Server.Set(edits, server);
            result = twin.Server.Set(server);
            break;
        }

        case 5:
        {
            TServerVector servers;
            for (int i = 0; i < number % 4; ++i)
            {
                TTestServer const server{ hosts[(number + i) % 3], static_cast<std::uint16_t>(i), true, std::nullopt };
                servers.push_back(server);
            }

            added = settings.Servers.Set(edits, servers);
            result = twin.Servers.Set(servers);
            break;
        }

        case 6:
        {
            TLimitMap limits{ { "a", number } };
            if (number % 2 == 0)
                limits["c"] = number + 1;

            added = settings.Limits.Set(edits, limits);
            result = twin.Limits.Set(limits);
            break;
        }

        case 7:
            added = settings.Cheats.Set(edits, number % 2 == 0);
            result = twin.Cheats.Set(number % 2 == 0);
            break;

        case 8:
            edits.Remove(removed[number % 5]);
            static_cast<void>(twin.Remove(removed[number % 5]));
            break;

        case 9:
            edits.SetValue("net.server.extra", number);
            result = twin.SetValue("net.server.extra", number);
            break;

        default:
            edits.SetComment(commented[number % 3], "Raw " + std::to_string(number));
            result = twin.SetComment(commented[number % 3], "Raw " + std::to_string(number));
            break;
    }

    if (failure.IsOk() && added.IsOk())
        failure = result;
}
//---------------------------------------------------------------------------
/*
    BindAndUnbind

    A binder thread of Bindings_ComeAndGoWhileLoading: binds the table binder<index> with a comment, and in it the
    setting key (the index) and the array of tables servers (BinderServers()), each with a comment, checks that they
    read their default values, and destroys them again; at least test.Iterations times, and on until test.Loading is
    false, at most test.Largest times.
*/
void BindAndUnbind(TBindingsTest& test, std::size_t index)
{
    std::mt19937 generator(ThreadsSeed + static_cast<std::uint32_t>(index));
    std::string const name = "binder" + std::to_string(index);
    TServerVector const defaultServers = BinderServers(index);
    for (int i = 0; i < test.Largest; ++i)
    {
        {
            TASWTomlTable const table{ test.Toml, name, "A binder's table." };
            TASWTomlSetting<int> const key{ table, "key", static_cast<int>(index), "A binder's key." };
            TASWTomlSetting<TServerVector> const servers{ table, "servers", defaultServers, "A binder's servers." };
            MaybeYield(generator);
            if (key.Get() != static_cast<int>(index) || servers.Get() != defaultServers)
                test.Failures.Add(name + "'s settings should read their default values");
        }

        if (i >= test.Iterations && !test.Loading)
            break;

        MaybeYield(generator);
    }
}
//---------------------------------------------------------------------------
/*
    BinderServers

    The default value of the servers of the binder thread number 'index': one server, b<index>:<index + 1>.
*/
TServerVector BinderServers(std::size_t index)
{
    TTestServer const server{ "b" + std::to_string(index), static_cast<std::uint16_t>(index + 1), true, std::nullopt };

    return TServerVector{ server };
}
//---------------------------------------------------------------------------
/*
    BindingsProblem

    What is wrong with 'document', which a load of Bindings_ComeAndGoWhileLoading gave: "" when the table stays has its
    comment and its keys key0 to key<stayers - 1>, each with its value (its number) and its comment, and when the
    table of each binder (binder0 to binder<binders - 1>) that is there has its comment and its key with its comment,
    and its servers, when they are there, their element and their comment. A binder binds its table, key and servers
    in that order and unbinds them in the other, so a load may apply its key without its servers, but its table is
    there only with its key.
*/
std::string BindingsProblem(TASWTomlDocument const& document, std::size_t stayers, std::size_t binders)
{
    if (document.GetComment("stays") != "Settings that stay.")
        return "the table that stays should have its comment";

    for (std::size_t i = 0; i < stayers; ++i)
    {
        std::string const path = "stays.key" + std::to_string(i);
        TASWTomlValue const* const value = document.Find(path);
        bool const whole = value != nullptr && value->AsInteger() != nullptr &&
            *value->AsInteger() == static_cast<std::int64_t>(i) && document.GetComment(path) == "A comment.";
        if (!whole)
            return path + " should be there with its value and its comment";
    }

    for (std::size_t i = 0; i < binders; ++i)
    {
        std::string const table = "binder" + std::to_string(i);
        if (document.Find(table) == nullptr)
            continue;

        bool const tableWhole = document.GetComment(table) == "A binder's table." &&
            document.Find(table + ".key") != nullptr && document.GetComment(table + ".key") == "A binder's key.";
        if (!tableWhole)
            return "the table " + table + " should have its comment, and its key with its comment";

        TASWTomlValue const* const servers = document.Find(table + ".servers");
        if (servers == nullptr)
            continue;

        bool const serversWhole = servers->AsArray() != nullptr && servers->AsArray()->size() == 1 &&
            document.GetComment(table + ".servers") == "A binder's servers.";
        if (!serversWhole)
            return "the servers of " + table + " should have their element and their comment";
    }

    return std::string();
}
//---------------------------------------------------------------------------
/*
    CountSettingUp

    A writer thread of GetAndSet_SeeWholeSettingsWhileLoading: sets its count, test.Counts[index], to 1, 2 and so on,
    to at least test.Iterations, and on until test.Left is 0, at most to test.Largest.
*/
void CountSettingUp(TWholeSettingsTest& test, std::size_t index)
{
    std::mt19937 generator(ThreadsSeed + static_cast<std::uint32_t>(index));
    for (int value = 1; value <= test.Largest; ++value)
    {
        TASWTomlResult const result = test.Counts[index]->Set(value);
        if (!result.IsOk())
            test.Failures.Add("Set() should succeed: " + result.Message);

        if (value >= test.Iterations && test.Left == 0)
            break;

        MaybeYield(generator);
    }
}

//---------------------------------------------------------------------------
/*
    DefaultCluster

    The default value of a cluster setting: a screen of 800 by 600, a spare one of 1 by 1, and one server, x:9
*/
TTestCluster DefaultCluster()
{
    TTestCluster cluster;
    cluster.Name = "main";
    cluster.Screen = TTestScreen(800, 600);
    cluster.Spare = TTestScreen(1, 1);
    cluster.Servers.push_back(TTestServer{ "x", 9, true, std::nullopt });

    return cluster;
}
//---------------------------------------------------------------------------
/*
    LfOptions

    The default options, but new lines end with LF (as TPreloaded's)
*/
TASWTomlOptions LfOptions()
{
    TASWTomlOptions options;
    options.Writing.LineEnding = ASWToml::ELineEnding::LF;

    return options;
}
//---------------------------------------------------------------------------
/*
    LoadAndCheckBindings

    The loader thread of Bindings_ComeAndGoWhileLoading: test.Loads times, loads an empty text and checks the document
    that gives (see BindingsProblem()), then sets test.Loading to false.
*/
void LoadAndCheckBindings(TBindingsTest& test, std::size_t index)
{
    std::mt19937 generator(ThreadsSeed + static_cast<std::uint32_t>(index));
    for (int i = 0; i < test.Loads; ++i)
    {
        TASWTomlResult const result = test.Toml.LoadText("");
        if (!result.IsOk())
            test.Failures.Add("LoadText() should succeed: " + result.Message);

        std::string const problem = BindingsProblem(test.Toml.GetDocument(), test.Stayers.size(), test.Binders);
        if (!problem.empty())
            test.Failures.Add("in a loaded document, " + problem);

        MaybeYield(generator);
    }

    test.Loading = false;
}
//---------------------------------------------------------------------------
/*
    LoadSettingsRepeatedly

    A file thread of GetAndSet_SeeWholeSettingsWhileLoading: test.FileOperations times, loads in turn test.InvalidText,
    the file test.FilePath (which the saves write), and the file test.InvalidPath, first written with test.InvalidText,
    after which it saves the object as test.FilePath, so the saves go on writing that; then counts itself out of
    test.Left.
*/
void LoadSettingsRepeatedly(TWholeSettingsTest& test, std::size_t index)
{
    std::mt19937 generator(ThreadsSeed + static_cast<std::uint32_t>(index));
    for (int i = 0; i < test.FileOperations; ++i)
    {
        TASWTomlResult result;
        switch (i % 3)
        {
            case 0:
                result = test.Toml.LoadText(test.InvalidText);
                break;

            case 1:
                result = test.Toml.Load(test.FilePath);
                break;

            default:
                if (!WriteTestFile(ASWToml::Utf8ToPath(test.InvalidPath), test.InvalidText))
                    test.Failures.Add("the file that isn't valid should be written");

                result = test.Toml.Load(test.InvalidPath);
                if (result.IsOk())
                    result = test.Toml.SaveAs(test.FilePath);

                break;
        }

        if (!result.IsOk())
            test.Failures.Add("a load, and the save after it, should succeed: " + result.Message);

        MaybeYield(generator);
    }

    --test.Left;
}
//---------------------------------------------------------------------------
/*
    MessageKinds

    The severity, code and path of each of 'messages', one per line (e.g. "Warning ValueOutOfRange logging.level")
*/
std::string MessageKinds(TMessageVector const& messages)
{
    std::string text;
    for (TASWTomlMessage const& message : messages)
    {
        text += ASWToml::ToString(message.Severity) + " "s + ASWToml::ToString(message.Code) + " " + message.Path +
            "\n";
    }

    return text;
}
//---------------------------------------------------------------------------
/*
    MessagesText

    'messages' as text, one per line (ToString()), for the messages of failed checks
*/
std::string MessagesText(TMessageVector const& messages)
{
    std::string text;
    for (TASWTomlMessage const& message : messages)
        text += ASWToml::ToString(message) + "\n";

    return text;
}
//---------------------------------------------------------------------------
/*
    OddOnly

    The validator of an odd number
*/
std::string OddOnly(int const& value)
{
    return value % 2 == 0 ? "it must be odd" : "";
}
//---------------------------------------------------------------------------
/*
    OneServer

    The validator of a list of at most one server
*/
std::string OneServer(TServerVector const& value)
{
    return value.size() > 1 ? "it can have at most one server" : "";
}
//---------------------------------------------------------------------------
/*
    ReadWholeSettings

    A reader thread of GetAndSet_SeeWholeSettingsWhileLoading: checks that a writer's count is there and Get() gives a
    number in range, that Get() gives two servers, and that a copy of the document holds whole settings (see
    WholeSettingsProblem()); at least test.Iterations times, and on until test.Left is 0, at most test.Largest times.
*/
void ReadWholeSettings(TWholeSettingsTest& test, std::size_t index)
{
    std::mt19937 generator(ThreadsSeed + static_cast<std::uint32_t>(index));
    for (int i = 0; i < test.Largest; ++i)
    {
        std::size_t const key = generator() % test.Counts.size();
        if (!test.Counts[key]->IsPresent())
            test.Failures.Add(test.Counts[key]->GetPath().ToString() + " should always be there");

        int const value = test.Counts[key]->Get();
        if (value < 0 || value > test.Largest)
            test.Failures.Add(test.Counts[key]->GetPath().ToString() + " should be a number in range: " +
                std::to_string(value));

        std::size_t const servers = test.Servers->Get().size();
        if (servers != 2)
            test.Failures.Add("there should be two servers, not " + std::to_string(servers));

        std::string const problem = WholeSettingsProblem(test.Toml.GetDocument(), test.Counts.size(),
            test.Fillers.size());
        if (!problem.empty())
            test.Failures.Add("in a copy of the document, " + problem);

        if (i >= test.Iterations && test.Left == 0)
            break;

        MaybeYield(generator);
    }
}
//---------------------------------------------------------------------------
/*
    RemovedFirstCluster

    The text a setting of clusters writes when the first of two clusters is removed: two clusters that are the same
    but for their note, which no field names, and the keys 'first' and 'second' (each a line, or more), with the first
    removed by Set()
*/
std::string RemovedFirstCluster(std::string const& first, std::string const& second)
{
    std::string const same = "name = \"c\"\nscreen = { width = 1, height = 1 }\n";
    TPreloaded toml("[[clusters]]\n" + same + "note = \"first\"\n" + first + "\n[[clusters]]\n" + same +
        "note = \"second\"\n" + second);
    TASWTomlTable root{ toml, "" };
    TASWTomlSetting<TClusterVector> clusters{ root, "clusters", TClusterVector() };
    TClusterVector value = clusters.Get();
    value.erase(value.begin());
    static_cast<void>(clusters.Set(value));

    return toml.ToText();
}
//---------------------------------------------------------------------------
/*
    ResultProblem

    What differs between 'result' and 'expected' (their code, message and path); empty when nothing does
*/
std::string ResultProblem(TASWTomlResult const& result, TASWTomlResult const& expected)
{
    std::string problem;
    if (result.Code != expected.Code)
        problem += std::string(" code ") + ASWToml::ToString(result.Code) + " for " + ASWToml::ToString(expected.Code);

    if (result.Message != expected.Message)
        problem += " message '" + result.Message + "' for '" + expected.Message + "'";

    if (result.Path != expected.Path)
        problem += " path '" + result.Path + "' for '" + expected.Path + "'";

    return problem.empty() ? problem : "the result has" + problem;
}
//---------------------------------------------------------------------------
/*
    SaveSettingsRepeatedly

    A file thread of GetAndSet_SeeWholeSettingsWhileLoading: saves the object test.FileOperations times, then counts
    itself out of test.Left.
*/
void SaveSettingsRepeatedly(TWholeSettingsTest& test, std::size_t index)
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
    SetServerPorts

    The servers' writer of GetAndSet_SeeWholeSettingsWhileLoading: reads the servers with Get(), sets each one's port to
    the next number (1 to 1000 and round again) and writes them with Set(), whose overlay keeps the key note that their
    struct doesn't know; at least test.Iterations times, and on until test.Left is 0, at most test.Largest times.
*/
void SetServerPorts(TWholeSettingsTest& test, std::size_t index)
{
    std::mt19937 generator(ThreadsSeed + static_cast<std::uint32_t>(index));
    for (int i = 0; i < test.Largest; ++i)
    {
        TServerVector servers = test.Servers->Get();
        for (TTestServer& server : servers)
            server.Port = static_cast<std::uint16_t>(i % 1000 + 1);

        TASWTomlResult const result = test.Servers->Set(servers);
        if (!result.IsOk())
            test.Failures.Add("Set() of the servers should succeed: " + result.Message);

        if (i >= test.Iterations && test.Left == 0)
            break;

        MaybeYield(generator);
    }
}
//---------------------------------------------------------------------------
/*
    UseScratchSetting

    The scratch thread of GetAndSet_SeeWholeSettingsWhileLoading: removes the key of the optional setting scratch and
    sets it again (so Set() adds it with its comment); at least test.Iterations times, and on until test.Left is 0, at
    most test.Largest times.
*/
void UseScratchSetting(TWholeSettingsTest& test, std::size_t index)
{
    std::mt19937 generator(ThreadsSeed + static_cast<std::uint32_t>(index));
    for (int i = 0; i < test.Largest; ++i)
    {
        static_cast<void>(test.Toml.Remove(test.Scratch->GetPath()));
        TASWTomlResult const result = test.Scratch->Set(i % 100);
        if (!result.IsOk())
            test.Failures.Add("Set() of the scratch key should succeed: " + result.Message);

        if (i >= test.Iterations && test.Left == 0)
            break;

        MaybeYield(generator);
    }
}
//---------------------------------------------------------------------------
/*
    WholeSettingsProblem

    What is wrong with 'document', a copy of the object of GetAndSet_SeeWholeSettingsWhileLoading: "" when the tables
    values and others have their comments, the keys count0 to count<writers - 1> of values and key0 to
    key<others - 1> of others are there with their comments, the key scratch, when it is there, has its comment, and
    the array of tables servers has its comment and two elements, each a table with the key note, "kept" (which no
    field of their struct names).
*/
std::string WholeSettingsProblem(TASWTomlDocument const& document, std::size_t writers, std::size_t others)
{
    if (document.GetComment("values") != "The counts." || document.GetComment("others") != "Other settings.")
        return "each table should have its comment";

    for (std::size_t i = 0; i < writers; ++i)
    {
        std::string const path = "values.count" + std::to_string(i);
        if (document.Find(path) == nullptr || document.GetComment(path) != "A count.")
            return path + " should be there with its comment";
    }

    for (std::size_t i = 0; i < others; ++i)
    {
        std::string const path = "others.key" + std::to_string(i);
        if (document.Find(path) == nullptr || document.GetComment(path) != "A comment.")
            return path + " should be there with its comment";
    }

    if (document.Find("values.scratch") != nullptr && document.GetComment("values.scratch") != "The scratch key.")
        return "the scratch key should have its comment";

    TASWTomlValue const* const servers = document.Find("values.servers");
    bool const twoServers = servers != nullptr && servers->AsArray() != nullptr && servers->AsArray()->size() == 2 &&
        document.GetComment("values.servers") == "The servers.";
    if (!twoServers)
        return "the servers should be there, two of them, with their comment";

    for (TASWTomlValue const& server : *servers->AsArray())
    {
        TASWTomlValue const* const note = server.Find("note");
        if (note == nullptr || note->AsString() == nullptr || *note->AsString() != "kept")
            return "each server should keep its note";
    }

    return std::string();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TAppSettings
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
std::string TAppSettings::NotEmpty(std::string const& value)
{
    return value.empty() ? "it can't be empty" : "";
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TPreloaded
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TPreloaded::TPreloaded(std::string_view text)
{
    static_cast<void>(SetOptions(LfOptions()));
    SetLogCallback(TRecorder(Messages));
    Parsed = LoadText(text).IsOk();
}
//---------------------------------------------------------------------------
TASWTomlValueOptions TPreloaded::Hexadecimal()
{
    TASWTomlValueOptions values;
    values.IntegerBase = EIntegerBase::Hexadecimal;

    return values;
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TLoadSettings
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TLoadSettings::TLoadSettings(std::string_view text)
    : TPreloaded(text)
{
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
// TReportCounter
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TReportCounter::TReportCounter(std::atomic<int>& count)
    : m_Count(&count)
{
}
//---------------------------------------------------------------------------
void TReportCounter::operator()(TASWTomlMessage const& /*message*/) const
{
    ++*m_Count;
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TCodeRecorder
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TCodeRecorder::TCodeRecorder(TCodeVector& codes)
    : m_Codes(&codes)
{
}
//---------------------------------------------------------------------------
void TCodeRecorder::operator()(TASWTomlMessage const& message) const
{
    m_Codes->push_back(message.Code);
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTestSettings
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTestSettings::TTestSettings(std::string_view text)
    : TPreloaded(text)
{
}
//---------------------------------------------------------------------------
std::string TTestSettings::NotEmpty(std::string const& value)
{
    return value.empty() ? "it can't be empty" : "";
}
//---------------------------------------------------------------------------
std::string TTestSettings::NotLong(TTestWord const& value)
{
    return value.Text.size() > 8 ? "it is longer than 8 letters." : "";
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TSaveAsOnFirst
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TSaveAsOnFirst::TSaveAsOnFirst(TASWBaseToml& toml, std::string otherFile, TMessageVector& messages)
    : m_Toml(&toml),
      m_OtherFile(std::move(otherFile)),
      m_Messages(&messages)
{
}
//---------------------------------------------------------------------------
void TSaveAsOnFirst::operator()(TASWTomlMessage const& message) const
{
    m_Messages->push_back(message);
    if (m_Messages->size() == 1)
        static_cast<void>(m_Toml->SaveAs(m_OtherFile));
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TSetDuringSave
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TSetDuringSave::TSetDuringSave(std::string_view text, bool addKey)
    : TTestSettings(text),
      m_AddKey(addKey)
{
}
//---------------------------------------------------------------------------
TASWTomlResult TSetDuringSave::WriteFile(std::string const& /*filePath*/, std::string_view /*contents*/,
    ASWToml::TASWTomlSavingOptions const& /*options*/, TMessageVector& /*warnings*/)
{
    return m_AddKey ? Level.Set(3) : Zoom.Set(2.0);
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TThrowingValidator
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
std::string TThrowingValidator::operator()(int const& /*value*/) const
{
    throw std::runtime_error("boom");
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_SettingsAccess
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_SettingsAccess::TTest_ASWToml_SettingsAccess()
    : inherited("ASWToml_SettingsAccess_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_SettingsAccess::~TTest_ASWToml_SettingsAccess()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SettingsAccess::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SettingsAccess::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SettingsAccess::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SettingsAccess::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsAccess, Get_InvalidValueGivesTheDefault)
{
    // Arrange
    TTestSettings settings("[general]\nlevel = \"x\"\nunnamed = \"Purple\"\n[[net.servers]]\nhost = \"a\"\n"
        "port = 1\n[[net.servers]]\nhost = \"b\"\nport = \"x\"\n");
    CheckTrue(settings.Parsed, "the text should parse");

    // Act
    unsigned const level = settings.Level.Get();
    TServerVector const servers = settings.Servers.Get();
    EColor const unnamed = settings.Unnamed.Get();

    // Assert
    CheckEquals(2u, level, "a value that doesn't convert gives the default");
    CheckTrue(servers.size() == 2 && servers[0].Port == 1 && servers[1].Host == "b" && servers[1].Port == 80,
        "and a part of one, its own default");
    CheckTrue(unnamed == EColor::Unlisted, "and a value whose default can't be written");
    CheckEquals(std::size_t(3), settings.Messages.size(), "each is reported once: " + MessagesText(settings.Messages));
    if (settings.Messages.size() != 3)
        return;

    TASWTomlMessage const& first = settings.Messages[0];
    CheckTrue(first.Severity == ESeverity::Warning, "as a Warning");
    CheckEquals(ETomlCode::TypeMismatch, first.Code, "with the converter's code");
    CheckEquals("The value \"x\" is a string, not an integer. The default value 2 is used."s, first.Text,
        "the converter's text and the default value");
    CheckEquals("general.level"s, first.Path, "the setting's path");
    CheckEquals("The value \"x\" is a string, not an integer. The default value 80 is used."s,
        settings.Messages[1].Text, "a part of a value, with the part's default");
    CheckEquals("net.servers[1].port"s, settings.Messages[1].Path, "with the path of the part");
    CheckEquals(ETomlCode::InvalidValue, settings.Messages[2].Code, "a name the enum lacks");
    CheckContains(settings.Messages[2].Text, " The default value is used.", "a default that can't be written");
    CheckEquals("general.unnamed"s, settings.Messages[2].Path, "its path");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsAccess, Get_ReadsTheDocument)
{
    // Arrange
    TTestSettings settings("[general]\nname = \"Ann\"\ncheats = true\nlevel = 3\nzoom = 1.5\ncolor = \"lime\"\n"
        "folder = 'C:\\data'\ncolumns = [\"a\", \"b\", \"c\"]\nmask = 0x10\nword = \"hello\"\n"
        "[net.server]\nhost = \"h\"\nport = 81\n[net.limits]\nx = 2\n[[net.servers]]\nhost = \"a\"\nport = 1\n"
        "[[net.servers]]\nhost = \"b\"\nport = 80\nenabled = false\n");
    CheckTrue(settings.Parsed, "the text should parse");
    TTestServer const first{ "a", 1, true, std::nullopt };
    TTestServer const second{ "b", 80, false, std::nullopt };
    TServerVector const servers{ first, second };

    // Act & Assert
    CheckEquals("Ann"s, settings.Name.Get(), "a string");
    CheckTrue(settings.Cheats.Get(), "a boolean");
    CheckEquals(3u, settings.Level.Get(), "an integer");
    CheckTrue(settings.Zoom.Get() == 1.5, "a float");
    CheckTrue(settings.Color.Get() == EColor::Green, "an enum, by a name in any case");
    CheckTrue(settings.Folder.Get() == std::filesystem::path("C:\\data"), "a path");
    CheckTrue(settings.Columns.Get() == TStringVector{ "a", "b", "c" }, "an array");
    CheckEquals(16, settings.Mask.Get(), "a hexadecimal integer");
    CheckEquals("hello"s, settings.Word.Get().Text, "a type with a converter of its own");
    CheckTrue(settings.Server.Get() == TTestServer{ "h", 81, true, std::nullopt }, "a struct, a table");
    CheckTrue(settings.Servers.Get() == servers, "a vector of structs, an array of tables");
    CheckTrue(settings.Limits.Get() == TLimitMap{ { "x", 2 } }, "a map");
    CheckTrue(settings.Level.IsPresent() && settings.Servers.IsPresent(), "the keys are there");
    CheckFalse(settings.AtLeast.IsPresent(), "a missing key isn't");
    CheckEquals(5, settings.AtLeast.Get(), "and gives its default");

    CheckTrue(settings.SetValue("general.level", 4).IsOk(), "a raw edit should succeed");
    CheckEquals(4u, settings.Level.Get(), "a raw edit is seen at once");
    CheckTrue(settings.Remove("general.level"), "a raw removal should succeed");
    CheckEquals(2u, settings.Level.Get(), "a removed key gives the default");
    CheckTrue(settings.Messages.empty(), "nothing is reported: " + MessagesText(settings.Messages));
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsAccess, Messages_WriteValuesAsTheSettingDoes)
{
    // Arrange
    // Settings that write their integers in hexadecimal, of a text with values in decimal and hexadecimal
    TPreloaded toml("[t]\nlevel = 9\nodd = 0x4\nbyte = 300\n");
    TASWTomlTable table{ toml, "t" };
    TASWTomlSetting<unsigned> level{ table, "level", 2, "",
        { .Min = 1, .Max = 4, .Values = TPreloaded::Hexadecimal() } };
    TASWTomlSetting<int> odd{ table, "odd", 1, "", { .Validator = OddOnly, .Values = TPreloaded::Hexadecimal() } };
    TASWTomlSetting<std::uint8_t> byte{ table, "byte", 1, "", { .Values = TPreloaded::Hexadecimal() } };
    TASWTomlSetting<int> atMost{ table, "at_most", 1, "", { .Max = 16, .Values = TPreloaded::Hexadecimal() } };
    TASWTomlSetting<int> atLeast{ table, "at_least", 16, "", { .Min = 16, .Values = TPreloaded::Hexadecimal() } };
    TASWTomlSetting<unsigned> plain{ table, "plain", 2, "", { .Max = 4 } };
    CheckTrue(toml.Parsed, "the text should parse");

    // Act
    static_cast<void>(level.Get());
    static_cast<void>(odd.Get());
    static_cast<void>(byte.Get());
    TASWTomlResult const refused = level.Set(5);
    TASWTomlResult const aboveMost = atMost.Set(32);
    TASWTomlResult const belowLeast = atLeast.Set(1);
    TASWTomlResult const refusedPlain = plain.Set(5);

    // Assert
    CheckEquals("The value 0x5 is out of the setting's range: it must be from 0x1 to 0x4."s, refused.Message,
        "Set() shows the value and the range as the setting writes them");
    CheckEquals("The value 0x20 is out of the setting's range: it must be at most 0x10."s, aboveMost.Message,
        "a range with only a largest value");
    CheckEquals("The value 0x1 is out of the setting's range: it must be at least 0x10."s, belowLeast.Message,
        "a range with only a smallest value");
    CheckEquals("The value 5 is out of the setting's range: it must be at most 4."s, refusedPlain.Message,
        "in decimal for a setting that writes in decimal");
    CheckEquals(std::size_t(7), toml.Messages.size(), "each failure is reported: " + MessagesText(toml.Messages));
    if (toml.Messages.size() == 7)
    {
        CheckEquals("The value 0x9 is out of the setting's range: it must be from 0x1 to 0x4. The default value 0x2 is "
            "used."s, toml.Messages[0].Text, "Get()'s Warning shows the value read, the range and the default so");
        CheckEquals("The value 0x4 isn't valid: it must be odd. The default value 0x1 is used."s, toml.Messages[1].Text,
            "and with the validator's refusal");
        CheckEquals("The value 0x12C is out of range: it must be from 0x0 to 0xFF. The default value 0x1 is used."s,
            toml.Messages[2].Text, "and with the converter's");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsAccess, New_GivesTheDefaults)
{
    // Arrange
    TTestSettings settings;

    // Act & Assert
    CheckEquals("Guest"s, settings.Name.Get(), "a string's default");
    CheckEquals(2u, settings.Level.Get(), "an integer's default");
    CheckTrue(settings.Server.Get() == settings.Server.GetDefault(), "a struct's default");
    CheckTrue(settings.Columns.Get() == TStringVector{ "name", "size" }, "an array's default");
    CheckFalse(settings.Level.IsPresent(), "nothing is in the document");
    CheckEquals("level"s, settings.Level.GetKey(), "the key");
    CheckEquals("general.level"s, settings.Level.GetPath().ToString(), "the path: the table's and the key");
    CheckEquals("From 0 to 4."s, settings.Level.GetComment(), "the comment");
    CheckEquals(2u, settings.Level.GetDefault(), "the default");
    CheckTrue(settings.Level.GetOptions().Min == 0u && settings.Level.GetOptions().Max == 4u, "the range");
    CheckTrue(settings.Cheats.GetOptions().Optional, "an option");
    CheckEquals("general"s, settings.General.GetPath().ToString(), "the table's path");
    CheckEquals("The general settings."s, settings.General.GetComment(), "the table's comment");
    CheckTrue(&settings.General.GetOwner() == &settings, "the table's owner");
    CheckEmpty(settings.ToText(), "the document stays empty");
    CheckTrue(settings.Messages.empty(), "nothing is reported");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsAccess, OutOfMemory_SetChangesNothing)
{
    // Arrange
    if (!CanFailAllocations())
        Skip("allocations can't be made to fail in this build (see CanFailAllocations())");

    std::string const text = "[general]\nname = \"Ann\"\n";
    std::size_t failures = 0;

    // Act & Assert
    // Each allocation of a Set() that adds a key with its comment fails in turn, on a new object, until there are none
    for (std::size_t number = 1; number < 10000; ++number)
    {
        TTestSettings settings(text);
        std::string const which = " (allocation " + std::to_string(number) + ")";
        TASWTomlResult result;
        bool failed = false;
        {
            TFailingAlloc failingAlloc(number);
            result = settings.Level.Set(3);
            failed = failingAlloc.HasFailed();
        }

        if (result.IsOk())
        {
            CheckTrue(settings.Level.Get() == 3 && settings.GetComment("general.level") == "From 0 to 4.",
                "a Set() that succeeded should be complete" + which);
        }
        else
        {
            ++failures;
            CheckTrue(failed, "Set() should only fail when an allocation fails" + which);
            CheckEquals(ETomlCode::UnexpectedException, result.Code, "a failed allocation is UnexpectedException" + which);
            CheckEquals(text, settings.ToText(), "a failed Set() should change nothing" + which);
            CheckFalse(settings.IsModified(), "a failed Set() shouldn't set the modified flag" + which);
        }

        if (!failed)
            break;
    }

    CheckGreaterThan(failures, std::size_t(0), "at least one allocation should have failed");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsAccess, Paths_ThatCantBeUsedAreErrors)
{
    // Arrange
    TTestSettings settings("[[servers]]\nx = 5\n");
    TASWTomlTable badTable{ settings, "a..b" };
    TASWTomlTable element{ settings, "servers[0]" };
    TASWTomlSetting<int> inBadTable{ badTable, "x", 1 };
    TASWTomlSetting<int> inElement{ element, "x", 1 };
    TASWTomlSetting<int> badKey{ settings.General, "\xFF", 1 };
    CheckTrue(settings.Parsed, "the text should parse");

    // Act
    int const fromBadTable = inBadTable.Get();
    int const fromElement = inElement.Get();
    int const fromBadKey = badKey.Get();
    TASWTomlResult const setBadTable = inBadTable.Set(2);
    TASWTomlResult const setElement = inElement.Set(2);
    TASWTomlResult const setBadKey = badKey.Set(2);

    // Assert
    CheckTrue(fromBadTable == 1 && fromElement == 1 && fromBadKey == 1, "Get() gives the default");
    CheckEquals(ETomlCode::InvalidArgument, setBadTable.Code, "a table path that isn't valid");
    CheckStartsWith(setBadTable.Message, "The path of the setting's table isn't valid: "s, "its message");
    CheckEquals(ETomlCode::InvalidArgument, setElement.Code, "a table path with an index");
    CheckEquals("The path of the setting's table, servers[0], has an index: a table of settings is named by keys "
        "only."s, setElement.Message, "its message");
    CheckEquals("servers[0].x"s, setElement.Path, "its path");
    CheckEquals(ETomlCode::InvalidArgument, setBadKey.Code, "a key that isn't valid UTF-8");
    CheckStartsWith(setBadKey.Message, "The setting's key isn't valid: "s, "its message");
    CheckEquals(std::size_t(6), settings.Messages.size(), "each Get() and Set() is reported: " +
        MessagesText(settings.Messages));
    for (TASWTomlMessage const& message : settings.Messages)
    {
        CheckTrue(message.Severity == ESeverity::Error && message.Code == ETomlCode::InvalidArgument,
            "as an Error (InvalidArgument): " + ASWToml::ToString(message));
    }

    CheckEquals("[[servers]]\nx = 5\n"s, settings.ToText(), "nothing changes");
    CheckTrue(ASWToml::Detail::IsSettingsTablePath("") && ASWToml::Detail::IsSettingsTablePath("a.b"),
        "a table of settings has a path of keys, or the root path");
    CheckFalse(ASWToml::Detail::IsSettingsTablePath("a..b") || ASWToml::Detail::IsSettingsTablePath("a[0]"),
        "not one that isn't valid, or has an index");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsAccess, Range_ChecksGetAndSet)
{
    // Arrange
    TTestSettings settings("[general]\nlevel = 9\nat_least = 0\nat_most = 10\n");
    CheckTrue(settings.Parsed, "the text should parse");

    // Act
    unsigned const level = settings.Level.Get();
    int const atLeast = settings.AtLeast.Get();
    int const atMost = settings.AtMost.Get();
    TASWTomlResult const setLevel = settings.Level.Set(5);
    TASWTomlResult const setInRange = settings.Level.Set(4);

    // Assert
    CheckTrue(level == 2 && atLeast == 5 && atMost == 5, "a value out of range gives the default");
    CheckTrue(setInRange.IsOk(), "a value in range is set: " + setInRange.Message);
    CheckEquals(ETomlCode::ValueOutOfRange, setLevel.Code, "a value out of range isn't set");
    CheckEquals("The value 5 is out of the setting's range: it must be from 0 to 4."s, setLevel.Message,
        "its message names the range");
    CheckEquals("general.level"s, setLevel.Path, "its path");
    CheckEquals(std::size_t(4), settings.Messages.size(), "each failure is reported: " +
        MessagesText(settings.Messages));
    if (settings.Messages.size() == 4)
    {
        CheckEquals("The value 9 is out of the setting's range: it must be from 0 to 4. The default value 2 is used."s,
            settings.Messages[0].Text, "Get()'s Warning");
        CheckTrue(settings.Messages[0].Severity == ESeverity::Warning, "Get() reports a Warning");
        CheckEquals("The value 0 is out of the setting's range: it must be at least 1. The default value 5 is used."s,
            settings.Messages[1].Text, "a range with only a smallest value");
        CheckEquals("The value 10 is out of the setting's range: it must be at most 9. The default value 5 is used."s,
            settings.Messages[2].Text, "a range with only a largest value");
        CheckTrue(settings.Messages[3].Severity == ESeverity::Error, "Set() reports an Error");
        CheckEquals("general.level"s, settings.Messages[3].Path, "with the path");
    }

    CheckEquals("[general]\nlevel = 4\nat_least = 0\nat_most = 10\n"s, settings.ToText(),
        "only the value in range is written");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsAccess, Reset_SetsTheDefault)
{
    // Arrange
    TTestSettings settings("[general]\nlevel = 3  # Mine\n");
    CheckTrue(settings.Parsed, "the text should parse");

    // Act
    TASWTomlResult const result = settings.Level.Reset();

    // Assert
    CheckTrue(result.IsOk(), "Reset() should succeed: " + result.Message);
    CheckEquals("[general]\nlevel = 2  # Mine\n"s, settings.ToText(), "the default is written in place");
    CheckEquals(2u, settings.Level.Get(), "and read");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsAccess, Set_AddsTheKeyWithItsComment)
{
    // Arrange
    TTestSettings inTable("[general]\nother = 1\n");
    TTestSettings mine("[general]\n# Mine\nlevel = 1\n");
    TTestSettings newTable("x = 1\n");
    TTestSettings inlineTable("general = { other = 1 }\n");
    TTestSettings structs("x = 1\n");
    TTestSettings noComment("[general]\n");
    CheckTrue(inTable.Parsed && mine.Parsed && newTable.Parsed && inlineTable.Parsed && structs.Parsed &&
        noComment.Parsed, "the texts should parse");
    TServerVector const servers{ TTestServer{ "a", 1, true, std::nullopt } };

    // Act
    TASWTomlResult const setInTable = inTable.Level.Set(3);
    TASWTomlResult const setMine = mine.Level.Set(3);
    TASWTomlResult const setNewTable = newTable.Level.Set(3);
    TASWTomlResult const setInline = inlineTable.Level.Set(3);
    TASWTomlResult const setServer = structs.Server.Set(TTestServer{ "h", 81, true, std::nullopt });
    TASWTomlResult const setServers = structs.Servers.Set(servers);
    TASWTomlResult const setNoComment = noComment.Cheats.Set(true);

    // Assert
    CheckTrue(setInTable.IsOk() && setMine.IsOk() && setNewTable.IsOk() && setInline.IsOk() && setServer.IsOk() &&
        setServers.IsOk() && setNoComment.IsOk(), "each Set() should succeed");
    CheckEquals("[general]\nother = 1\n# From 0 to 4.\nlevel = 3\n"s, inTable.ToText(),
        "a new key gets its comment above it");
    CheckEquals("[general]\n# Mine\nlevel = 3\n"s, mine.ToText(), "a key that is there keeps its own");
    CheckEquals("x = 1\n\n[general]\n# From 0 to 4.\nlevel = 3\n"s, newTable.ToText(), "a missing table is added");
    CheckEquals("general = { other = 1, level = 3 }\n"s, inlineTable.ToText(),
        "a key in an inline table has no line for a comment, and is added without it");
    CheckEquals(Escaped("x = 1\n\n# The server.\n[net.server]\nhost = \"h\"\nport = 81\nenabled = true\n\n"
        "# The servers.\n[[net.servers]]\nhost = \"a\"\nport = 1\nenabled = true\n"), Escaped(structs.ToText()),
        "a table and an array of tables get theirs above their headers");
    CheckEquals("[general]\ncheats = true\n"s, noComment.ToText(), "a setting without a comment adds none");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsAccess, Set_CommentThatCantBeWrittenChangesNothing)
{
    // Arrange
    TTestSettings settings("[general]\n");
    TASWTomlSetting<int> badComment{ settings.General, "bad", 1, "Bad\x01" };
    TTestSettings there("[general]\nbad = 1\n");
    TASWTomlSetting<int> badCommentThere{ there.General, "bad", 1, "Bad\x01" };
    CheckTrue(settings.Parsed && there.Parsed, "the texts should parse");

    // Act
    TASWTomlResult const added = badComment.Set(2);
    TASWTomlResult const changed = badCommentThere.Set(2);

    // Assert
    CheckEquals(ETomlCode::InvalidValue, added.Code, "a key that would be added with it isn't");
    CheckEquals("general.bad"s, added.Path, "with the path");
    CheckEquals("[general]\n"s, settings.ToText(), "nothing changes");
    CheckFalse(settings.IsModified(), "the document isn't modified");
    CheckEquals(std::size_t(1), settings.Messages.size(), "the failure is reported");
    CheckTrue(changed.IsOk(), "a key that is there is set, as its comment isn't written: " + changed.Message);
    CheckEquals("[general]\nbad = 2\n"s, there.ToText(), "the value changes");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsAccess, Set_DuringASaveKeepsTheDocumentModified)
{
    // Arrange
    std::string const text = "[general]\nzoom = 1.0\n";
    TSetDuringSave adding(text, true);
    TSetDuringSave changing(text, false);
    CheckTrue(adding.Parsed && changing.Parsed, "the texts should parse");

    // Act
    TASWTomlResult const added = adding.SaveAs("unused.toml");
    TASWTomlResult const changed = changing.SaveAs("unused.toml");

    // Assert
    CheckTrue(added.IsOk() && changed.IsOk(), "both saves should succeed");
    CheckTrue(adding.IsModified(), "a key added while the file is written isn't in it, so the document stays modified");
    CheckTrue(changing.IsModified(), "nor is a value changed then");
    CheckEquals("[general]\nzoom = 2.0\n"s, changing.ToText(), "the value is changed");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsAccess, Set_FailuresAreReported)
{
    // Arrange
    std::string const text = "[general]\ncolor = \"Red\"\n";
    TTestSettings settings(text);
    TASWTomlSetting<int> throwing{ settings.General, "throwing", 1, "", { .Validator = TThrowingValidator() } };
    CheckTrue(settings.Parsed, "the text should parse");
    TTestServer const valid{ "a", 1, true, std::nullopt };
    TTestServer const invalid{ "b\xFF", 2, true, std::nullopt };
    TServerVector const servers{ valid, invalid };

    // Act
    TASWTomlResult const unnamed = settings.Color.Set(EColor::Unlisted);
    TASWTomlResult const invalidPart = settings.Servers.Set(servers);
    TASWTomlResult const thrown = throwing.Set(2);

    // Assert
    CheckEquals(ETomlCode::InvalidValue, unnamed.Code, "a value that can't be written");
    CheckEquals("The value 7 has no name in the enum's ASWTomlEnumNames(), so it can't be written."s, unnamed.Message,
        "the converter's message");
    CheckEquals("general.color"s, unnamed.Path, "the setting's path");
    CheckEquals(ETomlCode::InvalidValue, invalidPart.Code, "a part of a value that can't be written");
    CheckEquals("net.servers[1].host"s, invalidPart.Path, "the path of the part");
    CheckEquals(ETomlCode::UnexpectedException, thrown.Code, "a validator that throws");
    CheckEquals("An unexpected exception was thrown: boom"s, thrown.Message, "what it threw");
    CheckEquals("general.throwing"s, thrown.Path, "the setting's path");
    CheckEquals(text, settings.ToText(), "nothing changes");
    CheckEquals(std::size_t(3), settings.Messages.size(), "each failure is reported: " +
        MessagesText(settings.Messages));
    if (settings.Messages.size() == 3)
    {
        CheckTrue(settings.Messages[0].Severity == ESeverity::Error && settings.Messages[0].Path == "general.color" &&
            settings.Messages[0].Text == unnamed.Message, "as an Error with the result's text and path");
        CheckEquals("net.servers[1].host"s, settings.Messages[1].Path, "the part's path");
        CheckEquals(ETomlCode::UnexpectedException, settings.Messages[2].Code, "the exception");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsAccess, Set_WritesTheDocument)
{
    // Arrange
    TTestSettings settings("[general]\nlevel = 1  # End\n");
    TTestSettings same("[general]\nlevel = 1\n");
    TTestSettings empty;
    CheckTrue(settings.Parsed && same.Parsed && empty.Parsed, "the texts should parse");
    TServerVector const servers{ TTestServer{ "a", 1, true, std::nullopt }, TTestServer{ "b", 2, false, "Spare" } };

    // Act
    TASWTomlResult const level = settings.Level.Set(3);
    TASWTomlResult const unchanged = same.Level.Set(1);
    TASWTomlResult const columns = empty.Columns.Set(TStringVector{ "a", "b" });
    TASWTomlResult const limits = empty.Limits.Set(TLimitMap{ { "y", 2 }, { "x", 1 } });
    TASWTomlResult const setServers = empty.Servers.Set(servers);

    // Assert
    CheckTrue(level.IsOk() && unchanged.IsOk() && columns.IsOk() && limits.IsOk() && setServers.IsOk(),
        "each Set() should succeed");
    CheckEquals("[general]\nlevel = 3  # End\n"s, settings.ToText(), "a value is replaced in place");
    CheckTrue(settings.IsModified(), "the document is modified");
    CheckEquals(3u, settings.Level.Get(), "and reads back");
    CheckFalse(same.IsModified(), "the same value changes nothing");
    CheckEquals(Escaped("[general]\ncolumns = [\"a\", \"b\"]\n\n[net.limits]\nx = 1\ny = 2\n\n# The servers.\n"
        "[[net.servers]]\nhost = \"a\"\nport = 1\nenabled = true\n\n[[net.servers]]\nhost = \"b\"\nport = 2\n"
        "enabled = false\ncomment = \"Spare\"\n"), Escaped(empty.ToText()), "an array, a map and an array of tables");
    CheckTrue(empty.Servers.Get() == servers, "the array of tables reads back");
    CheckTrue(settings.Messages.empty() && same.Messages.empty() && empty.Messages.empty(), "nothing is reported");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsAccess, Types_WithoutOperatorLessWork)
{
    // Arrange
    TTestSettings settings("[general]\nword = \"two words\"\n");
    CheckTrue(settings.Parsed, "the text should parse");

    // Act
    TTestWord const invalid = settings.Word.Get();
    TASWTomlResult const tooLong = settings.Word.Set(TTestWord{ "verylongword" });
    TASWTomlResult const set = settings.Word.Set(TTestWord{ "one" });

    // Assert
    CheckEquals("word"s, invalid.Text, "a value its converter refuses gives the default");
    CheckEquals(ETomlCode::InvalidValue, tooLong.Code, "its validator is asked");
    CheckEquals("The value \"verylongword\" isn't valid: it is longer than 8 letters."s, tooLong.Message,
        "no second '.' after the validator's");
    CheckTrue(set.IsOk(), "a valid value is set: " + set.Message);
    CheckEquals("one"s, settings.Word.Get().Text, "and reads back");
    CheckEquals(std::size_t(2), settings.Messages.size(), "the default used and the failure are reported: " +
        MessagesText(settings.Messages));
    if (!settings.Messages.empty())
    {
        CheckEquals("The value \"two words\" isn't one word. The default value \"word\" is used."s,
            settings.Messages[0].Text, "the converter's text");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsAccess, Validator_ChecksGetAndSet)
{
    // Arrange
    TTestSettings settings("[general]\nname = \"\"\n");
    CheckTrue(settings.Parsed, "the text should parse");

    // Act
    std::string const name = settings.Name.Get();
    TASWTomlResult const empty = settings.Name.Set("");
    TASWTomlResult const set = settings.Name.Set("Bob");

    // Assert
    CheckEquals("Guest"s, name, "a value the validator refuses gives the default");
    CheckEquals(ETomlCode::InvalidValue, empty.Code, "and isn't set");
    CheckEquals("The value \"\" isn't valid: it can't be empty."s, empty.Message, "the validator's text, with a '.'");
    CheckEquals("general.name"s, empty.Path, "its path");
    CheckTrue(set.IsOk(), "a valid value is set: " + set.Message);
    CheckEquals("[general]\nname = \"Bob\"\n"s, settings.ToText(), "in place");
    CheckEquals(std::size_t(2), settings.Messages.size(), "Get()'s Warning and Set()'s Error: " +
        MessagesText(settings.Messages));
    if (!settings.Messages.empty())
    {
        CheckEquals("The value \"\" isn't valid: it can't be empty. The default value \"Guest\" is used."s,
            settings.Messages[0].Text, "Get()'s Warning");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsAccess, ValueOptions_AreTheSettingsOwnOrTheOwners)
{
    // Arrange
    TTestSettings settings("[general]\n");
    TASWTomlOptions octal;
    octal.Writing.LineEnding = ASWToml::ELineEnding::LF;
    octal.Values.IntegerBase = EIntegerBase::Octal;
    CheckTrue(settings.Parsed && settings.SetOptions(octal).IsOk(), "the text and the options should be taken");

    // Act
    TASWTomlResult const mask = settings.Mask.Set(255);
    TASWTomlResult const plain = settings.Plain.Set(8);

    // Assert
    CheckTrue(mask.IsOk() && plain.IsOk(), "both Set()s should succeed");
    CheckEquals("[general]\nmask = 0xFF\nplain = 0o10\n"s, settings.ToText(),
        "the setting's own value options, else the owner's");
    CheckTrue(settings.Mask.Get() == 255 && settings.Plain.Get() == 8, "both read back");
}
//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_SettingsBatch
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_SettingsBatch::TTest_ASWToml_SettingsBatch()
    : inherited("ASWToml_SettingsBatch_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_SettingsBatch::~TTest_ASWToml_SettingsBatch()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SettingsBatch::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SettingsBatch::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SettingsBatch::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SettingsBatch::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsBatch, Apply_AddsKeysWithTheirComments)
{
    // Arrange
    // The keys the settings add: color and name with their comments, cheats (which has none), and the server in an
    // inline table, which has no line of its own for its comment
    std::string const text = "net = { other = 1 }\n\n[general]\nlevel = 1\n";
    TTestSettings settings(text);
    TTestSettings twin(text);
    TTestServer const server{ "s", 5, true, std::nullopt };
    TASWTomlEdits edits;
    TASWTomlResult const added[] = { settings.Color.Set(edits, EColor::Blue), settings.Cheats.Set(edits, true),
        settings.Name.Set(edits, "Cy"), settings.Server.Set(edits, server), settings.Level.Set(edits, 2u) };
    TASWTomlResult const expected[] = { twin.Color.Set(EColor::Blue), twin.Cheats.Set(true), twin.Name.Set("Cy"),
        twin.Server.Set(server), twin.Level.Set(2u) };
    TASWTomlDocument document = settings.GetDocument();
    ASWToml::Detail::TBatchStats stats;

    // Act
    TASWTomlResult const result = settings.Apply(edits);
    TASWTomlResult const viaDocument = document.Apply(edits, &settings, stats);

    // Assert
    for (std::size_t i = 0; i < sizeof(added) / sizeof(added[0]); ++i)
        CheckTrue(added[i].IsOk() && expected[i].IsOk(), "each edit should be added and set: " + added[i].Message);

    CheckTrue(result.IsOk() && viaDocument.IsOk(), "the batch should succeed: " + result.Message);
    CheckEquals(Escaped(twin.ToText()), Escaped(settings.ToText()), "the text should be the Set()s' one by one");
    CheckTrue(settings.ToText().find("# The color.\ncolor = ") != std::string::npos &&
        settings.ToText().find("# The name.\nname = \"Cy\"") != std::string::npos,
        "a key added gets its setting's comment");
    CheckEquals(settings.ToText(), document.ToText(), "the document's Apply() for the owner should make the same");
    CheckEquals(std::size_t(0), stats.Replays, "no group should be made again one by one");
    CheckEquals(std::size_t(0), settings.Messages.size(), "nothing should be reported");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsBatch, Apply_GroupsLikeOtherEdits)
{
    // Arrange
    // Settings' edits of different places are written together as other edits are, new keys without comments of one
    // table merged; a key added with a comment takes one more group, for the comment
    std::string const text = "[general]\nlevel = 1\nname = \"a\"\n";
    TTestSettings settings(text);
    TTestSettings twin(text);
    TASWTomlEdits together;
    TASWTomlResult const added[] = { settings.Level.Set(together, 3u), settings.Zoom.Set(together, 2.0),
        settings.Plain.Set(together, 9) };
    TASWTomlEdits commented;
    TASWTomlResult const addedColor = settings.Color.Set(commented, EColor::Blue);
    TASWTomlResult const addedAfter = settings.Name.Set(commented, "b");
    TASWTomlDocument document = settings.GetDocument();
    ASWToml::Detail::TBatchStats togetherStats;
    ASWToml::Detail::TBatchStats commentedStats;

    // Act
    TASWTomlResult const togetherResult = document.Apply(together, &settings, togetherStats);
    TASWTomlResult const commentedResult = document.Apply(commented, &settings, commentedStats);
    TASWTomlResult const expected[] = { twin.Level.Set(3u), twin.Zoom.Set(2.0), twin.Plain.Set(9),
        twin.Color.Set(EColor::Blue), twin.Name.Set("b") };

    // Assert
    CheckTrue(added[0].IsOk() && added[1].IsOk() && added[2].IsOk() && addedColor.IsOk() && addedAfter.IsOk(),
        "each setting's edit should be added");
    for (TASWTomlResult const& one : expected)
        CheckTrue(one.IsOk(), "each Set() should succeed: " + one.Message);

    CheckTrue(togetherResult.IsOk() && commentedResult.IsOk(), "each batch should succeed");
    CheckEquals(Escaped(twin.ToText()), Escaped(document.ToText()), "the text should be the Set()s' one by one");
    CheckEquals(std::size_t(1), togetherStats.Groups, "edits of different places should be one group");
    CheckEquals(std::size_t(2), commentedStats.Groups, "a key added with a comment, then its comment with the rest");
    CheckEquals(std::size_t(0), togetherStats.Replays + commentedStats.Replays, "no group should be made again");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsBatch, Apply_MatchesSetOneByOne)
{
    // Arrange
    TTestSettings settings(BatchText);
    TTestSettings twin(BatchText);
    TServerVector servers = settings.Servers.Get();
    servers.erase(servers.begin());
    TTestServer const server{ "b", 2, true, std::nullopt };
    TLimitMap const limits{ { "a", 5 } };
    TASWTomlEdits edits;
    TASWTomlResult const added[] = { settings.Name.Set(edits, "Bea"), settings.Level.Set(edits, 3u),
        settings.Server.Set(edits, server), settings.Servers.Set(edits, servers), settings.Limits.Set(edits, limits),
        settings.Zoom.Set(edits, 2.0), settings.Mask.Set(edits, 16) };
    edits.SetComment("net.server", "Raw");
    TASWTomlResult const expected[] = { twin.Name.Set("Bea"), twin.Level.Set(3u), twin.Server.Set(server),
        twin.Servers.Set(servers), twin.Limits.Set(limits), twin.Zoom.Set(2.0), twin.Mask.Set(16),
        twin.SetComment("net.server", "Raw") };
    TASWTomlDocument document = settings.GetDocument();
    ASWToml::Detail::TBatchStats stats;

    // Act
    TASWTomlResult const result = settings.Apply(edits);
    TASWTomlResult const viaDocument = document.Apply(edits, &settings, stats);

    // Assert
    for (std::size_t i = 0; i < sizeof(added) / sizeof(added[0]); ++i)
        CheckTrue(added[i].IsOk(), "each setting's edit should be added: " + added[i].Message);

    for (std::size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
        CheckTrue(expected[i].IsOk(), "each Set() should succeed: " + expected[i].Message);

    CheckTrue(result.IsOk() && viaDocument.IsOk(), "the batch should succeed: " + result.Message);
    CheckEquals(Escaped(twin.ToText()), Escaped(settings.ToText()), "the text should be the Set()s' one by one");
    CheckTrue(settings.ToText().find("extra = true") != std::string::npos, "the struct keeps the key it doesn't know");
    CheckTrue(settings.ToText().find("mask = 0x10") != std::string::npos, "a setting writes with its value options");
    CheckTrue(settings.ToText().find("note = \"Y\"") != std::string::npos &&
        settings.ToText().find("X's comment") == std::string::npos,
        "the element removed goes, the other keeps its key");
    CheckEquals(settings.ToText(), document.ToText(), "the document's Apply() for the owner should make the same");
    CheckTrue(stats.Groups < edits.GetEdits().size(), "edits of different places should be written together");
    CheckEquals(std::size_t(0), stats.Replays, "no group should be made again one by one");
    CheckTrue(settings.IsModified(), "the object should be modified");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsBatch, Apply_RandomBatchesMatchSetOneByOne)
{
    // Arrange & Act & Assert
    // Random batches of settings' edits and other edits, each compared with the same Set()s and edits one by one on a
    // twin: the same text, or when an edit fails, its result, and nothing changed
    std::string const texts[] = { "", BatchText, "general = { name = \"B\" }\nnet = { limits = { a = 1 } }\n" };
    std::mt19937 generator(20261027);
    std::size_t made = 0;
    for (std::string const& text : texts)
    {
        for (int batch = 0; batch < 100; ++batch)
        {
            TTestSettings settings(text);
            TTestSettings twin(text);
            TASWTomlEdits edits;
            TASWTomlResult failure;
            int const count = 1 + static_cast<int>(generator() % 6);
            for (int i = 0; i < count; ++i)
                AddRandomStep(settings, twin, edits, generator, failure);

            TASWTomlDocument document = settings.GetDocument();
            ASWToml::Detail::TBatchStats stats;
            TASWTomlResult const result = settings.Apply(edits);
            TASWTomlResult const viaDocument = document.Apply(edits, &settings, stats);
            std::string const which = " (text " + std::to_string(&text - texts) + ", batch " + std::to_string(batch) +
                ")";
            if (failure.IsOk())
            {
                CheckTrue(result.IsOk(), "the batch should succeed as the edits one by one do" + which + ": " +
                    result.Message);
                CheckEquals(Escaped(twin.ToText()), Escaped(settings.ToText()),
                    "the text should be the edits' one by one" + which);
            }
            else
            {
                CheckEmpty(ResultProblem(result, failure), "the batch should fail as the edit one by one does" + which);
                CheckEquals(Escaped(text), Escaped(settings.ToText()), "a failed batch should change nothing" + which);
            }

            CheckEmpty(ResultProblem(viaDocument, result), "the document's Apply() for the owner should do the same" +
                which);
            CheckEquals(settings.ToText(), document.ToText(), "and make the same text" + which);
            CheckEquals(std::size_t(0), stats.Replays, "no group should be made again one by one" + which);
            made += edits.GetEdits().size();
        }
    }

    CheckGreaterThan(made, std::size_t(500), "the batches should have edits");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsBatch, Apply_SeesTheEditsBeforeIt)
{
    // Arrange
    // Each setting's edit follows an edit of its value in the same batch: the struct is set over the value with extra
    // changed, the name is added again with its comment after its removal, and the servers are paired with the
    // elements left after the first one's removal
    TTestSettings settings(BatchText);
    TTestSettings twin(BatchText);
    TTestServer const server{ "b", 2, true, std::nullopt };
    TServerVector const servers{ TTestServer{ "y", 30, true, std::nullopt } };
    TASWTomlEdits edits;
    edits.SetValue("net.server.extra", 2);
    TASWTomlResult const serverAdded = settings.Server.Set(edits, server);
    edits.Remove("general.name");
    TASWTomlResult const nameAdded = settings.Name.Set(edits, "Dee");
    edits.Remove("net.servers[0]");
    TASWTomlResult const serversAdded = settings.Servers.Set(edits, servers);
    TASWTomlResult const extraSet = twin.SetValue("net.server.extra", 2);
    TASWTomlResult const serverSet = twin.Server.Set(server);
    bool const nameRemoved = twin.Remove("general.name");
    TASWTomlResult const nameSet = twin.Name.Set("Dee");
    bool const serverRemoved = twin.Remove("net.servers[0]");
    TASWTomlResult const serversSet = twin.Servers.Set(servers);

    // Act
    TASWTomlResult const result = settings.Apply(edits);

    // Assert
    CheckTrue(serverAdded.IsOk() && nameAdded.IsOk() && serversAdded.IsOk(), "each setting's edit should be added");
    CheckTrue(extraSet.IsOk() && serverSet.IsOk() && nameRemoved && nameSet.IsOk() && serverRemoved &&
        serversSet.IsOk(), "each edit one by one should succeed");
    CheckTrue(result.IsOk(), "the batch should succeed: " + result.Message);
    CheckEquals(Escaped(twin.ToText()), Escaped(settings.ToText()), "the text should be the edits' one by one");
    CheckTrue(settings.ToText().find("extra = 2") != std::string::npos,
        "the struct is set over the value as it is then");
    CheckTrue(settings.ToText().find("# The name.\nname = \"Dee\"") != std::string::npos,
        "a key removed before is added again with its comment");
    CheckTrue(settings.ToText().find("port = 30\nnote = \"Y\"") != std::string::npos,
        "the element left keeps the key the struct doesn't know");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsBatch, Apply_SettingOfAnotherObjectFails)
{
    // Arrange
    std::string const text = "[general]\nlevel = 1\n";
    TTestSettings settings(text);
    TTestSettings other(text);
    TASWTomlDocument document;
    TASWTomlResult const parsed = document.Parse(text);
    TASWTomlEdits edits;
    TASWTomlResult const added = settings.Level.Set(edits, 3u);
    TASWTomlEdits failsBefore;
    failsBefore.Rename("general.missing", "x");
    TASWTomlResult const addedAfter = settings.Level.Set(failsBefore, 3u);
    TASWTomlResult const notFound = TTestSettings(text).Rename("general.missing", "x");
    TASWTomlResult const refused = ASWToml::Detail::SettingEditFailure("general.level");

    // Act
    TASWTomlResult const toOther = other.Apply(edits);
    TASWTomlResult const toDocument = document.Apply(edits);
    TASWTomlResult const firstFails = other.Apply(failsBefore);
    TASWTomlResult const toOwner = settings.Apply(edits);

    // Assert
    CheckTrue(parsed.IsOk() && added.IsOk() && addedAfter.IsOk(), "the edits should be added");
    CheckEquals(ETomlCode::InvalidArgument, refused.Code, "a setting's edit elsewhere is InvalidArgument");
    CheckEquals("general.level"s, refused.Path, "at its path");
    CheckEmpty(ResultProblem(toOther, refused), "another object's Apply() should refuse it");
    CheckEmpty(ResultProblem(toDocument, refused), "and a document's");
    CheckEmpty(ResultProblem(firstFails, notFound), "an edit before it that fails fails first");
    CheckEquals(text, other.ToText(), "the other object shouldn't change");
    CheckEquals(text, document.ToText(), "nor the document");
    CheckEquals(std::size_t(2), other.Messages.size(), "each failure should be reported once");
    CheckTrue(toOwner.IsOk() && settings.Level.Get() == 3u, "the owner's Apply() should make it");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsBatch, Set_FailureAddsNothing)
{
    // Arrange
    std::string const text = "[general]\nlevel = 1\n";
    TTestSettings settings(text);
    TASWTomlTable badTable{ settings, "a..b" };
    TASWTomlSetting<int> inBadTable{ badTable, "x", 1 };
    TTestSettings twin(text);
    TASWTomlTable twinBadTable{ twin, "a..b" };
    TASWTomlSetting<int> twinInBadTable{ twinBadTable, "x", 1 };
    TASWTomlEdits edits;

    // Act
    TASWTomlResult const results[] = { settings.Level.Set(edits, 9u), settings.Name.Set(edits, ""),
        settings.Unnamed.Set(edits, EColor::Unlisted), inBadTable.Set(edits, 2) };
    TASWTomlResult const expected[] = { twin.Level.Set(9u), twin.Name.Set(""), twin.Unnamed.Set(EColor::Unlisted),
        twinInBadTable.Set(2) };

    // Assert
    for (std::size_t i = 0; i < sizeof(results) / sizeof(results[0]); ++i)
    {
        CheckFalse(results[i].IsOk(), "the value should be refused (" + std::to_string(i) + ")");
        CheckEmpty(ResultProblem(results[i], expected[i]), "as Set() refuses it (" + std::to_string(i) + ")");
    }

    CheckTrue(edits.IsEmpty(), "no edit should be added");
    CheckTrue(edits.GetResult().IsOk(), "and the failures aren't kept in the edits");
    CheckEquals(std::size_t(4), settings.Messages.size(), "each failure should be reported once");
    CheckEquals(text, settings.ToText(), "nothing should change");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_SettingsLoad
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_SettingsLoad::TTest_ASWToml_SettingsLoad()
    : inherited("ASWToml_SettingsLoad_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_SettingsLoad::~TTest_ASWToml_SettingsLoad()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SettingsLoad::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SettingsLoad::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SettingsLoad::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SettingsLoad::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, AppSettings_OldFileIsBroughtUpToDate)
{
    // Arrange
    // A file as the application's earlier version wrote it (CRLF), which a user then edited: an empty user name, a
    // level out of range, a note at the end of a line, a key of their own in a server and a port out of range. The
    // days the logs are kept have their earlier name, the servers have no field secure yet, the table view is missing,
    // and the table general, the key theme and the table network have no comments.
    std::string const oldText = "# Settings of the ASWToml example.\r\n\r\n[general]\r\n"
        "# The name in the title bar.\r\nuser_name = \"\"\r\ntheme = \"dark\"\r\n\r\n[logging]\r\n"
        "# From 0 (errors only) to 4 (everything).\r\n"
        "level = 9\r\nlog_days = 14  # Two weeks\r\n\r\n[network]\r\ntimeout = 30\r\n\r\n"
        "# The servers, tried in order.\r\n[[network.servers]]\r\nhost = \"main.example.com\"\r\nport = 443\r\n"
        "weight = 2  # Mine\r\n\r\n[[network.servers]]\r\nhost = \"backup.example.com\"\r\nport = 99999\r\n";
    std::string const expected = "# Settings of the ASWToml example.\r\n\r\n# What the application looks like.\r\n"
        "[general]\r\n# The name in the title bar.\r\nuser_name = \"Guest\"\r\n# Light, Dark or System.\r\n"
        "theme = \"dark\"\r\n\r\n[logging]\r\n# From 0 (errors only) to 4 (everything).\r\nlevel = 2\r\n"
        "# How many days the logs are kept; 0 keeps them forever.\r\ndays_kept = 14  # Two weeks\r\n"
        "# Relative to the folder of this file.\r\nfolder = \"logs\"\r\n\r\n# Where the application connects.\r\n"
        "[network]\r\n# In seconds.\r\ntimeout = 30\r\n\r\n# The servers, tried in order.\r\n[[network.servers]]\r\n"
        "host = \"main.example.com\"\r\nport = 443\r\nweight = 2  # Mine\r\nsecure = false\r\n\r\n"
        "[[network.servers]]\r\nhost = \"backup.example.com\"\r\nport = 80\r\nsecure = false\r\n\r\n[view]\r\n"
        "# From 0.25 to 4.\r\nzoom = 1.0\r\n# The columns shown, in order.\r\n"
        "columns = [\"name\", \"size\", \"date\"]\r\n";
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const file = folder.Path() / "app.toml";
    AssertTrue(WriteTestFile(file, oldText), "the old file should be written");
    std::string const path = ASWToml::PathToUtf8(file);
    TAppSettings settings;
    TMessageVector messages;
    settings.SetLogCallback(TRecorder(messages));
    TAppSettings again;
    TMessageVector againMessages;
    again.SetLogCallback(TRecorder(againMessages));
    TAppServerVector const changedServers{ TAppServer{ "main.example.com", 8443, false },
        TAppServer{ "backup.example.com", 80, false } };

    // Act
    // As the application would: load, save when needed and read the settings; then load the file saved, change the
    // port of a server and save again
    TASWTomlResult const loaded = settings.Load(path);
    bool const modified = settings.IsModified();
    TASWTomlResult const saved = modified ? settings.Save() : TASWTomlResult();
    std::string written;
    bool const read = ReadTestFile(file, written);
    TASWTomlResult const loadedAgain = again.Load(path);
    bool const modifiedAgain = again.IsModified();
    TASWTomlResult const set = again.Servers.Set(changedServers);
    TASWTomlResult const savedAgain = again.Save();
    std::string writtenAgain;
    bool const readAgain = ReadTestFile(file, writtenAgain);

    // Assert
    CheckTrue(loaded.IsOk() && modified, "the old file should load, and need saving");
    CheckTrue(saved.IsOk() && read, "the file should be saved");
    CheckEquals(Escaped(expected), Escaped(written), "only what needs it should change: values that aren't valid "
        "replaced, the key renamed in place, missing keys, fields and the table added in the file's line ending, the "
        "code's comments set, and the user's key and notes kept");
    CheckEquals("Warning InvalidValue general.user_name\nWarning ValueOutOfRange logging.level\n"
        "Info KeyRenamed logging.days_kept\nInfo KeyAdded logging.folder\nInfo KeyAdded view.zoom\n"
        "Info KeyAdded view.columns\nInfo KeyAdded network.servers[0].secure\n"
        "Warning ValueOutOfRange network.servers[1].port\nInfo KeyAdded network.servers[1].secure\n"s,
        MessageKinds(messages), "each change should be reported, in the order the settings are declared");
    CheckEquals("Guest"s, settings.UserName.Get(), "the user name should be its default");
    CheckTrue(settings.Theme.Get() == ETheme::Dark, "the theme should be the file's");
    CheckFalse(settings.EnableCheats.Get() || settings.EnableCheats.IsPresent(),
        "the optional setting should be its default, and not be added");
    CheckEquals(2u, settings.Level.Get(), "the level should be its default");
    CheckEquals(14u, settings.DaysKept.Get(), "the days kept should keep the old key's value");
    CheckTrue(settings.Folder.Get() == std::filesystem::path("logs") && settings.Zoom.Get() == 1.0 &&
        settings.Columns.Get() == settings.Columns.GetDefault(), "the keys added should be their defaults");
    TAppServerVector const servers = settings.Servers.Get();
    CheckEquals(std::size_t(2), servers.size(), "the user's servers should be kept");
    if (servers.size() == 2)
    {
        CheckTrue(servers[0].Host == "main.example.com" && servers[0].Port == 443 && !servers[0].Secure,
            "the first as the user wrote it, with the new field's default");
        CheckTrue(servers[1].Host == "backup.example.com" && servers[1].Port == 80,
            "the second with its port repaired");
    }

    CheckTrue(loadedAgain.IsOk() && !modifiedAgain, "the saved file should load without a change");
    CheckTrue(againMessages.empty(), "or a message: " + MessagesText(againMessages));
    CheckTrue(set.IsOk() && savedAgain.IsOk() && readAgain, "a server should be changed and saved");
    std::string changed = expected;
    changed.replace(changed.find("port = 443"), 10, "port = 8443");
    CheckEquals(Escaped(changed), Escaped(writtenAgain), "only its port should change, and the user's key stay");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, Bindings_FollowTheirLifetimes)
{
    // Arrange
    TLoadSettings settings;
    std::string withLocal;

    // Act
    {
        TASWTomlSetting<int> local{ settings.Logging, "local", 1, "Local." };
        TASWTomlTable extra{ settings, "extra", "Extra." };
        TASWTomlSetting<int> extraKey{ extra, "key", 1 };
        CheckTrue(settings.LoadText("").IsOk(), "the empty text should load");
        withLocal = settings.ToText();
    }

    TASWTomlResult const loaded = settings.LoadText("");

    // Assert
    CheckEquals(Escaped("# Settings of the test.\n\n# What it looks like.\n[general]\n# The name.\nname = \"Guest\"\n"
        "# From 0 to 4.\nlevel = 2\n\n[logging]\n# Days kept.\ndays = 30\n# Local.\nlocal = 1\nmask = 0xFF\n\n"
        "# Extra.\n[extra]\nkey = 1\n"), Escaped(withLocal), "a setting and a table are applied while they exist, in "
        "declaration order");
    CheckTrue(loaded.IsOk(), "the empty text should load again");
    CheckEquals(Escaped(FirstRunText), Escaped(settings.ToText()), "and isn't once it is destroyed");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, Comments_CodeWinsReplacesTheFilesComments)
{
    // Arrange
    std::string const edited = "# Old file comment.\n\n# Old\n[general]\n# Mine\nname = \"Guest\"\n# From 0 to 4.\n"
        "level = 2\n\n[logging]\n# Days kept.\ndays = 30\nmask = 0xFF\n";
    TLoadSettings settings;
    TLoadSettings same;

    // Act
    TASWTomlResult const loaded = settings.LoadText(edited);
    TASWTomlResult const loadedSame = same.LoadText(FirstRunText);

    // Assert
    CheckTrue(loaded.IsOk() && loadedSame.IsOk(), "the texts should load");
    CheckEquals(Escaped(FirstRunText), Escaped(settings.ToText()), "the code's comments replace the file's");
    CheckTrue(settings.IsModified(), "which modifies the document");
    CheckEquals(Escaped(FirstRunText), Escaped(same.ToText()), "comments that read the same stay");
    CheckFalse(same.IsModified(), "and don't modify it");
    CheckTrue(settings.Messages.empty() && same.Messages.empty(), "nothing is reported");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, Comments_EmptyCodeCommentKeepsTheFiles)
{
    // Arrange
    // Comments of the table logging and the setting cheats, which have none in the code
    std::string text = FirstRunText;
    text.replace(text.find("level = 2\n"), 10, "level = 2\n# Mine\ncheats = true\n");
    text.replace(text.find("[logging]"), 9, "# My logging\n[logging]");
    TLoadSettings settings;

    // Act
    TASWTomlResult const loaded = settings.LoadText(text);

    // Assert
    CheckTrue(loaded.IsOk(), "the text should load");
    CheckEquals(Escaped(text), Escaped(settings.ToText()), "a setting or table without a comment keeps the file's");
    CheckFalse(settings.IsModified(), "so nothing changes");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, Comments_FileWinsKeepsTheFilesComments)
{
    // Arrange
    std::string const edited = "# Old file comment.\n\n# Old\n[general]\n# Mine\nname = \"Guest\"\n# From 0 to 4.\n"
        "level = 2\n\n[logging]\n# Days kept.\ndays = 30\nmask = 0xFF\n";
    TASWTomlOptions fileWins = LfOptions();
    fileWins.Writing.CommentPolicy = ASWToml::ECommentPolicy::FileWins;
    TLoadSettings settings;
    TLoadSettings bare;
    CheckTrue(settings.SetOptions(fileWins).IsOk() && bare.SetOptions(fileWins).IsOk(), "the options should be taken");

    // Act
    TASWTomlResult const loaded = settings.LoadText(edited);
    TASWTomlResult const loadedBare = bare.LoadText("[general]\nname = \"Ann\"\n");

    // Assert
    CheckTrue(loaded.IsOk() && loadedBare.IsOk(), "the texts should load");
    CheckEquals(Escaped(edited), Escaped(settings.ToText()), "the file's comments stay");
    CheckFalse(settings.IsModified(), "so nothing changes");
    CheckEquals(Escaped("# Settings of the test.\n\n# What it looks like.\n[general]\n# The name.\nname = \"Ann\"\n"
        "# From 0 to 4.\nlevel = 2\n\n[logging]\n# Days kept.\ndays = 30\nmask = 0xFF\n"), Escaped(bare.ToText()),
        "the code's comments go where the file has none");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, Comments_WithoutALineOfTheirOwnAreSkipped)
{
    // Arrange
    TLoadSettings settings;

    // Act
    // The table general is made by dotted keys, and the keys of logging are in an inline table
    TASWTomlResult const loaded = settings.LoadText("general.name = \"Ann\"\ngeneral.level = 1\n"
        "logging = { days = 3, mask = 0x10 }\n");

    // Assert
    CheckTrue(loaded.IsOk(), "the text should load");
    CheckEquals(Escaped("# Settings of the test.\n\n# The name.\ngeneral.name = \"Ann\"\n# From 0 to 4.\n"
        "general.level = 1\nlogging = { days = 3, mask = 0x10 }\n"), Escaped(settings.ToText()),
        "the comments that have a line are set, the others skipped");
    CheckTrue(settings.Messages.empty(), "without a message: " + MessagesText(settings.Messages));
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, Load_AddsMissingKeysInDeclarationOrder)
{
    // Arrange
    TLoadSettings settings;

    // Act
    TASWTomlResult const loaded = settings.LoadText("[logging]\nother = 1\n\n[general]\nname = \"Ann\"\n");

    // Assert
    CheckTrue(loaded.IsOk(), "the text should load");
    CheckEquals(Escaped("# Settings of the test.\n\n[logging]\nother = 1\n# Days kept.\ndays = 30\nmask = 0xFF\n\n"
        "# What it looks like.\n[general]\n# The name.\nname = \"Ann\"\n# From 0 to 4.\nlevel = 2\n"),
        Escaped(settings.ToText()), "missing keys after their table's last key, in declaration order");
    CheckTrue(settings.IsModified(), "the document needs saving");
    CheckEquals(std::size_t(3), settings.Messages.size(), "one note per key added: " + MessagesText(settings.Messages));
    if (settings.Messages.size() == 3)
    {
        TASWTomlMessage const& level = settings.Messages[0];
        CheckTrue(level.Severity == ESeverity::Info && level.Code == ETomlCode::KeyAdded, "an Info, KeyAdded");
        CheckEquals("The key general.level is missing: it is added with its default value 2."s, level.Text,
            "its text");
        CheckEquals("general.level"s, level.Path, "its path");
        CheckEquals("logging.days"s, settings.Messages[1].Path, "in declaration order");
        CheckEquals("logging.mask"s, settings.Messages[2].Path, "a setting with value options of its own too");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, Load_CommentThatCantBeWrittenIsAnError)
{
    // Arrange
    TLoadSettings settings;
    TASWTomlSetting<int> badComment{ settings.Logging, "bad", 1, "Bad\x01" };

    // Act
    TASWTomlResult const loaded = settings.LoadText(FirstRunText);

    // Assert
    CheckTrue(loaded.IsOk(), "the text should load");
    CheckEquals(Escaped(FirstRunText + "bad = 1\n"s), Escaped(settings.ToText()), "the key is added without it");
    CheckEquals(std::size_t(2), settings.Messages.size(), "the key added and the comment that failed: " +
        MessagesText(settings.Messages));
    if (settings.Messages.size() == 2)
    {
        TASWTomlMessage const& failure = settings.Messages[1];
        CheckTrue(failure.Severity == ESeverity::Error && failure.Code == ETomlCode::InvalidValue,
            "an Error with the edit's code");
        CheckEquals("logging.bad"s, failure.Path, "its path");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, Load_DeclaredTableThatIsntOneIsReplaced)
{
    // Arrange
    TLoadSettings settings;

    // Act
    TASWTomlResult const loaded = settings.LoadText("logging = 5\n[general]\nname = \"Ann\"\nlevel = 1\n");

    // Assert
    CheckTrue(loaded.IsOk(), "the text should load");
    CheckEquals(Escaped("# Settings of the test.\n\n# What it looks like.\n[general]\n# The name.\nname = \"Ann\"\n"
        "# From 0 to 4.\nlevel = 1\n\n[logging]\n# Days kept.\ndays = 30\nmask = 0xFF\n"),
        Escaped(settings.ToText()), "the value is replaced by a table of the settings");
    CheckEquals(std::size_t(3), settings.Messages.size(), "the keys added and the value replaced: " +
        MessagesText(settings.Messages));
    if (settings.Messages.size() == 3)
    {
        TASWTomlMessage const& replaced = settings.Messages[2];
        CheckTrue(replaced.Severity == ESeverity::Warning && replaced.Code == ETomlCode::TypeMismatch,
            "a Warning, TypeMismatch");
        CheckEquals("The value 5 is an integer, not a table. It is replaced by a table of its settings' values."s,
            replaced.Text, "its text");
        CheckEquals("logging"s, replaced.Path, "the table's path");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, Load_FailureAppliesNothing)
{
    // Arrange
    TLoadSettings settings(FirstRunText);

    // Act
    TASWTomlResult const loaded = settings.LoadText("[general\n");

    // Assert
    CheckEquals(ETomlCode::ParseFailed, loaded.Code, "the text doesn't parse");
    CheckEquals(Escaped(FirstRunText), Escaped(settings.ToText()), "the document stays");
    CheckEquals(std::size_t(1), settings.Messages.size(), "only the failure is reported: " +
        MessagesText(settings.Messages));
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, Load_InvalidDefaultIsAnError)
{
    // Arrange
    TLoadSettings settings;
    TASWTomlSetting<unsigned> outOfRange{ settings.General, "out_of_range", 9, "", { .Max = 4 } };
    TASWTomlSetting<EColor> unnamed{ settings.General, "unnamed", EColor::Unlisted };

    // Act
    TASWTomlResult const loaded = settings.LoadText(FirstRunText);

    // Assert
    CheckTrue(loaded.IsOk(), "the text should load");
    CheckEquals(Escaped(FirstRunText), Escaped(settings.ToText()), "the keys aren't added");
    CheckEquals(std::size_t(2), settings.Messages.size(), "each is reported: " + MessagesText(settings.Messages));
    if (settings.Messages.size() == 2)
    {
        TASWTomlMessage const& range = settings.Messages[0];
        CheckTrue(range.Severity == ESeverity::Error && range.Code == ETomlCode::ValueOutOfRange,
            "an Error with the check's code");
        CheckEquals("The default value of the setting general.out_of_range can't be used: The value 9 is out of the "
            "setting's range: it must be at most 4."s, range.Text, "its text");
        CheckEquals("general.out_of_range"s, range.Path, "its path");
        CheckEquals(ETomlCode::InvalidValue, settings.Messages[1].Code, "a default that can't be written");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, Load_KeyWinsOverItsOldKeys)
{
    // Arrange
    TPreloaded toml("");
    TASWTomlTable general{ toml, "general" };
    TASWTomlSetting<int> width{ general, "width", 640, "", { .OldKeys = { "w" } } };
    TASWTomlSetting<TServerVector> servers{ general, "servers", TServerVector(), "", { .OldKeys = { "mirrors" } } };
    std::string const text = "[general]\nw = 800\nwidth = 900\n\n[[general.mirrors]]\nhost = \"a\"\nport = 1\n\n"
        "[[general.servers]]\nhost = \"b\"\nport = 2\n";

    // Act
    TASWTomlResult const loaded = toml.LoadText(text);

    // Assert
    CheckTrue(loaded.IsOk(), "the text should load");
    CheckEquals(Escaped(text), Escaped(toml.ToText()), "the keys and their old keys stay as they are");
    CheckFalse(toml.IsModified(), "nothing changes");
    CheckTrue(toml.Messages.empty(), "nothing is reported: " + MessagesText(toml.Messages));
    CheckEquals(900, width.Get(), "the setting reads its own key");
    TServerVector const read = servers.Get();
    CheckTrue(read.size() == 1 && read[0].Host == "b", "and so does a setting of an array of tables");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, Load_LeavesAValidDocumentAsItIs)
{
    // Arrange
    // Valid values in other forms than the settings would write
    std::string const otherForms = std::string(FirstRunText).replace(std::string(FirstRunText).find("level = 2"), 9,
        "level = 0x2").replace(std::string(FirstRunText).find("mask = 0xFF") + 2, 11, "mask = 255");
    TLoadSettings settings;
    TLoadSettings forms;

    // Act
    TASWTomlResult const loaded = settings.LoadText(FirstRunText);
    TASWTomlResult const loadedForms = forms.LoadText(otherForms);

    // Assert
    CheckTrue(loaded.IsOk() && loadedForms.IsOk(), "the texts should load");
    CheckEquals(Escaped(FirstRunText), Escaped(settings.ToText()), "a document that needs nothing stays byte for byte");
    CheckFalse(settings.IsModified(), "and isn't modified");
    CheckEquals(Escaped(otherForms), Escaped(forms.ToText()), "a valid value keeps its form");
    CheckFalse(forms.IsModified(), "nor is that modified");
    CheckTrue(settings.Messages.empty() && forms.Messages.empty(), "nothing is reported");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, Load_MessagesNameTheFileLoaded)
{
    // Arrange
    // The callback saves the object as another file on the first message, so the object's file path is that file when
    // the others are reported
    TTempDir const folder;
    std::string const file = ASWToml::PathToUtf8(folder.Path() / "app.toml");
    std::string const other = ASWToml::PathToUtf8(folder.Path() / "other.toml");
    TMessageVector messages;
    TLoadSettings settings;
    settings.SetLogCallback(TSaveAsOnFirst(settings, other, messages));

    // Act
    TASWTomlResult const loaded = settings.Load(file);

    // Assert
    CheckEquals(ETomlCode::FileNotFound, loaded.Code, "the file is missing");
    CheckEquals(other, settings.GetFilePath(), "the callback saved the object elsewhere");
    CheckEquals(std::size_t(5), messages.size(), "the missing file and the keys added: " + MessagesText(messages));
    for (TASWTomlMessage const& message : messages)
        CheckEquals(file, message.FilePath, "each names the file loaded: " + ASWToml::ToString(message));
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, Load_MessagesWriteValuesAsTheSettingDoes)
{
    // Arrange
    // A setting with value options of its own (mask), and settings that write with the document's, in hexadecimal
    TASWTomlOptions hexadecimal = LfOptions();
    hexadecimal.Values.IntegerBase = ASWToml::EIntegerBase::Hexadecimal;
    TLoadSettings own;
    TPreloaded document("");
    CheckTrue(document.SetOptions(hexadecimal).IsOk(), "the options should be taken");
    TASWTomlTable table{ document, "t" };
    TASWTomlSetting<unsigned> level{ table, "level", 2, "", { .Min = 1, .Max = 4 } };
    TASWTomlSetting<TServerVector> servers{ table, "servers", TServerVector() };
    TASWTomlSetting<int> added{ table, "added", 16 };

    // Act
    TASWTomlResult const loadedOwn = own.LoadText("[logging]\ndays = 30\n");
    TASWTomlResult const loaded = document.LoadText("[t]\nlevel = 9\n\n[[t.servers]]\nhost = \"a\"\nport = \"x\"\n\n"
        "[[t.servers]]\nhost = \"b\"\n");

    // Assert
    CheckTrue(loadedOwn.IsOk() && loaded.IsOk(), "the texts should load");
    CheckEquals(std::size_t(3), own.Messages.size(), "three keys added: " + MessagesText(own.Messages));
    if (own.Messages.size() == 3)
    {
        CheckEquals("The key general.level is missing: it is added with its default value 2."s, own.Messages[1].Text,
            "in decimal for a setting that writes in decimal");
        CheckEquals("The key logging.mask is missing: it is added with its default value 0xFF."s, own.Messages[2].Text,
            "in hexadecimal for one with value options of its own that write it so");
    }

    CheckEquals("[t]\nlevel = 0x2\nadded = 0x10\n\n[[t.servers]]\nhost = \"a\"\nport = 0x50\n\n[[t.servers]]\n"
        "host = \"b\"\nport = 0x50\n"s, document.ToText(), "the document's options write the values in hexadecimal");
    CheckEquals(std::size_t(4), document.Messages.size(), "a value and a part replaced, a field and a key added: " +
        MessagesText(document.Messages));
    if (document.Messages.size() == 4)
    {
        CheckEquals("The value 0x9 is out of the setting's range: it must be from 0x1 to 0x4. The default value 0x2 is "
            "used."s, document.Messages[0].Text, "and so do the messages");
        CheckEquals("The value \"x\" is a string, not an integer. The default value 0x50 is used."s,
            document.Messages[1].Text, "of a part too");
        CheckEquals("The key t.servers[1].port is missing: it is added with its default value 0x50."s,
            document.Messages[2].Text, "and of a field added");
        CheckEquals("The key t.added is missing: it is added with its default value 0x10."s, document.Messages[3].Text,
            "and of a key added");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, Load_MissingFileGetsEverySetting)
{
    // Arrange
    TTempDir const folder;
    std::string const file = ASWToml::PathToUtf8(folder.Path() / "app.toml");
    TLoadSettings settings;
    TLoadSettings again;

    // Act
    TASWTomlResult const loaded = settings.Load(file);
    TASWTomlResult const saved = settings.Save();
    std::string written;
    bool const read = ReadTestFile(folder.Path() / "app.toml", written);
    TASWTomlResult const reloaded = again.Load(file);

    // Assert
    CheckEquals(ETomlCode::FileNotFound, loaded.Code, "a first run has no file");
    CheckEquals(Escaped(FirstRunText), Escaped(settings.ToText()), "the document gets every setting");
    CheckTrue(saved.IsOk() && read, "Save() creates the file");
    CheckEquals(Escaped(FirstRunText), Escaped(written), "with the document's text");
    CheckTrue(reloaded.IsOk() && !again.IsModified() && again.Messages.empty(),
        "a file that needs nothing loads without a change or a message");
    CheckEquals(std::size_t(5), settings.Messages.size(), "the missing file and each key added: " +
        MessagesText(settings.Messages));
    if (settings.Messages.size() == 5)
    {
        CheckEquals(ETomlCode::FileNotFound, settings.Messages[0].Code, "the missing file's Warning first");
        for (TASWTomlMessage const& message : settings.Messages)
            CheckEquals(file, message.FilePath, "each names the file: " + ASWToml::ToString(message));

        CheckEquals("general.name"s, settings.Messages[1].Path, "then the keys added, in declaration order");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, Load_OptionalKeyIsNotAdded)
{
    // Arrange
    TLoadSettings settings;
    TASWTomlTable hidden{ settings, "hidden", "Hidden." };
    TASWTomlSetting<bool> secret{ hidden, "secret", false, "A secret.", { .Optional = true } };
    TLoadSettings present;
    TASWTomlTable presentHidden{ present, "hidden", "Hidden." };
    TASWTomlSetting<bool> presentSecret{ presentHidden, "secret", false, "A secret.", { .Optional = true } };
    TLoadSettings value;
    TASWTomlTable valueHidden{ value, "hidden", "Hidden." };
    TASWTomlSetting<bool> valueSecret{ valueHidden, "secret", false, "A secret.", { .Optional = true } };
    std::string const firstRun = FirstRunText;
    std::string const fileComment = "# Settings of the test.\n\n";

    // Act
    TASWTomlResult const loaded = settings.LoadText("");
    TASWTomlResult const loadedPresent = present.LoadText(FirstRunText + "\n[hidden]\nsecret = true\n"s);
    TASWTomlResult const loadedValue = value.LoadText("hidden = 5\n");

    // Assert
    CheckTrue(loaded.IsOk() && loadedPresent.IsOk(), "the texts should load");
    CheckEquals(Escaped(FirstRunText), Escaped(settings.ToText()),
        "an optional key isn't added, nor a table whose settings are all optional");
    CheckEquals(Escaped(FirstRunText + "\n# Hidden.\n[hidden]\n# A secret.\nsecret = true\n"s),
        Escaped(present.ToText()), "one that is there gets its comment, and so does its table");
    CheckTrue(presentSecret.Get(), "and its value is read");
    CheckTrue(loadedValue.IsOk(), "a text where the table is another value should load");
    CheckEquals(Escaped(fileComment + "hidden = 5\n\n" + firstRun.substr(fileComment.size())), Escaped(value.ToText()),
        "a table that is another value, with no setting to add, stays as it is, without the table's comment");
    TMessageVector reported = settings.Messages;
    reported.insert(reported.end(), value.Messages.begin(), value.Messages.end());
    for (TASWTomlMessage const& message : reported)
    {
        CheckTrue(message.Code == ETomlCode::KeyAdded, "only the keys added are reported, not the comment of a "
            "table that is missing or another value: " + ASWToml::ToString(message));
    }

    CheckTrue(present.Messages.empty(), "nothing is reported for a file that has it all: " +
        MessagesText(present.Messages));
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, Load_PathsThatCantBeUsedAreErrors)
{
    // Arrange
    TLoadSettings settings;
    TASWTomlTable badTable{ settings, "a..b", "Bad." };
    TASWTomlSetting<int> inBadTable{ badTable, "x", 1 };
    TASWTomlTable element{ settings, "servers[0]", "Element." };
    TASWTomlSetting<int> inElement{ element, "x", 1 };
    std::string const text = FirstRunText + "\n[[servers]]\nx = 5\n"s;

    // Act
    TASWTomlResult const loaded = settings.LoadText(text);

    // Assert
    CheckTrue(loaded.IsOk(), "the text should load");
    CheckEquals(Escaped(text), Escaped(settings.ToText()), "nothing is written for them, nor a table's comment");
    CheckEquals(std::size_t(2), settings.Messages.size(), "each setting is reported: " +
        MessagesText(settings.Messages));
    for (TASWTomlMessage const& message : settings.Messages)
    {
        CheckTrue(message.Severity == ESeverity::Error && message.Code == ETomlCode::InvalidArgument,
            "as an Error (InvalidArgument): " + ASWToml::ToString(message));
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, Load_RenamedKeyIsAppliedAsAnyOther)
{
    // Arrange
    TASWTomlOptions fileWins = LfOptions();
    fileWins.Writing.CommentPolicy = ASWToml::ECommentPolicy::FileWins;
    TPreloaded toml("");
    CheckTrue(toml.SetOptions(fileWins).IsOk(), "the options should be taken");
    TASWTomlTable general{ toml, "general" };
    TASWTomlSetting<unsigned> level{ general, "level", 2, "From 0 to 4.",
        { .Min = 0, .Max = 4, .OldKeys = { "lvl" } } };
    TASWTomlSetting<bool> cheats{ general, "cheats", false, "", { .Optional = true, .OldKeys = { "cheats_on" } } };
    TASWTomlSetting<int> width{ general, "width", 640, "In pixels.", { .OldKeys = { "w" } } };

    // Act
    TASWTomlResult const loaded = toml.LoadText("[general]\nlvl = 9\ncheats_on = \"maybe\"\n# The user's note\n"
        "w = 800\n");

    // Assert
    CheckTrue(loaded.IsOk(), "the text should load");
    CheckEquals(Escaped("[general]\n# From 0 to 4.\nlevel = 2\ncheats = false\n# The user's note\nwidth = 800\n"),
        Escaped(toml.ToText()), "a renamed key's value is checked and its comment written as any key's, also an "
        "optional one's");
    CheckEquals(std::size_t(5), toml.Messages.size(), "each key renamed and each value replaced: " +
        MessagesText(toml.Messages));
    if (toml.Messages.size() == 5)
    {
        CheckEquals(ETomlCode::KeyRenamed, toml.Messages[0].Code, "the rename first");
        TASWTomlMessage const& replaced = toml.Messages[1];
        CheckTrue(replaced.Severity == ESeverity::Warning && replaced.Code == ETomlCode::ValueOutOfRange,
            "then the value replaced, a Warning");
        CheckEquals("general.level"s, replaced.Path, "at the key's new path");
        CheckEquals("general.cheats"s, toml.Messages[2].Path, "an optional key is renamed");
        CheckEquals(ETomlCode::TypeMismatch, toml.Messages[3].Code, "and its value checked");
        CheckEquals(ETomlCode::KeyRenamed, toml.Messages[4].Code, "then the last key renamed");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, Load_RenamesAnOldKey)
{
    // Arrange
    TPreloaded toml("");
    TASWTomlTable logging{ toml, "logging" };
    TASWTomlSetting<int> days{ logging, "days_kept", 7, "Days to keep the logs.",
        { .OldKeys = { "days", "log days" } } };
    TASWTomlSetting<int> width{ logging, "width", 640, "", { .OldKeys = { "w", "size_x" } } };
    TASWTomlSetting<int> height{ logging, "height", 480, "", { .OldKeys = { "h" } } };

    // Act
    // An old key is looked for in the setting's own table only
    TASWTomlResult const loaded = toml.LoadText("[logging]\n# The user's note\n  \"log days\"  = 0x1E  # Mine\n"
        "size_x = 800\nw = 900\n\n[other]\nh = 1\n");
    std::string const text = toml.ToText();
    bool const modified = toml.IsModified();
    int const daysValue = days.Get();
    int const widthValue = width.Get();
    TMessageVector const messages = toml.Messages;
    TASWTomlResult const reloaded = toml.LoadText(text);

    // Assert
    CheckTrue(loaded.IsOk(), "the text should load");
    CheckEquals(Escaped("[logging]\n  # Days to keep the logs.\n  days_kept  = 0x1E  # Mine\nsize_x = 800\n"
        "width = 900\nheight = 480\n\n[other]\nh = 1\n"), Escaped(text), "the first old key in the setting's order "
        "is renamed in place, keeping its spacing, value and end-of-line comment");
    CheckTrue(modified, "renaming a key modifies the document");
    CheckEquals(30, daysValue, "the setting reads the renamed key's value");
    CheckEquals(900, widthValue, "the setting reads the renamed key's value");
    CheckEquals(std::size_t(3), messages.size(), "each key renamed or added: " + MessagesText(messages));
    if (messages.size() == 3)
    {
        TASWTomlMessage const& renamed = messages[0];
        CheckTrue(renamed.Severity == ESeverity::Info && renamed.Code == ETomlCode::KeyRenamed, "an Info, KeyRenamed");
        CheckEquals("The key logging.days_kept is missing: its earlier name \"log days\" is renamed to it."s,
            renamed.Text, "its text names the key and its earlier name");
        CheckEquals("logging.days_kept"s, renamed.Path, "its path is the setting's");
        CheckEquals("logging.width"s, messages[1].Path, "in declaration order");
        CheckEquals(ETomlCode::KeyAdded, messages[2].Code, "a key whose old key is in another table is added");
    }

    CheckTrue(reloaded.IsOk(), "the text with the new keys should load");
    CheckEquals(Escaped(text), Escaped(toml.ToText()), "and stays as it is");
    CheckFalse(toml.IsModified(), "without a change");
    CheckEquals(messages.size(), toml.Messages.size(), "or a message: " + MessagesText(toml.Messages));
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, Load_RenamesOldTablesAndArraysOfTables)
{
    // Arrange
    TPreloaded toml("");
    TASWTomlTable net{ toml, "net" };
    TASWTomlSetting<TTestServer> server{ net, "server", TTestServer{ "localhost", 80, true, std::nullopt },
        "The server.", { .OldKeys = { "host" } } };
    TASWTomlSetting<TServerVector> servers{ net, "servers", TServerVector(), "The servers.",
        { .OldKeys = { "mirrors" } } };
    TASWTomlTable ui{ toml, "ui" };
    TASWTomlSetting<int> width{ ui, "width", 640, "In pixels.", { .OldKeys = { "w" } } };
    TASWTomlTable limits{ toml, "limits" };
    TASWTomlSetting<int> most{ limits, "most", 1, "The most.", { .OldKeys = { "max" } } };

    // Act
    // A table, an array of tables with a sub-table, a dotted key and an entry of an inline table
    TASWTomlResult const loaded = toml.LoadText("ui.w = 800\nlimits = { max = 5, min = 1 }\n\n# Mine\n[net.host]\n"
        "host = \"example.com\"\nport = 8080\n\n[[net.mirrors]]\nhost = \"a\"\nport = 1\n\n[net.mirrors.extra]\n"
        "x = 1\n\n[[net.mirrors]]\nhost = \"b\"\nport = 2\n");

    // Assert
    CheckTrue(loaded.IsOk(), "the text should load");
    CheckEquals(Escaped("# In pixels.\nui.width = 800\nlimits = { most = 5, min = 1 }\n\n# The server.\n[net.server]\n"
        "host = \"example.com\"\nport = 8080\n\n# The servers.\n[[net.servers]]\nhost = \"a\"\nport = 1\n\n"
        "[net.servers.extra]\nx = 1\n\n[[net.servers]]\nhost = \"b\"\nport = 2\n"), Escaped(toml.ToText()),
        "each is renamed in every header and key that names it, and gets its comment where it has a line");
    CheckEquals(std::size_t(4), toml.Messages.size(), "each rename: " + MessagesText(toml.Messages));
    for (TASWTomlMessage const& message : toml.Messages)
        CheckEquals(ETomlCode::KeyRenamed, message.Code, "is KeyRenamed: " + ASWToml::ToString(message));

    CheckEquals("example.com"s, server.Get().Host, "the struct is read from the renamed table");
    CheckEquals(std::size_t(2), servers.Get().size(), "the vector from the renamed array of tables");
    CheckEquals(800, width.Get(), "the dotted key's value");
    CheckEquals(5, most.Get(), "the inline table's");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, Load_ReplacesAnInvalidValueWithTheDefault)
{
    // Arrange
    TLoadSettings settings;

    // Act
    TASWTomlResult const loaded = settings.LoadText("[general]\n# The name.\nname = 5  # Mine\n# From 0 to 4.\n"
        "level = 9\n");

    // Assert
    CheckTrue(loaded.IsOk(), "the text should load");
    CheckEquals(Escaped("# Settings of the test.\n\n# What it looks like.\n[general]\n# The name.\n"
        "name = \"Guest\"  # Mine\n# From 0 to 4.\nlevel = 2\n\n[logging]\n# Days kept.\ndays = 30\nmask = 0xFF\n"),
        Escaped(settings.ToText()), "values that aren't valid are replaced in place");
    CheckEquals(std::size_t(4), settings.Messages.size(), "two Warnings, two keys added: " +
        MessagesText(settings.Messages));
    if (settings.Messages.size() == 4)
    {
        TASWTomlMessage const& name = settings.Messages[0];
        CheckTrue(name.Severity == ESeverity::Warning && name.Code == ETomlCode::TypeMismatch, "a Warning");
        CheckEquals("The value 5 is an integer, not a string. The default value \"Guest\" is used."s, name.Text,
            "that says what was read and that the default is used");
        CheckEquals("general.name"s, name.Path, "its path");
        CheckEquals("The value 9 is out of the setting's range: it must be from 0 to 4. The default value 2 is used."s,
            settings.Messages[1].Text, "a value out of range");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, Load_TablesWithoutSettingsGetTheirComments)
{
    // Arrange
    TPreloaded toml("");
    TASWTomlTable root{ toml, "", "The file." };
    TASWTomlTable table{ toml, "t", "The table." };

    // Act
    TASWTomlResult const loaded = toml.LoadText("x = 1\n[t]\ny = 2\n");

    // Assert
    CheckTrue(loaded.IsOk(), "the text should load");
    CheckEquals(Escaped("# The file.\n\nx = 1\n# The table.\n[t]\ny = 2\n"), Escaped(toml.ToText()),
        "an object with tables and no settings sets the tables' comments");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, Load_WriteFailuresAreErrors)
{
    // Arrange
    // A default the table can't hold: nested deeper than the reading options allow
    TASWTomlOptions shallow = LfOptions();
    shallow.Reading.MaxNestingDepth = 2;
    std::vector<int> const one{ 1 };
    TNestedVector const nested{ one };
    TLoadSettings settings;
    TASWTomlSetting<TNestedVector> deep{ settings.General, "deep", nested };
    CheckTrue(settings.SetOptions(shallow).IsOk(), "the options should be taken");

    // Act
    TASWTomlResult const loaded = settings.LoadText("");

    // Assert
    CheckTrue(loaded.IsOk(), "the text should load");
    CheckEquals(Escaped(FirstRunText), Escaped(settings.ToText()), "the other settings of the table are written");
    CheckEquals(std::size_t(5), settings.Messages.size(), "four keys added and one failure: " +
        MessagesText(settings.Messages));
    if (settings.Messages.size() == 5)
    {
        TASWTomlMessage const& failure = settings.Messages[4];
        CheckTrue(failure.Severity == ESeverity::Error && failure.Code == ETomlCode::NestingTooDeep,
            "an Error with the edit's code, in the setting's place: " + ASWToml::ToString(failure));
        CheckEquals("general.deep"s, failure.Path, "its path");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, LoadText_AppliesTheSettings)
{
    // Arrange
    TLoadSettings settings;

    // Act
    TASWTomlResult const loaded = settings.LoadText("");

    // Assert
    CheckTrue(loaded.IsOk(), "the empty text should load");
    CheckEquals(Escaped(FirstRunText), Escaped(settings.ToText()), "the document gets every setting");
    CheckTrue(settings.IsModified(), "and needs saving");
    CheckEquals(std::size_t(4), settings.Messages.size(), "one note per key: " + MessagesText(settings.Messages));
    if (!settings.Messages.empty())
    {
        CheckEquals("The key general.name is missing: it is added with its default value \"Guest\"."s,
            settings.Messages[0].Text, "the first one");
        CheckEmpty(settings.Messages[0].FilePath, "about no file");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, OutOfMemory_LoadAppliesAllOrNothing)
{
    // Arrange
    if (!CanFailAllocations())
        Skip("allocations can't be made to fail in this build (see CanFailAllocations())");

    // An old key renamed, an array of tables brought up to date by parts (a field that isn't valid, a missing field),
    // a key added, and the comments of the keys, the table and the file
    std::string const text = "[general]\nold = 3\n\n[[general.servers]]\nhost = \"a\"\nport = \"x\"\n\n"
        "[[general.servers]]\nhost = \"b\"\n";
    std::string const loadedText = "# The file.\n\n# General's comment.\n[general]\n# A's comment.\na = 3\n"
        "# C's comment.\nc = 5\n\n# The servers.\n[[general.servers]]\nhost = \"a\"\nport = 80\n\n"
        "[[general.servers]]\nhost = \"b\"\nport = 80\n";
    TCodeVector const loadedCodes{ ETomlCode::KeyRenamed, ETomlCode::TypeMismatch, ETomlCode::KeyAdded,
        ETomlCode::KeyAdded };
    std::size_t failures = 0;

    // Act & Assert
    // Each allocation of the load fails in turn, on a new object, until the load makes no more. A failed allocation
    // fails the load, which then changes nothing and reports only its failure (not what applying the settings did
    // before it failed), or, when the standard library copes with it, doesn't.
    for (std::size_t number = 1; number < 100000; ++number)
    {
        TASWBaseToml toml;
        static_cast<void>(toml.SetOptions(LfOptions()));
        static_cast<void>(toml.SetValue("before", "value"));
        std::string const before = toml.ToText();
        TASWTomlTable const root{ toml, "", "The file." };
        TASWTomlTable const general{ toml, "general", "General's comment." };
        TASWTomlSetting<int> const first{ general, "a", 1, "A's comment.", { .OldKeys = { "old" } } };
        TASWTomlSetting<TServerVector> const servers{ general, "servers", TServerVector(), "The servers." };
        TASWTomlSetting<int> const third{ general, "c", 5, "C's comment." };
        TCodeVector codes;
        codes.reserve(10);
        toml.SetLogCallback(TCodeRecorder(codes));
        std::string const which = " (allocation " + std::to_string(number) + ")";
        TASWTomlResult result;
        bool failed = false;
        {
            TFailingAlloc failingAlloc(number);
            result = toml.LoadText(text);
            failed = failingAlloc.HasFailed();
        }

        if (result.IsOk())
        {
            CheckEquals(Escaped(loadedText), Escaped(toml.ToText()),
                "a load that succeeded should have applied every setting" + which);
            CheckTrue(codes == loadedCodes, "a load that succeeded should report each change" + which);
        }
        else
        {
            ++failures;
            CheckTrue(failed, "the load should only fail when an allocation fails" + which);
            CheckEquals(ETomlCode::UnexpectedException, result.Code, "a failed allocation is UnexpectedException" + which);
            CheckEquals(Escaped(before), Escaped(toml.ToText()), "a failed load should change nothing" + which);
            CheckTrue(toml.IsModified(), "a failed load should keep the modified flag" + which);
            CheckTrue(codes.size() == 1 && codes[0] == ETomlCode::UnexpectedException,
                "a failed load should report only its failure" + which);
        }

        if (!failed)
            break;
    }

    CheckGreaterThan(failures, std::size_t(0), "at least one allocation should have failed");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsLoad, SetOptions_DoesntApplyTheSettings)
{
    // Arrange
    TASWTomlOptions fileWins = LfOptions();
    fileWins.Writing.CommentPolicy = ASWToml::ECommentPolicy::FileWins;
    TLoadSettings settings("x = 1\n");

    // Act
    TASWTomlResult const set = settings.SetOptions(fileWins);

    // Assert
    CheckTrue(set.IsOk(), "the options should be taken");
    CheckEquals("x = 1\n"s, settings.ToText(), "the settings aren't applied");
    CheckFalse(settings.IsModified(), "nothing changes");
    CheckTrue(settings.Messages.empty(), "nothing is reported");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_SettingsStructs
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_SettingsStructs::TTest_ASWToml_SettingsStructs()
    : inherited("ASWToml_SettingsStructs_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_SettingsStructs::~TTest_ASWToml_SettingsStructs()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SettingsStructs::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SettingsStructs::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SettingsStructs::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SettingsStructs::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsStructs, Get_RepairsWithoutChangingTheDocument)
{
    // Arrange
    // The text is loaded before the setting is bound, so it isn't applied to it
    std::string const text = "[[servers]]\nhost = \"a\"\n\n[[servers]]\nhost = \"b\"\nport = 99999\n";
    TPreloaded toml(text);
    TASWTomlTable root{ toml, "" };
    TASWTomlSetting<TServerVector> servers{ root, "servers", TServerVector() };

    // Act
    TServerVector const read = servers.Get();

    // Assert
    CheckEquals(std::size_t(2), read.size(), "both elements are read");
    if (read.size() == 2)
    {
        CheckTrue(read[0].Host == "a" && read[0].Port == 80, "a missing field gets the element's default");
        CheckTrue(read[1].Host == "b" && read[1].Port == 80, "and so does one that isn't valid");
    }

    CheckEquals(Escaped(text), Escaped(toml.ToText()), "the document doesn't change");
    CheckEquals(std::size_t(1), toml.Messages.size(), "only the part that isn't valid is reported: " +
        MessagesText(toml.Messages));
    if (toml.Messages.size() == 1)
    {
        TASWTomlMessage const& replaced = toml.Messages[0];
        CheckTrue(replaced.Severity == ESeverity::Warning && replaced.Code == ETomlCode::ValueOutOfRange,
            "as a Warning with the converter's code");
        CheckEquals("servers[1].port"s, replaced.Path, "the part's path");
        CheckEndsWith(replaced.Text, " The default value 80 is used.", "the part's default value");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsStructs, Load_AddsMissingFields)
{
    // Arrange
    TPreloaded toml("");
    TASWTomlTable root{ toml, "" };
    TASWTomlSetting<TTestCluster> cluster{ root, "cluster", DefaultCluster() };
    std::string const text = "# The cluster.\n[cluster]\nname = \"web\"  # Mine\nextra = true\n\n[cluster.screen]\n"
        "height = 700\n\n[[cluster.servers]]\nhost = \"a\"\nport = 1\nnote = \"first\"\n\n[[cluster.servers]]\n"
        "# The second.\nhost = \"b\"\n";

    // Act
    TASWTomlResult const loaded = toml.LoadText(text);
    std::string const applied = toml.ToText();
    TTestCluster const read = cluster.Get();
    TMessageVector const messages = toml.Messages;
    TASWTomlResult const reloaded = toml.LoadText(applied);

    // Assert
    CheckTrue(loaded.IsOk(), "the text should load");
    CheckEquals(Escaped("# The cluster.\n[cluster]\nname = \"web\"  # Mine\nextra = true\n\n[cluster.screen]\n"
        "height = 700\nwidth = 800\n\n[[cluster.servers]]\nhost = \"a\"\nport = 1\nnote = \"first\"\n\n"
        "[[cluster.servers]]\n# The second.\nhost = \"b\"\nport = 80\n"), Escaped(applied), "only the missing fields "
        "that aren't optional are added; the other keys, comments and layout stay");
    CheckEquals(std::size_t(2), messages.size(), "each key added: " + MessagesText(messages));
    if (messages.size() == 2)
    {
        TASWTomlMessage const& added = messages[0];
        CheckTrue(added.Severity == ESeverity::Info && added.Code == ETomlCode::KeyAdded, "an Info, KeyAdded");
        CheckEquals("The key cluster.screen.width is missing: it is added with its default value 800."s, added.Text,
            "the default of the setting's own struct is the setting's default's member");
        CheckEquals("cluster.screen.width"s, added.Path, "its path");
        CheckEquals("The key cluster.servers[1].port is missing: it is added with its default value 80."s,
            messages[1].Text, "an element's is a new element's");
    }

    CheckTrue(read.Name == "web" && read.Screen == TTestScreen(800, 700) && !read.Spare.has_value(),
        "the struct reads the file's values");
    CheckTrue(read.Servers.size() == 2 && read.Servers[1].Port == 80, "and the elements'");
    CheckTrue(reloaded.IsOk() && !toml.IsModified() && toml.Messages.size() == messages.size(),
        "the text applied needs nothing more");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsStructs, Load_DefaultThatCantBeWrittenUsesTheSettings)
{
    // Arrange
    // A new paint's color has no name, so a paint without one can't get it
    TPreloaded toml("");
    TASWTomlTable root{ toml, "" };
    TASWTomlSetting<TPaintVector> paints{ root, "paints", TPaintVector{ TTestPaint{ "wall", EColor::Red } } };
    TASWTomlSetting<TPaletteVector> palettes{ root, "palettes", TPaletteVector() };

    // Act
    TASWTomlResult const loaded = toml.LoadText("[[paints]]\nname = \"door\"\n\n[[palettes]]\n\n"
        "[[palettes.paints]]\nname = \"door\"\n");

    // Assert
    CheckTrue(loaded.IsOk(), "the text should load");
    CheckEquals(Escaped("palettes = []\n\n[[paints]]\nname = \"wall\"\ncolor = \"Red\"\n"), Escaped(toml.ToText()),
        "the setting's default value replaces the whole value (an empty array is a key of the root)");
    CheckEquals(std::size_t(2), toml.Messages.size(), "each is reported once: " + MessagesText(toml.Messages));
    if (toml.Messages.size() == 2)
    {
        TASWTomlMessage const& replaced = toml.Messages[0];
        CheckTrue(replaced.Severity == ESeverity::Warning && replaced.Code == ETomlCode::NotFound,
            "a Warning about the part that couldn't be repaired");
        CheckEquals("paints[0].color"s, replaced.Path, "its path");
        CheckStartsWith(replaced.Text, "The table has no key color. The default value ", "the problem and the default");
        CheckEquals("palettes[0].paints[0].color"s, toml.Messages[1].Path, "through a field too");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsStructs, Load_ReplacesOnlyThePartsThatArentValid)
{
    // Arrange
    TPreloaded toml("");
    TASWTomlTable root{ toml, "" };
    TASWTomlSetting<TLimitMap> limits{ root, "limits", TLimitMap{ { "a", 1 } } };
    TASWTomlSetting<TNameMap> names{ root, "names", TNameMap{ { 1, "one" } } };
    TASWTomlSetting<TColorMap> colors{ root, "colors", TColorMap{ { EColor::Blue, 3 } } };
    TASWTomlSetting<TCountMap> counts{ root, "counts", TCountMap() };
    TASWTomlSetting<TServerVector> backups{ root, "backups", TServerVector() };
    TASWTomlSetting<std::vector<int> > ports{ root, "ports", std::vector<int>{ 1 } };
    TASWTomlSetting<TServerVector> servers{ root, "servers", TServerVector() };

    // Act
    // A map's value; a map's key that doesn't read, after a value that doesn't; a key that reads as one before it; an
    // unordered map's value; an inline element that isn't a table; an element of numbers; a field of an element
    TASWTomlResult const loaded = toml.LoadText("limits = { a = 5, b = \"x\", c = 7 }\n"
        "names = { 1 = \"one\", 2 = 5, x = \"two\" }\ncolors = { Red = 1, red = 2 }\ncounts = { a = \"x\" }\n"
        "backups = [{ host = \"c\", port = 3 }, 5]\nports = [80, \"x\"]\n\n"
        "[[servers]]\nhost = \"a\"\nport = 99999  # Too big\n\n[[servers]]\nhost = \"b\"\nport = 2\n");

    // Assert
    CheckTrue(loaded.IsOk(), "the text should load");
    CheckEquals(Escaped("limits = { a = 5, b = 0, c = 7 }\nnames = { 1 = \"one\" }\ncolors = { Blue = 3 }\n"
        "counts = { a = 0 }\nbackups = [{ host = \"c\", port = 3 }, { host = \"\", port = 80, enabled = true }]\n"
        "ports = [80, 0]\n\n[[servers]]\nhost = \"a\"\nport = 80  # Too big\n\n[[servers]]\nhost = \"b\"\nport = 2\n"),
        Escaped(toml.ToText()), "each part that isn't valid is replaced by its default, the rest stays");
    CheckEquals(std::size_t(7), toml.Messages.size(), "each part is reported: " + MessagesText(toml.Messages));
    if (toml.Messages.size() == 7)
    {
        CheckEquals("limits.b"s, toml.Messages[0].Path, "a map's value, with the map's default for a value");
        CheckEndsWith(toml.Messages[0].Text, " The default value 0 is used.", "a new value's default");
        CheckEquals("names.x"s, toml.Messages[1].Path, "a key that doesn't read, without the value before it");
        CheckEndsWith(toml.Messages[1].Text, " The default value { 1 = \"one\" } is used.",
            "makes the whole map one part");
        CheckEquals("colors.red"s, toml.Messages[2].Path, "a key that reads as one before it");
        CheckEquals("counts.a"s, toml.Messages[3].Path, "an unordered map's value");
        CheckEquals("backups[1]"s, toml.Messages[4].Path, "an element that isn't a table");
        CheckEquals(ETomlCode::TypeMismatch, toml.Messages[4].Code, "is the wrong type");
        CheckEquals("ports[1]"s, toml.Messages[5].Path, "an element of numbers, with a new element's default");
        CheckEquals("servers[0].port"s, toml.Messages[6].Path, "a field of an element");
    }

    TLimitMap const repaired{ { "a", 5 }, { "b", 0 }, { "c", 7 } };
    CheckTrue(limits.Get() == repaired, "the map reads as repaired");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsStructs, Load_ValueOfAnotherTypeIsReplacedWhole)
{
    // Arrange
    TPreloaded toml("");
    TASWTomlTable root{ toml, "" };
    TASWTomlSetting<std::vector<int> > ports{ root, "ports", std::vector<int>{ 1 } };
    TASWTomlSetting<TLimitMap> limits{ root, "limits", TLimitMap{ { "a", 1 } } };

    // Act
    TASWTomlResult const loaded = toml.LoadText("ports = 5\nlimits = 6\n");

    // Assert
    CheckTrue(loaded.IsOk(), "the text should load");
    CheckEquals(Escaped("ports = [1]\nlimits = { a = 1 }\n"), Escaped(toml.ToText()),
        "a vector or a map of another type is replaced by its default");
    CheckEquals(std::size_t(2), toml.Messages.size(), "each is reported: " + MessagesText(toml.Messages));
    for (TASWTomlMessage const& message : toml.Messages)
        CheckEquals(ETomlCode::TypeMismatch, message.Code, "as the wrong type: " + ASWToml::ToString(message));
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsStructs, Load_WholeValueRefusedUsesTheDefault)
{
    // Arrange
    // At most one server, after the repairs
    TPreloaded toml("");
    TASWTomlTable root{ toml, "" };
    TASWTomlSetting<TServerVector> servers{ root, "servers", TServerVector{ TTestServer{ "x", 9, true, std::nullopt } },
        "", { .Validator = OneServer } };

    // Act
    TASWTomlResult const loaded = toml.LoadText("[[servers]]\nhost = \"a\"\nport = \"x\"\n\n[[servers]]\n"
        "host = \"b\"\n");

    // Assert
    CheckTrue(loaded.IsOk(), "the text should load");
    CheckEquals(Escaped("[[servers]]\nhost = \"x\"\nport = 9\nenabled = true\n"), Escaped(toml.ToText()),
        "the setting's default replaces the whole value");
    CheckEquals(std::size_t(1), toml.Messages.size(), "only the refusal is reported, not the parts: " +
        MessagesText(toml.Messages));
    if (toml.Messages.size() == 1)
    {
        CheckEquals(ETomlCode::InvalidValue, toml.Messages[0].Code, "the validator's refusal");
        CheckEquals("servers"s, toml.Messages[0].Path, "the setting's path");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsStructs, OutOfMemory_SetOverlayChangesNothing)
{
    // Arrange
    if (!CanFailAllocations())
        Skip("allocations can't be made to fail in this build (see CanFailAllocations())");

    // Servers with a key their struct doesn't know: Set() removes the first, changes the port of the last and adds
    // one, so the overlay, under the lock, pairs the elements and keeps the notes of the others
    std::string const text = "[[servers]]\nhost = \"a\"\nport = 1\nenabled = true\nnote = \"A\"\n\n[[servers]]\n"
        "host = \"b\"\nport = 2\nenabled = true\nnote = \"B\"\n\n[[servers]]\nhost = \"c\"\nport = 3\nenabled = true\n"
        "note = \"C\"\n";
    std::string const setText = "[[servers]]\nhost = \"b\"\nport = 2\nenabled = true\nnote = \"B\"\n\n[[servers]]\n"
        "host = \"c\"\nport = 30\nenabled = true\nnote = \"C\"\n\n[[servers]]\nhost = \"d\"\nport = 4\n"
        "enabled = true\n";
    TServerVector const value{ TTestServer{ "b", 2, true, std::nullopt }, TTestServer{ "c", 30, true, std::nullopt },
        TTestServer{ "d", 4, true, std::nullopt } };
    std::size_t failures = 0;

    // Act & Assert
    // Each allocation of the Set() fails in turn, on a new object, until it makes no more
    for (std::size_t number = 1; number < 100000; ++number)
    {
        TPreloaded toml(text);
        TASWTomlTable const root{ toml, "" };
        TASWTomlSetting<TServerVector> servers{ root, "servers", TServerVector() };
        std::string const which = " (allocation " + std::to_string(number) + ")";
        TASWTomlResult result;
        bool failed = false;
        {
            TFailingAlloc failingAlloc(number);
            result = servers.Set(value);
            failed = failingAlloc.HasFailed();
        }

        if (result.IsOk())
        {
            CheckEquals(Escaped(setText), Escaped(toml.ToText()), "a Set() that succeeded should be complete" + which);
        }
        else
        {
            ++failures;
            CheckTrue(failed, "Set() should only fail when an allocation fails" + which);
            CheckEquals(ETomlCode::UnexpectedException, result.Code, "a failed allocation is UnexpectedException" + which);
            CheckEquals(Escaped(text), Escaped(toml.ToText()), "a failed Set() should change nothing" + which);
            CheckFalse(toml.IsModified(), "a failed Set() shouldn't set the modified flag" + which);
        }

        if (!failed)
            break;
    }

    CheckGreaterThan(failures, std::size_t(0), "at least one allocation should have failed");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsStructs, Set_KeepsTheKeysTheStructDoesntKnow)
{
    // Arrange
    // The texts are loaded before the setting is bound, so it isn't applied to them
    std::string const text = "[cluster]\nname = \"web\"\nextra = true  # Mine\n\n[cluster.screen]\nwidth = 800\n"
        "depth = 24\n\n[cluster.spare]\nwidth = 1\nheight = 2\n";
    TPreloaded toml(text);
    TASWTomlTable root{ toml, "" };
    TASWTomlSetting<TTestCluster> cluster{ root, "cluster", TTestCluster() };
    TPreloaded raw(text);
    TTestCluster value = cluster.Get();
    value.Name = "api";
    value.Screen = TTestScreen(1024, 768);
    value.Spare = std::nullopt;

    // Act
    TASWTomlResult const set = cluster.Set(value);
    TASWTomlResult const rawSet = raw.SetValue("cluster", value);

    // Assert
    CheckTrue(set.IsOk() && rawSet.IsOk(), "both should set the value");
    // The optional fields that the file lacked are written (the empty map as a table of its own)
    CheckEquals(Escaped("[cluster]\nname = \"api\"\nextra = true  # Mine\nservers = []\n\n[cluster.screen]\n"
        "width = 1024\ndepth = 24\nheight = 768\n\n[cluster.limits]\n"), Escaped(toml.ToText()),
        "the keys the struct and its nested struct don't know stay, and a std::nullopt member's key goes");
    CheckTrue(cluster.Get() == value, "the value reads back");
    CheckFalse(raw.ToText().find("extra") != std::string::npos || raw.ToText().find("depth") != std::string::npos,
        "IASWToml::SetValue() sets the value as it is");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsStructs, Set_MapOverlaysByKey)
{
    // Arrange
    std::string const text = "[hosts.a]\nhost = \"a\"\nport = 1\nnote = \"A\"\n\n[hosts.b]\nhost = \"b\"\nport = 2\n";
    TPreloaded toml(text);
    TASWTomlTable root{ toml, "" };
    TASWTomlSetting<THostMap> hosts{ root, "hosts", THostMap() };
    THostMap value = hosts.Get();
    value.erase("b");
    value["a"].Port = 10;
    value["c"] = TTestServer{ "c", 3, true, std::nullopt };

    // Act
    TASWTomlResult const set = hosts.Set(value);

    // Assert
    CheckTrue(set.IsOk(), "the value should be set");
    CheckEquals(Escaped("[hosts.a]\nhost = \"a\"\nport = 10\nnote = \"A\"\nenabled = true\n\n[hosts.c]\nhost = \"c\"\n"
        "port = 3\nenabled = true\n"), Escaped(toml.ToText()),
        "a key that stays keeps what its value's type doesn't know, and a key removed goes");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsStructs, Set_PairsElementsByWhatTheStructKnows)
{
    // Arrange
    std::string const text = "[[servers]]\nhost = \"a\"\nport = 1\nnote = \"A\"\n\n[[servers]]\n# B's comment.\n"
        "host = \"b\"\nport = 2\nnote = \"B\"\n\n[[servers]]\nhost = \"c\"\nport = 3\nnote = \"C\"\n";
    TPreloaded removed(text);
    TASWTomlTable removedRoot{ removed, "" };
    TASWTomlSetting<TServerVector> removedServers{ removedRoot, "servers", TServerVector() };
    TPreloaded changed(text);
    TASWTomlTable changedRoot{ changed, "" };
    TASWTomlSetting<TServerVector> changedServers{ changedRoot, "servers", TServerVector() };
    TPreloaded nested("[cluster]\nname = \"web\"\n\n[cluster.screen]\nwidth = 1\nheight = 2\n\n[cluster.limits]\n\n"
        "[[cluster.servers]]\nhost = \"a\"\nport = 1\nnote = \"A\"\n\n[[cluster.servers]]\nhost = \"b\"\nport = 2\n"
        "note = \"B\"\n");
    TASWTomlTable nestedRoot{ nested, "" };
    TASWTomlSetting<TTestCluster> cluster{ nestedRoot, "cluster", TTestCluster() };
    TServerVector withoutFirst = removedServers.Get();
    withoutFirst.erase(withoutFirst.begin());
    TServerVector other = changedServers.Get();
    other[1].Port = 20;
    other.push_back(TTestServer{ "d", 4, true, std::nullopt });
    TTestCluster clusterValue = cluster.Get();
    clusterValue.Servers.erase(clusterValue.Servers.begin());

    // Act
    TASWTomlResult const setRemoved = removedServers.Set(withoutFirst);
    TASWTomlResult const setChanged = changedServers.Set(other);
    TASWTomlResult const setNested = cluster.Set(clusterValue);

    // Assert
    CheckTrue(setRemoved.IsOk() && setChanged.IsOk() && setNested.IsOk(), "each should set the value");
    // The struct writes its optional field enabled, which the elements lacked; that doesn't change which element is
    // which
    CheckEquals(Escaped("[[servers]]\n# B's comment.\nhost = \"b\"\nport = 2\nnote = \"B\"\nenabled = true\n\n"
        "[[servers]]\nhost = \"c\"\nport = 3\nnote = \"C\"\nenabled = true\n"), Escaped(removed.ToText()),
        "removing the first element removes its block, and the others keep theirs");
    CheckEquals(Escaped("[[servers]]\nhost = \"a\"\nport = 1\nnote = \"A\"\nenabled = true\n\n[[servers]]\n"
        "# B's comment.\nhost = \"b\"\nport = 20\nnote = \"B\"\nenabled = true\n\n[[servers]]\nhost = \"c\"\nport = 3\n"
        "note = \"C\"\nenabled = true\n\n[[servers]]\nhost = \"d\"\nport = 4\nenabled = true\n"),
        Escaped(changed.ToText()), "a changed element keeps what the struct doesn't know, and a new one is added");
    CheckEquals(Escaped("[cluster]\nname = \"web\"\n\n[cluster.screen]\nwidth = 1\nheight = 2\n\n[cluster.limits]\n\n"
        "[[cluster.servers]]\nhost = \"b\"\nport = 2\nnote = \"B\"\nenabled = true\n"), Escaped(nested.ToText()),
        "and so are the elements of a vector in a struct");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsStructs, Set_PairsNestedElementsByWhatTheyKnow)
{
    // Arrange
    // Two clusters that differ in a vector, a map or an optional member: with the first removed, the second keeps its
    // block (its note), so it is told apart from the first by that alone
    // Each has the optional fields limits and servers, which the struct writes
    std::string const noLimits = "limits = {}\n";
    std::string const noServers = "servers = []\n";
    std::string const servers = noLimits + "servers = [{ host = \"a\", port = 1, enabled = true }]\n";
    std::string const twoServers = noLimits + "servers = [{ host = \"a\", port = 1, enabled = true }, "
        "{ host = \"b\", port = 2, enabled = true }]\n";
    std::string const otherServer = noLimits + "servers = [{ host = \"b\", port = 2, enabled = true }]\n";
    std::string const limits = noServers + "limits = { a = 1 }\n";
    std::string const twoLimits = noServers + "limits = { a = 1, b = 2 }\n";
    std::string const otherValue = noServers + "limits = { a = 2 }\n";
    std::string const otherKey = noServers + "limits = { b = 1 }\n";
    std::string const spare = noLimits + noServers + "spare = { width = 2, height = 2 }\n";
    std::string const second = "[[clusters]]\nname = \"c\"\nscreen = { width = 1, height = 1 }\nnote = \"second\"\n";

    // Act & Assert
    CheckEquals(Escaped(second + servers), Escaped(RemovedFirstCluster(twoServers, servers)),
        "a vector of another size");
    CheckEquals(Escaped(second + otherServer), Escaped(RemovedFirstCluster(servers, otherServer)),
        "a vector with another element");
    CheckEquals(Escaped(second + limits), Escaped(RemovedFirstCluster(twoLimits, limits)), "a map of another size");
    CheckEquals(Escaped(second + otherValue), Escaped(RemovedFirstCluster(limits, otherValue)),
        "a map with another value");
    CheckEquals(Escaped(second + otherKey), Escaped(RemovedFirstCluster(limits, otherKey)), "a map with another key");
    CheckEquals(Escaped(second + noLimits + noServers), Escaped(RemovedFirstCluster(spare, noLimits + noServers)),
        "an optional member that only one has");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_SettingsThreads
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_SettingsThreads::TTest_ASWToml_SettingsThreads()
    : inherited("ASWToml_SettingsThreads_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_SettingsThreads::~TTest_ASWToml_SettingsThreads()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SettingsThreads::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SettingsThreads::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SettingsThreads::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SettingsThreads::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsThreads, Bindings_ComeAndGoWhileLoading)
{
    // Arrange
    // Binder threads bind a table and settings of their own (an array of tables among them) and destroy them again,
    // over and over, while a loader loads a text that has none of the keys and checks each document it gives (see
    // BindingsProblem()). Each load applies the settings that stay, and a binder's table and settings whole or not at
    // all: when its table is in the document, so are its keys, each with its comment. Many settings that stay make
    // applying take long enough for bindings to come and go while it runs. The binders go on until the loader is done.
    TBindingsTest test;
    test.Binders = 2;
    test.Iterations = 300;
    test.Largest = 1000000;
    test.Loads = 200;
    static_cast<void>(test.Toml.SetOptions(LfOptions()));
    std::string expected = "# Settings that stay.\n[stays]\n";
    for (std::size_t i = 0; i < 100; ++i)
    {
        std::string const key = "key" + std::to_string(i);
        test.Stayers.push_back(std::make_unique<TASWTomlSetting<int> >(test.Stays, key, static_cast<int>(i),
            "A comment."));
        expected += "# A comment.\n" + key + " = " + std::to_string(i) + "\n";
    }

    TWorkVector work;
    for (std::size_t i = 0; i < test.Binders; ++i)
        work.push_back(std::bind(BindAndUnbind, std::ref(test), i));

    work.push_back(std::bind(LoadAndCheckBindings, std::ref(test), test.Binders));

    // Act
    // Then, with the binders' bindings gone, the object is loaded again
    RunTogether(work, test.Failures);
    TASWTomlResult const result = test.Toml.LoadText("");

    // Assert
    CheckEquals(std::string(), test.Failures.Summary(), "no thread should find a problem");
    CheckTrue(result.IsOk(), "the last load should succeed");
    CheckEquals(Escaped(expected), Escaped(test.Toml.ToText()),
        "the last load should apply only the settings that stay");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsThreads, GetAndSet_SeeWholeSettingsWhileLoading)
{
    // Arrange
    // Writers count their own settings up with Set(), a scratch thread removes the key of an optional setting and sets
    // it again, and the servers' writer sets the ports of the servers (an array of tables whose elements have a key
    // their struct doesn't know) with Set(), while a loader loads the saved file, or a text or a file whose values
    // aren't valid and that lacks most keys, a saver saves the file, and readers check what they see: each count's key
    // is there and Get() gives a number in range, Get() gives two servers, and a copy of the document holds whole
    // settings (see WholeSettingsProblem()), since a load applies the settings before anyone sees the document, Set()
    // adds a key with its comment in one edit, and the overlay of the servers is made under the same lock as their
    // edit. Many other settings make applying take long enough for a reader to see a document that isn't whole. The
    // writers, the scratch thread, the servers' writer and the readers go on until the file threads are done.
    TWholeSettingsTest test;
    std::size_t const writers = 3;
    std::size_t const readers = 2;
    test.Iterations = 300;
    test.Largest = 1000000;
    test.FileOperations = 9;
    test.Left = 2;
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    test.FilePath = ASWToml::PathToUtf8(folder.Path() / "app.toml");
    test.InvalidPath = ASWToml::PathToUtf8(folder.Path() / "invalid.toml");
    static_cast<void>(test.Toml.SetOptions(LfOptions()));
    test.InvalidText = "[values]\nscratch = -1\n";
    for (std::size_t i = 0; i < writers; ++i)
    {
        std::string const key = "count" + std::to_string(i);
        test.Counts.push_back(std::make_unique<TASWTomlSetting<int> >(test.Values, key, 0, "A count.",
            ASWToml::TASWTomlSettingOptions<int>{ .Min = 0, .Max = test.Largest }));
        test.InvalidText += key + " = \"many\"\n";
    }

    test.Scratch = std::make_unique<TASWTomlSetting<int> >(test.Values, "scratch", 0, "The scratch key.",
        ASWToml::TASWTomlSettingOptions<int>{ .Min = 0, .Max = test.Largest, .Optional = true });
    test.Servers = std::make_unique<TASWTomlSetting<TServerVector> >(test.Values, "servers", TServerVector(),
        "The servers.");
    for (std::size_t i = 0; i < 100; ++i)
    {
        test.Fillers.push_back(std::make_unique<TASWTomlSetting<int> >(test.Others, "key" + std::to_string(i),
            static_cast<int>(i), "A comment."));
    }

    // Two servers whose ports aren't valid, each with a key their struct doesn't know
    test.InvalidText += "\n[[values.servers]]\nhost = \"h0\"\nport = \"x\"\nnote = \"kept\"\n\n[[values.servers]]\n"
        "host = \"h1\"\nport = 99999\nnote = \"kept\"\n";

    // Each load of the invalid text (two loads in three) reports a Warning for each count, the scratch key and each
    // server's port, and an Info for each other key
    int const reportsPerInvalidLoad = static_cast<int>(writers + 1 + 2 + test.Fillers.size());
    int const invalidLoads = test.FileOperations / 3 * 2;
    TASWTomlResult const loaded = test.Toml.LoadText(test.InvalidText);
    TASWTomlResult const saved = test.Toml.SaveAs(test.FilePath);
    AssertTrue(loaded.IsOk() && saved.IsOk(), "the settings should be saved");
    std::atomic<int> reports{ 0 };
    test.Toml.SetLogCallback(TReportCounter(reports));
    TWorkVector work;
    for (std::size_t i = 0; i < writers; ++i)
        work.push_back(std::bind(CountSettingUp, std::ref(test), i));

    work.push_back(std::bind(UseScratchSetting, std::ref(test), writers));
    work.push_back(std::bind(SetServerPorts, std::ref(test), writers + 1));
    work.push_back(std::bind(LoadSettingsRepeatedly, std::ref(test), writers + 2));
    work.push_back(std::bind(SaveSettingsRepeatedly, std::ref(test), writers + 3));
    for (std::size_t i = 0; i < readers; ++i)
        work.push_back(std::bind(ReadWholeSettings, std::ref(test), writers + 4 + i));

    // Act
    // Then the object's text is loaded again, which needs nothing
    RunTogether(work, test.Failures);
    TASWTomlResult const reloaded = test.Toml.LoadText(test.Toml.ToText());

    // Assert
    CheckEquals(std::string(), test.Failures.Summary(), "no thread should find a problem");
    CheckEquals(invalidLoads * reportsPerInvalidLoad, reports.load(),
        "only the loads of the text and the file that aren't valid should report, each what it changed");
    CheckTrue(reloaded.IsOk(), "the last load should succeed");
    CheckFalse(test.Toml.IsModified(), "the object's text should hold every setting");
    CheckEquals(std::string(), WholeSettingsProblem(test.Toml.GetDocument(), writers, test.Fillers.size()),
        "the document should hold every setting with its comment");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_SettingsAccess, "Settings")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_SettingsBatch, "Settings")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_SettingsLoad, "Settings")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_SettingsStructs, "Settings")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_SettingsThreads, "Settings")
