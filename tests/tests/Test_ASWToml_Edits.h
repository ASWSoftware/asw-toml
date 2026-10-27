/* **************************************************************************
Test_ASWToml_Edits.h
Author: Anthony S. West - ASW Software

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#ifndef Test_ASWToml_EditsH
#define Test_ASWToml_EditsH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Edits
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_Edits : public TAutoTestGroup<TTest_ASWToml_Edits>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_Edits> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_Add_ConverterGetsTheValueOptions();
    void Test_Add_ConvertsTypedValues();
    void Test_Add_FailedConversionAddsNothing();
    void Test_Add_KeepsEachEditInOrder();
    void Test_Add_TextsAreStrings();
    void Test_Copies_KeepEditsAndResult();

public:
    TTest_ASWToml_Edits();
    ~TTest_ASWToml_Edits() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWToml_EditsH
