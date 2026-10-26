/* **************************************************************************
Check_Usage.cpp
Author: Anthony S. West - ASW Software

Check of the installed CMake package: tests/package/CMakeLists.txt builds this file against an ASWToml installed with
`cmake --install` and found with find_package(), and running it returns 0 when the installed library works: every
installed header compiles, a settings class (with a struct and an array of tables) reads and writes a text and a
file, and a document is parsed, edited and written back (so the library, its file layer and the threading library it
links to are all there).

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWToml_Base.h"
#include "ASWToml_Converters.h"
#include "ASWToml_Document.h"
#include "ASWToml_File.h"
#include "ASWToml_Index.h"
#include "ASWToml_Interface.h"
#include "ASWToml_Options.h"
#include "ASWToml_Parser.h"
#include "ASWToml_Path.h"
#include "ASWToml_Scalars.h"
#include "ASWToml_Settings.h"
#include "ASWToml_Types.h"
#include "ASWToml_Utils.h"
#include "ASWToml_Value.h"
#include "ASWToml_Version.h"
#include "ASWToml_Writer.h"
//---------------------------------------------------------------------------

namespace
{

// The problems found; main() returns 1 when there are any
unsigned ProblemCount = 0;

/////////////////////////////////////////////////////////////////////////////
// TWindow
//
// A setting that is a table
/////////////////////////////////////////////////////////////////////////////
struct TWindow
{
    int Width = 800;
    int Height = 600;
};

// The fields of TWindow, by their keys in the table
auto ASWTomlFields(TWindow const&)
{
    return std::tuple{ ASWToml::Field("width", &TWindow::Width), ASWToml::Field("height", &TWindow::Height) };
}


/////////////////////////////////////////////////////////////////////////////
// TServer
//
// An element of a setting that is an array of tables
/////////////////////////////////////////////////////////////////////////////
struct TServer
{
    std::string Host;
    int Port = 443;
};

// The servers of a setting
using TServerVector = std::vector<TServer>;

// The fields of TServer, by their keys in the table
auto ASWTomlFields(TServer const&)
{
    return std::tuple{ ASWToml::Field("host", &TServer::Host), ASWToml::Field("port", &TServer::Port) };
}


/////////////////////////////////////////////////////////////////////////////
// TCheckSettings
//
// A settings class with one table and three settings: a number, a table and an array of tables
/////////////////////////////////////////////////////////////////////////////
class TCheckSettings : public ASWToml::TASWBaseToml
{
public:
    ASWToml::TASWTomlTable Check{ *this, "check", "Written by the ASWToml package check." };
    ASWToml::TASWTomlSetting<int> Count{ Check, "count", 3, "From 0 to 9.", { .Min = 0, .Max = 9 } };
    ASWToml::TASWTomlSetting<TWindow> Window{ Check, "window", TWindow(), "The main window." };
    ASWToml::TASWTomlSetting<TServerVector> Servers{ Check, "servers", TServerVector{ TServer{ "main.example.com" } },
        "The servers, tried in order." };
};

//---------------------------------------------------------------------------

// Counts a problem, saying what it is, when 'condition' is false
void Check(bool condition, char const* what);

//---------------------------------------------------------------------------
/*
    Check

    Counts a problem, saying what it is, when 'condition' is false.
*/
void Check(bool condition, char const* what)
{
    if (condition)
        return;

    ++ProblemCount;
    std::cerr << "Problem: " << what << "\n";
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

int main()
{
    // A text: the settings are added to an empty document, read, set, and a value outside its range is refused
    TCheckSettings settings;
    Check(settings.LoadText("").IsOk(), "an empty text should load");
    Check(settings.Count.Get() == 3, "the setting should have its default value");
    Check(settings.IsModified(), "loading should add the settings");
    Check(settings.Count.Set(5).IsOk(), "the setting should be set");
    Check(settings.Count.Set(10).Code == ASWToml::ETomlCode::ValueOutOfRange, "10 should be out of the range");
    Check(settings.Count.Get() == 5, "a refused value should change nothing");

    TWindow window = settings.Window.Get();
    window.Width = 1024;
    Check(settings.Window.Set(window).IsOk(), "the table setting should be set");

    TServerVector servers = settings.Servers.Get();
    Check(servers.size() == 1 && servers[0].Port == 443, "the array setting should have its default value");
    servers.push_back(TServer{ "backup.example.com", 8443 });
    Check(settings.Servers.Set(servers).IsOk(), "the array setting should be set");

    std::string const text = settings.ToText();
    Check(text.find("count = 5") != std::string::npos, "the text should have the value set");
    Check(text.find("[check.window]") != std::string::npos, "the text should have the table");
    Check(text.find("width = 1024") != std::string::npos, "the text should have the table's value set");
    Check(text.find("[[check.servers]]") != std::string::npos, "the text should have the array of tables");
    Check(text.find("port = 8443") != std::string::npos, "the text should have the server added");

    // A file, in a folder of the temp folder that the save creates
    std::error_code error;
    std::filesystem::path const folder = std::filesystem::temp_directory_path(error) / "ASWTomlPackageCheck";
    std::filesystem::remove_all(folder, error);
    std::string const path = ASWToml::PathToUtf8(folder / "check.toml");
    Check(settings.SaveAs(path).IsOk(), "the file should be saved");

    TCheckSettings loaded;
    Check(loaded.Load(path).IsOk(), "the file should load");
    Check(loaded.Count.Get() == 5, "the loaded setting should have the value saved");
    Check(loaded.Window.Get().Width == 1024, "the loaded table should have the value saved");
    TServerVector const loadedServers = loaded.Servers.Get();
    Check(loadedServers.size() == 2 && loadedServers[1].Host == "backup.example.com",
        "the loaded array of tables should have the server added");
    Check(!loaded.IsModified(), "a file that has every setting shouldn't need saving");
    Check(loaded.ToText() == text, "the file should load as it was saved");
    std::filesystem::remove_all(folder, error);

    // The document on its own: an edit changes only the value's text, and an unchanged text is written back as it was
    std::string const original = "# Colors\n[colors]\nback = \"white\"  # The background\nfore = 0x000000\n";
    ASWToml::TASWTomlDocument document;
    Check(document.Parse(original).IsOk(), "the text should parse");
    Check(document.ToText() == original, "the text should be written back as it was");
    Check(document.SetValue("colors.back", ASWToml::TASWTomlValue("navy")).IsOk(), "the value should be set");
    Check(document.ToText() == "# Colors\n[colors]\nback = \"navy\"  # The background\nfore = 0x000000\n",
        "only the value's text should change");

    if (ProblemCount != 0)
        return 1;

    std::cout << "ASWToml " << ASWToml::Version << ": settings, texts, files and documents work\n";

    return 0;
}
