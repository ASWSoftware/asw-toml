/* **************************************************************************
Test_ASWToml_Interface.h
Author: Anthony S. West - ASW Software

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#ifndef Test_ASWToml_InterfaceH
#define Test_ASWToml_InterfaceH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Interface
//
// The typed GetValue(), TryGetValue(), SetValue() and Append() of IASWToml, and the default Apply(), over a fake
// IASWToml that holds a TASWTomlDocument without locks
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_Interface : public TAutoTestGroup<TTest_ASWToml_Interface>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_Interface> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_Append_ConversionFailureIsReported();
    void Test_Append_ConverterExceptionIsReported();
    void Test_Append_ConvertsTheValue();
    void Test_Append_UsesTheValueOptions();
    void Test_Apply_ConversionFailureChangesNothing();
    void Test_Apply_ExceptionIsReported();
    void Test_Apply_MakesTheEditsByTheOperations();
    void Test_Apply_RemovesAsABatchDoes();
    void Test_Apply_StopsAtTheFirstFailure();
    void Test_Apply_UsesTheValueOptions();
    void Test_GetValue_ConverterExceptionIsThrown();
    void Test_GetValue_ConvertsTheValue();
    void Test_GetValue_InvalidPathGivesTheDefaultAndAnError();
    void Test_GetValue_MissingValueGivesTheDefault();
    void Test_GetValue_RefusedValueGivesTheDefaultAndAWarning();
    void Test_GetValue_StructKeepsTheDefaultsMembers();
    void Test_GetValue_TextDefaultReadsAString();
    void Test_GetValue_UsesTheValueOptions();
    void Test_Helpers_PathFailure();
    void Test_ReportFailure_CopiesEveryField();
    void Test_SetValue_ConversionFailureIsReported();
    void Test_SetValue_ConverterExceptionIsReported();
    void Test_SetValue_ConvertsTheValue();
    void Test_SetValue_DocumentFailureIsReportedOnce();
    void Test_SetValue_TextIsSetAsAString();
    void Test_SetValue_UsesTheValueOptions();
    void Test_TryGetValue_ConverterExceptionIsReturned();
    void Test_TryGetValue_ConvertsTheValue();
    void Test_TryGetValue_FailureKeepsTheValue();
    void Test_TryGetValue_InvalidPathIsInvalidArgument();
    void Test_TryGetValue_MissingValueIsNotFound();
    void Test_TryGetValue_UsesTheValueOptions();

public:
    TTest_ASWToml_Interface();
    ~TTest_ASWToml_Interface() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWToml_InterfaceH
