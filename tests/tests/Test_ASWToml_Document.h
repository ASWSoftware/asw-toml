/* **************************************************************************
Test_ASWToml_Document.h
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
#ifndef Test_ASWToml_DocumentH
#define Test_ASWToml_DocumentH
//---------------------------------------------------------------------------
#include <cstddef>
#include <source_location>
#include <string>
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------
#include "ASWToml_Document.h"
#include "ASWToml_Options.h"
#include "ASWToml_Types.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DocumentModel
//
// The pieces Detail::ParseDocument() splits a text into
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_DocumentModel : public TAutoTestGroup<TTest_ASWToml_DocumentModel>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_DocumentModel> inherited;

private:
    // Checks that 'text' parses, into 'model'
    void CheckParses(std::string const& text, ASWToml::Detail::TDocModel& model, std::string const& msg,
        std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_Arrays_ElementsWithTheirText();
    void Test_Bom_IsFlaggedAndLeftOutOfTheBlocks();
    void Test_EmptyText_HasOnlyTheRootBlock();
    void Test_Entries_BlankCommentAndKeyValueLines();
    void Test_Failure_LeavesTheModelAsItWas();
    void Test_Headers_StartBlocks();
    void Test_InlineTables_EntriesWithTheirText();
    void Test_Keys_PartsWithTheirBlanks();
    void Test_LineBreak_MostCommonOrFirst();
    void Test_Scalars_TextAndValue();

public:
    TTest_ASWToml_DocumentModel();
    ~TTest_ASWToml_DocumentModel() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DocumentParse
//
// TASWTomlDocument::Parse(): what it accepts and what it keeps
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_DocumentParse : public TAutoTestGroup<TTest_ASWToml_DocumentParse>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_DocumentParse> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_Default_IsEmpty();
    void Test_Parse_AcceptsEveryConstruct();
    void Test_Parse_FailureChangesNothing();
    void Test_Parse_KeepsBomLineBreakAndOptions();
    void Test_Parse_RejectsInvalidOptions();
    void Test_Parse_RejectsInvalidUtf8BeforeSyntax();
    void Test_Parse_RejectsUtf16AndUtf32();

public:
    TTest_ASWToml_DocumentParse();
    ~TTest_ASWToml_DocumentParse() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DocumentErrors
//
// The code, place and message of each kind of error, and the nesting limit
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_DocumentErrors : public TAutoTestGroup<TTest_ASWToml_DocumentErrors>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_DocumentErrors> inherited;

private:
    // Checks that 'text' fails to parse with 'code' at 'line' and 'column', with a message holding 'expectedText'
    void CheckFails(std::string const& text, ASWToml::ETomlCode code, std::size_t line, std::size_t column,
        std::string const& expectedText, std::string const& msg,
        std::source_location loc = std::source_location::current());

    // Checks that 'text' parses with Reading.MaxNestingDepth 'maxDepth'
    void CheckNestingOk(std::string const& text, std::size_t maxDepth, std::string const& msg,
        std::source_location loc = std::source_location::current());

    // Checks that 'text' fails with NestingTooDeep at 'line' and 'column' with Reading.MaxNestingDepth 'maxDepth',
    // with a message holding 'expectedText'
    void CheckTooDeep(std::string const& text, std::size_t maxDepth, std::size_t line, std::size_t column,
        std::string const& expectedText, std::string const& msg,
        std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_Arrays_Errors();
    void Test_Comments_RejectControlCharacters();
    void Test_Headers_Errors();
    void Test_InlineTables_Errors();
    void Test_Keys_Errors();
    void Test_LineAndColumn_CountLinesAndCharacters();
    void Test_LineBreaks_RejectLoneCarriageReturns();
    void Test_Nesting_CountsArraysAndInlineTables();
    void Test_Nesting_CountsTablesOfKeysAndHeaders();
    void Test_Nesting_WorksAtTheLimit();
    void Test_Values_Errors();

public:
    TTest_ASWToml_DocumentErrors();
    ~TTest_ASWToml_DocumentErrors() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DocumentToText
//
// TASWTomlDocument::ToText(): the text back byte for byte, and the writing options' byte order mark and line endings
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_DocumentToText : public TAutoTestGroup<TTest_ASWToml_DocumentToText>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_DocumentToText> inherited;

private:
    // Checks that 'text' parses with 'options' and that ToText() gives 'expected'
    void CheckWrites(std::string const& text, ASWToml::TASWTomlOptions const& options, std::string const& expected,
        std::string const& msg, std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_Bom_AsTheOptionsSay();
    void Test_Copies_WriteTheSameText();
    void Test_Default_WritesNothing();
    void Test_LineEnding_ConvertsEveryLineBreak();
    void Test_RoundTrip_ByteForByte();

public:
    TTest_ASWToml_DocumentToText();
    ~TTest_ASWToml_DocumentToText() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWToml_DocumentH
