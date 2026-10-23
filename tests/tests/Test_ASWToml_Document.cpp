/* **************************************************************************
Test_ASWToml_Document.cpp
Author: Anthony S. West - ASW Software

See header for info.

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
// Module header
#include "Test_ASWToml_Document.h"
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWToml_Parser.h"
#include "ASWToml_Utils.h"
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

using namespace std::string_literals;

using ASWToml::ELineBreak;
using ASWToml::ETomlCode;
using ASWToml::TASWTomlDocument;
using ASWToml::TASWTomlOptions;
using ASWToml::Detail::EEntryKind;
using ASWToml::Detail::EValueKind;
using ASWToml::Detail::TDocBlock;
using ASWToml::Detail::TDocEntry;
using ASWToml::Detail::TDocKeyPart;
using ASWToml::Detail::TDocModel;
using ASWToml::Detail::TDocValue;

using TStringVector = std::vector<std::string>;

// The UTF-8 byte order mark
std::string const Bom = "\xEF\xBB\xBF";

// Valid texts with every construct of the syntax: each parses, and is written back byte for byte
TStringVector const ValidTexts = {
    "",
    "\n",
    "\r\n",
    "   ",
    "# comment",
    "# comment\n",
    "# a\ttab\n",
    "a = 1",
    "a = 1\n",
    "a = 1\r\n",
    "  a=1  # c\n\tb\t=\t2\n",
    "a = 1#c",
    "a.b.c = 1\n",
    "a . b . c = 1\n",
    "\"a b\".'c.d' = 1\n",
    "\"\" = 1\n",
    "1234 = 1\n-_ = 2\n",
    "s = \"basic \\\"q\\\" \\u00E9\"\n",
    "s = 'C:\\path'\n",
    "s = \"\"\"\nmulti\r\nline\"\"\"\n",
    "s = '''\nraw \\n'''\n",
    "i = [0xFF, 0o17, 0b101, -12, +3, 1_000]\n",
    "f = [1.5, -2e10, inf, -nan, 6.02e+23]\n",
    "b = [true, false]\n",
    "d = [1979-05-27T07:32:00Z, 1979-05-27 07:32:00.5-07:00, 1979-05-27T07:32:00, 1979-05-27, 07:32:00, 07:32]\n",
    "key = 1979-05-27 # a date, then a comment\n",
    "a = []\n",
    "a = [ ]\n",
    "a = [1,]\n",
    "a = [[1, 2], [3], []]\n",
    "a = [\n  1, # one\n  2,\n\n  # before three\n  3\n]\n",
    "a = [\r\n  1,\r\n  2\r\n]\r\n",
    "a = [ { x = 1 }, { y = [ 2 ] } ]\n",
    "t = {}\n",
    "t = { }\n",
    "t = { x = 1, y.z = 'a', \"q r\" = [1] }\n",
    "t = { a = { b = { c = 1 } } }\n",
    "t = {\n  x = 1, # c\n  y = 2,\n}\n",
    "t = { x = 1 , y = 2\t,\n}\n",
    "[table]\n",
    "[table]\nkey = 1\n",
    "[ spaced . \"quoted key\" ]  # c\n",
    "  [indented]\n",
    "[a]\n[a.b]\n[c]\n",
    "[[products]]\nname = 1\n\n[[products]]\nname = 2\n",
    "[[ fruits . x ]]\n",
    Bom + "a = 1\n",
    "a = 1\nb = 2\r\nc = 3",
    "a = \"\xC3\xA9\" # \xE2\x82\xAC\n",
    "x = 1\n[t]\nx = 2\n",
};

ASWToml::TASWTomlOptions DepthOptions(std::size_t maxDepth);
std::string Escaped(std::string const& text);
std::string NestedArrays(std::size_t depth);
std::string NestedInlineTables(std::size_t depth);

//---------------------------------------------------------------------------
/*
    DepthOptions

    Default options with Reading.MaxNestingDepth 'maxDepth'
*/
ASWToml::TASWTomlOptions DepthOptions(std::size_t maxDepth)
{
    ASWToml::TASWTomlOptions options;
    options.Reading.MaxNestingDepth = maxDepth;

    return options;
}
//---------------------------------------------------------------------------
/*
    Escaped

    'text' for a message, with line breaks, tabs and other control characters as escapes
*/
std::string Escaped(std::string const& text)
{
    std::string escaped;

    for (char const c : text)
    {
        unsigned char const byte = static_cast<unsigned char>(c);
        if (c == '\n')
            escaped += "\\n";
        else if (c == '\r')
            escaped += "\\r";
        else if (c == '\t')
            escaped += "\\t";
        else if (byte < 0x20 || byte == 0x7F)
            escaped += "\\x" + std::to_string(byte);
        else
            escaped += c;
    }

    return escaped;
}
//---------------------------------------------------------------------------
/*
    NestedArrays

    a = [[...[1]...]] with 'depth' arrays
*/
std::string NestedArrays(std::size_t depth)
{
    return "a = " + std::string(depth, '[') + "1" + std::string(depth, ']') + "\n";
}
//---------------------------------------------------------------------------
/*
    NestedInlineTables

    a = { b = { b = ... 1 } } with 'depth' inline tables
*/
std::string NestedInlineTables(std::size_t depth)
{
    std::string text = "a = ";
    for (std::size_t i = 0; i < depth; ++i)
        text += "{ b = ";

    text += "1";
    for (std::size_t i = 0; i < depth; ++i)
        text += " }";

    return text + "\n";
}
//---------------------------------------------------------------------------

} // namespace


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DocumentModel
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_DocumentModel::TTest_ASWToml_DocumentModel()
    : inherited("ASWToml_DocumentModel_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_DocumentModel::~TTest_ASWToml_DocumentModel()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentModel::CheckParses(std::string const& text, TDocModel& model, std::string const& msg,
    std::source_location loc)
{
    ASWToml::TASWTomlResult const result = ASWToml::Detail::ParseDocument(text, ASWToml::TASWTomlReadingOptions(),
        model);

    CheckTrue(result.IsOk(), msg + ": the text should parse: " + result.Message, loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentModel::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentModel::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentModel::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentModel::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentModel, Arrays_ElementsWithTheirText)
{
    // Arrange
    TDocModel model;

    // Act
    CheckParses("a = [ 1 , 2,\n  3, # c\n]\nb = [1, 2 ]\nc = [ ]\nd = [[1], []]", model, "arrays");

    // Assert
    TDocBlock::TEntryVector const& entries = model.Blocks[0].Entries;
    CheckEquals(std::size_t(4), entries.size(), "four keys");

    TDocValue const& a = entries[0].KeyValue.Value;
    CheckEquals(EValueKind::Array, a.Kind, "a is an array");
    CheckEquals(std::size_t(3), a.Elements.size(), "a has three values");
    CheckEquals(" "s, a.Elements[0].Before, "the blank after '['");
    CheckEquals("1"s, a.Elements[0].Value.Text, "the first value");
    CheckEquals(" "s, a.Elements[0].After, "the blank before the first comma");
    CheckTrue(a.Elements[0].Comma, "a comma after the first value");
    CheckEquals(" "s, a.Elements[1].Before, "the blank after the first comma");
    CheckEquals(""s, a.Elements[1].After, "nothing before the second comma");
    CheckEquals("\n  "s, a.Elements[2].Before, "a line break and indentation before the third value");
    CheckTrue(a.Elements[2].Comma, "a trailing comma");
    CheckEquals(" # c\n"s, a.Close, "everything after the trailing comma is before ']'");

    TDocValue const& b = entries[1].KeyValue.Value;
    CheckEquals(std::size_t(2), b.Elements.size(), "b has two values");
    CheckFalse(b.Elements[1].Comma, "no comma after the last value");
    CheckEquals(""s, b.Elements[1].After, "After is empty without a comma");
    CheckEquals(" "s, b.Close, "the blank after the last value is before ']'");

    TDocValue const& c = entries[2].KeyValue.Value;
    CheckTrue(c.Elements.empty(), "c has no values");
    CheckEquals(" "s, c.Close, "the blank inside an empty array");

    TDocValue const& d = entries[3].KeyValue.Value;
    CheckEquals(std::size_t(2), d.Elements.size(), "d has two arrays");
    CheckEquals(EValueKind::Array, d.Elements[0].Value.Kind, "an array in an array");
    CheckEquals("1"s, d.Elements[0].Value.Elements[0].Value.Text, "the value of the inner array");
    CheckTrue(d.Elements[1].Value.Elements.empty(), "an empty inner array");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentModel, Bom_IsFlaggedAndLeftOutOfTheBlocks)
{
    // Arrange
    TDocModel model;

    // Act
    CheckParses(Bom + "a = 1", model, "a byte order mark");

    // Assert
    CheckTrue(model.HasBom, "the byte order mark should be flagged");
    CheckEquals(""s, model.Blocks[0].Entries[0].KeyValue.Key.Parts[0].Before, "it isn't part of the first line");
    CheckEquals("a"s, model.Blocks[0].Entries[0].KeyValue.Key.Parts[0].Text, "the key");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentModel, EmptyText_HasOnlyTheRootBlock)
{
    // Arrange
    TDocModel model;
    model.HasBom = true;

    // Act
    CheckParses("", model, "an empty text");

    // Assert
    CheckEquals(std::size_t(1), model.Blocks.size(), "one block");
    CheckFalse(model.Blocks[0].HasHeader, "the block without a header");
    CheckTrue(model.Blocks[0].Entries.empty(), "no entries");
    CheckFalse(model.HasBom, "no byte order mark");
    CheckEquals(ELineBreak::None, model.LineBreak, "no line break");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentModel, Entries_BlankCommentAndKeyValueLines)
{
    // Arrange
    TDocModel model;

    // Act
    CheckParses("  \n# c\n  a = 1 # x\r\nb=2", model, "four lines");

    // Assert
    TDocBlock::TEntryVector const& entries = model.Blocks[0].Entries;
    CheckEquals(std::size_t(4), entries.size(), "four entries");

    CheckEquals(EEntryKind::Blank, entries[0].Kind, "a blank line");
    CheckEquals("  "s, entries[0].Text, "its blanks");
    CheckEquals(ELineBreak::LF, entries[0].LineBreak, "its line break");

    CheckEquals(EEntryKind::Comment, entries[1].Kind, "a comment line");
    CheckEquals("# c"s, entries[1].Text, "its comment");

    TDocEntry const& a = entries[2];
    TDocKeyPart const& key = a.KeyValue.Key.Parts[0];
    CheckEquals(EEntryKind::KeyValue, a.Kind, "a key and its value");
    CheckEquals(std::size_t(1), a.KeyValue.Key.Parts.size(), "one key part");
    CheckEquals("  "s, key.Before, "the indentation is before the key");
    CheckEquals("a"s, key.Text, "the key as written");
    CheckEquals("a"s, key.Name, "the key's name");
    CheckEquals(" "s, key.After, "the blank before '='");
    CheckEquals(" "s, a.KeyValue.BeforeValue, "the blank after '='");
    CheckEquals(EValueKind::Scalar, a.KeyValue.Value.Kind, "a scalar");
    CheckEquals("1"s, a.KeyValue.Value.Text, "the value as written");
    CheckTrue(a.KeyValue.Value.Scalar == ASWToml::TASWTomlValue(1), "the value read");
    CheckEquals(" # x"s, a.Text, "the blank and the comment after the value");
    CheckEquals(ELineBreak::CRLF, a.LineBreak, "a CRLF");

    TDocEntry const& b = entries[3];
    CheckEquals(""s, b.KeyValue.Key.Parts[0].Before, "no indentation");
    CheckEquals(""s, b.KeyValue.Key.Parts[0].After, "no blank before '='");
    CheckEquals(""s, b.KeyValue.BeforeValue, "no blank after '='");
    CheckEquals(""s, b.Text, "nothing after the value");
    CheckEquals(ELineBreak::None, b.LineBreak, "the last line has no line break");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentModel, Failure_LeavesTheModelAsItWas)
{
    // Arrange
    TDocModel model;
    CheckParses("a = 1", model, "a valid text");
    TStringVector const failing = { "a = ", "\xFF\xFE" "a = 2", "a = 2\n\xFF", "a = [[2]]" };

    for (std::string const& text : failing)
    {
        // Act
        ASWToml::TASWTomlReadingOptions options;
        options.MaxNestingDepth = 1;
        ASWToml::TASWTomlResult const result = ASWToml::Detail::ParseDocument(text, options, model);

        // Assert
        CheckFalse(result.IsOk(), Escaped(text) + " should fail");
        CheckEquals(std::size_t(1), model.Blocks[0].Entries.size(), Escaped(text) + ": the model should be kept");
        CheckEquals("1"s, model.Blocks[0].Entries[0].KeyValue.Value.Text, Escaped(text) + ": its value");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentModel, Headers_StartBlocks)
{
    // Arrange
    TDocModel model;

    // Act
    CheckParses("a = 1\n[server]  # s\nport = 80\n  [[products]]\n[ x . \"y z\" ]", model, "three headers");

    // Assert
    CheckEquals(std::size_t(4), model.Blocks.size(), "the block without a header and one per header");
    CheckFalse(model.Blocks[0].HasHeader, "the first block has no header");
    CheckEquals(std::size_t(1), model.Blocks[0].Entries.size(), "the key before the first header");

    TDocBlock const& server = model.Blocks[1];
    CheckTrue(server.HasHeader, "a header");
    CheckFalse(server.Header.IsArray, "a table header");
    CheckEquals(""s, server.Header.Indent, "no indentation");
    CheckEquals("server"s, server.Header.Key.Parts[0].Text, "its key");
    CheckEquals("  # s"s, server.Header.Text, "the blanks and comment after it");
    CheckEquals(ELineBreak::LF, server.Header.LineBreak, "its line break");
    CheckEquals(std::size_t(1), server.Entries.size(), "the key under it");

    TDocBlock const& products = model.Blocks[2];
    CheckTrue(products.Header.IsArray, "the header of an element of an array of tables");
    CheckEquals("  "s, products.Header.Indent, "the blanks before '[['");
    CheckTrue(products.Entries.empty(), "no keys");

    TDocBlock const& x = model.Blocks[3];
    TDocKeyPart const& first = x.Header.Key.Parts[0];
    TDocKeyPart const& second = x.Header.Key.Parts[1];
    CheckEquals(std::size_t(2), x.Header.Key.Parts.size(), "a dotted key");
    CheckEquals(" "s, first.Before, "the blank after '['");
    CheckEquals(" "s, first.After, "the blank before the dot");
    CheckEquals(" "s, second.Before, "the blank after the dot");
    CheckEquals("\"y z\""s, second.Text, "a quoted part as written");
    CheckEquals("y z"s, second.Name, "its name");
    CheckEquals(" "s, second.After, "the blank before ']'");
    CheckEquals(ELineBreak::None, x.Header.LineBreak, "the last line has no line break");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentModel, InlineTables_EntriesWithTheirText)
{
    // Arrange
    TDocModel model;

    // Act
    CheckParses("t = { x = 1, y.z = \"a\" }\nu = {\n  x = 1, # c\n}\nv = { x = 1 # c\n , }", model, "inline tables");

    // Assert
    TDocValue const& t = model.Blocks[0].Entries[0].KeyValue.Value;
    CheckEquals(EValueKind::InlineTable, t.Kind, "an inline table");
    CheckEquals(std::size_t(2), t.Entries.size(), "two keys");
    CheckEquals(" "s, t.Entries[0].Before, "the blank after '{'");
    CheckEquals(""s, t.Entries[0].KeyValue.Key.Parts[0].Before, "the key has no blanks before it of its own");
    CheckEquals("x"s, t.Entries[0].KeyValue.Key.Parts[0].Text, "the first key");
    CheckEquals(" "s, t.Entries[0].KeyValue.Key.Parts[0].After, "the blank before '='");
    CheckEquals(" "s, t.Entries[0].KeyValue.BeforeValue, "the blank after '='");
    CheckEquals("1"s, t.Entries[0].KeyValue.Value.Text, "the first value");
    CheckEquals(""s, t.Entries[0].After, "nothing before the comma");
    CheckTrue(t.Entries[0].Comma, "a comma after the first key");
    CheckEquals(std::size_t(2), t.Entries[1].KeyValue.Key.Parts.size(), "a dotted key");
    CheckTrue(t.Entries[1].KeyValue.Value.Scalar == ASWToml::TASWTomlValue("a"), "its value");
    CheckFalse(t.Entries[1].Comma, "no comma after the last key");
    CheckEquals(" "s, t.Close, "the blank before '}'");

    TDocValue const& u = model.Blocks[0].Entries[1].KeyValue.Value;
    CheckEquals("\n  "s, u.Entries[0].Before, "a line break and indentation before the key (TOML 1.1)");
    CheckTrue(u.Entries[0].Comma, "a trailing comma (TOML 1.1)");
    CheckEquals(" # c\n"s, u.Close, "a comment and a line break before '}' (TOML 1.1)");

    TDocValue const& v = model.Blocks[0].Entries[2].KeyValue.Value;
    CheckEquals(" # c\n "s, v.Entries[0].After, "a comment and a line break before the comma (TOML 1.1)");
    CheckEquals(" "s, v.Close, "the blank after the trailing comma");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentModel, Keys_PartsWithTheirBlanks)
{
    // Arrange
    TDocModel model;

    // Act
    CheckParses("a . \"b.c\" .'d'\t= 1", model, "a dotted key with blanks and quoted parts");

    // Assert
    TDocBlock::TEntryVector const& entries = model.Blocks[0].Entries;
    TDocEntry const& entry = entries[0];
    CheckEquals(std::size_t(3), entry.KeyValue.Key.Parts.size(), "three parts");

    TDocKeyPart const& a = entry.KeyValue.Key.Parts[0];
    CheckEquals("a"s, a.Text, "a bare part");
    CheckEquals(" "s, a.After, "the blank before the first dot");

    TDocKeyPart const& bc = entry.KeyValue.Key.Parts[1];
    CheckEquals(" "s, bc.Before, "the blank after the first dot");
    CheckEquals("\"b.c\""s, bc.Text, "a basic string as written");
    CheckEquals("b.c"s, bc.Name, "its name, with the dot");
    CheckEquals(" "s, bc.After, "the blank before the second dot");

    TDocKeyPart const& d = entry.KeyValue.Key.Parts[2];
    CheckEquals(""s, d.Before, "nothing after the second dot");
    CheckEquals("'d'"s, d.Text, "a literal string as written");
    CheckEquals("d"s, d.Name, "its name");
    CheckEquals("\t"s, d.After, "the tab before '='");
    CheckEquals(" "s, entry.KeyValue.BeforeValue, "the blank after '='");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentModel, LineBreak_MostCommonOrFirst)
{
    // Arrange
    struct TCase
    {
        std::string Text;
        ELineBreak Expected;
    };

    std::vector<TCase> const cases = {
        { "a=1\nb=2\r\nc=3\r\n", ELineBreak::CRLF },
        { "a=1\nb=2\nc=3\r\n", ELineBreak::LF },
        { "a=1\r\nb=2\n", ELineBreak::CRLF },
        { "a=1\nb=2\r\n", ELineBreak::LF },
        { "a=1", ELineBreak::None },
        { "s = \"\"\"\r\nx\r\n\"\"\"\n", ELineBreak::CRLF },
    };

    for (TCase const& item : cases)
    {
        // Act
        TDocModel model;
        CheckParses(item.Text, model, Escaped(item.Text));

        // Assert
        CheckEquals(item.Expected, model.LineBreak, Escaped(item.Text) + ": the most common line break, or the first");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentModel, Scalars_TextAndValue)
{
    // Arrange
    TDocModel model;

    // Act
    CheckParses("a = 0xFF\nb = 'C:\\x'\nc = 1979-05-27T07:32:00Z\nd = \"\"\"\nx\"\"\"\ne = -0.0", model, "scalars");

    // Assert
    TDocBlock::TEntryVector const& entries = model.Blocks[0].Entries;
    CheckEquals("0xFF"s, entries[0].KeyValue.Value.Text, "an integer as written");
    CheckTrue(entries[0].KeyValue.Value.Scalar == ASWToml::TASWTomlValue(255), "its value");
    CheckEquals("'C:\\x'"s, entries[1].KeyValue.Value.Text, "a literal string as written");
    CheckTrue(entries[1].KeyValue.Value.Scalar == ASWToml::TASWTomlValue("C:\\x"), "its value");
    CheckEquals("1979-05-27T07:32:00Z"s, entries[2].KeyValue.Value.Text, "an offset date-time as written");
    CheckTrue(entries[2].KeyValue.Value.Scalar.IsOffsetDateTime(), "its type");
    CheckEquals("\"\"\"\nx\"\"\""s, entries[3].KeyValue.Value.Text, "a multi-line string as written");
    CheckTrue(entries[3].KeyValue.Value.Scalar == ASWToml::TASWTomlValue("x"), "its value");

    double const* const zero = entries[4].KeyValue.Value.Scalar.AsFloat();
    CheckTrue(zero != nullptr && *zero == 0.0 && std::signbit(*zero), "-0.0 keeps its sign");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DocumentParse
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_DocumentParse::TTest_ASWToml_DocumentParse()
    : inherited("ASWToml_DocumentParse_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_DocumentParse::~TTest_ASWToml_DocumentParse()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentParse::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentParse::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentParse::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentParse::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentParse, Default_IsEmpty)
{
    // Act
    TASWTomlDocument const document;

    // Assert
    CheckEquals(""s, document.ToText(), "a new document has no text");
    CheckFalse(document.HasBom(), "a new document has no byte order mark");
    CheckEquals(ASWToml::Detail::NativeLineBreak(), document.GetLineBreak(), "the platform's line break");
    CheckEquals(ASWToml::ELineEnding::Auto, document.GetOptions().Writing.LineEnding, "the default options");
    CheckEquals(std::size_t(100), document.GetOptions().Reading.MaxNestingDepth, "the default reading options");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentParse, Parse_AcceptsEveryConstruct)
{
    for (std::string const& text : ValidTexts)
    {
        // Arrange
        TASWTomlDocument document;

        // Act
        ASWToml::TASWTomlResult const result = document.Parse(text);

        // Assert
        CheckTrue(result.IsOk(), "\"" + Escaped(text) + "\" should parse: " + result.Message);
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentParse, Parse_FailureChangesNothing)
{
    // Arrange
    TASWTomlOptions options;
    options.Writing.CommentPrefix = "#";
    TASWTomlDocument document;
    std::string const text = Bom + "a = 1\r\n";
    CheckTrue(document.Parse(text, options).IsOk(), "the first text should parse");

    struct TCase
    {
        std::string Text;
        ETomlCode Code;
    };

    std::vector<TCase> const cases = {
        { "\xFF\xFE" "b = 2", ETomlCode::UnsupportedEncoding },
        { "b = 2\n\xC3", ETomlCode::InvalidUtf8 },
        { "b = 2 3", ETomlCode::ParseFailed },
        { "b = 99999999999999999999", ETomlCode::ValueOutOfRange },
        { NestedArrays(101), ETomlCode::NestingTooDeep },
    };

    for (TCase const& item : cases)
    {
        // Act
        ASWToml::TASWTomlResult const result = document.Parse(item.Text);

        // Assert
        CheckEquals(item.Code, result.Code, Escaped(item.Text) + ": the code");
        CheckEquals(text, document.ToText(), Escaped(item.Text) + ": the text should be kept");
        CheckTrue(document.HasBom(), Escaped(item.Text) + ": the byte order mark should be kept");
        CheckEquals(ELineBreak::CRLF, document.GetLineBreak(), Escaped(item.Text) + ": the line break should be kept");
        CheckEquals("#"s, document.GetOptions().Writing.CommentPrefix, Escaped(item.Text) + ": the options too");
    }

    // Act
    TASWTomlOptions invalid;
    invalid.Reading.MaxNestingDepth = 0;
    ASWToml::TASWTomlResult const result = document.Parse("b = 2", invalid);

    // Assert
    CheckEquals(ETomlCode::InvalidOptions, result.Code, "options that aren't valid");
    CheckEquals(text, document.ToText(), "the text should be kept after options that aren't valid");
    CheckEquals("#"s, document.GetOptions().Writing.CommentPrefix, "the options should be kept too");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentParse, Parse_KeepsBomLineBreakAndOptions)
{
    // Arrange
    TASWTomlOptions options;
    options.Writing.CommentPrefix = "#";
    TASWTomlDocument document;

    // Act
    ASWToml::TASWTomlResult const first = document.Parse(Bom + "a = 1\r\nb = 2\r\nc = 3\n", options);

    // Assert
    CheckTrue(first.IsOk(), "the text should parse: " + first.Message);
    CheckTrue(document.HasBom(), "the byte order mark");
    CheckEquals(ELineBreak::CRLF, document.GetLineBreak(), "the most common line break");
    CheckEquals("#"s, document.GetOptions().Writing.CommentPrefix, "the options given");

    // Act
    ASWToml::TASWTomlResult const second = document.Parse("a = 1");

    // Assert
    CheckTrue(second.IsOk(), "the second text should parse: " + second.Message);
    CheckFalse(document.HasBom(), "no byte order mark now");
    CheckEquals(ASWToml::Detail::NativeLineBreak(), document.GetLineBreak(), "the platform's for a text without any");
    CheckEquals("# "s, document.GetOptions().Writing.CommentPrefix, "the default options now");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentParse, Parse_RejectsInvalidOptions)
{
    // Arrange
    TASWTomlDocument document;
    TASWTomlOptions zero;
    zero.Reading.MaxNestingDepth = 0;
    TASWTomlOptions tooDeep;
    tooDeep.Reading.MaxNestingDepth = ASWToml::TASWTomlReadingOptions::MaxNestingDepthLimit + 1;

    // Act
    ASWToml::TASWTomlResult const zeroResult = document.Parse("a = 1", zero);
    ASWToml::TASWTomlResult const tooDeepResult = document.Parse("a = 1", tooDeep);

    // Assert
    CheckEquals(ETomlCode::InvalidOptions, zeroResult.Code, "a MaxNestingDepth of 0");
    CheckContains(zeroResult.Message, "Reading.MaxNestingDepth is 0", "the message names the option");
    CheckEquals(std::size_t(0), zeroResult.Line, "not about a place in the text");
    CheckEquals(ETomlCode::InvalidOptions, tooDeepResult.Code, "a MaxNestingDepth above the limit");
    CheckEquals(""s, document.ToText(), "nothing should be read");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentParse, Parse_RejectsInvalidUtf8BeforeSyntax)
{
    // Arrange
    TASWTomlDocument document;

    // Act
    // A syntax error on line 1, and a byte that isn't UTF-8 on line 2: the whole text is checked first
    ASWToml::TASWTomlResult const later = document.Parse("a = = 1\n# \xFF");
    ASWToml::TASWTomlResult const afterBom = document.Parse(Bom + "\xFF");

    // Assert
    CheckEquals(ETomlCode::InvalidUtf8, later.Code, "the text isn't UTF-8");
    CheckEquals("The text holds bytes that aren't valid UTF-8."s, later.Message, "the message");
    CheckEquals(std::size_t(2), later.Line, "the line of the byte");
    CheckEquals(std::size_t(3), later.Column, "the column of the byte");
    CheckEquals(ETomlCode::InvalidUtf8, afterBom.Code, "a byte that isn't UTF-8 after a byte order mark");
    CheckEquals(std::size_t(1), afterBom.Column, "the byte order mark isn't counted");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentParse, Parse_RejectsUtf16AndUtf32)
{
    // Arrange
    TStringVector const marks = {
        "\xFF\xFE", "\xFE\xFF", std::string("\xFF\xFE\x00\x00", 4), std::string("\x00\x00\xFE\xFF", 4),
    };

    for (std::string const& mark : marks)
    {
        // Act
        TASWTomlDocument document;
        ASWToml::TASWTomlResult const result = document.Parse(mark + "a = 1");

        // Assert
        CheckEquals(ETomlCode::UnsupportedEncoding, result.Code, Escaped(mark) + ": the code");
        CheckContains(result.Message, "UTF-16 or UTF-32 byte order mark", Escaped(mark) + ": the message");
        CheckEquals(std::size_t(0), result.Line, Escaped(mark) + ": not about a place in the text");
    }
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DocumentErrors
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_DocumentErrors::TTest_ASWToml_DocumentErrors()
    : inherited("ASWToml_DocumentErrors_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_DocumentErrors::~TTest_ASWToml_DocumentErrors()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentErrors::CheckFails(std::string const& text, ETomlCode code, std::size_t line,
    std::size_t column, std::string const& expectedText, std::string const& msg, std::source_location loc)
{
    TASWTomlDocument document;

    ASWToml::TASWTomlResult const result = document.Parse(text);

    std::string const name = msg + " (\"" + Escaped(text) + "\")";
    CheckEquals(code, result.Code, name + ": the code (" + result.Message + ")", loc);
    CheckEquals(line, result.Line, name + ": the line", loc);
    CheckEquals(column, result.Column, name + ": the column", loc);
    CheckContains(result.Message, expectedText, name + ": the message", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentErrors::CheckNestingOk(std::string const& text, std::size_t maxDepth,
    std::string const& msg, std::source_location loc)
{
    TASWTomlDocument document;

    ASWToml::TASWTomlResult const result = document.Parse(text, DepthOptions(maxDepth));

    CheckTrue(result.IsOk(), msg + " (\"" + Escaped(text) + "\") should parse: " + result.Message, loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentErrors::CheckTooDeep(std::string const& text, std::size_t maxDepth, std::size_t line,
    std::size_t column, std::string const& expectedText, std::string const& msg, std::source_location loc)
{
    TASWTomlDocument document;

    ASWToml::TASWTomlResult const result = document.Parse(text, DepthOptions(maxDepth));

    std::string const name = msg + " (\"" + Escaped(text) + "\")";
    CheckEquals(ETomlCode::NestingTooDeep, result.Code, name + ": the code (" + result.Message + ")", loc);
    CheckEquals(line, result.Line, name + ": the line", loc);
    CheckEquals(column, result.Column, name + ": the column", loc);
    CheckContains(result.Message, expectedText, name + ": the message", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentErrors::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentErrors::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentErrors::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentErrors::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentErrors, Arrays_Errors)
{
    // Arrange & Act & Assert
    std::string const commas = "The values of an array must be separated by commas.";
    CheckFails("a = [1 2]", ETomlCode::ParseFailed, 1, 8, commas, "two values without a comma");
    CheckFails("a = [\n  1\n  2\n]", ETomlCode::ParseFailed, 3, 3, commas, "on another line");

    std::string const comma = "A comma in an array must follow a value.";
    CheckFails("a = [,]", ETomlCode::ParseFailed, 1, 6, comma, "only a comma");
    CheckFails("a = [1,,2]", ETomlCode::ParseFailed, 1, 8, comma, "two commas");

    // An unclosed array is reported at its opening bracket
    std::string const close = "The array has no closing ']'.";
    CheckFails("a = [1,\n", ETomlCode::ParseFailed, 1, 5, close, "the end of the text after a comma");
    CheckFails("a = [1 # c ]", ETomlCode::ParseFailed, 1, 5, close, "the bracket is in a comment");
    CheckFails("a = [", ETomlCode::ParseFailed, 1, 5, close, "the end of the text after the bracket");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentErrors, Comments_RejectControlCharacters)
{
    // Arrange & Act & Assert
    CheckFails("a = 1 # \x01", ETomlCode::ParseFailed, 1, 9, "A comment can't hold the control character U+0001.",
        "a control character in an end-of-line comment");
    CheckFails("# \x7F", ETomlCode::ParseFailed, 1, 3, "U+007F", "DEL in a comment line");
    CheckFails("# a\x1B", ETomlCode::ParseFailed, 1, 4, "U+001B", "ESC");
    CheckFails(std::string("# \0", 3), ETomlCode::ParseFailed, 1, 3, "U+0000", "NUL");
    CheckFails("a = [1, # \x08\n]", ETomlCode::ParseFailed, 1, 11, "U+0008", "a comment in an array");
    CheckFails("t = { # \x0C\n}", ETomlCode::ParseFailed, 1, 9, "U+000C", "a comment in an inline table");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentErrors, Headers_Errors)
{
    // Arrange & Act & Assert
    std::string const close = "A table header must end with ']'.";
    CheckFails("[a", ETomlCode::ParseFailed, 1, 3, close, "no closing bracket");
    CheckFails("[a b]", ETomlCode::ParseFailed, 1, 4, close, "two keys without a dot");

    std::string const arrayClose = "The header of an array of tables must end with ']]'.";
    CheckFails("[[a]", ETomlCode::ParseFailed, 1, 4, arrayClose, "one closing bracket");
    CheckFails("[[a] ]", ETomlCode::ParseFailed, 1, 4, arrayClose, "a blank between the closing brackets");

    std::string const key = "A key must be a bare key";
    CheckFails("[]", ETomlCode::ParseFailed, 1, 2, key, "no key");
    CheckFails("[a.]", ETomlCode::ParseFailed, 1, 4, key, "a dot last");
    CheckFails("[ [a] ]", ETomlCode::ParseFailed, 1, 3, key, "a blank between the opening brackets");

    std::string const after = "A table header must be followed by a comment or the end of the line.";
    CheckFails("[a] b", ETomlCode::ParseFailed, 1, 5, after, "a key after the header");
    CheckFails("[a]]", ETomlCode::ParseFailed, 1, 4, after, "a second closing bracket");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentErrors, InlineTables_Errors)
{
    // Arrange & Act & Assert
    CheckFails("t = {x = 1 y = 2}", ETomlCode::ParseFailed, 1, 12,
        "The keys of an inline table must be separated by commas.", "two keys without a comma");

    std::string const comma = "A comma in an inline table must follow a key and its value.";
    CheckFails("t = {,}", ETomlCode::ParseFailed, 1, 6, comma, "only a comma");
    CheckFails("t = {x = 1,,}", ETomlCode::ParseFailed, 1, 12, comma, "two commas");

    CheckFails("t = {x = 1", ETomlCode::ParseFailed, 1, 5, "The inline table has no closing '}'.",
        "no closing brace, reported at the opening one");
    CheckFails("t = {x\n= 1}", ETomlCode::ParseFailed, 1, 7, "A key must be followed by '='",
        "a line break between a key and '='");
    CheckFails("t = {x = }", ETomlCode::ParseFailed, 1, 10, "A value must be a string", "no value");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentErrors, Keys_Errors)
{
    // Arrange & Act & Assert
    std::string const equals = "A key must be followed by '=' and a value, or by '.' and another key.";
    CheckFails("a", ETomlCode::ParseFailed, 1, 2, equals, "a key alone");
    CheckFails("a b = 1", ETomlCode::ParseFailed, 1, 3, equals, "two keys without a dot");
    CheckFails("a\n= 1", ETomlCode::ParseFailed, 1, 2, equals, "a line break between a key and '='");

    std::string const key = "A key must be a bare key (A-Z, a-z, 0-9, '-' and '_') or a quoted string.";
    CheckFails("= 1", ETomlCode::ParseFailed, 1, 1, key, "no key");
    CheckFails("a. = 1", ETomlCode::ParseFailed, 1, 4, key, "a dot last");
    CheckFails("a..b = 1", ETomlCode::ParseFailed, 1, 3, key, "two dots");
    CheckFails("a.\xC3\xA9 = 1", ETomlCode::ParseFailed, 1, 3, key, "a character outside ASCII, unquoted");

    CheckFails("\"a = 1\n", ETomlCode::ParseFailed, 1, 1, "The string has no closing \" on its line.",
        "an unclosed quoted key");
    CheckFails("\"\"\"a\"\"\" = 1", ETomlCode::ParseFailed, 1, 1, "A key can't be a multi-line string",
        "a multi-line string as a key");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentErrors, LineAndColumn_CountLinesAndCharacters)
{
    // Arrange & Act & Assert
    std::string const value = "A value must be a string, a number, true, false, a date or a time.";
    CheckFails("a = 1\n\n  [b]\n  c = \xC3\xA9", ETomlCode::ParseFailed, 4, 7, value, "a later line");

    std::string const after = "A value must be followed by a comment or the end of the line.";
    CheckFails("s = \"\xC3\xA9\" x", ETomlCode::ParseFailed, 1, 9, after, "a character of two bytes counts once");
    CheckFails("\t\ta = 1 2", ETomlCode::ParseFailed, 1, 9, after, "a tab counts once");
    CheckFails("a = 1\r\nb = 2 3", ETomlCode::ParseFailed, 2, 7, after, "a CRLF ends one line");
    CheckFails(Bom + "a = 1 2", ETomlCode::ParseFailed, 1, 7, after, "the byte order mark isn't counted");
    CheckFails("a = 1\n" + Bom, ETomlCode::ParseFailed, 2, 1, "A key must be a bare key",
        "a byte order mark after the start is a character");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentErrors, LineBreaks_RejectLoneCarriageReturns)
{
    // Arrange & Act & Assert
    std::string const cr = "A carriage return must be followed by a line feed.";
    CheckFails("a = 1\r", ETomlCode::ParseFailed, 1, 6, cr, "after a value, at the end");
    CheckFails("a = 1\rb = 2", ETomlCode::ParseFailed, 1, 6, cr, "after a value");
    CheckFails("\r", ETomlCode::ParseFailed, 1, 1, cr, "on its own");
    CheckFails("# c\r", ETomlCode::ParseFailed, 1, 4, cr, "after a comment");
    CheckFails("[a]\r", ETomlCode::ParseFailed, 1, 4, cr, "after a header");
    CheckFails("a = [1,\r]", ETomlCode::ParseFailed, 1, 8, cr, "in an array");
    CheckFails("t = {\r}", ETomlCode::ParseFailed, 1, 6, cr, "in an inline table");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentErrors, Nesting_CountsArraysAndInlineTables)
{
    // Arrange & Act & Assert
    CheckNestingOk("a = [[[1]]]", 3, "three arrays");
    CheckNestingOk("a = {b={c={}}}", 3, "three inline tables");
    CheckNestingOk("a = [{b=[1]}]", 3, "an array in an inline table in an array");
    CheckNestingOk("a = [1, [2], [[3]]]", 3, "the deepest value counts, not the number of arrays");

    CheckTooDeep("a = [[[[1]]]]", 3, 1, 8,
        "The array is nested 4 deep, deeper than Reading.MaxNestingDepth (3) allows.",
        "four arrays, reported at the fourth");
    CheckTooDeep("a = {b={c={d={}}}}", 3, 1, 14, "The inline table is nested 4 deep", "four inline tables");
    CheckTooDeep("a = [{b=[{}]}]", 3, 1, 10, "The inline table is nested 4 deep", "arrays and inline tables");
    CheckTooDeep("a = [[1]]", 1, 1, 6, "The array is nested 2 deep, deeper than Reading.MaxNestingDepth (1) allows.",
        "a MaxNestingDepth of 1");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentErrors, Nesting_CountsTablesOfKeysAndHeaders)
{
    // Arrange & Act & Assert
    // Each part of a dotted key but the last is a table
    CheckNestingOk("a.b.c = [1]", 3, "the tables a and b and an array");
    CheckNestingOk("a.b.c.d = 1", 3, "three tables");
    CheckTooDeep("a.b.c.d = [1]", 3, 1, 11, "The array is nested 4 deep", "three tables and an array");
    CheckTooDeep("a.b.c.d.e = 1", 3, 1, 7, "The table \"d\" is nested 4 deep", "four tables");

    // Each part of a header is a table, and an element of an array of tables is an array and a table
    CheckNestingOk("[a.b.c]", 3, "a header of three tables");
    CheckTooDeep("[a.b.c.d]", 3, 1, 8, "The table \"d\" is nested 4 deep", "a header of four tables");
    CheckNestingOk("[[a.b]]", 3, "the table a, the array b and its element");
    CheckTooDeep("[[a.b.c]]", 3, 1, 7, "The table \"c\" is nested 4 deep", "an array of tables in two tables");

    // The keys under a header are inside its tables
    CheckNestingOk("[a.b]\nc = [1]", 3, "an array under a header of two tables");
    CheckTooDeep("[a.b]\nc.d = [1]", 3, 2, 7, "The array is nested 4 deep", "a dotted key under a header");
    CheckTooDeep("[[a]]\nb = [[1]]", 3, 2, 6, "The array is nested 4 deep", "arrays under an array of tables");

    // And the dotted keys in an inline table inside it
    CheckNestingOk("t = { a.b = [1] }", 3, "an inline table, a table and an array");
    CheckTooDeep("t = { a.b.c.d = 1 }", 3, 1, 11, "The table \"c\" is nested 4 deep",
        "a dotted key in an inline table");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentErrors, Nesting_WorksAtTheLimit)
{
    // Arrange
    // At the largest MaxNestingDepth, a value must be read, copied, written and destroyed in every build, an
    // unoptimized one included, without overflowing the stack
    std::size_t const limit = ASWToml::TASWTomlReadingOptions::MaxNestingDepthLimit;
    TASWTomlOptions const options = DepthOptions(limit);
    std::string const arrays = NestedArrays(limit);
    std::string const tables = NestedInlineTables(limit);

    std::string header = "[a";
    for (std::size_t i = 1; i < limit; ++i)
        header += ".a";

    header += "]\n";

    for (std::string const& text : { arrays, tables, header })
    {
        // Act
        TASWTomlDocument* document = new TASWTomlDocument;
        ASWToml::TASWTomlResult const result = document->Parse(text, options);
        TASWTomlDocument const copy = *document;
        TASWTomlDocument assigned;
        assigned = copy;
        delete document;

        // Assert
        CheckTrue(result.IsOk(), "a text " + std::to_string(limit) + " deep should parse: " + result.Message);
        CheckEquals(text, assigned.ToText(), "it should be written back");
    }

    // Arrange & Act & Assert
    CheckTooDeep(NestedArrays(limit + 1), limit, 1, 5 + limit, "The array is nested 129 deep", "one array more");
    CheckTooDeep("[" + header.substr(1, header.size() - 3) + ".b]", limit, 1, 2 + 2 * limit,
        "The table \"b\" is nested 129 deep", "one table more");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentErrors, Values_Errors)
{
    // Arrange & Act & Assert
    std::string const after = "A value must be followed by a comment or the end of the line.";
    CheckFails("a = 1 2", ETomlCode::ParseFailed, 1, 7, after, "two values");
    CheckFails("a = \"x\" \"y\"", ETomlCode::ParseFailed, 1, 9, after, "two strings");
    CheckFails("a = [1]x", ETomlCode::ParseFailed, 1, 8, after, "a character after an array");
    CheckFails("a = {}x", ETomlCode::ParseFailed, 1, 7, after, "a character after an inline table");

    // The scalar readers' errors
    std::string const noValue = "A value must be a string, a number, true, false, a date or a time.";
    CheckFails("a = ", ETomlCode::ParseFailed, 1, 5, noValue, "no value");
    CheckFails("a = abc", ETomlCode::ParseFailed, 1, 5, "a string must be in quotes", "an unquoted string");
    CheckFails("a = 99999999999999999999", ETomlCode::ValueOutOfRange, 1, 5, "99999999999999999999",
        "an integer beyond 64 bits");
    CheckFails("a = 2023-02-29", ETomlCode::ParseFailed, 1, 13, "the day must be 01 to 28", "a day that doesn't exist");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DocumentToText
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_DocumentToText::TTest_ASWToml_DocumentToText()
    : inherited("ASWToml_DocumentToText_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_DocumentToText::~TTest_ASWToml_DocumentToText()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentToText::CheckWrites(std::string const& text, TASWTomlOptions const& options,
    std::string const& expected, std::string const& msg, std::source_location loc)
{
    TASWTomlDocument document;

    ASWToml::TASWTomlResult const result = document.Parse(text, options);

    CheckTrue(result.IsOk(), msg + ": the text should parse: " + result.Message, loc);
    CheckEquals(Escaped(expected), Escaped(document.ToText()), msg + ": the text written", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentToText::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentToText::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentToText::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentToText::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentToText, Bom_AsTheOptionsSay)
{
    // Arrange
    std::string const text = "a = 1\n";
    TASWTomlOptions automatic;
    TASWTomlOptions always;
    always.Writing.Bom = ASWToml::EBomMode::Always;
    TASWTomlOptions never;
    never.Writing.Bom = ASWToml::EBomMode::Never;

    // Act & Assert
    CheckWrites(text, automatic, text, "Auto, without a byte order mark");
    CheckWrites(Bom + text, automatic, Bom + text, "Auto, with a byte order mark");
    CheckWrites(text, always, Bom + text, "Always adds one");
    CheckWrites(Bom + text, always, Bom + text, "Always keeps one");
    CheckWrites(Bom + text, never, text, "Never removes one");
    CheckWrites(text, never, text, "Never");
    CheckWrites("", always, Bom, "Always, for an empty text");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentToText, Copies_WriteTheSameText)
{
    // Arrange
    std::string const text = "# c\r\n[t]\na = [1, { b = 2 }] # d\n";
    TASWTomlDocument document;
    CheckTrue(document.Parse(text).IsOk(), "the text should parse");

    // Act
    TASWTomlDocument const copied(document);
    TASWTomlDocument assigned;
    assigned = document;
    TASWTomlDocument moved(std::move(assigned));
    TASWTomlDocument moveAssigned;
    moveAssigned = std::move(moved);
    CheckTrue(document.Parse("x = 1").IsOk(), "another text should parse");

    // Assert
    CheckEquals(text, copied.ToText(), "a copy");
    CheckEquals(text, moveAssigned.ToText(), "a copy assigned, moved and move-assigned");
    CheckEquals("x = 1"s, document.ToText(), "the copies are independent of the original");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentToText, Default_WritesNothing)
{
    // Act
    TASWTomlDocument const document;

    // Assert
    CheckEquals(""s, document.ToText(), "an empty text");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentToText, LineEnding_ConvertsEveryLineBreak)
{
    // Arrange
    // Line breaks after keys and headers, in a multi-line string and in an array; no line break at the end
    std::string const text = "a = 1\r\ns = \"\"\"\nx\r\ny\"\"\" # c\n[t]\r\nb = [\n1,\r\n] # d";
    std::string const lf = "a = 1\ns = \"\"\"\nx\ny\"\"\" # c\n[t]\nb = [\n1,\n] # d";
    std::string const crlf = "a = 1\r\ns = \"\"\"\r\nx\r\ny\"\"\" # c\r\n[t]\r\nb = [\r\n1,\r\n] # d";

    TASWTomlOptions automatic;
    TASWTomlOptions toLf;
    toLf.Writing.LineEnding = ASWToml::ELineEnding::LF;
    TASWTomlOptions toCrlf;
    toCrlf.Writing.LineEnding = ASWToml::ELineEnding::CRLF;
    TASWTomlOptions native;
    native.Writing.LineEnding = ASWToml::ELineEnding::Native;

    // Act & Assert
    CheckWrites(text, automatic, text, "Auto keeps each line break");
    CheckWrites(text, toLf, lf, "LF");
    CheckWrites(text, toCrlf, crlf, "CRLF");
    CheckWrites(text, native, ASWToml::Detail::NativeLineBreak() == ELineBreak::CRLF ? crlf : lf, "Native");

    // The multi-line string reads the same whatever its line breaks are
    for (std::string const& converted : { lf, crlf })
    {
        TDocModel model;
        ASWToml::TASWTomlResult const result = ASWToml::Detail::ParseDocument(converted,
            ASWToml::TASWTomlReadingOptions(), model);

        CheckTrue(result.IsOk(), Escaped(converted) + " should parse: " + result.Message);
        CheckTrue(model.Blocks[0].Entries[1].KeyValue.Value.Scalar == ASWToml::TASWTomlValue("x\ny"),
            Escaped(converted) + ": the multi-line string");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentToText, RoundTrip_ByteForByte)
{
    for (std::string const& text : ValidTexts)
    {
        // Arrange & Act & Assert
        CheckWrites(text, TASWTomlOptions(), text, "\"" + Escaped(text) + "\"");
    }
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DocumentErrors, "Document")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DocumentModel, "Document")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DocumentParse, "Document")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DocumentToText, "Document")
