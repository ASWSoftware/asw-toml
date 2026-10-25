/* **************************************************************************
Test_ASWToml_File.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "Test_ASWToml_File.h"
//---------------------------------------------------------------------------
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>
//---------------------------------------------------------------------------
#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <sys/stat.h>
#endif
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWToml_File.h"
#include "ASWToml_Options.h"
#include "ASWToml_Types.h"
//---------------------------------------------------------------------------
#include "UT_Helper_TempDir.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

std::string TrickyBytes();

//---------------------------------------------------------------------------
/*
    TrickyBytes

    Bytes a text-mode read or write would change: CRLF, a lone CR, a NUL, a Ctrl+Z (end of file to some C runtimes),
    a UTF-8 BOM and a byte above 0x7F
*/
std::string TrickyBytes()
{
    return std::string("\xEF\xBB\xBF[server]\r\nport = 1\rB\n\x1A\0x\xFF", 28);
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_File
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_File::TTest_ASWToml_File()
    : inherited("ASWToml_File_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_File::~TTest_ASWToml_File()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_File::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_File::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_File::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_File::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
#if defined(_WIN32)
ASW_TEST(TTest_ASWToml_File, PathToUtf8_ReplacesLoneSurrogate)
{
    // Arrange
    // A wide path that isn't valid UTF-16, which Windows allows in file names
    std::filesystem::path const path(std::wstring(L"x\xD800y.toml"));

    // Act
    std::string const utf8 = ASWToml::PathToUtf8(path);

    // Assert
    CheckEquals(std::string("x\xEF\xBF\xBD" "y.toml"), utf8, "the lone surrogate should become U+FFFD");
}
#endif
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_File, PathToUtf8_RoundTripsAscii)
{
    // Arrange
    std::string const name = "app settings.toml";
    std::string const pathText = "folder/sub folder/app settings.toml";

    // Act
    std::string const roundTrip = ASWToml::PathToUtf8(ASWToml::Utf8ToPath(name));
    std::filesystem::path const path = ASWToml::Utf8ToPath(pathText);

    // Assert
    // Paths are compared with ==, by their elements, not by how they spell their separators
    CheckEquals(name, roundTrip, "an ASCII name should convert back to the same text");
    CheckTrue(path == std::filesystem::path(pathText), "a path with separators should have the same elements");
    CheckTrue(ASWToml::Utf8ToPath("").empty(), "an empty text should be an empty path");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_File, ReadWholeFile_EmptyFile)
{
    // Arrange
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const path = folder.Path() / "empty.toml";
    AssertTrue(WriteTestFile(path, ""), "the test file should be written");
    std::string contents = "old";

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadWholeFile(path, 100, contents);

    // Assert
    CheckTrue(result.IsOk(), "an empty file should be read: " + result.Message);
    CheckEmpty(contents, "the contents should be empty");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_File, ReadWholeFile_EmptyPath)
{
    // Arrange
    std::string contents;

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadWholeFile(std::filesystem::path(), 100, contents);

    // Assert
    CheckEquals(ASWToml::ETomlCode::InvalidArgument, result.Code, "an empty path should be an invalid argument");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_File, ReadWholeFile_Folder)
{
    // Arrange
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::string contents;

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadWholeFile(folder.Path(), 100, contents);

    // Assert
    // Windows can't open a folder as a file (AccessDenied); elsewhere it opens, but isn't a file (OpenFailed)
    CheckFalse(result.IsOk(), "a folder should not be read as a file");
    CheckTrue(result.Code == ASWToml::ETomlCode::AccessDenied || result.Code == ASWToml::ETomlCode::OpenFailed,
        std::string("the code should be AccessDenied or OpenFailed, not ") + ASWToml::ToString(result.Code));
    CheckNotEmpty(result.Message, "the failure should be described");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_File, ReadWholeFile_LargerThanMaxSize)
{
    // Arrange
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const path = folder.Path() / "large.toml";
    AssertTrue(WriteTestFile(path, "0123456789A"), "the test file should be written");
    std::string contents;

    // Act
    ASWToml::TASWTomlResult const tooLarge = ASWToml::Detail::ReadWholeFile(path, 10, contents);
    std::string const contentsAfterFailure = contents;
    ASWToml::TASWTomlResult const exactSize = ASWToml::Detail::ReadWholeFile(path, 11, contents);

    // Assert
    CheckEquals(ASWToml::ETomlCode::FileTooLarge, tooLarge.Code,
        "an 11-byte file should be too large for a 10-byte limit");
    CheckContains(tooLarge.Message, "11 bytes", "the message should give the file's size");
    CheckEmpty(contentsAfterFailure, "nothing should be read from a file that is too large");
    CheckTrue(exactSize.IsOk(), "a file of exactly the limit should be read: " + exactSize.Message);
    CheckEquals(std::string("0123456789A"), contents, "the whole file should be read");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_File, ReadWholeFile_MissingFile)
{
    // Arrange
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const path = folder.Path() / "missing.toml";
    std::string contents;

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadWholeFile(path, 100, contents);

    // Assert
    CheckEquals(ASWToml::ETomlCode::FileNotFound, result.Code, "the code should be FileNotFound");
    CheckNotEquals(0, result.SystemError, "the system's error code should be kept");
    CheckEquals(ASWToml::PathToUtf8(path), result.FilePath, "the path should be kept");
    CheckContains(result.Message, "missing.toml", "the message should name the file");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_File, ReadWholeFile_MissingFolder)
{
    // Arrange
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::string contents;

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadWholeFile(folder.Path() / "no" / "app.toml", 100,
        contents);

    // Assert
    CheckEquals(ASWToml::ETomlCode::FileNotFound, result.Code, "a missing folder should be FileNotFound");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_File, ReadWholeFile_ReadsExactBytes)
{
    // Arrange
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const path = folder.Path() / "bytes.toml";
    AssertTrue(WriteTestFile(path, TrickyBytes()), "the test file should be written");
    std::string contents;

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadWholeFile(path, 100, contents);

    // Assert
    CheckTrue(result.IsOk(), "the file should be read: " + result.Message);
    CheckEquals(TrickyBytes(), contents, "every byte should be read unchanged");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_File, Utf8ToPath_ConvertsFourByteSequence)
{
    // Arrange
    // U+1F600, a surrogate pair in UTF-16
    std::string const utf8 = "\xF0\x9F\x98\x80.toml";

    // Act
    std::filesystem::path const path = ASWToml::Utf8ToPath(utf8);

    // Assert
#if defined(_WIN32)
    CheckTrue(path.wstring() == std::wstring(L"\xD83D\xDE00.toml"), "the wide path should hold the surrogate pair");
#else
    CheckEquals(utf8, path.string(), "the path should hold the UTF-8 bytes");
#endif
    CheckEquals(utf8, ASWToml::PathToUtf8(path), "the path should convert back");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_File, Utf8ToPath_ConvertsNonAscii)
{
    // Arrange
    // "cafe" with U+00E9, and the Greek lambda U+03BB
    std::string const utf8 = "caf\xC3\xA9_\xCE\xBB.toml";

    // Act
    std::filesystem::path const path = ASWToml::Utf8ToPath(utf8);

    // Assert
#if defined(_WIN32)
    CheckTrue(path.wstring() == std::wstring(L"caf\x00E9_\x03BB.toml"), "the wide path should hold U+00E9 and U+03BB");
#else
    CheckEquals(utf8, path.string(), "the path should hold the UTF-8 bytes");
#endif
    CheckEquals(utf8, ASWToml::PathToUtf8(path), "the path should convert back");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_File, Utf8ToPath_InvalidUtf8)
{
    // Arrange
    // The byte FF, which is never valid UTF-8
    std::string const utf8 = "a\xFF" "b.toml";

    // Act
    std::filesystem::path const path = ASWToml::Utf8ToPath(utf8);

    // Assert
#if defined(_WIN32)
    CheckTrue(path.wstring() == std::wstring(L"a\xFFFD" L"b.toml"), "the byte should become U+FFFD");
    CheckEquals(std::string("a\xEF\xBF\xBD" "b.toml"), ASWToml::PathToUtf8(path),
        "the path should convert back with U+FFFD");
#else
    CheckEquals(utf8, path.string(), "the path should hold the bytes as they are");
    CheckEquals(utf8, ASWToml::PathToUtf8(path), "the path should convert back to the same bytes");
#endif
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_File, WriteWholeFile_Backup_CopiesPreviousFile)
{
    // Arrange
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const path = folder.Path() / "app.toml";
    AssertTrue(WriteTestFile(path, "old"), "the test file should be written");
    ASWToml::TASWTomlSavingOptions options;
    options.Backup = true;
    ASWToml::TMessageVector warnings;

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::WriteWholeFile(path, "new", options, warnings);

    // Assert
    std::string contents;
    std::string backup;
    CheckTrue(result.IsOk(), "the file should be saved: " + result.Message);
    CheckEmpty(warnings, "there should be no warnings");
    CheckTrue(ReadTestFile(path, contents), "the file should be readable");
    CheckEquals(std::string("new"), contents, "the file should have the new contents");
    CheckTrue(ReadTestFile(folder.Path() / "app.toml.bak", backup), "the backup should exist");
    CheckEquals(std::string("old"), backup, "the backup should have the previous contents");

    // Act
    // A second save replaces the older backup
    ASWToml::TASWTomlResult const second = ASWToml::Detail::WriteWholeFile(path, "newer", options, warnings);

    // Assert
    CheckTrue(second.IsOk(), "the second save should succeed: " + second.Message);
    CheckTrue(ReadTestFile(folder.Path() / "app.toml.bak", backup), "the backup should exist");
    CheckEquals(std::string("new"), backup, "the backup should be replaced");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_File, WriteWholeFile_Backup_SkippedForNewFile)
{
    // Arrange
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    ASWToml::TASWTomlSavingOptions options;
    options.Backup = true;
    ASWToml::TMessageVector warnings;

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::WriteWholeFile(folder.Path() / "new.toml", "x", options,
        warnings);

    // Assert
    CheckTrue(result.IsOk(), "the file should be saved: " + result.Message);
    CheckEmpty(warnings, "there is nothing to back up, which isn't a warning");
    CheckEquals(1, static_cast<int>(CountEntries(folder.Path())), "the folder should only hold the file");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_File, WriteWholeFile_CreatesFile)
{
    // Arrange
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const path = folder.Path() / "app.toml";
    ASWToml::TMessageVector warnings;

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::WriteWholeFile(path, TrickyBytes(),
        ASWToml::TASWTomlSavingOptions(), warnings);

    // Assert
    std::string contents;
    CheckTrue(result.IsOk(), "the file should be saved: " + result.Message);
    CheckEmpty(warnings, "there should be no warnings");
    CheckTrue(ReadTestFile(path, contents), "the file should be readable");
    CheckEquals(TrickyBytes(), contents, "every byte should be written unchanged");
    CheckEquals(1, static_cast<int>(CountEntries(folder.Path())), "no temporary file should be left");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_File, WriteWholeFile_CreatesMissingFolders)
{
    // Arrange
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const path = folder.Path() / "Company" / "App" / "app.toml";
    ASWToml::TMessageVector warnings;

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::WriteWholeFile(path, "x", ASWToml::TASWTomlSavingOptions(),
        warnings);

    // Assert
    std::string contents;
    CheckTrue(result.IsOk(), "the file should be saved: " + result.Message);
    CheckTrue(ReadTestFile(path, contents), "the file should be in the new folders");
    CheckEquals(std::string("x"), contents, "the file should have its contents");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_File, WriteWholeFile_EmptyPath)
{
    // Arrange
    ASWToml::TMessageVector warnings;

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::WriteWholeFile(std::filesystem::path(), "x",
        ASWToml::TASWTomlSavingOptions(), warnings);

    // Assert
    CheckEquals(ASWToml::ETomlCode::InvalidArgument, result.Code, "an empty path should be an invalid argument");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_File, WriteWholeFile_FailedReplace_KeepsTargetAndRemovesTemp)
{
    // Arrange
    // A folder where the file should be, so the temporary file can be written but can't replace it
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const path = folder.Path() / "app.toml";
    std::error_code error;
    AssertTrue(std::filesystem::create_directory(path, error), "the blocking folder");
    AssertTrue(WriteTestFile(path / "inside.txt", "kept"), "a file in the blocking folder");
    ASWToml::TMessageVector warnings;

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::WriteWholeFile(path, "x", ASWToml::TASWTomlSavingOptions(),
        warnings);

    // Assert
    std::string inside;
    CheckEquals(ASWToml::ETomlCode::ReplaceFailed, result.Code, "the code should be ReplaceFailed");
    CheckNotEquals(0, result.SystemError, "the system's error code should be kept");
    CheckEmpty(warnings, "the temporary file should be deleted without a warning");
    CheckEquals(1, static_cast<int>(CountEntries(folder.Path())), "no temporary file should be left");
    CheckTrue(ReadTestFile(path / "inside.txt", inside), "the folder should be untouched");
    CheckEquals(std::string("kept"), inside, "the folder's file should be unchanged");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_File, WriteWholeFile_InPlace_OverwritesAndTruncates)
{
    // Arrange
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const path = folder.Path() / "app.toml";
    AssertTrue(WriteTestFile(path, "a much longer previous content"), "the test file should be written");
    ASWToml::TASWTomlSavingOptions options;
    options.AtomicSave = false;
    ASWToml::TMessageVector warnings;

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::WriteWholeFile(path, "short", options, warnings);

    // Assert
    std::string contents;
    CheckTrue(result.IsOk(), "the file should be saved: " + result.Message);
    CheckTrue(ReadTestFile(path, contents), "the file should be readable");
    CheckEquals(std::string("short"), contents, "the old contents should be cut off");
    CheckEquals(1, static_cast<int>(CountEntries(folder.Path())), "no other file should be made");
}
//---------------------------------------------------------------------------
#if defined(_WIN32)
ASW_TEST(TTest_ASWToml_File, WriteWholeFile_KeepsHiddenAttribute)
{
    // Arrange
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const path = folder.Path() / "hidden.toml";
    AssertTrue(WriteTestFile(path, "old"), "the test file should be written");
    AssertTrue(::SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_HIDDEN) != 0, "the file should be made hidden");
    ASWToml::TMessageVector warnings;

    // Act
    // Atomically, then in place
    ASWToml::TASWTomlResult const atomic = ASWToml::Detail::WriteWholeFile(path, "new",
        ASWToml::TASWTomlSavingOptions(), warnings);
    DWORD const attributesAfterAtomic = ::GetFileAttributesW(path.c_str());
    ASWToml::TASWTomlSavingOptions inPlaceOptions;
    inPlaceOptions.AtomicSave = false;
    ASWToml::TASWTomlResult const inPlace = ASWToml::Detail::WriteWholeFile(path, "newer", inPlaceOptions, warnings);
    DWORD const attributesAfterInPlace = ::GetFileAttributesW(path.c_str());

    // Assert
    std::string contents;
    CheckTrue(atomic.IsOk(), "the atomic save should succeed: " + atomic.Message);
    CheckTrue((attributesAfterAtomic & FILE_ATTRIBUTE_HIDDEN) != 0,
        "the file should still be hidden after an atomic save");
    CheckTrue(inPlace.IsOk(), "the in-place save should succeed: " + inPlace.Message);
    CheckTrue((attributesAfterInPlace & FILE_ATTRIBUTE_HIDDEN) != 0,
        "the file should still be hidden after an in-place save");
    CheckTrue(ReadTestFile(path, contents), "the file should be readable");
    CheckEquals(std::string("newer"), contents, "the file should have the last contents");
}
#endif
//---------------------------------------------------------------------------
#if !defined(_WIN32)
ASW_TEST(TTest_ASWToml_File, WriteWholeFile_KeepsPermissions)
{
    // Arrange
    // A private file, which an atomic save must not make readable by others
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const path = folder.Path() / "private.toml";
    AssertTrue(WriteTestFile(path, "old"), "the test file should be written");
    AssertEquals(0, ::chmod(path.c_str(), 0600), "chmod 0600");
    ASWToml::TMessageVector warnings;

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::WriteWholeFile(path, "new",
        ASWToml::TASWTomlSavingOptions(), warnings);

    // Assert
    struct stat status;
    CheckTrue(result.IsOk(), "the file should be saved: " + result.Message);
    AssertEquals(0, ::stat(path.c_str(), &status), "stat");
    CheckEquals(0600, static_cast<int>(status.st_mode & 07777), "the file should keep its 0600 permissions");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_File, WriteWholeFile_KeepsSymbolicLink)
{
    // Arrange
    // app.toml is a link to real.toml
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const real = folder.Path() / "real.toml";
    std::filesystem::path const link = folder.Path() / "app.toml";
    AssertTrue(WriteTestFile(real, "old"), "the test file should be written");
    std::error_code error;
    std::filesystem::create_symlink(real, link, error);
    AssertFalse(static_cast<bool>(error), "the link should be created: " + error.message());
    ASWToml::TMessageVector warnings;

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::WriteWholeFile(link, "new",
        ASWToml::TASWTomlSavingOptions(), warnings);

    // Assert
    std::string contents;
    CheckTrue(result.IsOk(), "the file should be saved: " + result.Message);
    CheckTrue(std::filesystem::is_symlink(link, error), "the link should still be a link");
    CheckTrue(ReadTestFile(real, contents), "the linked file should be readable");
    CheckEquals(std::string("new"), contents, "the linked file should have the new contents");
}
#endif
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_File, WriteWholeFile_NonAsciiPath)
{
    // Arrange
    // A folder and a file name with U+00E9 and U+03BB
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const path = folder.Path() / ASWToml::Utf8ToPath("caf\xC3\xA9") /
        ASWToml::Utf8ToPath("\xCE\xBB.toml");
    ASWToml::TMessageVector warnings;
    std::string contents;

    // Act
    ASWToml::TASWTomlResult const written = ASWToml::Detail::WriteWholeFile(path, "x", ASWToml::TASWTomlSavingOptions(),
        warnings);
    ASWToml::TASWTomlResult const read = ASWToml::Detail::ReadWholeFile(path, 100, contents);

    // Assert
    CheckTrue(written.IsOk(), "the file should be saved: " + written.Message);
    CheckTrue(read.IsOk(), "the file should be read: " + read.Message);
    CheckEquals(std::string("x"), contents, "the file should have its contents");
#if defined(_WIN32)
    std::error_code error;
    CheckTrue(std::filesystem::exists(folder.Path() / L"caf\x00E9" / L"\x03BB.toml", error),
        "the file should have the wide name");
#endif
}
//---------------------------------------------------------------------------
#if defined(_WIN32)
ASW_TEST(TTest_ASWToml_File, WriteWholeFile_ReadOnlyFile_Fails)
{
    // Arrange
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const path = folder.Path() / "readonly.toml";
    AssertTrue(WriteTestFile(path, "old"), "the test file should be written");
    AssertTrue(::SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY) != 0, "the file should be made read-only");
    ASWToml::TMessageVector warnings;

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::WriteWholeFile(path, "new",
        ASWToml::TASWTomlSavingOptions(), warnings);

    // Assert
    std::string contents;
    CheckEquals(ASWToml::ETomlCode::ReplaceFailed, result.Code, "the code should be ReplaceFailed");
    CheckEquals(static_cast<int>(ERROR_ACCESS_DENIED), result.SystemError, "the system error should be access denied");
    CheckTrue(ReadTestFile(path, contents), "the file should still be readable");
    CheckEquals(std::string("old"), contents, "the file should be unchanged");
    CheckEquals(1, static_cast<int>(CountEntries(folder.Path())), "no temporary file should be left");

    // So the folder can be deleted
    ::SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
}
#endif
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_File, WriteWholeFile_ReplacesFile)
{
    // Arrange
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    std::filesystem::path const path = folder.Path() / "app.toml";
    AssertTrue(WriteTestFile(path, "a longer previous content"), "the test file should be written");
    ASWToml::TMessageVector warnings;

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::WriteWholeFile(path, "new",
        ASWToml::TASWTomlSavingOptions(), warnings);

    // Assert
    std::string contents;
    CheckTrue(result.IsOk(), "the file should be saved: " + result.Message);
    CheckEmpty(warnings, "there should be no warnings");
    CheckTrue(ReadTestFile(path, contents), "the file should be readable");
    CheckEquals(std::string("new"), contents, "the file should have only the new contents");
    CheckEquals(1, static_cast<int>(CountEntries(folder.Path())), "no temporary file should be left");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_File, WriteWholeFile_WithoutCreateFolders_FailsForMissingFolder)
{
    // Arrange
    TTempDir const folder;
    AssertFalse(folder.Path().empty(), "the test folder should be created");
    ASWToml::TASWTomlSavingOptions options;
    options.CreateFolders = false;
    ASWToml::TMessageVector warnings;

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::WriteWholeFile(folder.Path() / "App" / "app.toml", "x",
        options, warnings);

    // Assert
    CheckEquals(ASWToml::ETomlCode::FileNotFound, result.Code, "a missing folder should be FileNotFound");
    CheckEquals(0, static_cast<int>(CountEntries(folder.Path())), "no folder should be created");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWToml_File)
