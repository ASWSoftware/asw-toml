/* **************************************************************************
Test_ASWToml_Base.h
Author: Anthony S. West - ASW Software

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#ifndef Test_ASWToml_BaseH
#define Test_ASWToml_BaseH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_BaseAccess
//
// TASWBaseToml's lookups, edits, options, LoadText() and ToText(), its locks, and what it does when memory runs out
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_BaseAccess : public TAutoTestGroup<TTest_ASWToml_BaseAccess>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_BaseAccess> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_Edits_FailureChangesNothing();
    void Test_Edits_MatchTheDocument();
    void Test_GetDocument_IsACopy();
    void Test_LoadText_FailureChangesNothing();
    void Test_LoadText_ReplacesTheDocument();
    void Test_LoadText_UsesTheOptions();
    void Test_Locks_WorkLikeStandardMutexes();
    void Test_Lookups_MatchTheDocument();
    void Test_New_IsEmptyWithTheDefaultOptions();
    void Test_OutOfMemory_EditChangesNothing();
    void Test_OutOfMemory_LoadTextChangesNothing();
    void Test_OutOfMemory_SetOptionsChangesNothing();
    void Test_Remove_ReturnsWhetherAnythingWasRemoved();
    void Test_SetOptions_InvalidOptionsChangeNothing();
    void Test_SetOptions_ReadsTheTextAgain();
    void Test_TypedValues_UseTheValueOptions();

public:
    TTest_ASWToml_BaseAccess();
    ~TTest_ASWToml_BaseAccess() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_BaseCallback
//
// How TASWBaseToml reports to its log callback: what, in which order, and with no lock held
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_BaseCallback : public TAutoTestGroup<TTest_ASWToml_BaseCallback>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_BaseCallback> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_Callback_ExceptionIsDropped();
    void Test_Callback_MayCallTheObject();
    void Test_Callback_MayReplaceItself();
    void Test_FailedEdit_IsReportedAsAnError();
    void Test_GetValue_WarningIsReported();
    void Test_LoadText_FailureIsReportedAsAnError();
    void Test_Lookups_ReportNothing();
    void Test_OutOfMemory_IsReportedAsAnError();
    void Test_SetLogCallback_EmptyTurnsReportingOff();
    void Test_SetOptions_FailureIsReportedAsAnError();

public:
    TTest_ASWToml_BaseCallback();
    ~TTest_ASWToml_BaseCallback() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_BaseFile
//
// TASWBaseToml's Load(), Save() and SaveAs(): real files, a missing file, failures that change nothing, the modified
// flag, the ReadFile() and WriteFile() seams, and what they do when memory runs out
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_BaseFile : public TAutoTestGroup<TTest_ASWToml_BaseFile>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_BaseFile> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_EmptyPath_IsInvalidArgument();
    void Test_FileOperations_DontOverlap();
    void Test_Load_FileErrorsChangeNothing();
    void Test_Load_MissingFileEmptiesTheDocument();
    void Test_Load_MissingFolderIsCreatedBySave();
    void Test_Load_ReadsTheFile();
    void Test_Load_ReadsWithoutTheLock();
    void Test_Load_SyntaxErrorChangesNothing();
    void Test_OutOfMemory_LoadChangesNothing();
    void Test_OutOfMemory_SaveChangesNothing();
    void Test_SaveAs_SetsTheFilePath();
    void Test_SaveAs_UsesUtf8Paths();
    void Test_Save_EditDuringSaveKeepsTheModifiedFlag();
    void Test_Save_NoFilePathFails();
#if defined(_WIN32)
    void Test_Save_ReadOnlyFileChangesNothing();
#endif
    void Test_Save_RoundTripsByteForByte();
    void Test_Save_WarningsAreReported();
    void Test_Save_WritesAndClearsTheModifiedFlag();
    void Test_Seams_ExceptionIsUnexpectedException();
    void Test_Seams_FailuresChangeNothing();
    void Test_Seams_FileNotFoundIsAMissingFile();
    void Test_Seams_GetTheirArguments();

public:
    TTest_ASWToml_BaseFile();
    ~TTest_ASWToml_BaseFile() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWToml_BaseH
