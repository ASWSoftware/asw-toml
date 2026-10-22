/* **************************************************************************
Test_ASWToml_Utils.h
Author: Anthony S. West - ASW Software

Copyright 2026 ASW Software

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

************************************************************************** */

//---------------------------------------------------------------------------
#ifndef Test_ASWToml_UtilsH
#define Test_ASWToml_UtilsH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Utils
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_Utils : public TAutoTestGroup<TTest_ASWToml_Utils>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_Utils> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_BomSize_MatchesEachMark();
    void Test_CountCodePoints_CountsEachCharacterOnce();
    void Test_CountCodePoints_InvalidText();
    void Test_DetectBom_FindsEachMark();
    void Test_DetectBom_IgnoresPartialMarks();
    void Test_FindInvalidUtf8_FindsFirstInvalidSequence();
    void Test_IsBlank_SpaceAndTabOnly();
    void Test_IsValidUtf8_AcceptsValidText();
    void Test_IsValidUtf8_RejectsInvalidSequences();
    void Test_LineBreakText_MatchesEachBreak();
    void Test_NativeLineBreak_MatchesPlatform();

public:
    TTest_ASWToml_Utils();
    ~TTest_ASWToml_Utils() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWToml_UtilsH
