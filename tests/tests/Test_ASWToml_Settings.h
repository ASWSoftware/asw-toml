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

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWToml_SettingsH
