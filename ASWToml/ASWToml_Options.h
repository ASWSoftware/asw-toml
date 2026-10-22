/* **************************************************************************
ASWToml_Options.h
Author: Anthony S. West - ASW Software

A TOML file reader/writer library.

TASWTomlOptions and its three groups: how TOML text is read
(TASWTomlReadingOptions: the largest file, the deepest nesting), written
(TASWTomlWritingOptions: comment prefix, line ending, byte order mark, blank
line before a new table) and saved (TASWTomlSavingOptions: atomically, with
a backup, creating the folder), each with Validate() to check that the
options can be used.

Requires C++ 20 or higher.

Copyright 2026 ASW Software

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

************************************************************************** */

#pragma once

#ifndef ASWToml_OptionsH
#define ASWToml_OptionsH
//---------------------------------------------------------------------------
#include <cstddef>
#include <string>
//---------------------------------------------------------------------------
#include "ASWToml_Types.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

// The line ending lines are written with
enum class ELineEnding : int
{
    Auto = 0, // Each line keeps its own; new lines get the file's most common one (the platform's for a new file)
    Native = 1, // Every line gets the platform's: CRLF on Windows, LF elsewhere
    LF = 2, // Every line gets "\n"
    CRLF = 3, // Every line gets "\r\n"
};

// Whether a saved file starts with a UTF-8 byte order mark
enum class EBomMode : int
{
    Auto = 0, // As the loaded file did; none for a new file
    Always = 1,
    Never = 2,
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlReadingOptions
//
// How TOML text is read: the limits that keep a hostile or broken file from using too much memory or stack. TOML has
// one syntax, so there is no dialect to choose.
/////////////////////////////////////////////////////////////////////////////
struct TASWTomlReadingOptions
{
    // The default MaxFileSize: 16 MiB, far more than a settings file needs
    static constexpr std::size_t DefaultMaxFileSize = 16 * 1024 * 1024;

    // The default MaxNestingDepth: far deeper than a settings file needs
    static constexpr std::size_t DefaultMaxNestingDepth = 100;

    // The largest file a load reads, in bytes; a larger one fails with ETomlCode::FileTooLarge
    std::size_t MaxFileSize = DefaultMaxFileSize;

    // How many arrays and inline tables a value may have inside one another, e.g. 2 for a = [[1, 2], [3]] or
    // b = { c = [1] }. A deeper value fails the load with ETomlCode::NestingTooDeep, so a file can't make the parser
    // overflow the stack.
    std::size_t MaxNestingDepth = DefaultMaxNestingDepth;

    // Ok if these options can be used, else ETomlCode::InvalidOptions with the first problem found, e.g. a MaxFileSize
    // of 0
    TASWTomlResult Validate() const;
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlWritingOptions
//
// How new lines are written: the comment prefix of comments that are added, the line ending and byte order mark of
// the whole text, and the blank line before a new table. Text read from a file is written back as it was, apart from
// what an edit changes.
/////////////////////////////////////////////////////////////////////////////
struct TASWTomlWritingOptions
{
    // Written before the text of a new comment line, so a comment "Valid range: 0-4" is "# Valid range: 0-4". Must
    // start with '#', the only comment character in TOML.
    std::string CommentPrefix = "# ";

    // The line ending lines are written with
    ELineEnding LineEnding = ELineEnding::Auto;

    // Whether a saved file starts with a UTF-8 byte order mark
    EBomMode Bom = EBomMode::Auto;

    // Whether a new table's header is written after a blank line (unless it is the first thing in the file)
    bool BlankLineBeforeNewTable = true;

    // Ok if these options can be used, else ETomlCode::InvalidOptions with the first problem found, e.g. a
    // CommentPrefix that doesn't start with '#'
    TASWTomlResult Validate() const;
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlSavingOptions
//
// How a file is saved: atomically or in place, with or without a backup, and whether a missing folder is created
/////////////////////////////////////////////////////////////////////////////
struct TASWTomlSavingOptions
{
    // Whether a save writes a temporary file in the same folder and then replaces the file with it, so a failure or a
    // crash while saving leaves the previous file whole. Turn it off only when the folder can't be written to (the file
    // itself is then overwritten in place).
    bool AtomicSave = true;

    // Whether a save first copies the previous file to the same name followed by BackupSuffix (e.g.
    // "app.toml.bak"), replacing an older backup. A backup that fails is reported as a warning
    // (ETomlCode::BackupFailed); the file is still saved.
    bool Backup = false;

    // Added to the file name for the backup
    std::string BackupSuffix = ".bak";

    // Whether a save creates the file's folder (and the folders above it) when it doesn't exist
    bool CreateFolders = true;

    // Ok if these options can be used, else ETomlCode::InvalidOptions with the first problem found, e.g. an empty
    // BackupSuffix with Backup on
    TASWTomlResult Validate() const;
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlOptions
//
// How TOML text is read, written and saved, e.g. options.Writing.LineEnding = ELineEnding::LF. The defaults write
// "# comment" lines, keep each line's line ending and the file's byte order mark, and save atomically.
/////////////////////////////////////////////////////////////////////////////
struct TASWTomlOptions
{
    TASWTomlReadingOptions Reading;
    TASWTomlWritingOptions Writing;
    TASWTomlSavingOptions Saving;

    // Ok if these options can be used, else ETomlCode::InvalidOptions with the first problem found by each group's
    // Validate(), in the order Reading, Writing, Saving
    TASWTomlResult Validate() const;
};

//---------------------------------------------------------------------------

// The value's name, e.g. "CRLF"; "Unknown" for a value that isn't one of the enumerators
char const* ToString(EBomMode bom) noexcept;
char const* ToString(ELineEnding lineEnding) noexcept;

} // namespace ASWToml

//---------------------------------------------------------------------------
#endif // #ifndef ASWToml_OptionsH
