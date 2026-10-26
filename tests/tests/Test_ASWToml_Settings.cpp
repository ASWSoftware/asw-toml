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
#include <type_traits>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWToml_Base.h"
#include "ASWToml_Converters.h"
#include "ASWToml_Options.h"
#include "ASWToml_Path.h"
#include "ASWToml_Settings.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------
#include "UT_Helper_Converters.h"
#include "UT_Helper_FailingAlloc.h"
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

using TLimitMap = std::map<std::string, int>;
using TServerVector = std::vector<TTestServer>;


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
// TTestSettings
//
// A settings class with a setting of each kind: every message it reports is in Messages, and new lines end with LF
/////////////////////////////////////////////////////////////////////////////
class TTestSettings : public TASWBaseToml
{
public:
    TMessageVector Messages;

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
    TTestSettings();

    // Value options that write an integer in hexadecimal
    static TASWTomlValueOptions Hexadecimal();

    // The validator of a text that can't be empty
    static std::string NotEmpty(std::string const& value);

    // The validator of a word of at most 8 letters
    static std::string NotLong(TTestWord const& value);
};


/////////////////////////////////////////////////////////////////////////////
// TSetDuringSave
//
// TTestSettings whose saves write no file, but set a setting while the file would be written: the first one Level to
// 3 (a key added with its comment), the others Zoom to 2.0 (a key that is there, without a comment)
/////////////////////////////////////////////////////////////////////////////
class TSetDuringSave : public TTestSettings
{
private:
    int m_Saves = 0;

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

std::string MessagesText(TMessageVector const& messages);

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
TTestSettings::TTestSettings()
{
    TASWTomlOptions options;
    options.Writing.LineEnding = ASWToml::ELineEnding::LF;
    static_cast<void>(SetOptions(options));
    SetLogCallback(TRecorder(Messages));
}
//---------------------------------------------------------------------------
TASWTomlValueOptions TTestSettings::Hexadecimal()
{
    TASWTomlValueOptions values;
    values.IntegerBase = EIntegerBase::Hexadecimal;

    return values;
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
// TSetDuringSave
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TSetDuringSave::WriteFile(std::string const& /*filePath*/, std::string_view /*contents*/,
    ASWToml::TASWTomlSavingOptions const& /*options*/, TMessageVector& /*warnings*/)
{
    ++m_Saves;

    return m_Saves == 1 ? Level.Set(3) : Zoom.Set(2.0);
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
    TTestSettings settings;
    CheckTrue(settings.LoadText("[general]\nlevel = \"x\"\nunnamed = \"Purple\"\n[[net.servers]]\nhost = \"a\"\n"
        "port = 1\n[[net.servers]]\nhost = \"b\"\nport = \"x\"\n").IsOk(), "the text should parse");

    // Act
    unsigned const level = settings.Level.Get();
    TServerVector const servers = settings.Servers.Get();
    EColor const unnamed = settings.Unnamed.Get();

    // Assert
    CheckEquals(2u, level, "a value that doesn't convert gives the default");
    CheckTrue(servers.empty(), "so does a part of one");
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
    CheckEquals("The value \"x\" is a string, not an integer. The default value [] is used."s,
        settings.Messages[1].Text, "a part of a value");
    CheckEquals("net.servers[1].port"s, settings.Messages[1].Path, "with the path of the part");
    CheckEquals(ETomlCode::InvalidValue, settings.Messages[2].Code, "a name the enum lacks");
    CheckContains(settings.Messages[2].Text, " The default value is used.", "a default that can't be written");
    CheckEquals("general.unnamed"s, settings.Messages[2].Path, "its path");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsAccess, Get_ReadsTheDocument)
{
    // Arrange
    TTestSettings settings;
    CheckTrue(settings.LoadText("[general]\nname = \"Ann\"\ncheats = true\nlevel = 3\nzoom = 1.5\ncolor = \"lime\"\n"
        "folder = 'C:\\data'\ncolumns = [\"a\", \"b\", \"c\"]\nmask = 0x10\nword = \"hello\"\n"
        "[net.server]\nhost = \"h\"\nport = 81\n[net.limits]\nx = 2\n[[net.servers]]\nhost = \"a\"\nport = 1\n"
        "[[net.servers]]\nhost = \"b\"\nport = 80\nenabled = false\n").IsOk(), "the text should parse");
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
        TTestSettings settings;
        static_cast<void>(settings.LoadText(text));
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
    TTestSettings settings;
    TASWTomlTable badTable{ settings, "a..b" };
    TASWTomlTable element{ settings, "servers[0]" };
    TASWTomlSetting<int> inBadTable{ badTable, "x", 1 };
    TASWTomlSetting<int> inElement{ element, "x", 1 };
    TASWTomlSetting<int> badKey{ settings.General, "\xFF", 1 };
    CheckTrue(settings.LoadText("[[servers]]\nx = 5\n").IsOk(), "the text should parse");

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
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsAccess, Range_ChecksGetAndSet)
{
    // Arrange
    TTestSettings settings;
    CheckTrue(settings.LoadText("[general]\nlevel = 9\nat_least = 0\nat_most = 10\n").IsOk(), "the text should parse");

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
    TTestSettings settings;
    CheckTrue(settings.LoadText("[general]\nlevel = 3  # Mine\n").IsOk(), "the text should parse");

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
    TTestSettings inTable;
    TTestSettings mine;
    TTestSettings newTable;
    TTestSettings inlineTable;
    TTestSettings structs;
    TTestSettings noComment;
    CheckTrue(inTable.LoadText("[general]\nother = 1\n").IsOk() && mine.LoadText("[general]\n# Mine\nlevel = 1\n")
        .IsOk() && newTable.LoadText("x = 1\n").IsOk() && inlineTable.LoadText("general = { other = 1 }\n").IsOk() &&
        structs.LoadText("x = 1\n").IsOk() && noComment.LoadText("[general]\n").IsOk(), "the texts should parse");
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
    TTestSettings settings;
    TASWTomlSetting<int> badComment{ settings.General, "bad", 1, "Bad\x01" };
    TTestSettings there;
    TASWTomlSetting<int> badCommentThere{ there.General, "bad", 1, "Bad\x01" };
    CheckTrue(settings.LoadText("[general]\n").IsOk() && there.LoadText("[general]\nbad = 1\n").IsOk(),
        "the texts should parse");

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
    TSetDuringSave settings;
    std::string const text = "[general]\nzoom = 1.0\n";
    CheckTrue(settings.LoadText(text).IsOk(), "the text should parse");

    // Act
    TASWTomlResult const added = settings.SaveAs("unused.toml");
    bool const modifiedAfterAdd = settings.IsModified();
    CheckTrue(settings.LoadText(text).IsOk(), "the text should parse again");
    TASWTomlResult const changed = settings.Save();
    bool const modifiedAfterChange = settings.IsModified();

    // Assert
    CheckTrue(added.IsOk() && changed.IsOk(), "both saves should succeed");
    CheckTrue(modifiedAfterAdd, "a key added while the file is written isn't in it, so the document stays modified");
    CheckTrue(modifiedAfterChange, "nor is a value changed then");
    CheckEquals("[general]\nzoom = 2.0\n"s, settings.ToText(), "the value is changed");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SettingsAccess, Set_FailuresAreReported)
{
    // Arrange
    TTestSettings settings;
    TASWTomlSetting<int> throwing{ settings.General, "throwing", 1, "", { .Validator = TThrowingValidator() } };
    std::string const text = "[general]\ncolor = \"Red\"\n";
    CheckTrue(settings.LoadText(text).IsOk(), "the text should parse");
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
    TTestSettings settings;
    TTestSettings same;
    TTestSettings empty;
    CheckTrue(settings.LoadText("[general]\nlevel = 1  # End\n").IsOk() && same.LoadText("[general]\nlevel = 1\n")
        .IsOk() && empty.LoadText("").IsOk(), "the texts should parse");
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
    TTestSettings settings;
    CheckTrue(settings.LoadText("[general]\nword = \"two words\"\n").IsOk(), "the text should parse");

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
    TTestSettings settings;
    CheckTrue(settings.LoadText("[general]\nname = \"\"\n").IsOk(), "the text should parse");

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
    TTestSettings settings;
    TASWTomlOptions octal;
    octal.Writing.LineEnding = ASWToml::ELineEnding::LF;
    octal.Values.IntegerBase = EIntegerBase::Octal;
    CheckTrue(settings.SetOptions(octal).IsOk() && settings.LoadText("[general]\n").IsOk(),
        "the options and the text should be taken");

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

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_SettingsAccess, "Settings")
