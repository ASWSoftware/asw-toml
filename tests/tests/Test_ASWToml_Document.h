/* **************************************************************************
Test_ASWToml_Document.h
Author: Anthony S. West - ASW Software

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

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
#include "ASWToml_Path.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
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
    void Test_Nesting_CountsTheArraysOfTablesAHeaderIsIn();
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
// TTest_ASWToml_DocumentFuzz
//
// Random and mutated texts (each picked by a seed, the same on every platform): each one parses, is written back byte
// for byte and keeps its values, or fails cleanly
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_DocumentFuzz : public TAutoTestGroup<TTest_ASWToml_DocumentFuzz>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_DocumentFuzz> inherited;

private:
    // Checks what Parse() does with 'text' and 'options' ('name' in a message), in a document that holds another
    // text: either it parses, and then the document writes it back byte for byte, Find() gives each value by its path,
    // and written with LF or CRLF line breaks it reads as the same values; or it fails with ParseFailed, InvalidUtf8,
    // ValueOutOfRange, NestingTooDeep, DuplicateKey or UnsupportedEncoding, a message, and a line and column in the
    // text (none for UnsupportedEncoding), and the document keeps its text, values and options. Returns the code.
    ASWToml::ETomlCode CheckParseOrFail(std::string const& text, ASWToml::TASWTomlOptions const& options,
        std::string const& name, std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_MutatedTexts_ParseOrFailCleanly();
    void Test_RandomDocuments_ParseOrFailCleanly();
    void Test_RandomPieces_ParseOrFailCleanly();
    void Test_RandomTexts_AreTheSameEverywhere();

public:
    TTest_ASWToml_DocumentFuzz();
    ~TTest_ASWToml_DocumentFuzz() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DocumentLookups
//
// TASWTomlDocument::GetRoot() and Find(): the document's values, and the value at a path
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_DocumentLookups : public TAutoTestGroup<TTest_ASWToml_DocumentLookups>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_DocumentLookups> inherited;

private:
    // Checks that 'document' has no value at 'path'
    void CheckNotFound(ASWToml::TASWTomlDocument const& document, ASWToml::TASWTomlPath const& path,
        std::string const& msg, std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_Copies_HaveTheirOwnValues();
    void Test_Default_RootIsAnEmptyTable();
    void Test_Find_ArrayElementsByIndex();
    void Test_Find_KeysAndTables();
    void Test_Find_NullWhenThereIsNoSuchValue();
    void Test_GetRoot_HoldsEveryKindOfValue();
    void Test_GetRoot_KeysInTheOrderFirstDefined();
    void Test_Parse_ReplacesTheValues();

public:
    TTest_ASWToml_DocumentLookups();
    ~TTest_ASWToml_DocumentLookups() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DocumentRoundTrip
//
// A corpus of texts of every layout: each reads as its values and is written back byte for byte, also with its line
// breaks converted
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_DocumentRoundTrip : public TAutoTestGroup<TTest_ASWToml_DocumentRoundTrip>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_DocumentRoundTrip> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_Corpus_ConvertedLineBreaksKeepTheValues();
    void Test_Corpus_HasTheExpectedValues();
    void Test_Corpus_WritesBackByteForByte();

public:
    TTest_ASWToml_DocumentRoundTrip();
    ~TTest_ASWToml_DocumentRoundTrip() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DocumentTables
//
// TOML's table rules: the tables headers, dotted keys and inline tables define, and DuplicateKey for each way to
// define a key or table twice
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_DocumentTables : public TAutoTestGroup<TTest_ASWToml_DocumentTables>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_DocumentTables> inherited;

private:
    // Checks that 'text' fails with DuplicateKey at 'line' and 'column', with the message 'expectedMessage'
    void CheckDuplicate(std::string const& text, std::size_t line, std::size_t column,
        std::string const& expectedMessage, std::string const& msg,
        std::source_location loc = std::source_location::current());

    // Checks that 'text' parses, to the values 'expected'
    void CheckRoot(std::string const& text, ASWToml::TASWTomlValue const& expected, std::string const& msg,
        std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_ArraysOfTables_AddAnElementEach();
    void Test_ArraysOfTables_Errors();
    void Test_DottedKeys_DefineTables();
    void Test_DottedKeys_Errors();
    void Test_Errors_PlaceCountsEveryPiece();
    void Test_Headers_DefineTables();
    void Test_Headers_Errors();
    void Test_InlineTables_AreClosed();
    void Test_Keys_DefinedOnce();

public:
    TTest_ASWToml_DocumentTables();
    ~TTest_ASWToml_DocumentTables() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_TomlTest
//
// toml-test's TOML 1.1 files (see tests/toml-test): each valid one reads as its expected values and is written back
// byte for byte, and each invalid one fails
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_TomlTest : public TAutoTestGroup<TTest_ASWToml_TomlTest>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_TomlTest> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_InvalidFiles_Fail();
    void Test_ValidFiles_HaveTheExpectedValues();
    void Test_ValidFiles_WriteBackByteForByte();

public:
    TTest_ASWToml_TomlTest();
    ~TTest_ASWToml_TomlTest() override;

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
