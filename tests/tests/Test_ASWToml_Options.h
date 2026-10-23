/* **************************************************************************
Test_ASWToml_Options.h
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
#ifndef Test_ASWToml_OptionsH
#define Test_ASWToml_OptionsH
//---------------------------------------------------------------------------
#include <source_location>
#include <string>
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------
#include "ASWToml_Options.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_ReadingOptions
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_ReadingOptions : public TAutoTestGroup<TTest_ASWToml_ReadingOptions>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_ReadingOptions> inherited;

private:
    // Checks that 'options' fail Validate() with InvalidOptions and a message that contains 'expectedText'
    void CheckInvalid(ASWToml::TASWTomlReadingOptions const& options, std::string const& expectedText,
        std::string const& msg, std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_Defaults_AreValid();
    void Test_Defaults_MatchDocumentedValues();
    void Test_Validate_AcceptsLargestMaxNestingDepth();
    void Test_Validate_AcceptsSmallestLimits();
    void Test_Validate_RejectsMaxNestingDepthAboveLimit();
    void Test_Validate_RejectsZeroMaxFileSize();
    void Test_Validate_RejectsZeroMaxNestingDepth();

public:
    TTest_ASWToml_ReadingOptions();
    ~TTest_ASWToml_ReadingOptions() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_WritingOptions
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_WritingOptions : public TAutoTestGroup<TTest_ASWToml_WritingOptions>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_WritingOptions> inherited;

private:
    // Checks that 'options' fail Validate() with InvalidOptions and a message that contains 'expectedText'
    void CheckInvalid(ASWToml::TASWTomlWritingOptions const& options, std::string const& expectedText,
        std::string const& msg, std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_Defaults_AreValid();
    void Test_Defaults_MatchDocumentedValues();
    void Test_ToString_EnumNames();
    void Test_Validate_AcceptsCommentPrefixVariants();
    void Test_Validate_RejectsCommentPrefixWithControlCharacter();
    void Test_Validate_RejectsCommentPrefixWithInvalidUtf8();
    void Test_Validate_RejectsCommentPrefixWithoutHash();
    void Test_Validate_RejectsUnknownEnumValues();

public:
    TTest_ASWToml_WritingOptions();
    ~TTest_ASWToml_WritingOptions() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_SavingOptions
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_SavingOptions : public TAutoTestGroup<TTest_ASWToml_SavingOptions>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_SavingOptions> inherited;

private:
    // Checks that 'options' fail Validate() with InvalidOptions and a message that contains 'expectedText'
    void CheckInvalid(ASWToml::TASWTomlSavingOptions const& options, std::string const& expectedText,
        std::string const& msg, std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_Defaults_AreValid();
    void Test_Defaults_MatchDocumentedValues();
    void Test_Validate_ChecksBackupSuffixOnlyWithBackup();

public:
    TTest_ASWToml_SavingOptions();
    ~TTest_ASWToml_SavingOptions() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_ValueOptions
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_ValueOptions : public TAutoTestGroup<TTest_ASWToml_ValueOptions>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_ValueOptions> inherited;

private:
    // Checks that 'options' fail Validate() with InvalidOptions and a message that contains 'expectedText'
    void CheckInvalid(ASWToml::TASWTomlValueOptions const& options, std::string const& expectedText,
        std::string const& msg, std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_Defaults_AreValid();
    void Test_Defaults_MatchDocumentedValues();
    void Test_ToString_EnumNames();
    void Test_Validate_AcceptsEveryEnumValue();
    void Test_Validate_RejectsUnknownEnumValues();

public:
    TTest_ASWToml_ValueOptions();
    ~TTest_ASWToml_ValueOptions() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Options
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_Options : public TAutoTestGroup<TTest_ASWToml_Options>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_Options> inherited;

private:
    // Checks that 'options' fail Validate() with InvalidOptions and a message that contains 'expectedText'
    void CheckInvalid(ASWToml::TASWTomlOptions const& options, std::string const& expectedText,
        std::string const& msg, std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_Defaults_AreValid();
    void Test_Validate_ReportsEachGroupsProblems();
    void Test_Validate_ReportsFirstGroupsProblem();

public:
    TTest_ASWToml_Options();
    ~TTest_ASWToml_Options() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWToml_OptionsH
