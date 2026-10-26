/* **************************************************************************
Test_ASWToml_File.h
Author: Anthony S. West - ASW Software

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#ifndef Test_ASWToml_FileH
#define Test_ASWToml_FileH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_File
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_File : public TAutoTestGroup<TTest_ASWToml_File>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_File> inherited;

public: // Test methods, each defined with ASW_TEST
#if defined(_WIN32)
    void Test_PathToUtf8_ReplacesLoneSurrogate();
#endif
    void Test_PathToUtf8_RoundTripsAscii();
    void Test_ReadWholeFile_EmptyFile();
    void Test_ReadWholeFile_EmptyPath();
    void Test_ReadWholeFile_Folder();
    void Test_ReadWholeFile_LargerThanMaxSize();
    void Test_ReadWholeFile_MissingFile();
    void Test_ReadWholeFile_MissingFolder();
    void Test_ReadWholeFile_ReadsExactBytes();
    void Test_Utf8ToPath_ConvertsFourByteSequence();
    void Test_Utf8ToPath_ConvertsNonAscii();
    void Test_Utf8ToPath_InvalidUtf8();
    void Test_WriteWholeFile_Backup_CopiesPreviousFile();
    void Test_WriteWholeFile_Backup_SkippedForNewFile();
    void Test_WriteWholeFile_CreatesFile();
    void Test_WriteWholeFile_CreatesMissingFolders();
    void Test_WriteWholeFile_EmptyPath();
    void Test_WriteWholeFile_FailedReplace_KeepsTargetAndRemovesTemp();
    void Test_WriteWholeFile_InPlace_OverwritesAndTruncates();
#if defined(_WIN32)
    void Test_WriteWholeFile_KeepsHiddenAttribute();
#endif
#if !defined(_WIN32)
    void Test_WriteWholeFile_KeepsPermissions();
    void Test_WriteWholeFile_KeepsSymbolicLink();
#endif
    void Test_WriteWholeFile_NonAsciiPath();
    void Test_WriteWholeFile_OutOfMemory_KeepsFileAndRemovesTemp();
#if defined(_WIN32)
    void Test_WriteWholeFile_ReadOnlyFile_Fails();
#endif
    void Test_WriteWholeFile_ReplacesFile();
    void Test_WriteWholeFile_WithoutCreateFolders_FailsForMissingFolder();

public:
    TTest_ASWToml_File();
    ~TTest_ASWToml_File() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWToml_FileH
