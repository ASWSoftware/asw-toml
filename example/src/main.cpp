/* **************************************************************************
main.cpp
Author: Anthony S. West - ASW Software

Example for ASWToml: an application's settings class (with a table and an
array of tables of its own structs), its first run, a file a user edited,
settings used from two threads, values the class doesn't declare, the
document on its own, and several changes as one with the values as JSON. It
writes ASWTomlExample.toml in the temp folder's ASWTomlExample folder,
starting afresh on each run, and returns 0 when every step did what it shows.
Run with --pause to wait for Enter before exiting.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWToml_Base.h"
#include "ASWToml_Converters.h"
#include "ASWToml_Document.h"
#include "ASWToml_Edits.h"
#include "ASWToml_File.h"
#include "ASWToml_Json.h"
#include "ASWToml_Options.h"
#include "ASWToml_Settings.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
#include "ASWToml_Version.h"
//---------------------------------------------------------------------------

namespace
{

// The steps that went differently from what they show; main() returns 1 when there are any
unsigned UnexpectedCount = 0;

// The application's color theme, a setting written by name (see ASWTomlEnumNames())
enum class ETheme : int
{
    Light = 0,
    Dark = 1,
    System = 2,
};

// The names ETheme's values are written and read by (TASWTomlEnumName): the first name of a value is written, and the
// others are read too, so "Auto", an earlier name of System, still reads (and the case of the letters doesn't
// matter). Found by argument-dependent lookup, and defined (not only declared) before ETheme's setting, so the
// converter sees that ETheme has names.
auto const& ASWTomlEnumNames(ETheme)
{
    static constexpr ASWToml::TASWTomlEnumName<ETheme> names[] = {
        { ETheme::Light, "Light" }, { ETheme::Dark, "Dark" }, { ETheme::System, "System" },
        { ETheme::System, "Auto" },
    };

    return names;
}


/////////////////////////////////////////////////////////////////////////////
// TWindow
//
// The application's main window, a setting that is a table (see ASWTomlFields())
/////////////////////////////////////////////////////////////////////////////
struct TWindow
{
    int Width = 1024;
    int Height = 768;
    bool Maximized = false;
};

// The fields of TWindow, by their keys in the table: found by argument-dependent lookup, as ASWTomlEnumNames()
auto ASWTomlFields(TWindow const&)
{
    return std::tuple{ ASWToml::Field("width", &TWindow::Width), ASWToml::Field("height", &TWindow::Height),
        ASWToml::Field("maximized", &TWindow::Maximized) };
}


/////////////////////////////////////////////////////////////////////////////
// TServer
//
// A server the application connects to: a std::vector of them is an array of tables. This version of the application
// added the field secure, which a file of the earlier version lacks, so Load() adds it to each server.
/////////////////////////////////////////////////////////////////////////////
struct TServer
{
    std::string Host;
    std::uint16_t Port = 443;
    bool Secure = true;
};

// The servers of a setting
using TServerVector = std::vector<TServer>;

auto ASWTomlFields(TServer const&)
{
    return std::tuple{ ASWToml::Field("host", &TServer::Host), ASWToml::Field("port", &TServer::Port),
        ASWToml::Field("secure", &TServer::Secure) };
}

// Keyboard shortcuts by name, a value the settings class doesn't declare (a table of strings)
using TShortcutMap = std::map<std::string, std::string>;


/////////////////////////////////////////////////////////////////////////////
// TAppSettings
//
// The application's settings, declared once each: its table, key, type, default value, comment and options. Load()
// brings the file up to date (missing keys and fields added, invalid values replaced by the defaults, old keys
// renamed), and each setting's Get() and Set() read and write the loaded document, from any thread.
/////////////////////////////////////////////////////////////////////////////
class TAppSettings : public ASWToml::TASWBaseToml
{
private:
    // Value options that write an integer in hexadecimal (every form is read)
    static ASWToml::TASWTomlValueOptions Hexadecimal();

    // The validator of a text that can't be empty: "" when it is valid, else what is wrong with it
    static std::string NotEmpty(std::string const& value);

public:
    // The root table "" holds the keys before the first header; its comment is the file comment
    ASWToml::TASWTomlTable FileTop{ *this, "", "Settings of the ASWToml example." };

    ASWToml::TASWTomlTable General{ *this, "general", "What the application looks like." };
    ASWToml::TASWTomlSetting<std::string> UserName{ General, "user_name", "Guest", "The name in the title bar.",
        { .Validator = NotEmpty } };
    ASWToml::TASWTomlSetting<ETheme> Theme{ General, "theme", ETheme::System, "Light, Dark or System." };
    ASWToml::TASWTomlSetting<int> AccentColor{ General, "accent_color", 0x3366CC, "As 0xRRGGBB.",
        { .Min = 0, .Max = 0xFFFFFF, .Values = Hexadecimal() } };
    ASWToml::TASWTomlSetting<std::chrono::year_month_day> LicenceExpires{ General, "licence_expires",
        std::chrono::year_month_day(std::chrono::year(9999), std::chrono::December, std::chrono::day(31)),
        "The day the licence ends." };
    // Not added to the file when it is missing: a hidden setting a user may add by hand
    ASWToml::TASWTomlSetting<bool> EnableCheats{ General, "enable_cheats", false, "", { .Optional = true } };

    ASWToml::TASWTomlTable Logging{ *this, "logging" };
    ASWToml::TASWTomlSetting<unsigned> Level{ Logging, "level", 2, "From 0 (errors only) to 4 (everything).",
        { .Min = 0, .Max = 4 } };
    // Called log_days in an earlier version of the application, so Load() renames that key
    ASWToml::TASWTomlSetting<unsigned> DaysKept{ Logging, "days_kept", 30,
        "How many days the logs are kept; 0 keeps them forever.", { .OldKeys = { "log_days" } } };
    ASWToml::TASWTomlSetting<std::filesystem::path> Folder{ Logging, "folder", std::filesystem::path("logs"),
        "Relative to the folder of this file." };

    ASWToml::TASWTomlTable View{ *this, "view" };
    ASWToml::TASWTomlSetting<double> Zoom{ View, "zoom", 1.0, "From 0.25 to 4.", { .Min = 0.25, .Max = 4.0 } };
    ASWToml::TASWTomlSetting<ASWToml::TStringVector> Columns{ View, "columns",
        ASWToml::TStringVector{ "name", "size", "date" }, "The columns shown, in order." };
    // A struct is a table, [view.window]
    ASWToml::TASWTomlSetting<TWindow> Window{ View, "window", TWindow(), "The main window." };

    ASWToml::TASWTomlTable Network{ *this, "network", "Where the application connects." };
    ASWToml::TASWTomlSetting<int> Timeout{ Network, "timeout", 30, "In seconds.", { .Min = 1 } };
    // A std::vector of a struct is an array of tables, [[network.servers]]
    ASWToml::TASWTomlSetting<TServerVector> Servers{ Network, "servers",
        TServerVector{ TServer{ "main.example.com", 443, true } }, "The servers, tried in order." };

public:
    TAppSettings();
};

//---------------------------------------------------------------------------

void Check(bool condition, char const* what);
void PrintFile(std::filesystem::path const& path);
void PrintHeading(char const* heading);
void PrintMessage(ASWToml::TASWTomlMessage const& message);
void PrintText(std::string const& text);
void SetLevels(TAppSettings& settings);
void Step1_FirstRun(TAppSettings& settings, std::filesystem::path const& file);
void Step2_UseTheSettings(TAppSettings& settings, std::filesystem::path const& file);
void Step3_AUserEditedTheFile(TAppSettings& settings, std::filesystem::path const& file);
void Step4_Threads(TAppSettings& settings);
void Step5_ValuesTheClassDoesntDeclare(TAppSettings& settings, std::filesystem::path const& file);
void Step6_TheDocument(TAppSettings const& settings);
void Step7_SeveralChangesAsOne(TAppSettings& settings, std::filesystem::path const& file);
template <typename T>
std::string Text(T const& value);
bool WriteFileText(std::filesystem::path const& path, std::string const& text);

//---------------------------------------------------------------------------
/*
    Check

    Counts 'what' as unexpected, saying so, when 'condition' is false.
*/
void Check(bool condition, char const* what)
{
    if (condition)
        return;

    ++UnexpectedCount;
    std::cout << "  UNEXPECTED: " << what << "\n";
}
//---------------------------------------------------------------------------
/*
    PrintFile

    Prints the file at 'path', each line indented, without its line break.
*/
void PrintFile(std::filesystem::path const& path)
{
    std::cout << "  " << ASWToml::PathToUtf8(path) << ":\n";
    std::ifstream file(path, std::ios::binary);
    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        std::cout << "    | " << line << "\n";
    }
}
//---------------------------------------------------------------------------
/*
    PrintHeading

    Prints a step's heading, after a blank line.
*/
void PrintHeading(char const* heading)
{
    std::cout << "\n" << heading << "\n";
}
//---------------------------------------------------------------------------
/*
    PrintMessage

    The log callback. The TOML object calls it on the thread of the operation, after the operation, with none of its
    locks held, so it may call the object, even Load() or Save(). An application would write the message to its log.
*/
void PrintMessage(ASWToml::TASWTomlMessage const& message)
{
    std::cout << "  [log] " << ASWToml::ToString(message) << "\n";
}
//---------------------------------------------------------------------------
/*
    PrintText

    Prints 'text', a TOML or JSON text, each line indented, without its line break.
*/
void PrintText(std::string const& text)
{
    std::string::size_type start = 0;
    while (start < text.size())
    {
        std::string::size_type end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();

        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        std::cout << "    | " << line << "\n";
        start = end + 1;
    }
}
//---------------------------------------------------------------------------
/*
    SetLevels

    Sets the logging level to 0, 1, 2, 3, 4, 0, ... 1000 times, as another thread of the application might.
*/
void SetLevels(TAppSettings& settings)
{
    for (unsigned i = 0; i < 1000; ++i)
        static_cast<void>(settings.Level.Set(i % 5));
}
//---------------------------------------------------------------------------
/*
    Step1_FirstRun

    Loads the file, which is missing on an application's first run: Load() fails with FileNotFound, reported as a
    warning, and the document is given every setting with its default value and comment, each reported as a note
    (KeyAdded). IsModified() then says the file needs saving.
*/
void Step1_FirstRun(TAppSettings& settings, std::filesystem::path const& file)
{
    PrintHeading("1. First run: the file is missing");

    ASWToml::TASWTomlResult const loaded = settings.Load(ASWToml::PathToUtf8(file));
    Check(loaded.Code == ASWToml::ETomlCode::FileNotFound, "the file should be missing");

    std::cout << "  IsModified(): " << Text(settings.IsModified()) << "\n";
    if (settings.IsModified())
    {
        ASWToml::TASWTomlResult const saved = settings.Save();
        Check(saved.IsOk(), "the file should be saved");
    }

    PrintFile(file);
}
//---------------------------------------------------------------------------
/*
    Step2_UseTheSettings

    Reads and sets the settings. A value outside a setting's range (or one its validator refuses) isn't set: Set()
    returns why, and reports it as an error. A table and an array of tables are read and set whole, as a struct and a
    std::vector of a struct; Set() changes only the keys and elements that differ.
*/
void Step2_UseTheSettings(TAppSettings& settings, std::filesystem::path const& file)
{
    PrintHeading("2. Reading and setting values");

    std::cout << "  user_name: " << settings.UserName.Get() << "\n";
    std::cout << "  theme: " << Text(settings.Theme.Get()) << "\n";
    std::cout << "  licence_expires: " << Text(settings.LicenceExpires.Get()) << "\n";
    std::cout << "  columns: " << Text(settings.Columns.Get()) << "\n";

    Check(settings.Theme.Set(ETheme::Dark).IsOk(), "the theme should be set");
    Check(settings.AccentColor.Set(0xFF8800).IsOk(), "the accent color should be set");
    Check(settings.Zoom.Set(1.5).IsOk(), "the zoom should be set");
    ASWToml::TStringVector columns = settings.Columns.Get();
    columns.push_back("type, if known");
    Check(settings.Columns.Set(columns).IsOk(), "the columns should be set");

    ASWToml::TASWTomlResult const refused = settings.Level.Set(7);
    Check(refused.Code == ASWToml::ETomlCode::ValueOutOfRange, "level 7 should be out of range");
    std::cout << "  Level.Set(7): " << ASWToml::ToString(refused.Code) << "\n";
    std::cout << "  level: " << settings.Level.Get() << "\n";

    TWindow window = settings.Window.Get();
    window.Width = 1280;
    window.Maximized = true;
    Check(settings.Window.Set(window).IsOk(), "the window should be set");

    TServerVector servers = settings.Servers.Get();
    servers.push_back(TServer{ "backup.example.com", 8443, true });
    Check(settings.Servers.Set(servers).IsOk(), "the servers should be set");
    std::cout << "  servers: " << settings.Servers.Get().size() << "\n";

    Check(settings.Save().IsOk(), "the file should be saved");
    PrintFile(file);
}
//---------------------------------------------------------------------------
/*
    Step3_AUserEditedTheFile

    Loads a file that a user edited by hand, written by an earlier version of the application: an empty user name, a
    theme by its earlier name, a level out of range, the earlier key log_days, a comment changed by hand, the hidden
    enable_cheats, servers without the field secure, one with a port out of range and one with a key of the user's
    own, and a table of the user's own. Load() repairs what isn't valid (in a server, only the port), renames log_days,
    adds the missing keys and fields, writes the code's comments back (TASWTomlWritingOptions::CommentPolicy), and
    keeps everything else as it was.
*/
void Step3_AUserEditedTheFile(TAppSettings& settings, std::filesystem::path const& file)
{
    PrintHeading("3. A user edited the file");

    std::string const edited =
        "# Settings of the ASWToml example.\n"
        "\n"
        "[general]\n"
        "user_name = \"\"\n"
        "theme = \"auto\"\n"
        "licence_expires = 2030-06-30\n"
        "enable_cheats = true\n"
        "\n"
        "[logging]\n"
        "# Turned up to find a bug\n"
        "level = 9\n"
        "log_days = 7  # A week\n"
        "\n"
        "[view]\n"
        "zoom = 1.25\n"
        "\n"
        "[view.window]\n"
        "width = 1600\n"
        "height = 900\n"
        "maximized = false\n"
        "\n"
        "[network]\n"
        "timeout = 30\n"
        "\n"
        "[[network.servers]]\n"
        "host = \"main.example.com\"\n"
        "port = 443\n"
        "weight = 2  # The user's own key\n"
        "\n"
        "[[network.servers]]\n"
        "host = \"backup.example.com\"\n"
        "port = 99999\n"
        "\n"
        "[plugins.spell]\n"
        "# Added by the spell checker\n"
        "enabled = true\n"
        "language = \"en-GB\"\n";
    Check(WriteFileText(file, edited), "the edited file should be written");
    PrintFile(file);

    ASWToml::TASWTomlResult const loaded = settings.Load(ASWToml::PathToUtf8(file));
    Check(loaded.IsOk(), "the edited file should load");

    TServerVector const servers = settings.Servers.Get();
    std::cout << "  user_name: " << settings.UserName.Get() << "\n";
    std::cout << "  theme: " << Text(settings.Theme.Get()) << "\n";
    std::cout << "  enable_cheats: " << Text(settings.EnableCheats.Get()) << "\n";
    std::cout << "  level: " << settings.Level.Get() << "\n";
    std::cout << "  days_kept: " << settings.DaysKept.Get() << "\n";
    for (TServer const& server : servers)
    {
        std::cout << "  server " << server.Host << ": port " << server.Port;
        std::cout << ", secure " << Text(server.Secure) << "\n";
    }

    Check(settings.EnableCheats.IsPresent() && settings.EnableCheats.Get(), "enable_cheats should be on");
    Check(settings.DaysKept.Get() == 7, "log_days should be renamed to days_kept");
    Check(servers.size() == 2 && servers[1].Port == 443, "the second server should keep only its host");

    std::cout << "  IsModified(): " << Text(settings.IsModified()) << "\n";
    if (settings.IsModified())
        Check(settings.Save().IsOk(), "the file should be saved");

    PrintFile(file);
}
//---------------------------------------------------------------------------
/*
    Step4_Threads

    Reads the logging level on this thread while another thread sets it. Each Get() sees the document before or after
    a Set(), never during one, so every value read is in the range.
*/
void Step4_Threads(TAppSettings& settings)
{
    PrintHeading("4. Two threads");

    std::thread setter(SetLevels, std::ref(settings));
    unsigned outOfRange = 0;
    for (unsigned i = 0; i < 1000; ++i)
    {
        if (settings.Level.Get() > 4)
            ++outOfRange;
    }

    setter.join();

    Check(outOfRange == 0, "every level read should be in the range");
    std::cout << "  1000 reads while another thread set the level 1000 times: " << outOfRange << " out of range\n";

    Check(settings.Level.Reset().IsOk(), "the level should be reset");
    std::cout << "  Level.Reset(): the level is " << settings.Level.Get() << " again\n";
}
//---------------------------------------------------------------------------
/*
    Step5_ValuesTheClassDoesntDeclare

    Reads and writes values the settings class doesn't declare, by their paths: typed values with GetValue(),
    TryGetValue(), SetValue() and Append(), key names, comments, renaming and removal. GetValue() gives the default
    for a missing value, and for one that doesn't convert (reported as a warning); TryGetValue() says why and reports
    nothing.
*/
void Step5_ValuesTheClassDoesntDeclare(TAppSettings& settings, std::filesystem::path const& file)
{
    PrintHeading("5. Values the settings class doesn't declare");

    bool const spell = settings.GetValue("plugins.spell.enabled", false);
    std::string const language = settings.GetValue("plugins.spell.language", "en-US");
    std::cout << "  plugins.spell: enabled " << Text(spell) << ", language " << language << "\n";

    int number = 0;
    ASWToml::TASWTomlResult const missing = settings.TryGetValue("plugins.spell.size", number);
    std::cout << "  TryGetValue(\"plugins.spell.size\"): " << missing.Message << "\n";
    int const wrongType = settings.GetValue("plugins.spell.language", 0);
    std::cout << "  GetValue(\"plugins.spell.language\", 0): " << wrongType << "\n";

    Check(settings.SetValue("plugins.spell.words", ASWToml::TStringVector{ "TOML", "ASWToml" }).IsOk(),
        "the words should be set");
    Check(settings.Append("plugins.spell.words", "dotted").IsOk(), "a word should be appended");
    Check(settings.Rename("plugins.spell.words", "dictionary").IsOk(), "the words should be renamed");
    Check(settings.Remove("plugins.spell.language"), "the language should be removed");

    TShortcutMap const shortcuts{ { "open", "Ctrl+O" }, { "save", "Ctrl+S" } };
    Check(settings.SetValue("shortcuts", shortcuts).IsOk(), "the shortcuts should be set");
    Check(settings.SetComment("shortcuts", "Keyboard shortcuts").IsOk(), "the shortcuts' comment should be set");
    Check(settings.SetTrailingComment("shortcuts.save", "Cmd+S on a Mac").IsOk(), "the trailing comment should be set");
    for (std::string const& name : settings.GetKeyNames("shortcuts"))
        std::cout << "  shortcuts." << name << ": " << settings.GetValue("shortcuts." + name, "") << "\n";

    Check(settings.Save().IsOk(), "the file should be saved");
    PrintFile(file);
}
//---------------------------------------------------------------------------
/*
    Step6_TheDocument

    Uses a TASWTomlDocument on its own, with no file and no lock: parses a text with a dotted key, an inline table, an
    array on several lines and a date, sets values (only their text changes, each in the form it was written in) and
    goes through its keys. A text with an error isn't read: Parse() fails with the line and column of the first
    error, and the document stays as it was. GetDocument() gives a copy of a TOML object's document.
*/
void Step6_TheDocument(TAppSettings const& settings)
{
    PrintHeading("6. The document");

    ASWToml::TASWTomlDocument document;
    ASWToml::TASWTomlResult const parsed = document.Parse(
        "# Colors of the editor\n"
        "title = \"Editor\"\n"
        "colors.back = \"white\"  # A dotted key\n"
        "font = { name = \"Consolas\", size = 11 }\n"
        "palette = [\n"
        "    \"red\",  # Warm\n"
        "    \"blue\",\n"
        "]\n"
        "updated = 2026-10-26\n");
    Check(parsed.IsOk(), "the text should be parsed");

    Check(document.SetValue("colors.back", ASWToml::TASWTomlValue("navy")).IsOk(), "colors.back should be set");
    Check(document.SetValue("font.size", ASWToml::TASWTomlValue(12)).IsOk(), "font.size should be set");
    Check(document.Append("palette", ASWToml::TASWTomlValue("green")).IsOk(), "a color should be appended");
    PrintText(document.ToText());

    for (ASWToml::TASWTomlKeyValue const& entry : *document.GetRoot().AsTable())
        std::cout << "  " << entry.Key << ": " << ASWToml::ToString(entry.Value.GetType()) << "\n";

    ASWToml::TASWTomlValue const* const name = document.Find("font.name");
    std::cout << "  font.name: " << (name != nullptr && name->AsString() != nullptr ? *name->AsString() : "?") << "\n";

    std::string const before = document.ToText();
    ASWToml::TASWTomlResult const failed = document.Parse("title = \"Editor\"\nfont = { name = \"Consolas\"\n");
    Check(failed.Code == ASWToml::ETomlCode::ParseFailed, "the text with an error should fail");
    Check(document.ToText() == before, "the document should stay as it was");
    std::cout << "  Parse() of a text with an error: " << ASWToml::ToString(failed.Code);
    std::cout << " at line " << failed.Line << ", column " << failed.Column << ": " << failed.Message << "\n";

    ASWToml::TASWTomlDocument const copy = settings.GetDocument();
    ASWToml::TASWTomlValue const* const servers = copy.Find("network.servers");
    std::size_t const count = servers != nullptr && servers->IsArray() ? servers->AsArray()->size() : 0;
    std::cout << "  The settings' document has " << count << " servers\n";
}
//---------------------------------------------------------------------------
/*
    Step7_SeveralChangesAsOne

    Makes several changes as one batch (TASWTomlEdits): two settings, added with Set() given the edits, and two
    values the class doesn't declare, which Apply() makes all at once, under one lock, so another thread sees all of
    them or none, at about the cost of one edit. A setting's value that Set() refuses is reported and left out, and
    the rest is made; a batch with an edit that fails changes nothing. Then writes the network table as JSON with
    ToJson(), and one value in toml-test's Tagged form, which keeps its TOML type.
*/
void Step7_SeveralChangesAsOne(TAppSettings& settings, std::filesystem::path const& file)
{
    PrintHeading("7. Several changes as one, and JSON");

    ASWToml::TASWTomlEdits edits;
    Check(settings.Theme.Set(edits, ETheme::Light).IsOk(), "the theme should be added to the batch");
    Check(settings.Zoom.Set(edits, 1.25).IsOk(), "the zoom should be added to the batch");
    ASWToml::TASWTomlResult const refused = settings.Level.Set(edits, 9);
    Check(refused.Code == ASWToml::ETomlCode::ValueOutOfRange, "level 9 should be refused");
    edits.SetValue("plugins.spell.enabled", true).SetComment("plugins.spell.enabled", "Checks while typing");
    std::cout << "  " << edits.GetEdits().size() << " edits (level 9 refused: " << ASWToml::ToString(refused.Code);
    std::cout << ")\n";

    Check(settings.Apply(edits).IsOk(), "the batch should be made");
    std::cout << "  Apply(): theme " << Text(settings.Theme.Get()) << ", zoom " << settings.Zoom.Get();
    std::cout << ", plugins.spell.enabled " << Text(settings.GetValue("plugins.spell.enabled", false)) << "\n";

    // days_kept exists, so the rename fails, and the user name isn't set either
    ASWToml::TASWTomlEdits failing;
    Check(settings.UserName.Set(failing, "Ada").IsOk(), "the user name should be added to the batch");
    failing.Rename("logging.level", "days_kept");
    std::string const before = settings.ToText();
    ASWToml::TASWTomlResult const failed = settings.Apply(failing);
    Check(failed.Code == ASWToml::ETomlCode::AlreadyExists, "the batch should fail");
    Check(settings.ToText() == before, "the failed batch should change nothing");
    std::cout << "  Apply() of a batch with an edit that fails: " << ASWToml::ToString(failed.Code);
    std::cout << "; the user name is still " << settings.UserName.Get() << "\n";

    Check(settings.Save().IsOk(), "the file should be saved");
    PrintFile(file);

    std::string json;
    Check(ASWToml::ToJson(settings.GetValue("network", ASWToml::TASWTomlValue()), json).IsOk(),
        "the network table should be written as JSON");
    std::cout << "  ToJson() of network:\n";
    PrintText(json);

    std::string tagged;
    Check(ASWToml::ToJson(settings.GetValue("general.licence_expires", ASWToml::TASWTomlValue()), tagged,
        { .Style = ASWToml::EJsonStyle::Tagged }).IsOk(), "licence_expires should be written as tagged JSON");
    std::cout << "  ToJson() of general.licence_expires, Tagged: " << tagged << "\n";
}
//---------------------------------------------------------------------------
/*
    Text

    'value' as TOML text, as the library writes it with the default value options: the value of a key v in a
    document of its own, without "v = " and the line break.
*/
template <typename T>
std::string Text(T const& value)
{
    ASWToml::TASWTomlValue written;
    ASWToml::TASWTomlDocument document;
    if (!ASWToml::TASWTomlConverter<T>::ToValue(value, written, ASWToml::TASWTomlValueOptions()).IsOk())
        return "?";

    if (!document.SetValue("v", written).IsOk())
        return "?";

    std::string text = document.ToText().substr(4);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
        text.pop_back();

    return text;
}
//---------------------------------------------------------------------------
/*
    WriteFileText

    Writes 'text' to the file at 'path' as it is (binary mode, so "\n" stays LF); false when it can't.
*/
bool WriteFileText(std::filesystem::path const& path, std::string const& text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    file.close();

    return !file.fail();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TAppSettings
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TAppSettings::TAppSettings()
{
    // The options are set before the first Load(), which reads the file with them
    ASWToml::TASWTomlOptions options;
    options.Saving.Backup = true; // Each save first copies the file to ASWTomlExample.toml.bak
    static_cast<void>(SetOptions(options));
}
//---------------------------------------------------------------------------
ASWToml::TASWTomlValueOptions TAppSettings::Hexadecimal()
{
    ASWToml::TASWTomlValueOptions values;
    values.IntegerBase = ASWToml::EIntegerBase::Hexadecimal;

    return values;
}
//---------------------------------------------------------------------------
std::string TAppSettings::NotEmpty(std::string const& value)
{
    return value.empty() ? "it can't be empty" : "";
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    bool pause = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string_view(argv[i]) == "--pause")
            pause = true;
    }

    std::cout << "ASWToml " << ASWToml::Version << " example\n";

    // The example's own folder in the temp folder, which the first save creates (TASWTomlSavingOptions::CreateFolders).
    // The file of an earlier run is deleted, so each run starts with the first run.
    std::error_code error;
    std::filesystem::path const folder = std::filesystem::temp_directory_path(error) / "ASWTomlExample";
    std::filesystem::path const file = folder / "ASWTomlExample.toml";
    std::filesystem::remove(file, error);
    std::filesystem::remove(folder / "ASWTomlExample.toml.bak", error);

    TAppSettings settings;
    settings.SetLogCallback(PrintMessage);

    Step1_FirstRun(settings, file);
    Step2_UseTheSettings(settings, file);
    Step3_AUserEditedTheFile(settings, file);
    Step4_Threads(settings);
    Step5_ValuesTheClassDoesntDeclare(settings, file);
    Step6_TheDocument(settings);
    Step7_SeveralChangesAsOne(settings, file);

    std::cout << "\nThe file is " << ASWToml::PathToUtf8(file) << "\n";
    if (UnexpectedCount != 0)
        std::cout << UnexpectedCount << " steps went differently from what they show\n";

    if (pause)
    {
        std::cout << "Press Enter to exit...\n";
        std::cin.get();
    }

    return UnexpectedCount == 0 ? 0 : 1;
}
