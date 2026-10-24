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
#include "ASWToml_Index.h"
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
    void Test_Modified_ClearUntilAnEditChangesTheText();
    void Test_Parse_AcceptsEveryConstruct();
    void Test_Parse_FailureChangesNothing();
    void Test_Parse_KeepsBomLineBreakAndOptions();
    void Test_Parse_RejectsInvalidOptions();
    void Test_Parse_RejectsInvalidUtf8BeforeSyntax();
    void Test_Parse_RejectsUtf16AndUtf32();
    void Test_Reset_EmptiesTheDocument();
    void Test_Reset_RejectsInvalidOptions();
    void Test_SetOptions_FailureChangesNothing();
    void Test_SetOptions_ReadsTheTextAgain();

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
// TTest_ASWToml_DocumentIndex
//
// Detail::BuildIndex() and FindNode(): the tree of tables with where each piece is written, which the edits use
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_DocumentIndex : public TAutoTestGroup<TTest_ASWToml_DocumentIndex>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_DocumentIndex> inherited;

private:
    // Checks that 'text' parses into a model and that its index builds, into 'index'
    void CheckIndexes(std::string const& text, ASWToml::Detail::TDocIndex& index, std::string const& msg,
        std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_BuildIndex_FailureLeavesTheIndexAsItWas();
    void Test_FindNode_FindsNodesByPath();
    void Test_Lines_KindsAndRanges();
    void Test_Lines_StartEachBlock();
    void Test_Names_EveryKeyPartThatNamesANode();
    void Test_Nodes_TheLinesAndBlocksTheyAreDefinedIn();
    void Test_Values_ItemsAndClosingBrackets();

public:
    TTest_ASWToml_DocumentIndex();
    ~TTest_ASWToml_DocumentIndex() override;

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


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DocumentSetValue
//
// TASWTomlDocument::SetValue(): values changed in place, and new keys and tables in their places and forms
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_DocumentSetValue : public TAutoTestGroup<TTest_ASWToml_DocumentSetValue>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_DocumentSetValue> inherited;

private:
    // Checks that SetValue() of 'path' to 'value' in a document of 'text' fails with 'code' and a message that holds
    // 'expectedText', and changes nothing
    void CheckFails(std::string const& text, ASWToml::TASWTomlPath const& path, ASWToml::TASWTomlValue const& value,
        ASWToml::ETomlCode code, std::string const& expectedText, std::string const& msg,
        std::source_location loc = std::source_location::current());

    // Checks that SetValue() of 'path' to 'value' (with 'options' for the document) in a document of 'text' succeeds,
    // gives the text 'expected' and the value at 'path', sets the modified flag, and that the text reads as the
    // document's values
    void CheckSets(std::string const& text, ASWToml::TASWTomlPath const& path, ASWToml::TASWTomlValue const& value,
        std::string const& expected, std::string const& msg,
        ASWToml::TASWTomlOptions const& options = ASWToml::TASWTomlOptions(),
        std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_ExistingScalars_KeepTheirForm();
    void Test_ExistingValues_ArraysAndTablesOnOneLine();
    void Test_IdenticalValue_ChangesNothing();
    void Test_LineBreaks_TheDocumentsOwn();
    void Test_NewKeys_AsDottedKeys();
    void Test_NewKeys_InHeaderTablesAndElements();
    void Test_NewKeys_InInlineTables();
    void Test_NewKeys_InTheRoot();
    void Test_NewTables_AsHeaders();
    void Test_NewTables_BlankLinesAsTheOptionsSay();
    void Test_RejectsWhatCantBeSet();
    void Test_ValueOptions_PerCall();

public:
    TTest_ASWToml_DocumentSetValue();
    ~TTest_ASWToml_DocumentSetValue() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DocumentAppend
//
// TASWTomlDocument::Append(): a new element of an array value or of an array of tables
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_DocumentAppend : public TAutoTestGroup<TTest_ASWToml_DocumentAppend>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_DocumentAppend> inherited;

private:
    // Checks that Append() of 'value' to 'path' in a document of 'text' succeeds, gives the text 'expected' and the
    // value at the array's end, sets the modified flag, and that the text reads as the document's values
    void CheckAppends(std::string const& text, ASWToml::TASWTomlPath const& path, ASWToml::TASWTomlValue const& value,
        std::string const& expected, std::string const& msg,
        std::source_location loc = std::source_location::current());

    // Checks that Append() of 'value' to 'path' in a document of 'text' fails with 'code' and a message that holds
    // 'expectedText', and changes nothing
    void CheckFails(std::string const& text, ASWToml::TASWTomlPath const& path, ASWToml::TASWTomlValue const& value,
        ASWToml::ETomlCode code, std::string const& expectedText, std::string const& msg,
        std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_ArraysOfTables_NewElementAfterTheLast();
    void Test_ArrayValues_InTheirLayout();
    void Test_MissingKeys_GetAnArray();
    void Test_RejectsWhatCantBeAppended();

public:
    TTest_ASWToml_DocumentAppend();
    ~TTest_ASWToml_DocumentAppend() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DocumentRemove
//
// TASWTomlDocument::Remove(): keys, elements, entries, tables and arrays of tables, each with the text that defines it
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_DocumentRemove : public TAutoTestGroup<TTest_ASWToml_DocumentRemove>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_DocumentRemove> inherited;

private:
    // Checks that Remove() of 'path' in a document of 'text' returns false and changes nothing
    void CheckKeeps(std::string const& text, ASWToml::TASWTomlPath const& path, std::string const& msg,
        std::source_location loc = std::source_location::current());

    // Checks that Remove() of 'path' in a document of 'text' returns true, gives the text 'expected', sets the
    // modified flag, and that the text reads as the document's values
    void CheckRemoves(std::string const& text, ASWToml::TASWTomlPath const& path, std::string const& expected,
        std::string const& msg, std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_ArrayElements_OnLinesOfTheirOwn();
    void Test_ArrayElements_WithOneComma();
    void Test_ArraysOfTables_ElementsWithTheirTables();
    void Test_BlankLines_DontPileUp();
    void Test_EmptyTables_GoWhenNothingDefinesThem();
    void Test_InlineTableEntries_WithOneComma();
    void Test_KeyLines_WithTheirComments();
    void Test_LineBreaks_AsTheTextHasThem();
    void Test_NothingThere_ReturnsFalse();
    void Test_Tables_WithTheTablesUnderThem();

public:
    TTest_ASWToml_DocumentRemove();
    ~TTest_ASWToml_DocumentRemove() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DocumentRename
//
// TASWTomlDocument::Rename(): a key or table under a new name, in every place that names it
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_DocumentRename : public TAutoTestGroup<TTest_ASWToml_DocumentRename>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_DocumentRename> inherited;

private:
    // Checks that Rename() of 'path' to 'newKey' in a document of 'text' fails with 'code' and a message that holds
    // 'expectedText', and changes nothing
    void CheckFails(std::string const& text, ASWToml::TASWTomlPath const& path, std::string const& newKey,
        ASWToml::ETomlCode code, std::string const& expectedText, std::string const& msg,
        std::source_location loc = std::source_location::current());

    // Checks that Rename() of 'path' to 'newKey' in a document of 'text' succeeds, gives the text 'expected' and the
    // value that was at 'path' at 'newPath' (and nothing at 'path'), sets the modified flag, and that the text reads
    // as the document's values
    void CheckRenames(std::string const& text, ASWToml::TASWTomlPath const& path, std::string const& newKey,
        ASWToml::TASWTomlPath const& newPath, std::string const& expected, std::string const& msg,
        std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_Keys_OnlyTheNameChanges();
    void Test_NewName_BareBasicOrLiteral();
    void Test_RejectsWhatCantBeRenamed();
    void Test_SameName_ChangesNothing();
    void Test_Tables_EveryPlaceThatNamesThem();

public:
    TTest_ASWToml_DocumentRename();
    ~TTest_ASWToml_DocumentRename() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWToml_DocumentH
