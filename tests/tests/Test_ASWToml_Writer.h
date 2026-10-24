/* **************************************************************************
Test_ASWToml_Writer.h
Author: Anthony S. West - ASW Software

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#ifndef Test_ASWToml_WriterH
#define Test_ASWToml_WriterH
//---------------------------------------------------------------------------
#include <source_location>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------
#include "ASWToml_Value.h"
#include "ASWToml_Writer.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Writer
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_Writer : public TAutoTestGroup<TTest_ASWToml_Writer>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_Writer> inherited;

private:
    // Checks that 'text', a whole TOML document, parses, and that its root table is 'expected'
    void CheckReadsAs(std::string const& text, ASWToml::TASWTomlValue const& expected, std::string const& msg,
        std::source_location loc = std::source_location::current());

    // Checks that WriteTableBlocks() writes 'table' as the table 'path' with 'style' as 'expected', and that it reads
    // back: as the root table for the root path, else as 'table' at 'path' (a bare key; an element of an array of
    // tables when 'isElement' is set)
    void CheckWritesBlocks(std::string_view path, bool isElement, bool blankLineFirst,
        ASWToml::TASWTomlValue const& table, ASWToml::Detail::TWriteStyle const& style, std::string const& expected,
        std::string const& msg, std::source_location loc = std::source_location::current());

    // Checks that WriteDottedKeys() writes 'table' after 'prefix' (a bare key) with 'style' as 'expected', and that
    // the lines read back as 'table' at 'prefix'
    void CheckWritesDotted(std::string_view prefix, ASWToml::TASWTomlValue const& table,
        ASWToml::Detail::TWriteStyle const& style, std::string const& expected, std::string const& msg,
        std::source_location loc = std::source_location::current());

    // Checks that WriteValueLike() writes 'value' in the form of 'oldText' with 'style' as 'expected', and that it
    // reads back as 'value'
    void CheckWritesLike(ASWToml::TASWTomlValue const& value, std::string_view oldText,
        ASWToml::Detail::TWriteStyle const& style, std::string const& expected, std::string const& msg,
        std::source_location loc = std::source_location::current());

    // Checks that WriteValue() writes 'value' with 'style' as 'expected', and that it reads back as 'value'
    void CheckWritesValue(ASWToml::TASWTomlValue const& value, ASWToml::Detail::TWriteStyle const& style,
        std::string const& expected, std::string const& msg,
        std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_WriteDottedKeys_EmptyTables();
    void Test_WriteDottedKeys_NestedTables();
    void Test_WriteDottedKeys_RejectsWhatCantBeWritten();
    void Test_WriteKeyValueLine_WritesALine();
    void Test_WriteTableBlocks_ArraysOfTables();
    void Test_WriteTableBlocks_BlankLines();
    void Test_WriteTableBlocks_HeadersAndLines();
    void Test_WriteTableBlocks_ImplicitAndEmptyTables();
    void Test_WriteTableBlocks_ReadsBackRandomTables();
    void Test_WriteTableBlocks_RejectsWhatCantBeWritten();
    void Test_WriteValue_ArraysAndInlineTables();
    void Test_WriteValue_NestingLimit();
    void Test_WriteValue_ReadsBackRandomValues();
    void Test_WriteValue_RejectsWhatCantBeWritten();
    void Test_WriteValue_Scalars();
    void Test_WriteValueLike_IntegerBases();
    void Test_WriteValueLike_OtherValues();
    void Test_WriteValueLike_StringForms();

public:
    TTest_ASWToml_Writer();
    ~TTest_ASWToml_Writer() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWToml_WriterH
