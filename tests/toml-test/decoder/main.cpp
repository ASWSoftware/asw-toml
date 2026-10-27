/* **************************************************************************
main.cpp
Author: Anthony S. West - ASW Software

A decoder for toml-test (https://github.com/toml-lang/toml-test), the test
suite of TOML parsers, whose runner starts it once for each test file: it
reads a TOML text on stdin and writes its values on stdout as toml-test's
tagged JSON (ToJson() in the Tagged style). For a text that isn't valid TOML
it writes the error on stderr and returns 1, as toml-test expects; it
returns 2 when anything else fails, which toml-test reports as a failure.

    toml-test test -toml=1.1 -decoder=<path of this program>

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#include <cstddef>
#include <cstdio>
#include <new>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif
//---------------------------------------------------------------------------
#include "ASWToml_Document.h"
#include "ASWToml_Json.h"
#include "ASWToml_Types.h"
//---------------------------------------------------------------------------

namespace
{

// The exit codes: toml-test takes 1 as "the text isn't valid TOML" and any other code but 0 as a failure
constexpr int ExitDecoded = 0;
constexpr int ExitInvalidToml = 1;
constexpr int ExitFailed = 2;

//---------------------------------------------------------------------------

void PrintError(ASWToml::TASWTomlResult const& result);
void PrintError(std::string_view text);
bool ReadAll(std::FILE* file, std::string& text);
bool SetBinaryMode();
bool WriteAll(std::FILE* file, std::string_view text);

//---------------------------------------------------------------------------
/*
    PrintError

    Writes 'result', an error, on stderr as one line, e.g.
    Error DuplicateKey: The key a is already defined on line 1. (line 2, column 1)
*/
void PrintError(ASWToml::TASWTomlResult const& result)
{
    ASWToml::TASWTomlMessage const message{ .Severity = ASWToml::ESeverity::Error, .Code = result.Code,
        .Text = result.Message, .FilePath = result.FilePath, .Line = result.Line, .Column = result.Column,
        .Path = result.Path, .SystemError = result.SystemError };

    PrintError(ASWToml::ToString(message));
}
//---------------------------------------------------------------------------
/*
    PrintError

    Writes 'text', an error, on stderr as one line.
*/
void PrintError(std::string_view text)
{
    static_cast<void>(WriteAll(stderr, text));
    static_cast<void>(WriteAll(stderr, "\n"));
}
//---------------------------------------------------------------------------
/*
    ReadAll

    Reads 'file' to its end into 'text'; false when it can't be read.
*/
bool ReadAll(std::FILE* file, std::string& text)
{
    char buffer[65536];
    std::size_t count = 0;
    while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
        text.append(buffer, count);

    return std::ferror(file) == 0;
}
//---------------------------------------------------------------------------
/*
    SetBinaryMode

    Makes stdin and stdout binary, so the text is read and the JSON written byte for byte: Windows' text mode would
    read each CRLF as LF, which changes the text the tests give, and stop at a Ctrl+Z. False when it can't be set.
*/
bool SetBinaryMode()
{
#if defined(_WIN32)
    return _setmode(_fileno(stdin), _O_BINARY) != -1 && _setmode(_fileno(stdout), _O_BINARY) != -1;
#else
    return true;
#endif
}
//---------------------------------------------------------------------------
/*
    WriteAll

    Writes 'text' to 'file' and flushes it; false when it can't be written.
*/
bool WriteAll(std::FILE* file, std::string_view text)
{
    return std::fwrite(text.data(), 1, text.size(), file) == text.size() && std::fflush(file) == 0;
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
int main()
{
    try
    {
        std::string text;
        if (!SetBinaryMode() || !ReadAll(stdin, text))
        {
            PrintError("The TOML text can't be read from stdin.");
            return ExitFailed;
        }

        // With the default options, as an application reads a file
        ASWToml::TASWTomlDocument document;
        ASWToml::TASWTomlResult const parsed = document.Parse(text);
        if (!parsed)
        {
            PrintError(parsed);
            return ExitInvalidToml;
        }

        std::string json;
        ASWToml::TASWTomlResult const written = ASWToml::ToJson(document.GetRoot(), json,
            { .Style = ASWToml::EJsonStyle::Tagged });
        if (!written)
        {
            PrintError(written);
            return ExitFailed;
        }

        json += "\n";
        if (!WriteAll(stdout, json))
        {
            PrintError("The JSON can't be written to stdout.");
            return ExitFailed;
        }
    }
    catch (std::bad_alloc const&)
    {
        PrintError("There isn't enough memory.");
        return ExitFailed;
    }

    return ExitDecoded;
}
//---------------------------------------------------------------------------
