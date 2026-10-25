/* **************************************************************************
Test_ASWToml_Converters_Containers.h
Author: Anthony S. West - ASW Software

The tests of ASWToml_Converters' array and table converters (std::vector,
std::array, the sets, std::pair, std::tuple and the maps; see
Test_ASWToml_Converters).

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#ifndef Test_ASWToml_Converters_ContainersH
#define Test_ASWToml_Converters_ContainersH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_ArrayConverters
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_ArrayConverters : public TAutoTestGroup<TTest_ASWToml_ArrayConverters>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_ArrayConverters> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_Array_ReadsExactlyItsLength();
    void Test_Elements_UseTheirTypesConverter();
    void Test_EveryType_RefusesWhatIsntAnArray();
    void Test_Helpers_SortByText();
    void Test_Pair_ReadsTwoElementsOfTheirOwnTypes();
    void Test_RoundTrip_EveryType_ThroughADocument();
    void Test_Set_ReadsEachElementOnce();
    void Test_Set_ReadsWithTheSetsOwnComparator();
    void Test_Set_WritesInItsOrder();
    void Test_ToValue_FailsAtTheFirstElementThatFails();
    void Test_Tuple_ReadsEachElementAsItsOwnType();
    void Test_UnorderedSet_WritesSortedByText();
    void Test_Vector_FromValue_FailsAtTheFirstElementThatFails();
    void Test_Vector_FromValue_ReadsAnyArray();
    void Test_Vector_ToValue_WritesEachElement();

public:
    TTest_ASWToml_ArrayConverters();
    ~TTest_ASWToml_ArrayConverters() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_TableConverters
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_TableConverters : public TAutoTestGroup<TTest_ASWToml_TableConverters>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_TableConverters> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_EnumKeys_ReadByName();
    void Test_EveryType_RefusesWhatIsntATable();
    void Test_FromValue_FailsAtTheFirstKeyThatFails();
    void Test_FromValue_ReadsAnyTable();
    void Test_Helpers_RepeatedEnumKey();
    void Test_IntegerKeys_ReadAsDecimalDigits();
    void Test_IntegerKeys_RefuseWhatIsntANumberInRange();
    void Test_ReadsWithTheMapsOwnComparator();
    void Test_RepeatedKey_FailsTheMap();
    void Test_RoundTrip_EveryType_ThroughADocument();
    void Test_ToValue_FailsAtTheFirstKeyOrValueThatFails();
    void Test_ToValue_WritesInTheMapsOrder();
    void Test_UnorderedMap_WritesKeysSortedByText();

public:
    TTest_ASWToml_TableConverters();
    ~TTest_ASWToml_TableConverters() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWToml_Converters_ContainersH
