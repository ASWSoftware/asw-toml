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
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWToml_Parser.h"
#include "ASWToml_Scalars.h"
#include "ASWToml_Utils.h"
#include "ASWToml_Value.h"
#include "TomlTest_Corpus.h"
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
using ASWToml::TASWTomlValue;
using ASWToml::Detail::EEntryKind;
using ASWToml::Detail::EValueKind;
using ASWToml::Detail::TDocBlock;
using ASWToml::Detail::TDocEntry;
using ASWToml::Detail::TDocKeyPart;
using ASWToml::Detail::TDocModel;
using ASWToml::Detail::TDocValue;

using TStringVector = std::vector<std::string>;

// An array and a table, to write the values a document should have
using TArray = TASWTomlValue::TValueVector;
using TTable = TASWTomlValue::TKeyValueVector;

// The expected values of a toml-test file: each path's type and value, as the corpus writes them
using TExpectedMap = std::map<std::string, std::pair<std::string, std::string> >;

// The values of a document by their paths, in the corpus's form
using TValueMap = std::map<std::string, TASWTomlValue const*>;

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
    "[x.y.z.w]\n[x]\n",
    "a.b.c = 1\n[a.b.d]\n",
    "[[a]]\n[a.b]\n[[a]]\n[a.b]\n",
    "[a.b.c]\n[a]\nb.d = 1\n",
    "[fruit]\napple.color = 1\n[fruit.apple.texture]\n",
};

// A text with values of every kind, for the lookups
std::string const LookupText =
    "title = \"x\"\n"
    "a.b.c = 1\n"
    "ports = [80, 443, 8080]\n"
    "m = [[1, 2], [3]]\n"
    "[server]\n"
    "host = \"h\"\n"
    "\"port no\" = 80\n"
    "[server.http]\n"
    "on = true\n"
    "[t]\n"
    "\"a.b\" = 2\n"
    "[[products]]\n"
    "name = \"p0\"\n"
    "[[products]]\n"
    "name = \"p1\"\n"
    "tags = [{ k = 1 }]\n";

std::string CorpusEscaped(std::string_view text);
char const* CorpusType(TASWTomlValue const& value) noexcept;
ASWToml::TASWTomlOptions DepthOptions(std::size_t maxDepth);
std::string Escaped(std::string const& text);
TExpectedMap ExpectedValues(std::string_view expected);
void FlattenValues(TASWTomlValue const& value, std::string const& path, TValueMap& values);
std::string KeyList(TASWTomlValue const& value);
std::string NestedArrays(std::size_t depth);
std::string NestedArraysOfTables(std::size_t count);
std::string NestedInlineTables(std::size_t depth);
bool SameValue(std::string const& expected, TASWTomlValue const& value);
std::string ValuesDiffer(ASWUnitTests::TTomlTestFile const& file, TASWTomlValue const& root);

//---------------------------------------------------------------------------
/*
    CorpusEscaped

    'text' escaped as the toml-test corpus writes keys and strings (see generate_corpus.py): a backslash, a double
    quote, a tab, a line feed and a carriage return as \\, \", \t, \n and \r, every other control character as \xHH
*/
std::string CorpusEscaped(std::string_view text)
{
    char const* const hex = "0123456789ABCDEF";
    std::string escaped;

    for (char const c : text)
    {
        unsigned char const byte = static_cast<unsigned char>(c);
        if (c == '\\')
        {
            escaped += "\\\\";
        }
        else if (c == '"')
        {
            escaped += "\\\"";
        }
        else if (c == '\t')
        {
            escaped += "\\t";
        }
        else if (c == '\n')
        {
            escaped += "\\n";
        }
        else if (c == '\r')
        {
            escaped += "\\r";
        }
        else if (byte < 0x20 || byte == 0x7F)
        {
            escaped += "\\x";
            escaped += hex[byte >> 4];
            escaped += hex[byte & 0x0F];
        }
        else
        {
            escaped += c;
        }
    }

    return escaped;
}
//---------------------------------------------------------------------------
/*
    CorpusType

    The toml-test name of the type of 'value', e.g. datetime-local
*/
char const* CorpusType(TASWTomlValue const& value) noexcept
{
    switch (value.GetType())
    {
        case ASWToml::ETomlType::String:
            return "string";

        case ASWToml::ETomlType::Integer:
            return "integer";

        case ASWToml::ETomlType::Float:
            return "float";

        case ASWToml::ETomlType::Boolean:
            return "bool";

        case ASWToml::ETomlType::OffsetDateTime:
            return "datetime";

        case ASWToml::ETomlType::LocalDateTime:
            return "datetime-local";

        case ASWToml::ETomlType::LocalDate:
            return "date-local";

        case ASWToml::ETomlType::LocalTime:
            return "time-local";

        case ASWToml::ETomlType::Array:
            return "array";

        case ASWToml::ETomlType::Table:
            return "table";
    }

    return "unknown";
}
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
    ExpectedValues

    The lines of a toml-test file's expected values (path TAB type TAB value), by path
