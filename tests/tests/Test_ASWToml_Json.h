/* **************************************************************************
Test_ASWToml_Json.h
Author: Anthony S. West - ASW Software

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#ifndef Test_ASWToml_JsonH
#define Test_ASWToml_JsonH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Json
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_Json : public TAutoTestGroup<TTest_ASWToml_Json>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_Json> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_Depth_ADocumentAtTheLimitIsWritten();
    void Test_Depth_DeeperThanTheLimitFails();
    void Test_Failures_HaveThePathAndChangeNothing();
    void Test_Indent_LaysOutLikeJsonStringify();
    void Test_Indent_ZeroWritesOneLine();
    void Test_Options_InvalidOnesFail();
    void Test_Options_ValidateRefusesWhatCantBeUsed();
    void Test_Plain_CorpusFilesHaveTheirStructure();
    void Test_Plain_EachType();
    void Test_Plain_Escapes();
    void Test_Plain_IntegersAndFloatsAreNumbers();
    void Test_Plain_KeysInTheTablesOrder();
    void Test_Plain_NonFiniteFloats();
    void Test_Plain_RandomValuesHaveTheirStructure();
    void Test_Scalar_RootIsAJsonScalar();
    void Test_Tagged_CorpusFilesHaveTheExpectedValues();
    void Test_Tagged_EachType();
    void Test_Tagged_NonFiniteFloatsAreWritten();
    void Test_Tagged_RandomValuesReadBackIdentical();
    void Test_ToString_NamesEachValue();

public:
    TTest_ASWToml_Json();
    ~TTest_ASWToml_Json() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWToml_JsonH
