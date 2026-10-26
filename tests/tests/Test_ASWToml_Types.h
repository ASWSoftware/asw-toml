/* **************************************************************************
Test_ASWToml_Types.h
Author: Anthony S. West - ASW Software

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#ifndef Test_ASWToml_TypesH
#define Test_ASWToml_TypesH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Types
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_Types : public TAutoTestGroup<TTest_ASWToml_Types>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_Types> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_Message_DefaultsToEmptyInfo();
    void Test_Result_DefaultsToOk();
    void Test_Result_FailsForAnyOtherCode();
    void Test_TomlCode_ValuesNeverChange();
    void Test_ToString_LineBreakNames();
    void Test_ToString_MessageWithAllParts();
    void Test_ToString_MessageWithLineButNoColumn();
    void Test_ToString_MessageWithTextOnly();
    void Test_ToString_SeverityNames();
    void Test_ToString_TomlCodeNamesAreUnique();
    void Test_ToString_UnknownValues();

public:
    TTest_ASWToml_Types();
    ~TTest_ASWToml_Types() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWToml_TypesH
