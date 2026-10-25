/* **************************************************************************
Test_ASWToml_Converters_Structs.h
Author: Anthony S. West - ASW Software

The tests of ASWToml_Converters' struct converter (see
Test_ASWToml_Converters).

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#ifndef Test_ASWToml_Converters_StructsH
#define Test_ASWToml_Converters_StructsH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_StructConverters
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_StructConverters : public TAutoTestGroup<TTest_ASWToml_StructConverters>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_StructConverters> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_EveryType_RefusesWhatIsntATable();
    void Test_Fields_OfABaseClassAndPrivateMembers();
    void Test_FromValue_FailsAtTheFirstFieldThatFails();
    void Test_FromValue_ReadsEachFieldIntoACopy();
    void Test_OptionalStruct_ReadsIntoTheValueItHolds();
    void Test_RoundTrip_EveryType_ThroughADocument();
    void Test_ToValue_FailsForAKeyOrValueThatCantBeWritten();
    void Test_ToValue_WritesEveryFieldInTheListsOrder();

public:
    TTest_ASWToml_StructConverters();
    ~TTest_ASWToml_StructConverters() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWToml_Converters_StructsH
