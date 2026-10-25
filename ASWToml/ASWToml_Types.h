/* **************************************************************************
ASWToml_Types.h
Author: Anthony S. West - ASW Software

A TOML file reader/writer library.

The types every part of the library shares: ETomlCode (what happened),
ELineBreak, ESeverity, TASWTomlMessage (a warning or error reported to the
application's log callback), TASWTomlResult (the outcome of an operation
such as a load or a save), and the container types several parts use.

Requires C++ 20 or higher.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

#pragma once

#ifndef ASWToml_TypesH
#define ASWToml_TypesH
//---------------------------------------------------------------------------
#include <cstddef>
#include <string>
#include <vector>
//---------------------------------------------------------------------------

namespace ASWToml
{

// What happened, in a TASWTomlResult or a TASWTomlMessage. The values never change (an application may store or
// compare them), and new codes are added at the end. Whether a code is an error or a warning depends on where it is
// reported: a TASWTomlResult only fails for an error, and a TASWTomlMessage has its own Severity (e.g. FileNotFound
// is a warning when a load finds no file, which only sets the file path).
enum class ETomlCode : int
{
    Ok = 0,

    // Errors of loading and parsing: the operation failed
    InvalidArgument = 1, // An argument is invalid, e.g. an empty file path
    InvalidOptions = 2, // TASWTomlOptions::Validate() rejected the options
    NoFilePath = 3, // There is no file path to save to or reload from
    FileNotFound = 4, // The file, or a folder in its path, doesn't exist
    AccessDenied = 5, // The operating system denied access to the file or its folder
    FileTooLarge = 6, // The file is larger than the reading options' MaxFileSize
    OpenFailed = 7, // The file couldn't be opened (for another reason than the codes above)
    ReadFailed = 8, // Reading the file failed
    UnsupportedEncoding = 9, // The text starts with a UTF-16 or UTF-32 byte order mark
    InvalidUtf8 = 10, // The text isn't valid UTF-8
    ParseFailed = 11, // The text isn't valid TOML
    DuplicateKey = 12, // A key or table is defined twice, or a table is defined again as another kind of value
    NestingTooDeep = 13, // Tables and arrays are nested deeper than the reading options' MaxNestingDepth

    // Errors of saving
    CreateFolderFailed = 14, // The file's folder doesn't exist and couldn't be created
    WriteFailed = 15, // Writing the file (or its temporary file) failed
    ReplaceFailed = 16, // The temporary file couldn't replace the file (an atomic save)

    // Errors of any operation
    UnexpectedException = 17, // An unexpected exception was caught, e.g. std::bad_alloc

    // Errors of edits and values
    NotFound = 18, // The key or table to read, change or rename doesn't exist
    AlreadyExists = 19, // A key or table can't be added, or renamed, with the name of one that exists
    TypeMismatch = 20, // The value's TOML type (e.g. a string) doesn't fit the type it is read as (e.g. an int)
    InvalidValue = 21, // The value isn't one the type accepts (e.g. a name an enumeration lacks), or can't be written
    ValueOutOfRange = 22, // A number is outside the range of its type (or setting), or a TOML integer beyond 64 bits

    // Warnings: the operation went on
    BackupFailed = 23, // The backup copy couldn't be made; the file was saved anyway
    TempFileCleanupFailed = 24, // A temporary file of a failed save couldn't be deleted

    // Notes: what applying the settings to a loaded document changed
    KeyAdded = 25, // A setting's key was missing, and was added with its default value
    KeyRenamed = 26, // A setting's key was missing, and an earlier name of it was renamed to it
};

// The line break that ends a line of a TOML text. TOML has no other: a carriage return on its own is an error.
enum class ELineBreak : int
{
    None = 0, // The last line of a text that doesn't end with a line break
    LF = 1, // "\n" (Linux, and most files)
    CRLF = 2, // "\r\n" (Windows)
};

// How serious a TASWTomlMessage is
enum class ESeverity : int
{
    Info = 0,
    Warning = 1,
    Error = 2,
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlMessage
//
// A warning, error or note about a file, reported to the application's log callback. The text is a complete
// sentence; the other fields let an application filter or format messages its own way (see ToString()).
/////////////////////////////////////////////////////////////////////////////
struct TASWTomlMessage
{
    ESeverity Severity = ESeverity::Info;
    ETomlCode Code = ETomlCode::Ok;
    std::string Text;
    std::string FilePath; // UTF-8; empty when the text wasn't loaded from or saved to a file
    std::size_t Line = 0; // 1-based; 0 when the message isn't about a place in the text
    std::size_t Column = 0; // 1-based, in characters (Unicode code points; a tab is one); 0 when Line is 0

    // The key's or table's path in TOML's dotted-key syntax, with [n] for an element of an array of tables, e.g.
    // server."host name".port or products[1].name; empty when the message isn't about a key or table
    std::string Path;

    int SystemError = 0; // The operating system's error code (GetLastError() or errno); 0 when there is none
};

// The messages an operation reports, e.g. the warnings of a save
using TMessageVector = std::vector<TASWTomlMessage>;


/////////////////////////////////////////////////////////////////////////////
// TASWTomlResult
//
// The outcome of an operation: Ok, or the error that made it fail, with a description. Warnings don't make an
// operation fail; they are reported as TASWTomlMessage objects instead.
/////////////////////////////////////////////////////////////////////////////
struct [[nodiscard]] TASWTomlResult
{
    ETomlCode Code = ETomlCode::Ok;
    std::string Message; // A complete sentence describing the error; empty when Code is Ok
    std::string FilePath; // UTF-8; empty when the operation wasn't about a file
    std::size_t Line = 0; // 1-based; 0 when the error isn't about a place in the text
    std::size_t Column = 0; // 1-based, in characters (Unicode code points; a tab is one); 0 when Line is 0

    // The path of the key, table or value the error is about, in TOML's dotted-key syntax as TASWTomlPath::ToString()
    // writes it, e.g. server."host name".port; empty when the error isn't about one, or is about the root table. A
    // failed edit of a TASWTomlDocument gives the path it was given; a failed TASWTomlConverter gives the place inside
    // the value it converted, e.g. [1].port (empty for the value itself).
    std::string Path;

    int SystemError = 0; // The operating system's error code (GetLastError() or errno); 0 when there is none

    bool IsOk() const noexcept
    {
        return Code == ETomlCode::Ok;
    }

    explicit operator bool() const noexcept
    {
        return IsOk();
    }
};

// The code's name, e.g. "FileNotFound"; "Unknown" for a value that isn't an ETomlCode
char const* ToString(ETomlCode code) noexcept;

// The line break's name: "None", "LF" or "CRLF"; "Unknown" for a value that isn't an ELineBreak
char const* ToString(ELineBreak lineBreak) noexcept;

// The severity's name: "Info", "Warning" or "Error"; "Unknown" for a value that isn't an ESeverity
char const* ToString(ESeverity severity) noexcept;

// The message as one line of text: the severity, the code and the text, followed by the parts that are set (the path
// as it is, since it quotes its own parts), e.g.
// Error DuplicateKey: The key "port" is defined twice. (file "app.toml", line 4, column 1, path server.port)
std::string ToString(TASWTomlMessage const& message);

} // namespace ASWToml

//---------------------------------------------------------------------------
#endif // #ifndef ASWToml_TypesH
