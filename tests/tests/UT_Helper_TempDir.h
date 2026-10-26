/* **************************************************************************
UT_Helper_TempDir.h
Author: Anthony S. West - ASW Software

Test helpers for files: TTempDir, a folder of its own for each test that is
deleted with everything in it, and functions that write and read test files
with the standard library, independently of the code under test.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#ifndef UT_Helper_TempDirH
#define UT_Helper_TempDirH
//---------------------------------------------------------------------------
#include <cstddef>
#include <filesystem>
#include <string>
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

/////////////////////////////////////////////////////////////////////////////
// TTempDir
//
// A new, empty folder under the system's temporary folder (ASWTomlTests/<unique name>), deleted with everything in it
// when the object is destroyed. Path() is empty if the folder couldn't be created.
/////////////////////////////////////////////////////////////////////////////
class TTempDir
{
private:
    std::filesystem::path m_Path;

public:
    TTempDir();
    TTempDir(TTempDir const&) = delete;
    ~TTempDir();

    TTempDir& operator=(TTempDir const&) = delete;

    std::filesystem::path const& Path() const noexcept;
};

//---------------------------------------------------------------------------

// The number of files and folders directly in 'folder' (0 if it can't be read)
std::size_t CountEntries(std::filesystem::path const& folder);

// Reads the whole file at 'path' in binary mode into 'bytes'; false if it can't be read
bool ReadTestFile(std::filesystem::path const& path, std::string& bytes);

// Writes 'bytes' to the file at 'path' in binary mode, replacing it; false if it can't be written
bool WriteTestFile(std::filesystem::path const& path, std::string const& bytes);

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef UT_Helper_TempDirH