*/
TExpectedMap ExpectedValues(std::string_view expected)
{
    TExpectedMap values;

    while (!expected.empty())
    {
        std::size_t const end = expected.find('\n');
        std::string_view const line = expected.substr(0, end);
        expected = end == std::string_view::npos ? std::string_view() : expected.substr(end + 1);

        std::size_t const tab = line.find('\t');
        std::size_t const secondTab = line.find('\t', tab + 1);
        values[std::string(line.substr(0, tab))] = { std::string(line.substr(tab + 1, secondTab - tab - 1)),
            std::string(line.substr(secondTab + 1)) };
    }

    return values;
}
//---------------------------------------------------------------------------
/*
    FlattenValues

    Adds 'value', at 'path' in the corpus's form, and the values in it to 'values'
*/
void FlattenValues(TASWTomlValue const& value, std::string const& path, TValueMap& values)
{
    values[path] = &value;

    if (TTable const* const table = value.AsTable())
    {
        for (ASWToml::TASWTomlKeyValue const& keyValue : *table)
            FlattenValues(keyValue.Value, path + ".\"" + CorpusEscaped(keyValue.Key) + "\"", values);
    }
    else if (TArray const* const array = value.AsArray())
    {
        for (std::size_t i = 0; i < array->size(); ++i)
            FlattenValues((*array)[i], path + "[" + std::to_string(i) + "]", values);
    }
}
//---------------------------------------------------------------------------
/*
    KeyList

    The keys of the table 'value' in order, separated by spaces; empty when it isn't a table
*/
std::string KeyList(TASWTomlValue const& value)
{
    std::string list;

    if (TTable const* const table = value.AsTable())
    {
        for (ASWToml::TASWTomlKeyValue const& keyValue : *table)
            list += (list.empty() ? "" : " ") + keyValue.Key;
    }

    return list;
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
    NestedArraysOfTables

    [[a]], [[a.a]] and so on, 'count' headers, each an array of tables in the last element of the one before; the
    last element is 2 * 'count' deep
*/
std::string NestedArraysOfTables(std::size_t count)
{
    std::string text;
    std::string key = "a";

    for (std::size_t i = 0; i < count; ++i)
    {
        text += "[[" + key + "]]\n";
        key += ".a";
    }

    return text;
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
/*
    SameValue

    Whether 'value' is the value the corpus writes as 'expected' for a value of its type: a table's or array's size, a
    string escaped, an integer or float compared as a number (NaN with NaN, and a zero's sign too), a boolean's word,
    and a date or time read by the library's reader and compared as a value
*/
bool SameValue(std::string const& expected, TASWTomlValue const& value)
{
    char const* const first = expected.data();
    char const* const last = expected.data() + expected.size();

    if (TTable const* const table = value.AsTable())
        return expected == std::to_string(table->size());

    if (TArray const* const array = value.AsArray())
        return expected == std::to_string(array->size());

    if (std::string const* const text = value.AsString())
        return expected == CorpusEscaped(*text);

    if (bool const* const flag = value.AsBoolean())
        return expected == (*flag ? "true" : "false");

    if (std::int64_t const* const integer = value.AsInteger())
    {
        std::int64_t number = 0;
        std::from_chars_result const read = std::from_chars(first, last, number);

        return read.ec == std::errc() && read.ptr == last && number == *integer;
    }

    if (double const* const real = value.AsFloat())
    {
        double number = 0.0;
        std::from_chars_result const read = std::from_chars(first, last, number);
        if (read.ec != std::errc() || read.ptr != last)
            return false;

        if (std::isnan(number) || std::isnan(*real))
            return std::isnan(number) && std::isnan(*real);

        return number == *real && std::signbit(number) == std::signbit(*real);
    }

    std::size_t position = 0;
    TASWTomlValue read;
    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadDateTime(expected, position, read);

    return result.IsOk() && position == expected.size() && read == value;
}
//---------------------------------------------------------------------------
/*
    ValuesDiffer

    How the values 'root' holds differ from the expected values of the toml-test file 'file', one sentence per path
    that differs; empty when they don't
*/
std::string ValuesDiffer(ASWUnitTests::TTomlTestFile const& file, TASWTomlValue const& root)
{
    TExpectedMap const expected = ExpectedValues(file.Expected);
    TValueMap actual;
    FlattenValues(root, std::string(), actual);

    std::string differences;
    for (TExpectedMap::value_type const& item : expected)
    {
        std::string const& type = item.second.first;
        std::string const& value = item.second.second;
        TValueMap::const_iterator const found = actual.find(item.first);

        if (found == actual.end())
            differences += " " + item.first + " is missing (expected: " + type + " " + value + ").";
        else if (type != CorpusType(*found->second))
            differences += " " + item.first + " is a " + CorpusType(*found->second) + ", not a " + type + ".";
        else if (!SameValue(value, *found->second))
            differences += " " + item.first + " isn't the " + type + " " + value + ".";
    }

    for (TValueMap::value_type const& item : actual)
    {
        if (expected.find(item.first) == expected.end())
            differences += " " + item.first + " (a " + CorpusType(*item.second) + ") isn't expected.";
    }

    return differences;
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
        { "b = 2\nb = 3", ETomlCode::DuplicateKey },
        { NestedArraysOfTables(51), ETomlCode::NestingTooDeep },
    };

    for (TCase const& item : cases)
    {
        // Act
        ASWToml::TASWTomlResult const result = document.Parse(item.Text);

        // Assert
        CheckEquals(item.Code, result.Code, Escaped(item.Text) + ": the code");
        CheckEquals(text, document.ToText(), Escaped(item.Text) + ": the text should be kept");
        CheckTrue(document.GetRoot() == TASWTomlValue(TTable{ { "a", 1 } }), Escaped(item.Text) + ": the values too");
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
ASW_TEST(TTest_ASWToml_DocumentErrors, Nesting_CountsTheArraysOfTablesAHeaderIsIn)
{
    // Arrange & Act & Assert
    // [a.b] after [[a]] is inside the array a and its last element, deeper than its two parts say
    CheckNestingOk("[[a]]\n[a.b]\nc = 1", 3, "the array a, its element and the table b");
    CheckTooDeep("[[a]]\n[a.b.c]", 3, 2, 6,
        "The table \"c\" is nested 4 deep, deeper than Reading.MaxNestingDepth (3) allows.",
        "a header in an element of an array of tables");
    CheckTooDeep("[[a]]\n[[a.b]]", 3, 2, 5, "The table \"b\" is nested 4 deep",
        "an array of tables in an element of another");
    CheckTooDeep("[[a]]\n[a.b]\nc = [1]", 3, 3, 5, "The array is nested 4 deep", "an array value under it");
    CheckTooDeep("[[a]]\n[a.b]\nc = {}", 3, 3, 5, "The inline table is nested 4 deep", "an inline table under it");
    CheckTooDeep("[[a]]\n[a.b]\nc.d = 1", 3, 3, 1, "The table \"c\" is nested 4 deep", "a dotted key under it");
    CheckTooDeep("[[a]]\n[[a.b]]\n[a.b.c.d]", 4, 3, 6, "The table \"c\" is nested 5 deep",
        "a part before the last of a header in two arrays of tables");
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
    std::string const arraysOfTables = NestedArraysOfTables(limit / 2);

    std::string header = "[a";
    for (std::size_t i = 1; i < limit; ++i)
        header += ".a";

    header += "]\n";

    for (std::string const& text : { arrays, tables, header, arraysOfTables })
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
        CheckTrue(assigned.GetRoot() == copy.GetRoot(), "its values should be copied and compared");
    }

    // Arrange & Act & Assert
    CheckTooDeep(NestedArrays(limit + 1), limit, 1, 5 + limit, "The array is nested 129 deep", "one array more");
    CheckTooDeep("[" + header.substr(1, header.size() - 3) + ".b]", limit, 1, 2 + 2 * limit,
        "The table \"b\" is nested 129 deep", "one table more");
    CheckTooDeep(NestedArraysOfTables(limit / 2 + 1), limit, limit / 2 + 1, 3 + limit,
        "The table \"a\" is nested 130 deep", "one array of tables more");
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
// TTest_ASWToml_DocumentLookups
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_DocumentLookups::TTest_ASWToml_DocumentLookups()
    : inherited("ASWToml_DocumentLookups_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_DocumentLookups::~TTest_ASWToml_DocumentLookups()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentLookups::CheckNotFound(TASWTomlDocument const& document, ASWToml::TASWTomlPath const& path,
    std::string const& msg, std::source_location loc)
{
    CheckTrue(document.Find(path) == nullptr, msg + ": there should be no value at " + path.ToString(), loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentLookups::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentLookups::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentLookups::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentLookups::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentLookups, Copies_HaveTheirOwnValues)
{
    // Arrange
    TASWTomlDocument document;
    CheckTrue(document.Parse("a = 1\n[t]\nb = [2]\n").IsOk(), "the text should parse");

    // Act
    TASWTomlDocument const copied(document);
    TASWTomlDocument assigned;
    assigned = document;
    TASWTomlDocument const moved(std::move(assigned));
    CheckTrue(document.Parse("a = 2").IsOk(), "another text should parse");

    // Assert
    TASWTomlValue const expected = TTable{ { "a", 1 }, { "t", TTable{ { "b", TArray{ 2 } } } } };
    CheckTrue(copied.GetRoot() == expected, "a copy has the values");
    CheckTrue(moved.GetRoot() == expected, "a copy assigned and moved has them too");
    TASWTomlValue const* const element = moved.Find("t.b[0]");
    CheckTrue(element != nullptr && *element == TASWTomlValue(2), "a lookup in the moved copy");
    CheckTrue(document.GetRoot() == TASWTomlValue(TTable{ { "a", 2 } }), "the original has the new text's values");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentLookups, Default_RootIsAnEmptyTable)
{
    // Act
    TASWTomlDocument const document;

    // Assert
    CheckTrue(document.GetRoot().IsTable(), "the root is a table");
    CheckTrue(document.GetRoot().AsTable()->empty(), "with no keys");
    CheckTrue(document.Find("") == &document.GetRoot(), "the root path finds the root");
    CheckNotFound(document, "a", "a key");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentLookups, Find_ArrayElementsByIndex)
{
    // Arrange
    TASWTomlDocument document;
    CheckTrue(document.Parse(LookupText).IsOk(), "the text should parse");

    struct TCase
    {
        ASWToml::TASWTomlPath Path;
        TASWTomlValue Expected;
    };

    std::vector<TCase> const cases = {
        { "ports[0]", 80 },
        { "ports[2]", 8080 },
        { "m[1]", TArray{ 3 } },
        { "m[0][1]", 2 },
        { "products[0].name", "p0" },
        { { "products", 1, "name" }, "p1" },
        { "products[1].tags[0].k", 1 },
        { "products[0]", TTable{ { "name", "p0" } } },
    };

    for (TCase const& item : cases)
    {
        // Act
        TASWTomlValue const* const value = document.Find(item.Path);

        // Assert
        CheckTrue(value != nullptr, item.Path.ToString() + " should be found");
        CheckTrue(value != nullptr && *value == item.Expected, item.Path.ToString() + ": the value");
    }

    // Act
    TASWTomlValue const* const products = document.Find("products");

    // Assert
    CheckTrue(products != nullptr && products->IsArray() && products->AsArray()->size() == 2,
        "an array of tables is an array of its elements");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentLookups, Find_KeysAndTables)
{
    // Arrange
    TASWTomlDocument document;
    CheckTrue(document.Parse(LookupText).IsOk(), "the text should parse");

    struct TCase
    {
        ASWToml::TASWTomlPath Path;
        TASWTomlValue Expected;
    };

    std::vector<TCase> const cases = {
        { "title", "x" },
        { "a.b.c", 1 },
        { "a . b", TTable{ { "c", 1 } } },
        { "server.\"port no\"", 80 },
        { { "server", "port no" }, 80 },
        { "server.http.on", true },
        { "server.http", TTable{ { "on", true } } },
        { "t.\"a.b\"", 2 },
        { { "t", "a.b" }, 2 },
    };

    for (TCase const& item : cases)
    {
        // Act
        TASWTomlValue const* const value = document.Find(item.Path);

        // Assert
        CheckTrue(value != nullptr, item.Path.ToString() + " should be found");
        CheckTrue(value != nullptr && *value == item.Expected, item.Path.ToString() + ": the value");
    }

    // Act & Assert
    CheckTrue(document.Find("") == &document.GetRoot(), "the root path finds the root");
    CheckTrue(document.Find(ASWToml::TASWTomlPath()) == &document.GetRoot(), "so does a default path");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentLookups, Find_NullWhenThereIsNoSuchValue)
{
    // Arrange
    TASWTomlDocument document;
    CheckTrue(document.Parse(LookupText).IsOk(), "the text should parse");

    // Act & Assert
    CheckNotFound(document, "missing", "a key the root doesn't have");
    CheckNotFound(document, "server.missing", "a key a table doesn't have");
    CheckNotFound(document, "a.b.c.d", "a key of a value that isn't a table");
    CheckNotFound(document, "Title", "a key in another case");
    CheckNotFound(document, "ports[3]", "an index past the end");
    CheckNotFound(document, "products[2]", "an index past the last element of an array of tables");
    CheckNotFound(document, "products.name", "an array of tables without an index: no implicit last element");
    CheckNotFound(document, "ports.x", "a key of an array");
    CheckNotFound(document, "title[0]", "an index of a string");
    CheckNotFound(document, "server[0]", "an index of a table");
    CheckNotFound(document, "t.a.b", "the dotted key a.b isn't the quoted key \"a.b\"");

    ASWToml::TASWTomlPath const invalid = "a..b";
    ASWToml::TASWTomlPath const negative = { "ports", -1 };
    CheckFalse(invalid.IsValid() || negative.IsValid(), "the paths aren't valid");
    CheckNotFound(document, invalid, "a text that isn't a valid path");
    CheckNotFound(document, negative, "a negative index");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentLookups, GetRoot_HoldsEveryKindOfValue)
{
    // Arrange
    TASWTomlDocument document;
    std::string const text =
        "s = \"a\\tb\"\n"
        "ml = \"\"\"\r\nx\r\ny\"\"\"\n"
        "i = 0xFF\n"
        "f = 1.5\n"
        "b = true\n"
        "odt = 1979-05-27T07:32:00-07:00\n"
        "ldt = 1979-05-27T07:32:00.5\n"
        "ld = 1979-05-27\n"
        "lt = 07:32\n"
        "arr = [1, \"two\", [3], { x = 4 }]\n"
        "it = { x = 1, y.z = 2 }\n"
        "[t]\n"
        "k = 'v'\n"
        "[[aot]]\n"
        "n = 1\n"
        "[[aot]]\n";

    std::chrono::year_month_day const date{ std::chrono::year{ 1979 }, std::chrono::May, std::chrono::day{ 27 } };
    ASWToml::TASWTomlLocalTime const time{ std::chrono::hours{ 7 }, std::chrono::minutes{ 32 } };
    ASWToml::TASWTomlLocalTime const halfSecond{ std::chrono::hours{ 7 }, std::chrono::minutes{ 32 },
        std::chrono::seconds{ 0 }, std::chrono::milliseconds{ 500 } };

    TASWTomlValue const expected = TTable{
        { "s", "a\tb" },
        { "ml", "x\ny" },
        { "i", 255 },
        { "f", 1.5 },
        { "b", true },
        { "odt", ASWToml::TASWTomlOffsetDateTime{ date, time, std::chrono::minutes{ -7 * 60 } } },
        { "ldt", ASWToml::TASWTomlLocalDateTime{ date, halfSecond } },
        { "ld", date },
        { "lt", time },
        { "arr", TArray{ 1, "two", TArray{ 3 }, TTable{ { "x", 4 } } } },
        { "it", TTable{ { "x", 1 }, { "y", TTable{ { "z", 2 } } } } },
        { "t", TTable{ { "k", "v" } } },
        { "aot", TArray{ TTable{ { "n", 1 } }, TTable() } },
    };

    // Act
    ASWToml::TASWTomlResult const result = document.Parse(text);

    // Assert
    CheckTrue(result.IsOk(), "the text should parse: " + result.Message);
    CheckEquals(ASWToml::ETomlType::Table, document.GetRoot().GetType(), "the root is a table");
    CheckTrue(document.GetRoot() == expected, "every value");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentLookups, GetRoot_KeysInTheOrderFirstDefined)
{
    // Arrange
    TASWTomlDocument document;

    // Act
    // b is first made by [b.c], then defined by [b]
    ASWToml::TASWTomlResult const result = document.Parse(
        "z = 1\n[b.c]\n[a]\nx = 1\n[b]\ny = 2\nw.v = 3\n[i]\nt = { q = 1, p.r = 2, o = 3 }\n");

    // Assert
    CheckTrue(result.IsOk(), "the text should parse: " + result.Message);
    CheckEquals("z b a i"s, KeyList(document.GetRoot()), "the root's keys");
    CheckEquals("c y w"s, KeyList(*document.Find("b")), "a table made by a longer header first");
    CheckEquals("q p o"s, KeyList(*document.Find("i.t")), "an inline table");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentLookups, Parse_ReplacesTheValues)
{
    // Arrange
    TASWTomlDocument document;
    CheckTrue(document.Parse("a = 1").IsOk(), "the first text should parse");

    // Act
    ASWToml::TASWTomlResult const second = document.Parse("b = 2");

    // Assert
    CheckTrue(second.IsOk(), "the second text should parse: " + second.Message);
    CheckNotFound(document, "a", "the first text's key");
    CheckTrue(document.GetRoot() == TASWTomlValue(TTable{ { "b", 2 } }), "the second text's values");

    // Act
    ASWToml::TASWTomlResult const failed = document.Parse("c = 3\nc = 4");

    // Assert
    CheckEquals(ETomlCode::DuplicateKey, failed.Code, "a key defined twice");
    CheckNotFound(document, "c", "the failed text's key");
    CheckTrue(document.GetRoot() == TASWTomlValue(TTable{ { "b", 2 } }), "the values are kept on a failure");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DocumentTables
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_DocumentTables::TTest_ASWToml_DocumentTables()
    : inherited("ASWToml_DocumentTables_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_DocumentTables::~TTest_ASWToml_DocumentTables()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentTables::CheckDuplicate(std::string const& text, std::size_t line, std::size_t column,
    std::string const& expectedMessage, std::string const& msg, std::source_location loc)
{
    TASWTomlDocument document;

    ASWToml::TASWTomlResult const result = document.Parse(text);

    std::string const name = msg + " (\"" + Escaped(text) + "\")";
    CheckEquals(ETomlCode::DuplicateKey, result.Code, name + ": the code (" + result.Message + ")", loc);
    CheckEquals(line, result.Line, name + ": the line", loc);
    CheckEquals(column, result.Column, name + ": the column", loc);
    CheckEquals(expectedMessage, result.Message, name + ": the message", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentTables::CheckRoot(std::string const& text, TASWTomlValue const& expected,
    std::string const& msg, std::source_location loc)
{
    TASWTomlDocument document;

    ASWToml::TASWTomlResult const result = document.Parse(text);

    std::string const name = msg + " (\"" + Escaped(text) + "\")";
    CheckTrue(result.IsOk(), name + " should parse: " + result.Message, loc);
    CheckTrue(document.GetRoot() == expected, name + ": the values", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentTables::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentTables::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentTables::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentTables::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentTables, ArraysOfTables_AddAnElementEach)
{
    // Arrange
    // The spec's example: a sub-table and a nested array of tables go into the array's last element
    std::string const fruits =
        "[[fruits]]\n"
        "name = \"apple\"\n"
        "[fruits.physical]\n"
        "color = \"red\"\n"
        "[[fruits.varieties]]\n"
        "name = \"red delicious\"\n"
        "[[fruits.varieties]]\n"
        "name = \"granny smith\"\n"
        "[[fruits]]\n"
        "name = \"banana\"\n"
        "[[fruits.varieties]]\n"
        "name = \"plantain\"\n";

    TASWTomlValue const apple = TTable{
        { "name", "apple" },
        { "physical", TTable{ { "color", "red" } } },
        { "varieties", TArray{ TTable{ { "name", "red delicious" } }, TTable{ { "name", "granny smith" } } } },
    };
    TASWTomlValue const banana = TTable{
        { "name", "banana" },
        { "varieties", TArray{ TTable{ { "name", "plantain" } } } },
    };

    // Act & Assert
    CheckRoot(fruits, TTable{ { "fruits", TArray{ apple, banana } } }, "the spec's fruits");
    CheckRoot("[[p]]\nn = 1\n[[p]]\n[[p]]\nn = 3\n",
        TTable{ { "p", TArray{ TTable{ { "n", 1 } }, TTable(), TTable{ { "n", 3 } } } } }, "an empty element");
    CheckRoot("[[a]]\n[a.b]\nx = 1\n[[a]]\n[a.b]\nx = 2\n",
        TTable{ { "a", TArray{ TTable{ { "b", TTable{ { "x", 1 } } } }, TTable{ { "b", TTable{ { "x", 2 } } } } } } },
        "each element has its own sub-tables");
    CheckRoot("[[a.b]]\nx = 1\n[a]\ny = 2\n",
        TTable{ { "a", TTable{ { "b", TArray{ TTable{ { "x", 1 } } } }, { "y", 2 } } } },
        "the table [[a.b]] makes can be defined afterwards");
    CheckRoot("[[t.a]]\n[[t.a.b]]\nx = 1\n",
        TTable{ { "t", TTable{ { "a", TArray{ TTable{ { "b", TArray{ TTable{ { "x", 1 } } } } } } } } } },
        "an array of tables in an element");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentTables, ArraysOfTables_Errors)
{
    // Arrange & Act & Assert
    CheckDuplicate("[[a]]\n[a]", 2, 2, "The array of tables a is already defined on line 1.", "a table after it");
    CheckDuplicate("[a]\n[[a]]", 2, 3, "The table a is already defined on line 1.", "after a table");
    CheckDuplicate("[a.b]\n[[a]]", 2, 3, "The table a is already defined on line 1.",
        "after a table a longer header made");
    CheckDuplicate("a = []\n[[a]]", 2, 3, "The key a is already defined on line 1.", "after an array value");
    CheckDuplicate("a = [{}]\n[[a]]", 2, 3, "The key a is already defined on line 1.",
        "after an array value of inline tables");
    CheckDuplicate("a = 1\n[[a]]", 2, 3, "The key a is already defined on line 1.", "after a key");
    CheckDuplicate("[[a.b]]\n[a]\nb.y = 2", 3, 1, "The array of tables a.b is already defined on line 1.",
        "dotted keys into it");
    CheckDuplicate("[[a]]\n[a.b]\n[[a.b]]", 3, 3, "The table a[0].b is already defined on line 2.",
        "a table of an element, then an array of tables of the same name");
    CheckDuplicate("[[a]]\n[[a.b]]\n[a.b]", 3, 2, "The array of tables a[0].b is already defined on line 2.",
        "an array of tables in an element, then a table of the same name");
    CheckDuplicate("[[a]]\n[[a]]\nx = 1\nx = 2", 4, 1, "The key a[1].x is already defined on line 3.",
        "a key twice in the second element");
    CheckDuplicate("[a]\nb = [{ c = 1 }]\n[a.b.d]", 3, 2, "The key a.b is already defined on line 2.",
        "a header into an array value");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentTables, DottedKeys_DefineTables)
{
    // Arrange & Act & Assert
    CheckRoot("a.b.c = 1\na.b.d = 2\na.e = 3\n",
        TTable{ { "a", TTable{ { "b", TTable{ { "c", 1 }, { "d", 2 } } }, { "e", 3 } } } },
        "more dotted keys add to the tables");
    CheckRoot("[t]\na.b = 1\na.c = 2\n", TTable{ { "t", TTable{ { "a", TTable{ { "b", 1 }, { "c", 2 } } } } } },
        "under a header");
    CheckRoot("a.b = 1\n[a.c]\nd = 2\n", TTable{ { "a", TTable{ { "b", 1 }, { "c", TTable{ { "d", 2 } } } } } },
        "a header defines a sub-table of a table dotted keys defined");
    CheckRoot("[a.b.c]\nz = 1\n[a]\nb.d = 2\n",
        TTable{ { "a", TTable{ { "b", TTable{ { "c", TTable{ { "z", 1 } } }, { "d", 2 } } } } } },
        "dotted keys define a table only a longer header made");
    CheckRoot("t = { a.b = 1, a.c = 2 }\n", TTable{ { "t", TTable{ { "a", TTable{ { "b", 1 }, { "c", 2 } } } } } },
        "in an inline table");
    CheckRoot("3.14159 = \"pi\"\n", TTable{ { "3", TTable{ { "14159", "pi" } } } },
        "a dotted key that looks like a float");
    CheckRoot("\"a.b\".c = 1\n", TTable{ { "a.b", TTable{ { "c", 1 } } } }, "a quoted key with a dot");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentTables, DottedKeys_Errors)
{
    // Arrange & Act & Assert
    CheckDuplicate("a = 1\na.b = 2", 2, 1, "The key a is already defined on line 1.", "into a key's value");
    CheckDuplicate("a.b = 1\na.b.c = 2", 2, 1, "The key a.b is already defined on line 1.", "into a dotted key's value");
    CheckDuplicate("a.b.c = 1\na.b = 2", 2, 1, "The table a.b is already defined on line 1.",
        "a key for a table dotted keys made");
    CheckDuplicate("[a.b.c]\nz = 9\n[a]\nb.c.t = 1", 4, 1, "The table a.b.c is already defined on line 1.",
        "into a table a header defined");
    CheckDuplicate("[a.b.c.d]\n[a]\nb.c.d.k = 1", 3, 1, "The table a.b.c.d is already defined on line 1.",
        "into a table a header defined, through tables it made");
    CheckDuplicate("[a.b]\n[a]\nb = 1", 3, 1, "The table a.b is already defined on line 1.",
        "a key for a table a header defined");
    CheckDuplicate("[a.b.c]\n[a]\nb = 1", 3, 1, "The table a.b is already defined on line 1.",
        "a key for a table a longer header made");
    CheckDuplicate("[a.b.c]\n[a]\nb.d = 1\nb = 2", 4, 1, "The table a.b is already defined on line 3.",
        "the line of the dotted key that defined it");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentTables, Errors_PlaceCountsEveryPiece)
{
    // Arrange & Act & Assert
    // Every kind of piece comes before the second definition, so a miscounted one would move its place
    std::string const text = Bom +
        "# c\r\n"
        "s = \"\"\"a\r\nb\"\"\" # c\r\n"
        "[ t ] # c\n"
        "arr = [\n  1, # one\n  [2,],\n]\n"
        "it = {\n  x = 1, # c\n  y.z = 2,\n}\n"
        "lit = '''\nx'''\n"
        "[[p]]\n"
        "  k = 'v' # c\n"
        "\t[  t . it ]\n";
    CheckDuplicate(text, 17, 5, "The key t.it is already defined on line 9.", "after every kind of piece");

    CheckDuplicate("[a]\r\nx = [\r\n  1,\r\n]\r\ny = 1 # c\r\n  x = 2\r\n", 6, 3,
        "The key a.x is already defined on line 2.", "CRLF, and a key after a multi-line array");
    CheckDuplicate("q = { \"\xC3\xA9\" = 1, \"\xC3\xA9\" = 2 }", 1, 16,
        "The key q.\"\xC3\xA9\" is already defined on line 1.", "a column counts characters");
    CheckDuplicate("a = { b = [1,\n2], c = 3,\nc = 4 }", 3, 1, "The key a.c is already defined on line 2.",
        "an inline table over lines");
    CheckDuplicate("a = { b = 1 , b = 2 }", 1, 15, "The key a.b is already defined on line 1.",
        "blanks before a comma in an inline table");
    CheckDuplicate("a = [ 1 , { b = 1, b = 2 } ]", 1, 20, "The key a[1].b is already defined on line 1.",
        "blanks before a comma in an array");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentTables, Headers_DefineTables)
{
    // Arrange
    TASWTomlValue const xyzw = TTable{ { "y", TTable{ { "z", TTable{ { "w", TTable() } } } } }, { "k", 1 } };
    TASWTomlValue const apple = TTable{
        { "color", "red" },
        { "taste", TTable{ { "sweet", true } } },
        { "texture", TTable{ { "smooth", true } } },
    };

    // Act & Assert
    CheckRoot("[x.y.z.w]\n[x]\nk = 1\n", TTable{ { "x", xyzw } }, "a super-table defined afterwards");
    CheckRoot("[a]\n[a.b]\n[a.c]\n", TTable{ { "a", TTable{ { "b", TTable() }, { "c", TTable() } } } }, "sub-tables");
    CheckRoot("[ a . \"b c\" . 'd' ]\n", TTable{ { "a", TTable{ { "b c", TTable{ { "d", TTable() } } } } } },
        "quoted parts and blanks");
    CheckRoot("[\"a.b\"]\nx = 1\n", TTable{ { "a.b", TTable{ { "x", 1 } } } }, "a quoted part with a dot");
    CheckRoot("[fruit]\napple.color = \"red\"\napple.taste.sweet = true\n[fruit.apple.texture]\nsmooth = true\n",
        TTable{ { "fruit", TTable{ { "apple", apple } } } }, "the spec's sub-table of a table dotted keys defined");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentTables, Headers_Errors)
{
    // Arrange & Act & Assert
    CheckDuplicate("[a]\n[a]", 2, 2, "The table a is already defined on line 1.", "a table twice");
    CheckDuplicate("[a]\nx = 1\n\n [ a ]", 4, 4, "The table a is already defined on line 1.",
        "reported at the first key part");
    CheckDuplicate("[a]\n[\"a\"]", 2, 2, "The table a is already defined on line 1.", "a bare and a quoted key");
    CheckDuplicate("[a.b]\n[a]\n[a]", 3, 2, "The table a is already defined on line 2.",
        "a super-table twice: a longer header doesn't define it");
    CheckDuplicate("[a]\nb = 1\n[a.b]", 3, 2, "The key a.b is already defined on line 2.", "a table for a key");
    CheckDuplicate("a = 1\n[a.b]", 2, 2, "The key a is already defined on line 1.", "a sub-table of a key");
    CheckDuplicate("a.b = 1\n[a]", 2, 2, "The table a is already defined on line 1.",
        "a table dotted keys defined");
    CheckDuplicate("[fruit]\napple.color = 1\n[fruit.apple]", 3, 2,
        "The table fruit.apple is already defined on line 2.", "the spec's table dotted keys defined");
    CheckDuplicate("[t1]\nt2.t3.v = 0\n[t1.t2.t3]", 3, 2, "The table t1.t2.t3 is already defined on line 2.",
        "a table dotted keys made in another table");
    CheckDuplicate("[a.b.c]\n[a]\nb.d = 1\n[a.b]", 4, 2, "The table a.b is already defined on line 3.",
        "a table a longer header made, then dotted keys defined");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentTables, InlineTables_AreClosed)
{
    // Arrange & Act & Assert
    CheckDuplicate("a = { b = 1 }\na.c = 2", 2, 1, "The key a is already defined on line 1.", "dotted keys into one");
    CheckDuplicate("a = { b = 1 }\n[a]", 2, 2, "The key a is already defined on line 1.", "a header for one");
    CheckDuplicate("a = { b = 1 }\n[a.c]", 2, 2, "The key a is already defined on line 1.", "a header into one");
    CheckDuplicate("a = {}\n[[a]]", 2, 3, "The key a is already defined on line 1.", "an array of tables for one");
    CheckDuplicate("a.b = 1\na = {}", 2, 1, "The table a is already defined on line 1.",
        "one for a table dotted keys defined");
    CheckDuplicate("[product]\ntype.name = \"Nail\"\ntype = { edible = false }", 3, 1,
        "The table product.type is already defined on line 2.", "the spec's example");

    // Inside one, as in a table
    CheckDuplicate("a = { b = 1, b = 2 }", 1, 14, "The key a.b is already defined on line 1.", "a key twice");
    CheckDuplicate("a = { b.c = 1, b = 2 }", 1, 16, "The table a.b is already defined on line 1.",
        "a key for a table dotted keys made");
    CheckDuplicate("a = { b = {}, b.c = 1 }", 1, 15, "The key a.b is already defined on line 1.",
        "dotted keys into an inline table in it");
    CheckDuplicate("a = [{ b = 1, b = 2 }]", 1, 15, "The key a[0].b is already defined on line 1.",
        "in an array: the path has the index");
    CheckDuplicate("a = [{}, { b = 1, b = 2 }]", 1, 19, "The key a[1].b is already defined on line 1.",
        "in the second element");
    CheckDuplicate("a = {\n  b = 1,\n  b = 2,\n}", 3, 3, "The key a.b is already defined on line 2.",
        "on separate lines");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentTables, Keys_DefinedOnce)
{
    // Arrange & Act & Assert
    CheckDuplicate("a = 1\na = 2", 2, 1, "The key a is already defined on line 1.", "a key twice");
    CheckDuplicate("a = 1\n\"a\" = 2", 2, 1, "The key a is already defined on line 1.", "bare and basic string");
    CheckDuplicate("'a' = 1\n\"\\u0061\" = 2", 2, 1, "The key a is already defined on line 1.",
        "literal string and an escape");
    CheckDuplicate("\"a b\" = 1\n'a b' = 2", 2, 1, "The key \"a b\" is already defined on line 1.",
        "a key that can't be bare is quoted in the message");
    CheckDuplicate("\"\" = 1\n'' = 2", 2, 1, "The key \"\" is already defined on line 1.", "the empty key");
    CheckDuplicate("[t]\na = 1\n  a = 2", 3, 3, "The key t.a is already defined on line 2.", "under a header");

    CheckRoot("a = 1\n[t]\na = 2\n", TTable{ { "a", 1 }, { "t", TTable{ { "a", 2 } } } }, "the same key in two tables");
    CheckRoot("A = 1\na = 2\n", TTable{ { "A", 1 }, { "a", 2 } }, "keys are case-sensitive");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_TomlTest
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_TomlTest::TTest_ASWToml_TomlTest()
    : inherited("ASWToml_TomlTest_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_TomlTest::~TTest_ASWToml_TomlTest()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_TomlTest::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_TomlTest::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_TomlTest::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_TomlTest::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TomlTest, InvalidFiles_Fail)
{
    // Arrange
    std::span<TTomlTestFile const> const files = TomlTestInvalidFiles();
    CheckEquals(std::size_t(467), files.size(), "toml-test " + std::string(TomlTestVersion()) + "'s invalid files");

    for (TTomlTestFile const& file : files)
    {
        // Act
        TASWTomlDocument document;
        ASWToml::TASWTomlResult const result = document.Parse(file.Text);

        // Assert
        std::string const name(file.Name);
        CheckFalse(result.IsOk(), name + " should fail to parse");

        // Every error is about a place in the text, except an encoding the library doesn't read
        if (result.Code != ETomlCode::UnsupportedEncoding)
            CheckTrue(result.Line >= 1 && result.Column >= 1, name + ": the error has a line and a column");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TomlTest, ValidFiles_HaveTheExpectedValues)
{
    // Arrange
    std::span<TTomlTestFile const> const files = TomlTestValidFiles();
    CheckEquals(std::size_t(214), files.size(), "toml-test " + std::string(TomlTestVersion()) + "'s valid files");

    for (TTomlTestFile const& file : files)
    {
        // Act
        TASWTomlDocument document;
        ASWToml::TASWTomlResult const result = document.Parse(file.Text);

        // Assert
        std::string const name(file.Name);
        CheckTrue(result.IsOk(), name + " should parse: " + result.Message);
        CheckEquals(""s, ValuesDiffer(file, document.GetRoot()), name + ": the values");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TomlTest, ValidFiles_WriteBackByteForByte)
{
    for (TTomlTestFile const& file : TomlTestValidFiles())
    {
        // Arrange
        TASWTomlDocument document;
        CheckTrue(document.Parse(file.Text).IsOk(), std::string(file.Name) + " should parse");

        // Act
        std::string const text = document.ToText();

        // Assert
        CheckEquals(Escaped(std::string(file.Text)), Escaped(text), std::string(file.Name) + ": the text written");
    }
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
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DocumentLookups, "Document")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DocumentModel, "Document")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DocumentParse, "Document")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DocumentTables, "Document")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DocumentToText, "Document")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_TomlTest, "Document")
