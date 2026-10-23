/* **************************************************************************
ASWToml_File.h
Author: Anthony S. West - ASW Software

A TOML file reader/writer library.

The file layer: reading a whole file in binary mode with a size limit, and
writing one atomically (a temporary file in the same folder that then
replaces the file), with an optional backup and missing folders created,
using the operating system's own file APIs. File paths are UTF-8 strings in
the library's interface; Utf8ToPath() and PathToUtf8() convert them, and are
public. The rest is internal (namespace ASWToml::Detail): not part of the
public interface, and may change in any release.

Requires C++ 20 or higher.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

#pragma once

#ifndef ASWToml_FileH
#define ASWToml_FileH
//---------------------------------------------------------------------------
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWToml_Options.h"
#include "ASWToml_Types.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

// 'path' as UTF-8, the form the library takes file paths in, e.g. ASWToml::PathToUtf8(folder / "app.toml"). On
// Windows, the path's wide (UTF-16) form is converted, since a narrow std::filesystem::path string is in the ANSI code
// page with some standard libraries and UTF-8 with others, and path::u8string() throws for a wide form that isn't
// valid UTF-16 with some (here, a lone surrogate becomes U+FFFD).
std::string PathToUtf8(std::filesystem::path const& path);

// The UTF-8 text 'utf8' (e.g. a file path the library returns) as a path. On Windows, it is converted to the wide
// (UTF-16) form the file APIs take; an invalid UTF-8 sequence becomes U+FFFD (where a std::u8string would throw with
// some standard libraries). Elsewhere, the bytes are the path.
std::filesystem::path Utf8ToPath(std::string_view utf8);

//---------------------------------------------------------------------------
// Internals of the library. Not part of the public interface.
//---------------------------------------------------------------------------
namespace Detail
{

// Reads the whole file at 'path', in binary mode (no line ending conversion), into 'contents'. Fails with
// ETomlCode::FileTooLarge if it is larger than 'maxSize' bytes (leaving 'contents' empty), FileNotFound if it or its
// folder doesn't exist, AccessDenied, OpenFailed (e.g. a folder) or ReadFailed. Never throws.
TASWTomlResult ReadWholeFile(std::filesystem::path const& path, std::size_t maxSize, std::string& contents) noexcept;

// Writes 'contents' to the file at 'path', in binary mode, as the saving options say:
// - CreateFolders: creates the file's folder (and those above it) when it doesn't exist.
// - Backup: first copies an existing file to its name followed by BackupSuffix; a failure is added to 'warnings'
//   (ETomlCode::BackupFailed) and the save goes on.
// - AtomicSave: writes a temporary file in the same folder, flushes it to the disk and replaces the file with it, so
//   the file is either the old one or the new one, never a mix, even after a crash. On Windows, ReplaceFileW() keeps
//   the file's attributes and security; elsewhere the file's permissions (and, when allowed, its owner) are copied
//   to the new file, and a symbolic link is followed, so the file it points to is replaced and the link stays. A
//   temporary file that can't be deleted after a failure is added to 'warnings' (TempFileCleanupFailed).
// - Otherwise the file is overwritten in place (opened, written, cut to the new size and flushed).
// Fails with CreateFolderFailed, FileNotFound (its folder doesn't exist), AccessDenied, OpenFailed, WriteFailed or
// ReplaceFailed. Never throws.
TASWTomlResult WriteWholeFile(std::filesystem::path const& path, std::string_view contents,
    TASWTomlSavingOptions const& options, TMessageVector& warnings) noexcept;

} // namespace Detail

} // namespace ASWToml

//---------------------------------------------------------------------------
#endif // #ifndef ASWToml_FileH
