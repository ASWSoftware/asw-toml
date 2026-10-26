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
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
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
using TColorMap = std::map<EColor, int>;
using TCountMap = std::unordered_map<std::string, int>;
using THostMap = std::map<std::string, TTestServer>;
using TLimitMap = std::map<std::string, int>;
using TNameMap = std::map<int, std::string>;
using TNestedVector = std::vector<std::vector<int> >;
using TServerVector = std::vector<TTestServer>;

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

//---------------------------------------------------------------------------

TTestCluster DefaultCluster();
TASWTomlOptions LfOptions();
std::string MessagesText(TMessageVector const& messages);
std::string OneServer(TServerVector const& value);
std::string RemovedFirstCluster(std::string const& first, std::string const& second);

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
    OneServer

    The validator of a list of at most one server
*/
std::string OneServer(TServerVector const& value)
{
    return value.size() > 1 ? "it can have at most one server" : "";
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

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_SettingsAccess, "Settings")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_SettingsLoad, "Settings")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_SettingsStructs, "Settings")
