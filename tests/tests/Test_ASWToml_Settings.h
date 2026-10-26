/* **************************************************************************
Test_ASWToml_Settings.h
Author: Anthony S. West - ASW Software

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#ifndef Test_ASWToml_SettingsH
#define Test_ASWToml_SettingsH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_SettingsAccess
//
// TASWTomlTable and TASWTomlSetting<T>: Get(), Set() and Reset() through the owner's document, the range, the
// validator, the value options, the comment of a key Set() adds, and what is reported
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_SettingsAccess : public TAutoTestGroup<TTest_ASWToml_SettingsAccess>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_SettingsAccess> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_Get_InvalidValueGivesTheDefault();
    void Test_Get_ReadsTheDocument();
    void Test_New_GivesTheDefaults();
    void Test_OutOfMemory_SetChangesNothing();
    void Test_Paths_ThatCantBeUsedAreErrors();
    void Test_Range_ChecksGetAndSet();
    void Test_Reset_SetsTheDefault();
    void Test_Set_AddsTheKeyWithItsComment();
    void Test_Set_CommentThatCantBeWrittenChangesNothing();
    void Test_Set_DuringASaveKeepsTheDocumentModified();
    void Test_Set_FailuresAreReported();
    void Test_Set_WritesTheDocument();
    void Test_Types_WithoutOperatorLessWork();
    void Test_Validator_ChecksGetAndSet();
    void Test_ValueOptions_AreTheSettingsOwnOrTheOwners();

public:
    TTest_ASWToml_SettingsAccess();
    ~TTest_ASWToml_SettingsAccess() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_SettingsLoad
//
// What Load() and LoadText() do with the settings and tables bound to the object: missing keys added, values that
// aren't valid replaced, comments as the CommentPolicy says, the edits they take, what is reported, and the bindings'
// lifetimes
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_SettingsLoad : public TAutoTestGroup<TTest_ASWToml_SettingsLoad>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_SettingsLoad> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_Bindings_FollowTheirLifetimes();
    void Test_Comments_CodeWinsReplacesTheFilesComments();
    void Test_Comments_EmptyCodeCommentKeepsTheFiles();
    void Test_Comments_FileWinsKeepsTheFilesComments();
    void Test_Comments_WithoutALineOfTheirOwnAreSkipped();
    void Test_Load_AddsMissingKeysInDeclarationOrder();
    void Test_Load_CommentThatCantBeWrittenIsAnError();
    void Test_Load_DeclaredTableThatIsntOneIsReplaced();
    void Test_Load_FailureAppliesNothing();
    void Test_Load_InvalidDefaultIsAnError();
    void Test_Load_KeyWinsOverItsOldKeys();
    void Test_Load_LeavesAValidDocumentAsItIs();
    void Test_Load_MessagesNameTheFileLoaded();
    void Test_Load_MissingFileGetsEverySetting();
    void Test_Load_OptionalKeyIsNotAdded();
    void Test_Load_PathsThatCantBeUsedAreErrors();
    void Test_Load_RenamedKeyIsAppliedAsAnyOther();
    void Test_Load_RenamesAnOldKey();
    void Test_Load_RenamesOldTablesAndArraysOfTables();
    void Test_Load_ReplacesAnInvalidValueWithTheDefault();
    void Test_Load_TablesWithoutSettingsGetTheirComments();
    void Test_Load_WriteFailuresAreErrors();
    void Test_LoadText_AppliesTheSettings();
    void Test_SetOptions_DoesntApplyTheSettings();

public:
    TTest_ASWToml_SettingsLoad();
    ~TTest_ASWToml_SettingsLoad() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWToml_SettingsH
