/* **************************************************************************
UT_Helper_TempDir.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "UT_Helper_TempDir.h"
//---------------------------------------------------------------------------
#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <system_error>
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

// Numbers the folders of this process
std::atomic<unsigned long> g_TempDirCounter{ 0 };

} // namespace

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
std::size_t CountEntries(std::filesystem::path const& folder)
{
    std::error_code error;
    std::filesystem::directory_iterator it(folder, error);
    if (error)
        return 0;

    std::size_t count = 0;
    for (std::filesystem::directory_iterator const end; it != end; it.increment(error))
    {
        if (error)
            break;

        ++count;
    }

    return count;
}
//---------------------------------------------------------------------------
bool ReadTestFile(std::filesystem::path const& path, std::string& bytes)
{
    bytes.clear();

    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;

    bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

    return !file.bad();
}
//---------------------------------------------------------------------------
bool WriteTestFile(std::filesystem::path const& path, std::string const& bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        return false;

    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();

    return !file.fail();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTempDir
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTempDir::TTempDir()
{
    std::error_code error;
    std::filesystem::path const root = std::filesystem::temp_directory_path(error) / "ASWTomlTests";
    if (error)
        return;

    // The time makes the name unique across processes, the counter within this one
    auto const ticks = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    for (int attempt = 0; attempt < 100; ++attempt)
    {
        std::filesystem::path const candidate = root / ("T" + std::to_string(ticks) + "_" +
            std::to_string(++g_TempDirCounter));

        std::filesystem::create_directories(root, error);
        if (std::filesystem::create_directory(candidate, error) && !error)
        {
            m_Path = candidate;
            return;
        }
    }
}
//---------------------------------------------------------------------------
TTempDir::~TTempDir()
{
    if (m_Path.empty())
        return;

    std::error_code error;
    std::filesystem::remove_all(m_Path, error);
}
//---------------------------------------------------------------------------
std::filesystem::path const& TTempDir::Path() const noexcept
{
    return m_Path;
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests
