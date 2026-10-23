/* **************************************************************************
Test_ASWToml_Value.h
Author: Anthony S. West - ASW Software

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#ifndef Test_ASWToml_ValueH
#define Test_ASWToml_ValueH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DateTimes
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_DateTimes : public TAutoTestGroup<TTest_ASWToml_DateTimes>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_DateTimes> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_LocalDateTime_DefaultsToEpoch();
    void Test_LocalDateTime_FromLocalTime_SplitsDateAndTime();
    void Test_LocalDateTime_IsValid_ChecksDateAndTime();
    void Test_LocalDateTime_ToLocalTime_AddsDateAndTime();
    void Test_LocalDateTime_ToLocalTime_RefusesOutOfRange();
    void Test_LocalTime_DefaultsToMidnight();
    void Test_LocalTime_FromDuration_SplitsAndWraps();
    void Test_LocalTime_IsValid_ChecksEachField();
    void Test_LocalTime_ToDuration_AddsFields();
    void Test_OffsetDateTime_Equality_ComparesFields();
    void Test_OffsetDateTime_FromSysTime_AppliesOffset();
    void Test_OffsetDateTime_IsValid_ChecksOffset();
    void Test_OffsetDateTime_ToSysTime_SubtractsOffset();

public:
    TTest_ASWToml_DateTimes();
    ~TTest_ASWToml_DateTimes() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_ValueModel
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_ValueModel : public TAutoTestGroup<TTest_ASWToml_ValueModel>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_ValueModel> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_Array_Append();
    void Test_As_ReturnsNullForOtherTypes();
    void Test_Assignment_IsDeep();
    void Test_Constructors_SetEachType();
    void Test_Copy_IsDeep();
    void Test_Default_IsEmptyTable();
    void Test_Equality_ComparesTypeAndValue();
    void Test_Equality_TablesIgnoreOrder();
    void Test_Table_SetFindRemove();
    void Test_TomlType_ValuesNeverChange();
    void Test_ToString_TomlTypeNames();

public:
    TTest_ASWToml_ValueModel();
    ~TTest_ASWToml_ValueModel() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWToml_ValueH
