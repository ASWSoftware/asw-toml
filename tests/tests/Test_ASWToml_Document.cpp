/* **************************************************************************
Test_ASWToml_Document.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "Test_ASWToml_Document.h"
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWToml_Index.h"
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
using ASWToml::ELineEnding;
using ASWToml::ETomlCode;
using ASWToml::TASWTomlDocument;
using ASWToml::TASWTomlOptions;
using ASWToml::TASWTomlValue;
using ASWToml::Detail::EEntryKind;
using ASWToml::Detail::ELineKind;
using ASWToml::Detail::ENodeKind;
using ASWToml::Detail::EValueKind;
using ASWToml::Detail::NoIndex;
using ASWToml::Detail::TDocBlock;
using ASWToml::Detail::TDocEntry;
using ASWToml::Detail::TDocIndex;
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

// How many texts of a fuzz test ended with each code
using TCodeCountMap = std::map<ETomlCode, std::size_t>;

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

// The fuzz tests: the seed of the first random text, and how many texts each test makes
constexpr std::uint32_t FuzzFirstSeed = 20261023;
constexpr std::size_t FuzzTextCount = 4000;


/////////////////////////////////////////////////////////////////////////////
// TCorpusText
//
// A text of the round-trip corpus, and the values it reads as
/////////////////////////////////////////////////////////////////////////////
struct TCorpusText
{
    std::string Name;
    std::string Text;
    TASWTomlValue Expected; // The root table, with each table's keys in the order the text defines them
};

// The round-trip corpus
using TCorpusTextVector = std::vector<TCorpusText>;


std::string CorpusEscaped(std::string_view text);
char const* CorpusType(TASWTomlValue const& value) noexcept;
ASWToml::TASWTomlOptions DepthOptions(std::size_t maxDepth);
std::string Escaped(std::string const& text);
TExpectedMap ExpectedValues(std::string_view expected);
std::string FindFailure(TASWTomlDocument const& document, TASWTomlValue const& value,
    ASWToml::TASWTomlPath const& path);
void FlattenValues(TASWTomlValue const& value, std::string const& path, TValueMap& values);
bool IdenticalValues(TASWTomlValue const& left, TASWTomlValue const& right);
bool IsInText(ASWToml::TASWTomlResult const& result, std::string_view text);
std::string KeyList(TASWTomlValue const& value);
std::string MutatedText(std::mt19937& generator, std::string_view source);
std::string NamesAt(std::string const& text, TDocIndex const& index, std::string const& path);
std::string NestedArrays(std::size_t depth);
std::string NestedArraysOfTables(std::size_t count);
std::string NestedInlineTables(std::size_t depth);
ASWToml::Detail::TIndexNode const& NodeAt(TDocIndex const& index, std::string const& path);
std::string RandomDocumentText(std::mt19937& generator);
std::string RandomKey(std::mt19937& generator);
ASWToml::TASWTomlOptions RandomOptions(std::mt19937& generator);
std::string RandomPieceText(std::mt19937& generator);
std::string RandomValueText(std::mt19937& generator, std::size_t depth);
TCorpusTextVector RoundTripCorpus();
bool SameValue(std::string const& expected, TASWTomlValue const& value);
std::string Slice(std::string const& text, ASWToml::Detail::TTextRange range);
std::string ValuesDiffer(ASWUnitTests::TTomlTestFile const& file, TASWTomlValue const& root);
std::string WithLineBreaks(std::string_view text, std::string_view lineBreak);

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
    FindFailure

    The path of the first value at or in 'value', which is at 'path' in 'document', that Find() doesn't give, by that
    path or by the path read back from its ToString(); empty when Find() gives each one
*/
std::string FindFailure(TASWTomlDocument const& document, TASWTomlValue const& value,
    ASWToml::TASWTomlPath const& path)
{
    if (document.Find(path) != &value || document.Find(ASWToml::TASWTomlPath(path.ToString())) != &value)
        return "\"" + Escaped(path.ToString()) + "\"";

    if (TTable const* const table = value.AsTable())
    {
        for (ASWToml::TASWTomlKeyValue const& keyValue : *table)
        {
            ASWToml::TASWTomlPath child = path;
            std::string failure = FindFailure(document, keyValue.Value, child.Append(keyValue.Key));
            if (!failure.empty())
                return failure;
        }
    }
    else if (TArray const* const array = value.AsArray())
    {
        for (std::size_t i = 0; i < array->size(); ++i)
        {
            ASWToml::TASWTomlPath child = path;
            std::string failure = FindFailure(document, (*array)[i], child.Append(i));
            if (!failure.empty())
                return failure;
        }
    }

    return std::string();
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
    IdenticalValues

    Whether 'left' and 'right' are the same values in the same order: each table's keys in the same order, and two
    floats the same number with the same sign (two NaNs are the same when their signs are)
*/
bool IdenticalValues(TASWTomlValue const& left, TASWTomlValue const& right)
{
    if (left.GetType() != right.GetType())
        return false;

    if (double const* const number = left.AsFloat())
    {
        double const other = *right.AsFloat();
        bool const same = std::isnan(*number) ? std::isnan(other) : *number == other;

        return same && std::signbit(*number) == std::signbit(other);
    }

    if (TArray const* const array = left.AsArray())
    {
        TArray const& others = *right.AsArray();
        if (array->size() != others.size())
            return false;

        for (std::size_t i = 0; i < array->size(); ++i)
        {
            if (!IdenticalValues((*array)[i], others[i]))
                return false;
        }

        return true;
    }

    if (TTable const* const table = left.AsTable())
    {
        TTable const& others = *right.AsTable();
        if (table->size() != others.size())
            return false;

        for (std::size_t i = 0; i < table->size(); ++i)
        {
            if ((*table)[i].Key != others[i].Key || !IdenticalValues((*table)[i].Value, others[i].Value))
                return false;
        }

        return true;
    }

    return left == right;
}
//---------------------------------------------------------------------------
/*
    IsInText

    Whether the Line and Column of 'result' are a place in 'text' (after a UTF-8 byte order mark, which isn't
    counted): a line it has, and a character of that line or the place after its last one
*/
bool IsInText(ASWToml::TASWTomlResult const& result, std::string_view text)
{
    if (text.substr(0, Bom.size()) == Bom)
        text.remove_prefix(Bom.size());

    if (result.Line == 0 || result.Column == 0)
        return false;

    std::size_t start = 0;
    for (std::size_t line = 1; line < result.Line; ++line)
    {
        std::size_t const lineFeed = text.find('\n', start);
        if (lineFeed == std::string_view::npos)
            return false;

        start = lineFeed + 1;
    }

    std::size_t const end = text.find('\n', start);
    std::string_view const line = text.substr(start, end == std::string_view::npos ? end : end - start);

    return result.Column <= ASWToml::Detail::CountCodePoints(line) + 1;
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
    MutatedText

    'source' with one to three random edits: a byte replaced, inserted or deleted, the text cut off, or a line
    repeated, deleted or swapped with another (which makes keys and tables defined twice, or out of order)
*/
std::string MutatedText(std::mt19937& generator, std::string_view source)
{
    char const bytes[] = {
        '[', ']', '{', '}', '=', ',', '.', '"', '\'', '#', '\n', '\r', ' ', '\t', 'a', '1', '-', ':', '\\',
        '\x7F', '\xC3', '\xFF', '\0',
    };

    std::string text(source);
    std::size_t const editCount = 1 + generator() % 3;

    for (std::size_t edit = 0; edit < editCount; ++edit)
    {
        std::uint32_t const kind = static_cast<std::uint32_t>(generator() % 7);
        std::size_t const offset = generator() % (text.size() + 1);
        char const byte = bytes[generator() % sizeof(bytes)];

        if (kind < 4)
        {
            if (kind == 0 && offset < text.size())
                text[offset] = byte;
            else if (kind == 1)
                text.insert(offset, 1, byte);
            else if (kind == 2 && offset < text.size())
                text.erase(offset, 1);
            else
                text.resize(offset);

            continue;
        }

        // The lines, each with its line feed
        TStringVector lines;
        for (std::size_t start = 0; start < text.size();)
        {
            std::size_t const end = std::min(text.find('\n', start), text.size() - 1) + 1;
            lines.push_back(text.substr(start, end - start));
            start = end;
        }

        if (lines.empty())
            continue;

        std::size_t const first = generator() % lines.size();
        std::size_t const second = generator() % lines.size();
        std::string const repeated = lines[first];
        if (kind == 4)
            lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(second), repeated);
        else if (kind == 5)
            lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(first));
        else
            std::swap(lines[first], lines[second]);

        text.clear();
        for (std::string const& line : lines)
            text += line;
    }

    return text;
}
//---------------------------------------------------------------------------
/*
    NamesAt

    The names of the node of 'index' at 'path' (see NodeAt()): each one's text in 'text', '@' and its offset, and a
    blank
*/
std::string NamesAt(std::string const& text, TDocIndex const& index, std::string const& path)
{
    std::string names;
    for (ASWToml::Detail::TTextRange const& range : NodeAt(index, path).Names)
        names += Slice(text, range) + "@" + std::to_string(range.Begin) + " ";

    return names;
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
    NodeAt

    The node of 'index' at 'path'; a default node (no names, lines or ranges) when there is none, so a test that
    expects one fails instead of crashing
*/
ASWToml::Detail::TIndexNode const& NodeAt(TDocIndex const& index, std::string const& path)
{
    static ASWToml::Detail::TIndexNode const none;
    std::size_t const node = ASWToml::Detail::FindNode(index, path);

    return node == NoIndex ? none : index.Nodes[node];
}
//---------------------------------------------------------------------------
/*
    RandomDocumentText

    A random TOML text of up to 11 lines: blank and comment lines, headers, headers of arrays of tables, and keys
    with values (of every kind, arrays and inline tables included, over several lines at times), with LF or CRLF
    line breaks and at times a byte order mark. The keys come from a few names, so a key or table is often defined
    twice or in a way TOML's table rules forbid.
*/
std::string RandomDocumentText(std::mt19937& generator)
{
    std::string text = generator() % 16 == 0 ? Bom : std::string();
    std::size_t const lineCount = generator() % 12;

    for (std::size_t line = 0; line < lineCount; ++line)
    {
        if (generator() % 6 == 0)
            text += generator() % 2 == 0 ? "  " : "\t";

        std::uint32_t const kind = static_cast<std::uint32_t>(generator() % 12);
        if (kind == 1)
        {
            text += "# comment";
        }
        else if (kind == 2 || kind == 3)
        {
            text += "[" + RandomKey(generator) + "]";
        }
        else if (kind == 4)
        {
            text += "[[" + RandomKey(generator) + "]]";
        }
        else if (kind > 4)
        {
            char const* const equals[] = { " = ", "=", " =\t" };
            text += RandomKey(generator) + equals[generator() % 3] + RandomValueText(generator, 0);
        }

        if (kind != 1 && generator() % 4 == 0)
            text += " # comment";

        if (line + 1 < lineCount || generator() % 2 == 0)
            text += generator() % 3 == 0 ? "\r\n" : "\n";
    }

    return text;
}
//---------------------------------------------------------------------------
/*
    RandomKey

    A random key: mostly one part, else two or three separated by dots (with blanks around them at times), each a
    bare key or a basic or literal string from a few names, some of them the same key written another way
*/
std::string RandomKey(std::mt19937& generator)
{
    char const* const names[] = { "a", "b", "c", "a", "b", "\"a\"", "'b'", "\"x.y\"", "\"\"", "1", "k-2" };
    char const* const dots[] = { ".", ".", " . ", "\t.\t" };

    std::size_t const count = generator() % 3 == 0 ? 2 + generator() % 2 : 1;
    std::string key = names[generator() % (sizeof(names) / sizeof(names[0]))];
    for (std::size_t i = 1; i < count; ++i)
        key += std::string(dots[generator() % 4]) + names[generator() % (sizeof(names) / sizeof(names[0]))];

    return key;
}
//---------------------------------------------------------------------------
/*
    RandomOptions

    Default options, but one time in four with a Reading.MaxNestingDepth of 1 to 4, so values nest too deep
*/
ASWToml::TASWTomlOptions RandomOptions(std::mt19937& generator)
{
    ASWToml::TASWTomlOptions options;
    if (generator() % 4 == 0)
        options.Reading.MaxNestingDepth = 1 + generator() % 4;

    return options;
}
//---------------------------------------------------------------------------
/*
    RandomPieceText

    A random text of up to 30 pieces of TOML from a weighted alphabet (brackets, braces, keys, '=', values, commas,
    blanks, comments and line breaks most often, then quotes, a carriage return, a byte order mark and bytes that
    aren't UTF-8), which is seldom valid TOML
*/
std::string RandomPieceText(std::mt19937& generator)
{
    std::string const pieces[] = {
        "[", "[", "]", "]", "[[", "]]", "{", "{", "}", "}", "=", " = ", " = ", ",", ",", ".", "a", "a", "b", "b",
        "\"k\"", "'k'", "1", "1", "\"s\"", "[1]", "{}", "true", "1979-05-27", "#", " # c", " ", " ", "\t", "\n",
        "\n", "\n", "\r\n", "\r", "\"\"\"", "'''", "\\", "\xC3\xA9", "\xFF", std::string(1, '\0'), Bom,
    };

    std::string text;
    std::size_t const count = generator() % 31;

    for (std::size_t i = 0; i < count; ++i)
        text += pieces[generator() % (sizeof(pieces) / sizeof(pieces[0]))];

    return text;
}
//---------------------------------------------------------------------------
/*
    RandomValueText

    A random value 'depth' arrays and inline tables deep: a scalar of any kind (one time in 64 an integer too large
    to read), or below a depth of 4 an array or an inline table of up to three values, with blanks, line breaks,
    comments and a trailing comma at times
*/
std::string RandomValueText(std::mt19937& generator, std::size_t depth)
{
    char const* const scalars[] = {
        "1", "-2", "0x1F", "0o17", "1_000", "3.5", "-inf", "nan", "6e-3", "\"s\"", "'C:\\x'", "\"\"\"m\nl\"\"\"",
        "'''r\r\n'''", "\"\\u00E9\\e\\x41\"", "true", "false", "1979-05-27", "07:32", "1979-05-27T07:32:00Z",
        "1979-05-27 07:32:00.5",
    };
    char const* const trivia[] = { "", "", " ", "\n  ", " # c\n", "\r\n" };

    std::uint32_t const kind = depth < 4 ? static_cast<std::uint32_t>(generator() % 8) : 0;
    if (kind < 6)
    {
        if (generator() % 64 == 0)
            return "99999999999999999999";

        return scalars[generator() % (sizeof(scalars) / sizeof(scalars[0]))];
    }

    bool const isArray = kind == 6;
    std::size_t const count = generator() % 4;
    std::string text = isArray ? "[" : "{";

    for (std::size_t i = 0; i < count; ++i)
    {
        if (i > 0)
            text += ",";

        text += trivia[generator() % 6];
        if (!isArray)
            text += RandomKey(generator) + " = ";

        text += RandomValueText(generator, depth + 1);
    }

    if (count > 0 && generator() % 3 == 0)
        text += ",";

    text += trivia[generator() % 6];

    return text + (isArray ? "]" : "}");
}
//---------------------------------------------------------------------------
/*
    RoundTripCorpus

    Texts of every layout the model keeps, each with the values it reads as
*/
TCorpusTextVector RoundTripCorpus()
{
    std::chrono::year_month_day const date{ std::chrono::year{ 1979 }, std::chrono::May, std::chrono::day{ 27 } };
    double const infinity = std::numeric_limits<double>::infinity();
    double const minusNan = -std::numeric_limits<double>::quiet_NaN();
    ASWToml::TASWTomlLocalTime const time{ std::chrono::hours{ 7 }, std::chrono::minutes{ 32 } };
    ASWToml::TASWTomlLocalTime const fraction{ std::chrono::hours{ 0 }, std::chrono::minutes{ 32 },
        std::chrono::seconds{ 0 }, std::chrono::microseconds{ 999999 } };

    ASWToml::TASWTomlLocalTime const halfSecond{ std::chrono::hours{ 0 }, std::chrono::minutes{ 32 },
        std::chrono::seconds{ 0 }, std::chrono::milliseconds{ 500 } };

    // Elements of the arrays of tables below
    TASWTomlValue const alpha = TTable{ { "name", "alpha" }, { "ports", TArray{ 8000, 8001, 8002 } } };
    TASWTomlValue const beta = TTable{ { "name", "beta" }, { "ports", TArray{ 8000 } },
        { "limits", TTable{ { "max", 100 } } } };
    TASWTomlValue const firstA = TTable{ { "x", 1 }, { "b", TTable{ { "y", 2 } } },
        { "c", TArray{ TTable{ { "z", 3 } }, TTable() } } };

    TCorpusTextVector corpus;

    corpus.push_back({ "empty", "", TTable() });
    corpus.push_back({ "blank and comment lines", "\n  \n\t\n# only comments\n   # indented\n#\n", TTable() });

    corpus.push_back({ "a settings file",
        "# Application settings\n"
        "title = \"My App\"   # shown in the title bar\n"
        "version = 3\n"
        "\n"
        "[window]\n"
        "width = 1024\n"
        "height = 768\n"
        "maximized = false\n"
        "\n"
        "[paths]\n"
        "data = 'C:\\Users\\me\\Data'\n"
        "cache = \"/var/cache/app\"\n"
        "\n"
        "[[servers]]\n"
        "name = \"alpha\"\n"
        "ports = [ 8000, 8001, 8002 ]\n"
        "\n"
        "[[servers]]\n"
        "name = \"beta\"\n"
        "ports = [8000]\n"
        "\n"
        "[servers.limits]   # of beta\n"
        "max = 100\n",
        TTable{
            { "title", "My App" },
            { "version", 3 },
            { "window", TTable{ { "width", 1024 }, { "height", 768 }, { "maximized", false } } },
            { "paths", TTable{ { "data", "C:\\Users\\me\\Data" }, { "cache", "/var/cache/app" } } },
            { "servers", TArray{ alpha, beta } },
        } });

    corpus.push_back({ "CRLF line breaks", "# c\r\na = 1\r\n\r\n[t]\r\nb = \"x\"\r\n",
        TTable{ { "a", 1 }, { "t", TTable{ { "b", "x" } } } } });
    corpus.push_back({ "mixed line breaks", "a = 1\r\nb = 2\nc = 3\r\n\n[t]\r\nx = 'y'\n",
        TTable{ { "a", 1 }, { "b", 2 }, { "c", 3 }, { "t", TTable{ { "x", "y" } } } } });
    corpus.push_back({ "no line break at the end", "a = 1\n[t]\nb = [1, 2] # end",
        TTable{ { "a", 1 }, { "t", TTable{ { "b", TArray{ 1, 2 } } } } } });
    corpus.push_back({ "a byte order mark", Bom + "# c\r\nkey = \"value\"\r\n", TTable{ { "key", "value" } } });

    corpus.push_back({ "comments everywhere",
        "# file comment\n"
        "\n"
        "[ t ]  # header comment\n"
        "a = [  # after the bracket\n"
        "  1,   # one\n"
        "  # a comment line in an array\n"
        "  2,\n"
        "  # before the closing bracket\n"
        "]  # after the array\n"
        "b = {  # after the brace\n"
        "  x = 1,  # in an inline table\n"
        "  # a comment line in an inline table\n"
        "  y = 2,\n"
        "  # before the closing brace\n"
        "}  # after the inline table\n"
        "[[ p ]]  # header of an array of tables\n"
        "c = 'v'#no blank before the hash\n",
        TTable{
            { "t", TTable{ { "a", TArray{ 1, 2 } }, { "b", TTable{ { "x", 1 }, { "y", 2 } } } } },
            { "p", TArray{ TTable{ { "c", "v" } } } },
        } });

    corpus.push_back({ "multi-line arrays and trailing commas",
        "a = [\n]\n"
        "b = [ # c\n]\n"
        "c = [\n  [1, 2,],\n  [\n    3,\n  ],\n]\n"
        "d = [1,2,3,]\n",
        TTable{
            { "a", TArray() },
            { "b", TArray() },
            { "c", TArray{ TArray{ 1, 2 }, TArray{ 3 } } },
            { "d", TArray{ 1, 2, 3 } },
        } });

    corpus.push_back({ "inline tables over lines",
        "t = {\n  a = 1,\n  b = { c = [\n    2,\n  ] },\n  d.e = 3,\n}\n"
        "u = {}\n"
        "v = { }\n"
        "w = {\n}\n",
        TTable{
            { "t", TTable{ { "a", 1 }, { "b", TTable{ { "c", TArray{ 2 } } } }, { "d", TTable{ { "e", 3 } } } } },
            { "u", TTable() },
            { "v", TTable() },
            { "w", TTable() },
        } });

    corpus.push_back({ "dotted keys",
        "a.b.c = 1\n"
        "a . b . d = 2\n"
        "a.\"e f\".g = 3\n"
        "[t]\n"
        "x.y = 4\n"
        "x\t.\tz = 5\n",
        TTable{
            { "a", TTable{ { "b", TTable{ { "c", 1 }, { "d", 2 } } }, { "e f", TTable{ { "g", 3 } } } } },
            { "t", TTable{ { "x", TTable{ { "y", 4 }, { "z", 5 } } } } },
        } });

    corpus.push_back({ "quoted keys",
        "\"a.b\" = 1\n"
        "'c.d'.e = 2\n"
        "\"\" = 3\n"
        "\"quote\\\"d\" = 4\n"
        "'\xC3\xA9t\xC3\xA9' = 5\n"
        "[\"x.y\".'z']\n"
        "k = 6\n",
        TTable{
            { "a.b", 1 },
            { "c.d", TTable{ { "e", 2 } } },
            { "", 3 },
            { "quote\"d", 4 },
            { "\xC3\xA9t\xC3\xA9", 5 },
            { "x.y", TTable{ { "z", TTable{ { "k", 6 } } } } },
        } });

    corpus.push_back({ "headers with blanks", "  [ a . b ]  \n\t[[ c ]]\t# c\n[ d.\"e f\" ]\n",
        TTable{
            { "a", TTable{ { "b", TTable() } } },
            { "c", TArray{ TTable() } },
            { "d", TTable{ { "e f", TTable() } } },
        } });

    corpus.push_back({ "arrays of tables with sub-tables",
        "[[a]]\nx = 1\n[a.b]\ny = 2\n[[a.c]]\nz = 3\n[[a.c]]\n[[a]]\n[a.b]\ny = 4\n",
        TTable{ { "a", TArray{ firstA, TTable{ { "b", TTable{ { "y", 4 } } } } } } } });

    corpus.push_back({ "blanks and tabs", "\ta\t=\t1\t\n  [\tt\t]\n b =  2  #\tc\n",
        TTable{ { "a", 1 }, { "t", TTable{ { "b", 2 } } } } });

    corpus.push_back({ "every form of scalar",
        "s1 = \"tab\\tquote\\\"back\\\\slash \\u00E9\\U0001F600\\x41\\e\"\n"
        "s2 = 'C:\\Users\\me'\n"
        "s3 = \"\"\"\nfirst\r\nsecond \\\n    third\"\"\"\n"
        "s4 = '''\nraw \\n\nline'''\n"
        "i1 = +17\n"
        "i2 = 0xDEAD_BEEF\n"
        "i3 = 0o755\n"
        "i4 = 0b1101\n"
        "i5 = -9_223_372_036_854_775_808\n"
        "f1 = 6.626e-34\n"
        "f2 = -0.0\n"
        "f3 = +inf\n"
        "f4 = -nan\n"
        "f5 = 1e1_0\n"
        "b1 = true\n"
        "d1 = 1979-05-27T07:32:00Z\n"
        "d2 = 1979-05-27 00:32:00.999999-07:00\n"
        "d3 = 1979-05-27t07:32:00z\n"
        "d4 = 1979-05-27T07:32:00\n"
        "d5 = 1979-05-27\n"
        "d6 = 07:32\n"
        "d7 = 00:32:00.5\n",
        TTable{
            { "s1", "tab\tquote\"back\\slash \xC3\xA9\xF0\x9F\x98\x80" "A\x1B" },
            { "s2", "C:\\Users\\me" },
            { "s3", "first\nsecond third" },
            { "s4", "raw \\n\nline" },
            { "i1", 17 },
            { "i2", 0xDEADBEEF },
            { "i3", 0755 },
            { "i4", 13 },
            { "i5", std::numeric_limits<std::int64_t>::min() },
            { "f1", 6.626e-34 },
            { "f2", -0.0 },
            { "f3", infinity },
            { "f4", minusNan },
            { "f5", 1e10 },
            { "b1", true },
            { "d1", ASWToml::TASWTomlOffsetDateTime{ date, time } },
            { "d2", ASWToml::TASWTomlOffsetDateTime{ date, fraction, std::chrono::minutes{ -7 * 60 } } },
            { "d3", ASWToml::TASWTomlOffsetDateTime{ date, time } },
            { "d4", ASWToml::TASWTomlLocalDateTime{ date, time } },
            { "d5", date },
            { "d6", time },
            { "d7", halfSecond },
        } });

    corpus.push_back({ "characters outside ASCII",
        "# Gr\xC3\xBC\xC3\x9F" "e \xE2\x82\xAC\n"
        "smile = \"\xF0\x9F\x98\x80\"\n"
        "\"cl\xC3\xA9\" = 'valeur'\n",
        TTable{ { "smile", "\xF0\x9F\x98\x80" }, { "cl\xC3\xA9", "valeur" } } });

    // Nested exactly as deep as the default Reading.MaxNestingDepth (100) allows: 30 tables of a header, 29 of a
    // dotted key, 20 arrays and 21 inline tables
    std::string header = "[h1";
    std::string key = "d1";
    for (int i = 2; i <= 30; ++i)
    {
        header += ".h" + std::to_string(i);
        key += ".d" + std::to_string(i);
    }

    std::string value = std::string(20, '[');
    for (int i = 0; i < 21; ++i)
        value += "{ i = ";

    value += "1";
    for (int i = 0; i < 21; ++i)
        value += " }";

    value += std::string(20, ']');

    TASWTomlValue deep = 1;
    for (int i = 0; i < 21; ++i)
        deep = TTable{ { "i", deep } };

    for (int i = 0; i < 20; ++i)
        deep = TArray{ deep };

    for (int i = 30; i >= 1; --i)
        deep = TTable{ { "d" + std::to_string(i), deep } };

    for (int i = 30; i >= 1; --i)
        deep = TTable{ { "h" + std::to_string(i), deep } };

    corpus.push_back({ "nesting at the limit", header + "]\n" + key + " = " + value + "\n", deep });

    return corpus;
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
    Slice

    The bytes of 'text' in 'range'; "(out of the text)" when the range isn't inside it
*/
std::string Slice(std::string const& text, ASWToml::Detail::TTextRange range)
{
    if (range.Begin > range.End || range.End > text.size())
        return "(out of the text)";

    return text.substr(range.Begin, range.End - range.Begin);
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
/*
    WithLineBreaks

    'text' with each line break ("\n" or "\r\n") written as 'lineBreak'
*/
std::string WithLineBreaks(std::string_view text, std::string_view lineBreak)
{
    std::string converted;

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
            continue;

        if (text[i] == '\n')
            converted += lineBreak;
        else
            converted += text[i];
    }

    return converted;
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
    CheckFalse(document.IsModified(), "a new document isn't modified");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentParse, Modified_ClearUntilAnEditChangesTheText)
{
    // Arrange
    TASWTomlDocument document;

    // Act
    ASWToml::TASWTomlResult const parsed = document.Parse("a = 1\n");
    bool const afterParse = document.IsModified();
    document.ClearModified();

    // Assert
    CheckTrue(parsed.IsOk(), "the text should parse: " + parsed.Message);
    CheckFalse(afterParse, "a parsed document isn't modified");
    CheckFalse(document.IsModified(), "clearing a clear flag leaves it clear");
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
ASW_TEST(TTest_ASWToml_DocumentParse, Reset_EmptiesTheDocument)
{
    // Arrange
    TASWTomlDocument document;
    CheckTrue(document.Parse(Bom + "a = 1\r\n[t]\r\n").IsOk(), "the text should parse");
    TASWTomlOptions options;
    options.Writing.CommentPrefix = "#";

    // Act
    ASWToml::TASWTomlResult const result = document.Reset(options);

    // Assert
    CheckTrue(result.IsOk(), "the document should be reset: " + result.Message);
    CheckEquals(""s, document.ToText(), "no text");
    CheckTrue(document.GetRoot() == TASWTomlValue(), "an empty root table");
    CheckTrue(document.Find("a") == nullptr, "no keys");
    CheckFalse(document.HasBom(), "no byte order mark");
    CheckEquals(ASWToml::Detail::NativeLineBreak(), document.GetLineBreak(), "the platform's line break");
    CheckEquals("#"s, document.GetOptions().Writing.CommentPrefix, "the options given");
    CheckFalse(document.IsModified(), "not modified");

    // Act
    ASWToml::TASWTomlResult const again = document.Reset(document.GetOptions());

    // Assert
    CheckTrue(again.IsOk(), "its own options should do: " + again.Message);
    CheckEquals("#"s, document.GetOptions().Writing.CommentPrefix, "its own options kept");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentParse, Reset_RejectsInvalidOptions)
{
    // Arrange
    TASWTomlDocument document;
    CheckTrue(document.Parse("a = 1\n").IsOk(), "the text should parse");
    TASWTomlOptions invalid;
    invalid.Writing.CommentPrefix = ";";

    // Act
    ASWToml::TASWTomlResult const result = document.Reset(invalid);

    // Assert
    CheckEquals(ETomlCode::InvalidOptions, result.Code, "options that aren't valid");
    CheckEquals("a = 1\n"s, document.ToText(), "the text should be kept");
    CheckTrue(document.Find("a") != nullptr, "the values too");
    CheckEquals("# "s, document.GetOptions().Writing.CommentPrefix, "and the options");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentParse, SetOptions_FailureChangesNothing)
{
    // Arrange
    std::string const text = Bom + "a = [[[1]]]\r\n";
    TASWTomlDocument document;
    CheckTrue(document.Parse(text).IsOk(), "the text should parse");
    TASWTomlOptions shallow;
    shallow.Reading.MaxNestingDepth = 2;
    shallow.Writing.CommentPrefix = "#";
    TASWTomlOptions invalid;
    invalid.Reading.MaxNestingDepth = 0;

    // Act
    ASWToml::TASWTomlResult const tooDeep = document.SetOptions(shallow);
    ASWToml::TASWTomlResult const notValid = document.SetOptions(invalid);

    // Assert
    CheckEquals(ETomlCode::NestingTooDeep, tooDeep.Code, "the text is nested deeper than the new options allow");
    CheckEquals(std::size_t(1), tooDeep.Line, "the place in the text");
    CheckEquals(std::size_t(7), tooDeep.Column, "the third array, the byte order mark not counted");
    CheckEquals(ETomlCode::InvalidOptions, notValid.Code, "options that aren't valid");
    CheckEquals(text, document.ToText(), "the text should be kept");
    CheckTrue(document.Find("a[0][0][0]") != nullptr, "the values too");
    CheckEquals("# "s, document.GetOptions().Writing.CommentPrefix, "and the options");
    CheckEquals(std::size_t(100), document.GetOptions().Reading.MaxNestingDepth, "the reading options too");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentParse, SetOptions_ReadsTheTextAgain)
{
    // Arrange
    std::string const text = Bom + "a = [[1]] # x\r\nb = 2\n";
    TASWTomlDocument document;
    CheckTrue(document.Parse(text).IsOk(), "the text should parse");
    TASWTomlOptions options;
    options.Reading.MaxNestingDepth = 2;
    options.Writing.LineEnding = ELineEnding::LF;

    // Act
    ASWToml::TASWTomlResult const result = document.SetOptions(options);

    // Assert
    CheckTrue(result.IsOk(), "the options should be set: " + result.Message);
    CheckEquals(std::size_t(2), document.GetOptions().Reading.MaxNestingDepth, "the new reading options");
    CheckEquals(Bom + "a = [[1]] # x\nb = 2\n", document.ToText(), "the new writing options apply to the text");
    CheckTrue(document.HasBom(), "the byte order mark is kept");
    CheckEquals(ELineBreak::CRLF, document.GetLineBreak(), "the line break new lines get (the first in a tie)");
    // (TArray{ TArray{ 1 } } would be a copy of the inner array, not an array holding it)
    TASWTomlValue const inner = TArray{ 1 };
    CheckTrue(document.GetRoot() == TASWTomlValue(TTable{ { "a", TArray{ inner } }, { "b", 2 } }),
        "the values read again");
    CheckFalse(document.IsModified(), "the modified flag is kept");

    // Act
    ASWToml::TASWTomlResult const own = document.SetOptions(document.GetOptions());

    // Assert
    CheckTrue(own.IsOk(), "its own options should do: " + own.Message);
    CheckEquals(std::size_t(2), document.GetOptions().Reading.MaxNestingDepth, "its own options kept");

    // Arrange & Act & Assert
    TASWTomlDocument empty;
    CheckTrue(empty.SetOptions(options).IsOk(), "a new document takes options");
    CheckEquals(""s, empty.ToText(), "and stays empty");
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
// TTest_ASWToml_DocumentFuzz
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_DocumentFuzz::TTest_ASWToml_DocumentFuzz()
    : inherited("ASWToml_DocumentFuzz_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_DocumentFuzz::~TTest_ASWToml_DocumentFuzz()
{
}
//---------------------------------------------------------------------------
ETomlCode TTest_ASWToml_DocumentFuzz::CheckParseOrFail(std::string const& text, TASWTomlOptions const& options,
    std::string const& name, std::source_location loc)
{
    std::string const before = "kept = 1\n";
    TASWTomlValue const beforeValues = TTable{ { "kept", 1 } };
    TASWTomlDocument document;
    CheckTrue(document.Parse(before).IsOk(), name + ": the first text should parse", loc);

    ASWToml::TASWTomlResult const result = document.Parse(text, options);
    if (!result.IsOk())
    {
        bool const known = result.Code == ETomlCode::ParseFailed || result.Code == ETomlCode::InvalidUtf8 ||
            result.Code == ETomlCode::ValueOutOfRange || result.Code == ETomlCode::NestingTooDeep ||
            result.Code == ETomlCode::DuplicateKey || result.Code == ETomlCode::UnsupportedEncoding;
        bool const unplaced = result.Code == ETomlCode::UnsupportedEncoding;
        bool const placed = unplaced ? result.Line == 0 && result.Column == 0 : IsInText(result, text);
        bool const kept = document.ToText() == before && document.GetRoot() == beforeValues &&
            document.GetOptions().Reading.MaxNestingDepth == ASWToml::TASWTomlReadingOptions::DefaultMaxNestingDepth;

        if (!known || result.Message.empty() || !placed || !kept)
        {
            CheckTrue(false, name + ": a text that doesn't parse should fail with a code of the text, a message and its "
                "place in the text, and leave the document as it was (" + ASWToml::ToString(result.Code) + " at " +
                std::to_string(result.Line) + ":" + std::to_string(result.Column) + ": " + result.Message + ")", loc);
        }

        return result.Code;
    }

    std::string failures;
    if (document.ToText() != text)
        failures += " It isn't written back byte for byte.";

    std::string const notFound = FindFailure(document, document.GetRoot(), ASWToml::TASWTomlPath());
    if (!notFound.empty())
        failures += " Find() doesn't give the value at " + notFound + ".";

    for (ELineBreak const lineBreak : { ELineBreak::LF, ELineBreak::CRLF })
    {
        TASWTomlOptions converting = options;
        converting.Writing.LineEnding = lineBreak == ELineBreak::LF ? ELineEnding::LF : ELineEnding::CRLF;
        std::string const expected = WithLineBreaks(text, ASWToml::Detail::LineBreakText(lineBreak));

        TASWTomlDocument converted;
        TASWTomlDocument reread;
        if (!converted.Parse(text, converting).IsOk() || converted.ToText() != expected ||
            !reread.Parse(expected, options).IsOk() || !IdenticalValues(reread.GetRoot(), document.GetRoot()))
        {
            failures += std::string(" Written with ") + ASWToml::ToString(lineBreak) + " line breaks, it doesn't read "
                "as the same values.";
        }
    }

    if (!failures.empty())
        CheckTrue(false, name + ": a text that parses should keep its text and values." + failures, loc);

    return ETomlCode::Ok;
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentFuzz::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentFuzz::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentFuzz::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentFuzz::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentFuzz, MutatedTexts_ParseOrFailCleanly)
{
    // Arrange
    // Our corpus and toml-test's valid files, mutated
    TStringVector sources;
    for (TCorpusText const& corpusText : RoundTripCorpus())
        sources.push_back(corpusText.Text);

    for (TTomlTestFile const& file : TomlTestValidFiles())
        sources.emplace_back(file.Text);

    TCodeCountMap counts;

    for (std::size_t i = 0; i < FuzzTextCount; ++i)
    {
        std::uint32_t const seed = FuzzFirstSeed + static_cast<std::uint32_t>(i);
        std::mt19937 generator(seed);
        std::string const text = MutatedText(generator, sources[generator() % sources.size()]);
        TASWTomlOptions const options = RandomOptions(generator);

        // Act & Assert
        ++counts[CheckParseOrFail(text, options, "seed " + std::to_string(seed) + ", \"" + Escaped(text) + "\"")];
    }

    // Assert
    CheckTrue(counts[ETomlCode::Ok] > 0, "some mutated texts should parse");
    CheckTrue(counts[ETomlCode::ParseFailed] > 0, "some should fail with ParseFailed");
    CheckTrue(counts[ETomlCode::DuplicateKey] > 0, "some should fail with DuplicateKey");
    CheckTrue(counts[ETomlCode::InvalidUtf8] > 0, "some should fail with InvalidUtf8");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentFuzz, RandomDocuments_ParseOrFailCleanly)
{
    // Arrange
    TCodeCountMap counts;

    for (std::size_t i = 0; i < FuzzTextCount; ++i)
    {
        std::uint32_t const seed = FuzzFirstSeed + static_cast<std::uint32_t>(i);
        std::mt19937 generator(seed);
        std::string const text = RandomDocumentText(generator);
        TASWTomlOptions const options = RandomOptions(generator);

        // Act & Assert
        ++counts[CheckParseOrFail(text, options, "seed " + std::to_string(seed) + ", \"" + Escaped(text) + "\"")];
    }

    // Assert
    CheckTrue(counts[ETomlCode::Ok] > FuzzTextCount / 10, "many random documents should parse");
    CheckTrue(counts[ETomlCode::DuplicateKey] > FuzzTextCount / 10, "many should break a table rule");
    CheckTrue(counts[ETomlCode::NestingTooDeep] > 0, "some should nest too deep");
    CheckTrue(counts[ETomlCode::ValueOutOfRange] > 0, "some should hold an integer too large");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentFuzz, RandomPieces_ParseOrFailCleanly)
{
    // Arrange
    TCodeCountMap counts;

    for (std::size_t i = 0; i < FuzzTextCount; ++i)
    {
        std::uint32_t const seed = FuzzFirstSeed + static_cast<std::uint32_t>(i);
        std::mt19937 generator(seed);
        std::string const text = RandomPieceText(generator);
        TASWTomlOptions const options = RandomOptions(generator);

        // Act & Assert
        ++counts[CheckParseOrFail(text, options, "seed " + std::to_string(seed) + ", \"" + Escaped(text) + "\"")];
    }

    // Assert
    CheckTrue(counts[ETomlCode::Ok] > 0, "some random texts should parse");
    CheckTrue(counts[ETomlCode::ParseFailed] > FuzzTextCount / 2, "most should fail with ParseFailed");
    CheckTrue(counts[ETomlCode::InvalidUtf8] > 0, "some should fail with InvalidUtf8");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentFuzz, RandomTexts_AreTheSameEverywhere)
{
    // Arrange
    // The standard requires the 10000th number of a default-constructed std::mt19937 to be 4123659995, so a seed in a
    // failure message gives the same text on every platform
    std::mt19937 generator;
    generator.discard(9999);
    std::mt19937 first(FuzzFirstSeed);
    std::mt19937 again(FuzzFirstSeed);
    std::mt19937 next(FuzzFirstSeed + 1);

    // Act
    std::uint32_t const tenThousandth = static_cast<std::uint32_t>(generator());
    std::string const text = RandomDocumentText(first);
    std::string const sameSeed = RandomDocumentText(again);
    std::string const nextSeed = RandomDocumentText(next);

    // Assert
    CheckTrue(tenThousandth == 4123659995u, "std::mt19937 should give the numbers the standard defines");
    CheckEquals(text, sameSeed, "the same seed should give the same text");
    CheckFalse(text == nextSeed, "another seed should give another text");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DocumentIndex
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_DocumentIndex::TTest_ASWToml_DocumentIndex()
    : inherited("ASWToml_DocumentIndex_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_DocumentIndex::~TTest_ASWToml_DocumentIndex()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentIndex::CheckIndexes(std::string const& text, TDocIndex& index, std::string const& msg,
    std::source_location loc)
{
    TDocModel model;
    ASWToml::TASWTomlResult result = ASWToml::Detail::ParseDocument(text, ASWToml::TASWTomlReadingOptions(), model);
    CheckTrue(result.IsOk(), msg + ": the text should parse: " + result.Message, loc);

    TASWTomlValue root;
    result = ASWToml::Detail::BuildIndex(model, text, 100, index, root);
    CheckTrue(result.IsOk(), msg + ": the index should build: " + result.Message, loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentIndex::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentIndex::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentIndex::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentIndex::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentIndex, BuildIndex_FailureLeavesTheIndexAsItWas)
{
    // Arrange
    TDocIndex index;
    CheckIndexes("a = 1\n[t]\nb = 2\n", index, "the first text");
    TDocModel model;
    CheckTrue(ASWToml::Detail::ParseDocument("x = 1\nx = 2\n", ASWToml::TASWTomlReadingOptions(), model).IsOk(),
        "the second text should parse");
    TASWTomlValue root = TASWTomlValue(TTable{ { "kept", true } });

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::BuildIndex(model, "x = 1\nx = 2\n", 100, index, root);

    // Assert
    CheckEquals(ETomlCode::DuplicateKey, result.Code, "a key defined twice");
    CheckEquals(std::size_t(2), result.Line, "the line of the second definition");
    CheckEquals(std::size_t(4), index.Nodes.size(), "the nodes should be the first text's");
    CheckEquals(std::size_t(3), index.Lines.size(), "the lines too");
    CheckTrue(root == TASWTomlValue(TTable{ { "kept", true } }), "the root should be left as it was");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentIndex, FindNode_FindsNodesByPath)
{
    // Arrange
    TDocIndex index;
    CheckIndexes("a = [1, [2, 3]]\n[t.u]\nx = { y = 1 }\n[[s]]\n[[s]]\nz = 1\n", index, "the text");

    // Act & Assert
    CheckEquals(std::size_t(0), ASWToml::Detail::FindNode(index, ""), "the root path is the root");
    CheckEquals("a"s, index.Nodes[ASWToml::Detail::FindNode(index, "a")].Key, "a key");
    CheckTrue(index.Nodes[ASWToml::Detail::FindNode(index, "a[1][0]")].Kind == ENodeKind::Scalar, "an element");
    CheckTrue(index.Nodes[ASWToml::Detail::FindNode(index, "t")].Kind == ENodeKind::ImplicitTable,
        "a table only a longer header made");
    CheckEquals("y"s, index.Nodes[ASWToml::Detail::FindNode(index, "t.u.x.y")].Key, "a key of an inline table");
    CheckTrue(index.Nodes[ASWToml::Detail::FindNode(index, "s")].Kind == ENodeKind::ArrayOfTables,
        "an array of tables");
    CheckEquals("z"s, index.Nodes[ASWToml::Detail::FindNode(index, "s[1].z")].Key, "a key of its second element");

    CheckEquals(NoIndex, ASWToml::Detail::FindNode(index, "b"), "a missing key");
    CheckEquals(NoIndex, ASWToml::Detail::FindNode(index, "a[2]"), "an index past the end");
    CheckEquals(NoIndex, ASWToml::Detail::FindNode(index, "a[0][0]"), "an index into a scalar");
    CheckEquals(NoIndex, ASWToml::Detail::FindNode(index, "t[0]"), "an index into a table");
    CheckEquals(NoIndex, ASWToml::Detail::FindNode(index, "t.u.x[0]"), "an index into an inline table");
    CheckEquals(NoIndex, ASWToml::Detail::FindNode(index, "a.b"), "a key of an array");
    CheckEquals(NoIndex, ASWToml::Detail::FindNode(index, "s.z"), "a key of an array of tables");
    CheckEquals(NoIndex, ASWToml::Detail::FindNode(index, "a..b"), "a path that isn't valid");
    CheckEquals(NoIndex, ASWToml::Detail::FindNode(TDocIndex(), ""), "an index that wasn't built");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentIndex, Lines_KindsAndRanges)
{
    // Arrange
    std::string const text = "# file\n\ntitle = \"x\" # note\r\n[server] # s\nports = [\n  80, # web\n  443,\n]\n"
        "  \"a b\".c = 1\n  [[srv]]\nname = \"one\"";
    TDocIndex index;

    // Act
    CheckIndexes(text, index, "the text");

    // Assert
    struct TLine
    {
        ELineKind Kind;
        std::string Text;
        std::string Trailing;
        std::size_t Block;
        std::size_t Entry;
        std::string Path; // Of its node; "-" for none
    };
    std::vector<TLine> const expected = {
        { ELineKind::Comment, "# file", "", 0, 0, "-" },
        { ELineKind::Blank, "", "", 0, 1, "-" },
        { ELineKind::KeyValue, "title = \"x\" # note", " # note", 0, 2, "title" },
        { ELineKind::Header, "[server] # s", " # s", 1, NoIndex, "server" },
        { ELineKind::KeyValue, "ports = [\n  80, # web\n  443,\n]", "", 1, 0, "server.ports" },
        { ELineKind::KeyValue, "  \"a b\".c = 1", "", 1, 1, "server.\"a b\".c" },
        { ELineKind::Header, "  [[srv]]", "", 2, NoIndex, "srv[0]" },
        { ELineKind::KeyValue, "name = \"one\"", "", 2, 0, "srv[0].name" },
    };

    CheckEquals(expected.size(), index.Lines.size(), "the number of lines");
    for (std::size_t i = 0; i < expected.size() && i < index.Lines.size(); ++i)
    {
        ASWToml::Detail::TIndexLine const& line = index.Lines[i];
        std::string const name = "line " + std::to_string(i);
        std::size_t const node = expected[i].Path == "-" ? NoIndex : ASWToml::Detail::FindNode(index, expected[i].Path);

        CheckTrue(line.Kind == expected[i].Kind, name + ": its kind");
        CheckEquals(expected[i].Text, Slice(text, line.Text), name + ": its text");
        CheckEquals(expected[i].Trailing, Slice(text, line.Trailing), name + ": the text after its value or header");
        CheckEquals(line.Text.End, line.Trailing.End, name + ": the trailing text ends the line");
        CheckEquals(expected[i].Block, line.Block, name + ": its block");
        CheckEquals(expected[i].Entry, line.Entry, name + ": its entry");
        CheckEquals(node, line.Node, name + ": its node");
    }

    // A line break of LF or CRLF, or none at the end
    CheckEquals(index.Lines[0].Text.End + 1, index.Lines[0].Next, "after a LF");
    CheckEquals(index.Lines[2].Text.End + 2, index.Lines[2].Next, "after a CRLF");
    CheckEquals(text.size(), index.Lines.back().Next, "the last line without a line break");
    CheckEquals(index.Lines[3].Next, index.Lines[4].Text.Begin, "each line starts where the one before it ends");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentIndex, Lines_StartEachBlock)
{
    // Arrange
    std::string const text = "[a]\n[b]\nx = 1\n\n[[c]]\n# c\n";
    TDocIndex index;

    // Act
    CheckIndexes(text, index, "the text");

    // Assert
    // The root block is empty, so its first line is the next block's
    CheckTrue(index.BlockLines == ASWToml::Detail::TIndexVector{ 0, 0, 1, 4 }, "the first line of each block");
    CheckEquals("[b]"s, Slice(text, index.Lines[index.BlockLines[2]].Text), "the third block starts with its header");

    // Arrange & Act & Assert
    CheckIndexes("", index, "an empty text");
    CheckTrue(index.BlockLines == ASWToml::Detail::TIndexVector{ 0 }, "an empty text has one block");
    CheckTrue(index.Lines.empty(), "and no lines");
    CheckEquals(std::size_t(1), index.Nodes.size(), "and the root table only");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentIndex, Names_EveryKeyPartThatNamesANode)
{
    // Arrange
    std::string const text = "title = 1\na.'b c'.d = 2\n[srv]\n[[ srv . list ]]\n[srv.list.tls]\n[[srv.list]]\n"
        "p = { x = 1, y.z = 2 }\n";
    TDocIndex index;

    // Act
    CheckIndexes(text, index, "the text");

    // Assert
    CheckEquals("title@0 "s, NamesAt(text, index, "title"), "the key of a key/value line");
    CheckEquals("a@10 "s, NamesAt(text, index, "a"), "a dotted table");
    CheckEquals("'b c'@12 "s, NamesAt(text, index, "a.\"b c\""), "a quoted part, with its quotes");
    CheckEquals("d@18 "s, NamesAt(text, index, "a.\"b c\".d"), "the last part of a dotted key");
    CheckEquals("srv@25 srv@33 srv@48 srv@64 "s, NamesAt(text, index, "srv"),
        "a table: its header, and the headers that go through it");
    CheckEquals("list@39 list@52 list@68 "s, NamesAt(text, index, "srv.list"),
        "an array of tables: each [[...]] header, and its elements' sub-tables' headers");
    CheckEquals(""s, NamesAt(text, index, "srv.list[0]"), "an element has no name");
    CheckEquals("tls@57 "s, NamesAt(text, index, "srv.list[0].tls"), "a table under an element");
    CheckEquals("p@75 "s, NamesAt(text, index, "srv.list[1].p"), "the key of an inline table");
    CheckEquals("x@81 "s, NamesAt(text, index, "srv.list[1].p.x"), "a key of an inline table");
    CheckEquals("y@88 "s, NamesAt(text, index, "srv.list[1].p.y"), "a dotted table in an inline table");
    CheckEquals(""s, NamesAt(text, index, ""), "the root has no name");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentIndex, Nodes_TheLinesAndBlocksTheyAreDefinedIn)
{
    // Arrange
    std::string const text = "a = 1\nb.c = 2\nd = { e = 3 }\n[t]\n[[s]]\n[s.u]\n[[s]]\nf = [4]\n";
    TDocIndex index;

    // Act
    CheckIndexes(text, index, "the text");

    // Assert
    CheckEquals(std::size_t(0), NodeAt(index, "a").Line, "a key's line");
    CheckEquals(NoIndex, NodeAt(index, "a").Block, "a key has no block of its own");
    CheckEquals(NoIndex, NodeAt(index, "b").Line, "a dotted table has no line of its own");
    CheckEquals(std::size_t(1), NodeAt(index, "b.c").Line, "the last part of a dotted key has its line");
    CheckEquals(std::size_t(2), NodeAt(index, "d").Line, "an inline table's key line");
    CheckEquals(NoIndex, NodeAt(index, "d.e").Line, "a key in an inline table has no line of its own");
    CheckEquals(std::size_t(3), NodeAt(index, "t").Line, "a header table's header");
    CheckEquals(std::size_t(1), NodeAt(index, "t").Block, "a header table's block");
    CheckEquals(NoIndex, NodeAt(index, "s").Line, "an array of tables has no line of its own");
    CheckEquals(std::size_t(4), NodeAt(index, "s[0]").Line, "an element's header");
    CheckEquals(std::size_t(2), NodeAt(index, "s[0]").Block, "an element's block");
    CheckEquals(std::size_t(3), NodeAt(index, "s[0].u").Block, "a table under an element");
    CheckEquals(std::size_t(4), NodeAt(index, "s[1]").Block, "the second element's block");
    CheckEquals(std::size_t(7), NodeAt(index, "s[1].f").Line, "a key of the second element");
    CheckEquals(NoIndex, NodeAt(index, "s[1].f[0]").Line, "an element of an array has no line of its own");
    CheckEquals(NoIndex, NodeAt(index, "").Line, "the root has no line");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentIndex, Values_ItemsAndClosingBrackets)
{
    // Arrange
    std::string const text = "s = 'x'\nports = [\n  80, # web\n  443,\n]\np = { x = 1, y = 2 }\ne = []\nt = {}\n"
        "n = [ [1], 2 ]\n";
    TDocIndex index;

    // Act
    CheckIndexes(text, index, "the text");

    // Assert
    CheckEquals("'x'"s, Slice(text, NodeAt(index, "s").Value), "a scalar's text");
    CheckEquals("[\n  80, # web\n  443,\n]"s, Slice(text, NodeAt(index, "ports").Value), "an array with its brackets");
    CheckEquals("\n]"s, Slice(text, NodeAt(index, "ports").Close), "the text before its closing bracket, and it");
    CheckEquals("80"s, Slice(text, NodeAt(index, "ports[0]").Value), "an element");
    CheckEquals("\n  80,"s, Slice(text, NodeAt(index, "ports[0]").Item), "an element, the text before it, a comma");
    CheckEquals(" # web\n  443,"s, Slice(text, NodeAt(index, "ports[1]").Item),
        "the comment after the comma before it is in its text");
    CheckEquals("{ x = 1, y = 2 }"s, Slice(text, NodeAt(index, "p").Value), "an inline table with its braces");
    CheckEquals(" x = 1,"s, Slice(text, NodeAt(index, "p.x").Item), "an entry with its comma");
    CheckEquals(" y = 2"s, Slice(text, NodeAt(index, "p.y").Item), "the last entry, without a comma");
    CheckEquals("1"s, Slice(text, NodeAt(index, "p.x").Value), "an entry's value");
    CheckEquals(" }"s, Slice(text, NodeAt(index, "p").Close), "an inline table's closing brace");
    CheckEquals("[]"s, Slice(text, NodeAt(index, "e").Value), "an empty array");
    CheckEquals("]"s, Slice(text, NodeAt(index, "e").Close), "its closing bracket");
    CheckEquals("{}"s, Slice(text, NodeAt(index, "t").Value), "an empty inline table");
    CheckEquals("}"s, Slice(text, NodeAt(index, "t").Close), "its closing brace");
    CheckEquals("[1]"s, Slice(text, NodeAt(index, "n[0]").Value), "an array in an array");
    CheckEquals(" [1],"s, Slice(text, NodeAt(index, "n[0]").Item), "its item");
    CheckEquals("1"s, Slice(text, NodeAt(index, "n[0][0]").Item), "an element of the inner array");
    CheckEquals(" 2"s, Slice(text, NodeAt(index, "n[1]").Item), "the last element");
    CheckEquals(" ]"s, Slice(text, NodeAt(index, "n").Close), "the text after the last element");
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
// TTest_ASWToml_DocumentRoundTrip
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_DocumentRoundTrip::TTest_ASWToml_DocumentRoundTrip()
    : inherited("ASWToml_DocumentRoundTrip_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_DocumentRoundTrip::~TTest_ASWToml_DocumentRoundTrip()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentRoundTrip::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentRoundTrip::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentRoundTrip::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentRoundTrip::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentRoundTrip, Corpus_ConvertedLineBreaksKeepTheValues)
{
    for (TCorpusText const& corpusText : RoundTripCorpus())
    {
        for (ELineBreak const lineBreak : { ELineBreak::LF, ELineBreak::CRLF })
        {
            // Arrange
            std::string const name = corpusText.Name + " with " + ASWToml::ToString(lineBreak);
            TASWTomlOptions options;
            options.Writing.LineEnding = lineBreak == ELineBreak::LF ? ELineEnding::LF : ELineEnding::CRLF;
            TASWTomlDocument document;
            CheckTrue(document.Parse(corpusText.Text, options).IsOk(), name + ": the text should parse");

            // Act
            std::string const converted = document.ToText();
            TASWTomlDocument reread;
            ASWToml::TASWTomlResult const result = reread.Parse(converted);

            // Assert
            CheckEquals(Escaped(WithLineBreaks(corpusText.Text, ASWToml::Detail::LineBreakText(lineBreak))),
                Escaped(converted), name + ": every line break converted, nothing else");
            CheckTrue(result.IsOk(), name + ": the converted text should parse: " + result.Message);
            CheckTrue(IdenticalValues(corpusText.Expected, reread.GetRoot()), name + ": the same values");
        }
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentRoundTrip, Corpus_HasTheExpectedValues)
{
    for (TCorpusText const& corpusText : RoundTripCorpus())
    {
        // Arrange
        TASWTomlDocument document;

        // Act
        ASWToml::TASWTomlResult const result = document.Parse(corpusText.Text);

        // Assert
        CheckTrue(result.IsOk(), corpusText.Name + ": the text should parse: " + result.Message);
        CheckTrue(IdenticalValues(corpusText.Expected, document.GetRoot()),
            corpusText.Name + ": the values, with each table's keys in order");
        CheckEquals(""s, FindFailure(document, document.GetRoot(), ASWToml::TASWTomlPath()),
            corpusText.Name + ": Find() should give each value by its path");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentRoundTrip, Corpus_WritesBackByteForByte)
{
    for (TCorpusText const& corpusText : RoundTripCorpus())
    {
        // Arrange
        TASWTomlDocument document;
        CheckTrue(document.Parse(corpusText.Text).IsOk(), corpusText.Name + ": the text should parse");

        // Act
        std::string const written = document.ToText();

        // Assert
        CheckEquals(Escaped(corpusText.Text), Escaped(written), corpusText.Name + ": the text written");
    }
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

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DocumentSetValue
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_DocumentSetValue::TTest_ASWToml_DocumentSetValue()
    : inherited("ASWToml_DocumentSetValue_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_DocumentSetValue::~TTest_ASWToml_DocumentSetValue()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentSetValue::CheckFails(std::string const& text, ASWToml::TASWTomlPath const& path,
    TASWTomlValue const& value, ETomlCode code, std::string const& expectedText, std::string const& msg,
    std::source_location loc)
{
    TASWTomlDocument document;
    CheckTrue(document.Parse(text).IsOk(), msg + ": the text should parse", loc);

    ASWToml::TASWTomlResult const result = document.SetValue(path, value);

    CheckEquals(code, result.Code, msg + ": the code", loc);
    CheckContains(result.Message, expectedText, msg + ": the message", loc);
    CheckEquals(std::size_t(0), result.Line, msg + ": not about a place in the text", loc);
    CheckEquals(text, document.ToText(), msg + ": the text should be kept", loc);
    CheckFalse(document.IsModified(), msg + ": the document isn't modified", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentSetValue::CheckSets(std::string const& text, ASWToml::TASWTomlPath const& path,
    TASWTomlValue const& value, std::string const& expected, std::string const& msg, TASWTomlOptions const& options,
    std::source_location loc)
{
    TASWTomlDocument document;
    ASWToml::TASWTomlResult const parsed = document.Parse(text, options);
    CheckTrue(parsed.IsOk(), msg + ": the text should parse: " + parsed.Message, loc);

    ASWToml::TASWTomlResult const result = document.SetValue(path, value);

    CheckTrue(result.IsOk(), msg + ": the value should be set: " + result.Message, loc);
    CheckEquals(Escaped(expected), Escaped(document.ToText()), msg + ": the text", loc);
    TASWTomlValue const* const set = document.Find(path);
    CheckTrue(set != nullptr && ASWToml::Detail::IdenticalValues(*set, value), msg + ": the value at the path", loc);
    CheckTrue(document.IsModified(), msg + ": the document is modified", loc);

    TASWTomlDocument reread;
    CheckTrue(reread.Parse(document.ToText(), options).IsOk() &&
        ASWToml::Detail::IdenticalValues(reread.GetRoot(), document.GetRoot()),
        msg + ": the text should read as the document's values", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentSetValue::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentSetValue::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentSetValue::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentSetValue::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentSetValue, ExistingScalars_KeepTheirForm)
{
    // Arrange & Act & Assert
    CheckSets("port = 0xff # mask\n", "port", 4095, "port = 0xFFF # mask\n", "hexadecimal stays hexadecimal");
    CheckSets("a  =   1   # c\n", "a", 2, "a  =   2   # c\n", "the blanks around '=' and the comment stay");
    CheckSets("path = 'C:\\x'\n", "path", "C:\\y", "path = 'C:\\y'\n", "a literal string stays literal");
    CheckSets("s = 'x'\n", "s", "it's", "s = \"it's\"\n", "a literal string that can't hold the text is basic");
    CheckSets("s = \"x\"\n", "s", "C:\\y", "s = \"C:\\\\y\"\n", "a basic string stays basic");
    CheckSets("text = \"\"\"\nold\"\"\"\n", "text", "a\nb", "text = \"\"\"\na\nb\"\"\"\n", "a multi-line basic string");
    CheckSets("text = '''old'''\n", "text", "C:\\x\ny", "text = '''C:\\x\ny'''\n", "a multi-line literal string");
    CheckSets("n = 0o17\n", "n", -1, "n = -1\n", "a negative number is decimal");
    CheckSets("a = 1\n", "a", "one", "a = \"one\"\n", "a value of another type");
    CheckSets("a = 1979-05-27 # d\n", "a", true, "a = true # d\n", "a date to a boolean");
    CheckSets("[t]\nx = 1.5\n", "t.x", 2.0, "[t]\nx = 2.0\n", "a key of a header table");
    CheckSets("p = { x = 0x1, y = 2 }\n", "p.x", 255, "p = { x = 0xFF, y = 2 }\n", "a key of an inline table");
    CheckSets("a = [1, 'x', 3]\n", "a[1]", "y", "a = [1, 'y', 3]\n", "an element of an array");
    CheckSets("[[s]]\nx = 1\n[[s]]\nx = 2\n", "s[1].x", 3, "[[s]]\nx = 1\n[[s]]\nx = 3\n",
        "a key of the second element of an array of tables");
    CheckSets("a = 1\nb = 2\n", "a", 5, "a = 5\nb = 2\n", "only the value changes");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentSetValue, ExistingValues_ArraysAndTablesOnOneLine)
{
    // Arrange & Act & Assert
    // (Until the whole-value merge comes, an array or inline table that exists is written again on one line)
    CheckSets("a = [\n  1,\n  2,\n] # c\n", "a", TArray{ 3, 4 }, "a = [3, 4] # c\n", "an array");
    CheckSets("p = { x = 1 }\n", "p", TTable{ { "y", 2 } }, "p = { y = 2 }\n", "an inline table");
    CheckSets("a = 1 # c\n", "a", TTable{ { "x", 1 } }, "a = { x = 1 } # c\n", "a scalar to a table, in place");
    CheckSets("a = 1\n", "a", TArray{ TTable{ { "x", 1 } } }, "a = [{ x = 1 }]\n",
        "an array of tables in place, as an array of inline tables");
    CheckSets("a = [[1], 2]\n", "a[0]", TArray{ 5 }, "a = [[5], 2]\n", "an array in an array");
    CheckSets("p = { x = 1 }\n", "p", 1, "p = 1\n", "an inline table to a scalar");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentSetValue, IdenticalValue_ChangesNothing)
{
    // Arrange
    double const nan = std::numeric_limits<double>::quiet_NaN();
    struct TCase
    {
        std::string Text;
        std::string Path;
        TASWTomlValue Value;
    };
    std::vector<TCase> const cases = {
        { "a = 1\n", "a", 1 },
        { "a = 0x1\n", "a", 1 },
        { "a = nan\n", "a", nan },
        { "a = -nan\n", "a", std::copysign(nan, -1.0) },
        { "p = { x = 1, y = 2 }\n", "p", TTable{ { "y", 2 }, { "x", 1 } } },
        { "[t]\nx = 1\n", "t", TTable{ { "x", 1 } } },
        { "x = 1\n", "", TTable{ { "x", 1 } } },
        { "a = [1, 2]\n", "a", TArray{ 1, 2 } },
    };

    for (TCase const& item : cases)
    {
        TASWTomlDocument document;
        CheckTrue(document.Parse(item.Text).IsOk(), Escaped(item.Text) + ": the text should parse");

        // Act
        ASWToml::TASWTomlResult const result = document.SetValue(item.Path, item.Value);

        // Assert
        CheckTrue(result.IsOk(), Escaped(item.Text) + ": setting the same value succeeds: " + result.Message);
        CheckEquals(item.Text, document.ToText(), Escaped(item.Text) + ": the text stays as it was");
        CheckFalse(document.IsModified(), Escaped(item.Text) + ": the document isn't modified");
    }

    // Arrange & Act & Assert
    // A float of the other sign isn't the same value
    CheckSets("a = 0.0\n", "a", -0.0, "a = -0.0\n", "-0.0 isn't 0.0");
    CheckSets("a = nan\n", "a", std::copysign(nan, -1.0), "a = -nan\n", "a NaN of the other sign");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentSetValue, LineBreaks_TheDocumentsOwn)
{
    // Arrange
    std::string const native(ASWToml::Detail::LineBreakText(ASWToml::Detail::NativeLineBreak()));
    TASWTomlOptions lf;
    lf.Writing.LineEnding = ELineEnding::LF;

    // Act & Assert
    CheckSets("a = 1\r\nb = 2\r\n", "c", 3, "a = 1\r\nb = 2\r\nc = 3\r\n", "a text with CRLF");
    CheckSets("a = 1\r\nb = 2\r\n[t]\n", "t.x", 1, "a = 1\r\nb = 2\r\n[t]\nx = 1\r\n", "the most common one");
    CheckSets("a = 1\nb = 2", "c", 3, "a = 1\nb = 2\nc = 3\n", "a last line without a line break gets one");
    CheckSets("", "a", 1, "a = 1" + native, "an empty text: the platform's");
    CheckSets("a = 1\r\n", "b", 2, "a = 1\nb = 2\n", "converted as the writing options say", lf);
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentSetValue, NewKeys_AsDottedKeys)
{
    // Arrange & Act & Assert
    CheckSets("server.port = 80\n[t]\n", "server.host", "h", "server.port = 80\nserver.host = \"h\"\n[t]\n",
        "after the last dotted key of the table");
    CheckSets("  a . b.c = 1\nx = 2\n", "a.b.d", 2, "  a . b.c = 1\n  a . b.d = 2\nx = 2\n",
        "the key parts and the indentation of the last one");
    CheckSets("[t]\nu.a = 1\nu.b = 2\nz = 3\n", "t.u.c", 3, "[t]\nu.a = 1\nu.b = 2\nu.c = 3\nz = 3\n",
        "in a header table");
    CheckSets("a.b = 1\n", "a.c", TTable{ { "d", 1 }, { "e", TTable{} } }, "a.b = 1\na.c.d = 1\na.c.e = {}\n",
        "a table as more dotted keys, an empty one as {}");
    CheckSets("a.b = 1\n", "a.c.d", 1, "a.b = 1\na.c.d = 1\n", "a missing table as a dotted key");
    CheckSets("a.b = 1\n", "a.s", TArray{ TTable{ { "x", 1 } } }, "a.b = 1\na.s = [{ x = 1 }]\n",
        "an array of tables as an array of inline tables");
    CheckSets("a.\"b c\".d = 1\n", "a.\"b c\".e", 2, "a.\"b c\".d = 1\na.\"b c\".e = 2\n", "quoted parts as written");
    CheckSets("a.x = 1\n[a.t]\ny = 2\n", "a.z", 3, "a.x = 1\na.z = 3\n[a.t]\ny = 2\n",
        "after the dotted keys, not in the header of a table under it");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentSetValue, NewKeys_InHeaderTablesAndElements)
{
    // Arrange & Act & Assert
    CheckSets("[t]\nx = 1\n\n# about u\n[u]\n", "t.y", 2, "[t]\nx = 1\ny = 2\n\n# about u\n[u]\n",
        "after the last key line, before the blank and comment lines");
    CheckSets("[t]\n# nothing\n[u]\n", "t.y", 2, "[t]\ny = 2\n# nothing\n[u]\n", "right after a header without keys");
    CheckSets("[t]\n  x = 1\n", "t.y", 2, "[t]\n  x = 1\n  y = 2\n", "with the last key line's indentation");
    CheckSets("[t]\nx = [\n  1,\n]\n", "t.y", 2, "[t]\nx = [\n  1,\n]\ny = 2\n", "after a value on several lines");
    CheckSets("[[s]]\nx = 1\n[[s]]\nx = 2\n", "s[0].y", 3, "[[s]]\nx = 1\ny = 3\n[[s]]\nx = 2\n",
        "in the first element of an array of tables");
    CheckSets("[t]\nx = 1\n[t.u]\n", "t.y", 2, "[t]\nx = 1\ny = 2\n[t.u]\n", "before the header of a sub-table");
    CheckSets("[t]\nu.a = 1\n", "t.z", 2, "[t]\nu.a = 1\nz = 2\n", "after a dotted key line");
    CheckSets("[\"a b\"]\n", "\"a b\".\"c d\"", 1, "[\"a b\"]\n\"c d\" = 1\n", "a key quoted where needed");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentSetValue, NewKeys_InInlineTables)
{
    // Arrange & Act & Assert
    CheckSets("p = { x = 1 }\n", "p.y", 2, "p = { x = 1, y = 2 }\n", "after the last entry");
    CheckSets("p = {x=1}\n", "p.y", 2, "p = {x=1, y = 2}\n", "after an entry without blanks");
    CheckSets("p = {}\n", "p.y", 2, "p = { y = 2 }\n", "in an empty inline table");
    CheckSets("p = {  }\n", "p.y", 2, "p = { y = 2 }\n", "in an empty inline table with blanks");
    CheckSets("p = { x = 1, }\n", "p.y", 2, "p = { x = 1, y = 2, }\n", "after a trailing comma (TOML 1.1)");
    CheckSets("p = { a.b = 1, c = 2 }\n", "p.a.d", 3, "p = { a.b = 1, c = 2, a.d = 3 }\n",
        "a dotted key in an inline table");
    CheckSets("p = { a.b.c = 1, x = 2 }\n", "p.a.b.d", 2, "p = { a.b.c = 1, x = 2, a.b.d = 2 }\n",
        "a dotted key of two tables, at the end of the inline table");
    CheckSets("p = { x = 1 }\n", "p.q.r", 1, "p = { x = 1, q = { r = 1 } }\n", "a missing table as an inline table");
    CheckSets("a = [{ x = 1 }]\n", "a[0].y", 2, "a = [{ x = 1, y = 2 }]\n", "an inline table in an array");
    CheckSets("p = {\n  x = 1,\n}\n", "p.y", 2, "p = {\n  x = 1,\n  y = 2,\n}\n",
        "an inline table on several lines (TOML 1.1)");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentSetValue, NewKeys_InTheRoot)
{
    // Arrange
    std::string const native(ASWToml::Detail::LineBreakText(ASWToml::Detail::NativeLineBreak()));

    // Act & Assert
    CheckSets("# file\n\n[t]\nx = 1\n", "b", "new", "# file\n\nb = \"new\"\n\n[t]\nx = 1\n",
        "after the file comment and the blank line under it");
    CheckSets("# file\n\n\n# more\n[t]\n", "a", 1, "# file\n\n\na = 1\n\n# more\n[t]\n",
        "after the blank lines under the file comment, apart from the next comment");
    CheckSets("  a = 1\n[t]\n", "b", 2, "  a = 1\n  b = 2\n[t]\n", "after the last key, with its indentation");
    CheckSets("[t]\n", "a", 1, "a = 1\n\n[t]\n", "at the start, apart from the first header");
    CheckSets("# about t\n[t]\n", "a", 1, "a = 1\n\n# about t\n[t]\n", "before the first header's comment");
    CheckSets("\n[t]\n", "a", 1, "a = 1\n\n[t]\n", "at the start, before a blank line");
    CheckSets("# c\n# d\n", "a", 1, "# c\n# d\n\na = 1\n", "after a file comment that ends the text");
    CheckSets("", "a", 1, "a = 1" + native, "in an empty text");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentSetValue, NewTables_AsHeaders)
{
    // Arrange & Act & Assert
    CheckSets("a = 1\n", "t", TTable{ { "x", 1 }, { "u", TTable{ { "y", 2 } } } },
        "a = 1\n\n[t]\nx = 1\n\n[t.u]\ny = 2\n", "a table and its sub-table at the end");
    CheckSets("[a]\nx = 1\n\n[c]\ny = 2\n", "a.b", TTable{ { "z", 3 } }, "[a]\nx = 1\n\n[a.b]\nz = 3\n\n[c]\ny = 2\n",
        "after its parent's block, not at the end");
    CheckSets("[a]\nx = 1\n[c]\n", "a.b", TTable{ { "z", 3 } }, "[a]\nx = 1\n\n[a.b]\nz = 3\n\n[c]\n",
        "with a blank line before and after it");
    CheckSets("[a]\n[a.b]\n", "a.c", TTable{ { "x", 1 } }, "[a]\n[a.b]\n\n[a.c]\nx = 1\n",
        "after its parent's sub-tables");
    CheckSets("[a]\nx = 1\n", "a.b.c.d", 1, "[a]\nx = 1\n\n[a.b.c]\nd = 1\n",
        "missing tables as one header, the ones with only tables left implicit");
    CheckSets("x = 1\n\n# about a.b\n[a.b]\ny = 2\n", "a.z", 3, "x = 1\n\n[a]\nz = 3\n\n# about a.b\n[a.b]\ny = 2\n",
        "a table only a longer header made gets its header before that header's comment");
    CheckSets("[[s]]\nx = 1\n[[s]]\nx = 2\n", "s[0].t", TTable{ { "y", 1 } },
        "[[s]]\nx = 1\n\n[s.t]\ny = 1\n\n[[s]]\nx = 2\n", "a table under the first element stays in it");
    CheckSets("a = 1\n", "s", TArray{ TTable{ { "x", 1 } }, TTable{ { "x", 2 } } },
        "a = 1\n\n[[s]]\nx = 1\n\n[[s]]\nx = 2\n", "an array of tables");
    CheckSets("a = 1\n", "t", TTable{}, "a = 1\n\n[t]\n", "an empty table");
    CheckSets("a = 1\n# end\n", "t", TTable{ { "x", 1 } }, "a = 1\n# end\n\n[t]\nx = 1\n",
        "a table of the root at the end of the text");
    CheckSets("[\"a b\"]\n", "\"a b\".c", TTable{ { "x", 1 } }, "[\"a b\"]\n\n[\"a b\".c]\nx = 1\n",
        "the parent's key parts as written");
    CheckSets("[ a . b ]\n", "a.b.c", TTable{ { "x", 1 } }, "[ a . b ]\n\n[a . b.c]\nx = 1\n",
        "the parent's key parts with their blanks");
    CheckSets("a = 1\nb = 2", "t", TTable{ { "x", 1 } }, "a = 1\nb = 2\n\n[t]\nx = 1\n",
        "after a last line without a line break");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentSetValue, NewTables_BlankLinesAsTheOptionsSay)
{
    // Arrange
    TASWTomlOptions options;
    options.Writing.BlankLineBeforeNewTable = false;

    // Act & Assert
    CheckSets("a = 1\n", "t", TTable{ { "x", 1 } }, "a = 1\n[t]\nx = 1\n", "no blank line before a new header",
        options);
    CheckSets("[a]\nx = 1\n[c]\n", "a.b", TTable{ { "z", 3 } }, "[a]\nx = 1\n[a.b]\nz = 3\n[c]\n",
        "nor after one", options);
    CheckSets("[t]\n", "a", 1, "a = 1\n[t]\n", "nor between a new root key and a header", options);
    CheckSets("a = 1\n", "s", TArray{ TTable{}, TTable{} }, "a = 1\n[[s]]\n[[s]]\n", "nor between elements",
        options);
    CheckSets("x = 1\n[a.b]\n", "a.z", 3, "x = 1\n[a]\nz = 3\n[a.b]\n", "nor around a header of an implicit table",
        options);

    // Arrange & Act & Assert
    // With the option on, elements after a blank line (so the first needs none) are apart from each other
    CheckSets("a = 1\n\n", "s", TArray{ TTable{ { "x", 1 } }, TTable{ { "x", 2 } } },
        "a = 1\n\n[[s]]\nx = 1\n\n[[s]]\nx = 2\n", "elements after a blank line");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentSetValue, RejectsWhatCantBeSet)
{
    // Arrange
    std::chrono::year_month_day const badDate{ std::chrono::year{ 2023 }, std::chrono::February,
        std::chrono::day{ 29 } };

    // Act & Assert
    CheckFails("a = 1\n", "a..b", 1, ETomlCode::InvalidArgument, "isn't valid", "a path that isn't valid");
    CheckFails("port = 80\n", "port.x", 1, ETomlCode::InvalidArgument,
        "Can't set port.x: port is a value of type Integer, not a table.", "a key of a scalar");
    CheckFails("a = [1]\n", "a.b", 1, ETomlCode::InvalidArgument, "a is an array, so an index must come before a key.",
        "a key of an array");
    CheckFails("[t]\n", "t[0]", 1, ETomlCode::InvalidArgument, "t isn't an array.", "an index of a table");
    CheckFails("a = [1]\n", "a[3]", 1, ETomlCode::NotFound, "the array a has 1 element.", "an index past the end");
    CheckFails("a = 1\n", "b[0]", 1, ETomlCode::NotFound, "b doesn't exist, so it has no elements.",
        "an index of a missing key");
    CheckFails("a = 1\n", "", 1, ETomlCode::InvalidArgument, "Can't set the root table: it can only be set to a table.",
        "the root to a scalar");
    CheckFails("[t]\nx = 1\n", "t", TTable{ { "x", 2 } }, ETomlCode::InvalidArgument,
        "a table can't be set as a whole yet", "a header table as a whole");
    CheckFails("x = 1\n", "", TTable{ { "x", 2 } }, ETomlCode::InvalidArgument, "a table can't be set as a whole yet",
        "the root as a whole");
    CheckFails("[[s]]\n", "s", TArray{ TTable{}, TTable{} }, ETomlCode::InvalidArgument,
        "an array of tables can't be set as a whole yet", "an array of tables as a whole");
    CheckFails("[t]\nx = 1\n", "t", 1, ETomlCode::InvalidArgument, "a table can't be set as a whole yet",
        "a header table to a scalar");
    CheckFails("a = 1\n", "b", "\xFF", ETomlCode::InvalidValue, "isn't valid UTF-8", "a string that isn't UTF-8");
    CheckFails("a = 1\n", "a", badDate, ETomlCode::InvalidValue, "The date can't be written", "a date that isn't");

    // Arrange
    TASWTomlOptions shallow;
    shallow.Reading.MaxNestingDepth = 2;
    TASWTomlDocument document;
    CheckTrue(document.Parse("a = [1]\n", shallow).IsOk(), "the text should parse");
    TASWTomlValue const deep = TArray{ TASWTomlValue(TArray{ 1 }) };
    ASWToml::TASWTomlValueOptions badValues;
    badValues.StringStyle = static_cast<ASWToml::EStringStyle>(9);

    // Act
    ASWToml::TASWTomlResult const tooDeep = document.SetValue("t.b", deep);
    ASWToml::TASWTomlResult const deepElement = document.SetValue("a[0]", deep);
    ASWToml::TASWTomlResult const fits = document.SetValue("b", deep);
    ASWToml::TASWTomlResult const fitsInPlace = document.SetValue("a", deep);
    ASWToml::TASWTomlResult const badOptions = document.SetValue("c", 1, badValues);

    // Assert
    CheckEquals(ETomlCode::NestingTooDeep, tooDeep.Code, "a table and two arrays are 3 deep, beyond 2");
    CheckEquals(ETomlCode::NestingTooDeep, deepElement.Code, "an element of an array and two arrays in it");
    CheckTrue(fits.IsOk(), "two arrays in the root fit: " + fits.Message);
    CheckTrue(fitsInPlace.IsOk(), "and in place of a key's value: " + fitsInPlace.Message);
    CheckEquals(ETomlCode::InvalidOptions, badOptions.Code, "value options that aren't valid");
    CheckEquals("a = [[1]]\nb = [[1]]\n"s, document.ToText(), "only the edits that fit changed the text");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentSetValue, ValueOptions_PerCall)
{
    // Arrange
    ASWToml::TASWTomlValueOptions hexadecimal;
    hexadecimal.IntegerBase = ASWToml::EIntegerBase::Hexadecimal;
    ASWToml::TASWTomlValueOptions decimal;
    decimal.IntegerBase = ASWToml::EIntegerBase::Decimal;
    TASWTomlOptions literal;
    literal.Values.StringStyle = ASWToml::EStringStyle::Literal;
    TASWTomlDocument document;
    CheckTrue(document.Parse("a = 0xFF\n", literal).IsOk(), "the text should parse");

    // Act
    ASWToml::TASWTomlResult const first = document.SetValue("b", 255, hexadecimal);
    ASWToml::TASWTomlResult const second = document.SetValue("a", 10, decimal);
    ASWToml::TASWTomlResult const third = document.SetValue("s", "x");
    ASWToml::TASWTomlResult const fourth = document.SetValue("n", 16);

    // Assert
    CheckTrue(first.IsOk() && second.IsOk() && third.IsOk() && fourth.IsOk(), "the values should be set");
    CheckEquals("a = 10\nb = 0xFF\ns = 'x'\nn = 16\n"s, document.ToText(),
        "the options of each call, else the document's");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DocumentAppend
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_DocumentAppend::TTest_ASWToml_DocumentAppend()
    : inherited("ASWToml_DocumentAppend_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_DocumentAppend::~TTest_ASWToml_DocumentAppend()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentAppend::CheckAppends(std::string const& text, ASWToml::TASWTomlPath const& path,
    TASWTomlValue const& value, std::string const& expected, std::string const& msg, std::source_location loc)
{
    TASWTomlDocument document;
    CheckTrue(document.Parse(text).IsOk(), msg + ": the text should parse", loc);

    ASWToml::TASWTomlResult const result = document.Append(path, value);

    CheckTrue(result.IsOk(), msg + ": the value should be appended: " + result.Message, loc);
    CheckEquals(Escaped(expected), Escaped(document.ToText()), msg + ": the text", loc);
    TASWTomlValue const* const array = document.Find(path);
    CheckTrue(array != nullptr && array->IsArray() && !array->AsArray()->empty() &&
        ASWToml::Detail::IdenticalValues(array->AsArray()->back(), value), msg + ": the value at the end", loc);
    CheckTrue(document.IsModified(), msg + ": the document is modified", loc);

    TASWTomlDocument reread;
    CheckTrue(reread.Parse(document.ToText()).IsOk() && reread.GetRoot() == document.GetRoot(),
        msg + ": the text should read as the document's values", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentAppend::CheckFails(std::string const& text, ASWToml::TASWTomlPath const& path,
    TASWTomlValue const& value, ETomlCode code, std::string const& expectedText, std::string const& msg,
    std::source_location loc)
{
    TASWTomlDocument document;
    CheckTrue(document.Parse(text).IsOk(), msg + ": the text should parse", loc);

    ASWToml::TASWTomlResult const result = document.Append(path, value);

    CheckEquals(code, result.Code, msg + ": the code", loc);
    CheckContains(result.Message, expectedText, msg + ": the message", loc);
    CheckEquals(text, document.ToText(), msg + ": the text should be kept", loc);
    CheckFalse(document.IsModified(), msg + ": the document isn't modified", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentAppend::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentAppend::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentAppend::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentAppend::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentAppend, ArraysOfTables_NewElementAfterTheLast)
{
    // Arrange & Act & Assert
    CheckAppends("[[s]]\nx = 1\n\n[s.t]\ny = 1\n\n[u]\n", "s", TTable{ { "x", 2 } },
        "[[s]]\nx = 1\n\n[s.t]\ny = 1\n\n[[s]]\nx = 2\n\n[u]\n", "after the last element's sub-tables");
    CheckAppends("[[s]]\nx = 1\n", "s", TTable{}, "[[s]]\nx = 1\n\n[[s]]\n", "an empty element, at the end");
    CheckAppends("[[ a . s ]]\n", "a.s", TTable{ { "x", 1 } }, "[[ a . s ]]\n\n[[a . s]]\nx = 1\n",
        "the key parts of the last element's header");
    CheckAppends("[[\"s\"]]\n[[s]]\n", "s", TTable{}, "[[\"s\"]]\n[[s]]\n\n[[s]]\n",
        "the last element's, not the first's");
    CheckAppends("[t]\n[[t.s]]\nx = 1\n[t.u]\n", "t.s", TTable{ { "x", 2 } },
        "[t]\n[[t.s]]\nx = 1\n\n[[t.s]]\nx = 2\n\n[t.u]\n", "before the next table of its parent");
    CheckAppends("[[s]]\nx = 1", "s", TTable{ { "x", 2 } }, "[[s]]\nx = 1\n\n[[s]]\nx = 2\n",
        "after a last line without a line break");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentAppend, ArrayValues_InTheirLayout)
{
    // Arrange & Act & Assert
    CheckAppends("ports = [80, 443]\n", "ports", 8080, "ports = [80, 443, 8080]\n", "an array on one line");
    CheckAppends("ports = [\n  80,\n  443 # https\n]\n", "ports", 8080, "ports = [\n  80,\n  443, # https\n  8080\n]\n",
        "one per line, the comment kept on its line");
    CheckAppends("ports = [\n  80,\n  443, # https\n]\n", "ports", 8080,
        "ports = [\n  80,\n  443, # https\n  8080,\n]\n", "one per line with a trailing comma");
    CheckAppends("ports = []\n", "ports", 1, "ports = [1]\n", "an empty array");
    CheckAppends("ports = [ ]\n", "ports", 1, "ports = [1]\n", "an empty array with a blank");
    CheckAppends("ports = [1]\n", "ports", 2, "ports = [1, 2]\n", "after the first element");
    CheckAppends("ports = [ 1 ]\n", "ports", 2, "ports = [ 1, 2 ]\n", "with the blanks inside the brackets");
    CheckAppends("a = [[1]]\n", "a[0]", 2, "a = [[1, 2]]\n", "an array in an array");
    CheckAppends("a = [1]\n", "a", TTable{ { "x", 1 } }, "a = [1, { x = 1 }]\n", "an inline table");
    CheckAppends("a = [\r\n  1,\r\n]\r\n", "a", 2, "a = [\r\n  1,\r\n  2,\r\n]\r\n", "with its CRLF line breaks");
    CheckAppends("[t]\na = [\n    'x', # first\n    'y'\n] # list\n", "t.a", "z",
        "[t]\na = [\n    'x', # first\n    'y',\n    \"z\"\n] # list\n", "the indentation of the last element");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentAppend, MissingKeys_GetAnArray)
{
    // Arrange & Act & Assert
    CheckAppends("a = 1\n", "list", 1, "a = 1\nlist = [1]\n", "an array of the value");
    CheckAppends("a = 1\n", "s", TTable{ { "x", 1 } }, "a = 1\n\n[[s]]\nx = 1\n", "an array of tables");
    CheckAppends("[t]\nx = 1\n", "t.list", "a", "[t]\nx = 1\nlist = [\"a\"]\n", "in a header table");
    CheckAppends("p = {}\n", "p.l", 1, "p = { l = [1] }\n", "in an inline table");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentAppend, RejectsWhatCantBeAppended)
{
    // Arrange & Act & Assert
    CheckFails("a = 1\n", "a", 2, ETomlCode::InvalidArgument, "Can't append to a: it isn't an array.", "a scalar");
    CheckFails("[t]\n", "t", 1, ETomlCode::InvalidArgument, "Can't append to t: it isn't an array.", "a table");
    CheckFails("a = 1\n", "", 1, ETomlCode::InvalidArgument, "Can't append to the root table: it isn't an array.",
        "the root");
    CheckFails("[[s]]\n", "s", 1, ETomlCode::InvalidArgument,
        "An element of an array of tables must be a table, not a value of type Integer.", "a scalar to tables");
    CheckFails("a = 1\n", "a..b", 1, ETomlCode::InvalidArgument, "isn't valid", "a path that isn't valid");
    CheckFails("port = 80\n", "port.x", 1, ETomlCode::InvalidArgument,
        "Can't append to port.x: port is a value of type Integer, not a table.", "a key of a scalar");
    CheckFails("a = [1]\n", "a[2]", 1, ETomlCode::NotFound, "the array a has 1 element.", "an index past the end");
    CheckFails("a = [1]\n", "a", "\xFF", ETomlCode::InvalidValue, "isn't valid UTF-8", "a string that isn't UTF-8");

    // Arrange
    TASWTomlOptions shallow;
    shallow.Reading.MaxNestingDepth = 2;
    TASWTomlDocument document;
    CheckTrue(document.Parse("a = [[1]]\n", shallow).IsOk(), "the text should parse");
    ASWToml::TASWTomlValueOptions badValues;
    badValues.IntegerBase = static_cast<ASWToml::EIntegerBase>(9);

    // Act
    ASWToml::TASWTomlResult const tooDeep = document.Append("a", TArray{ TASWTomlValue(TArray{ 2 }) });
    ASWToml::TASWTomlResult const fits = document.Append("a[0]", 2);
    ASWToml::TASWTomlResult const badOptions = document.Append("a[0]", 3, badValues);

    // Assert
    CheckEquals(ETomlCode::NestingTooDeep, tooDeep.Code, "an array of an array in the array would be 3 deep");
    CheckTrue(fits.IsOk(), "a scalar fits: " + fits.Message);
    CheckEquals(ETomlCode::InvalidOptions, badOptions.Code, "value options that aren't valid");
    CheckEquals("a = [[1, 2]]\n"s, document.ToText(), "only the edit that fits changed the text");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DocumentRemove
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_DocumentRemove::TTest_ASWToml_DocumentRemove()
    : inherited("ASWToml_DocumentRemove_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_DocumentRemove::~TTest_ASWToml_DocumentRemove()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentRemove::CheckKeeps(std::string const& text, ASWToml::TASWTomlPath const& path,
    std::string const& msg, std::source_location loc)
{
    TASWTomlDocument document;
    CheckTrue(document.Parse(text).IsOk(), msg + ": the text should parse", loc);

    bool const removed = document.Remove(path);

    CheckFalse(removed, msg + ": nothing is removed", loc);
    CheckEquals(text, document.ToText(), msg + ": the text should be kept", loc);
    CheckFalse(document.IsModified(), msg + ": the document isn't modified", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentRemove::CheckRemoves(std::string const& text, ASWToml::TASWTomlPath const& path,
    std::string const& expected, std::string const& msg, std::source_location loc)
{
    TASWTomlDocument document;
    ASWToml::TASWTomlResult const parsed = document.Parse(text);
    CheckTrue(parsed.IsOk(), msg + ": the text should parse: " + parsed.Message, loc);

    bool const removed = document.Remove(path);

    CheckTrue(removed, msg + ": it should be removed", loc);
    CheckEquals(Escaped(expected), Escaped(document.ToText()), msg + ": the text", loc);
    CheckTrue(document.IsModified(), msg + ": the document is modified", loc);

    TASWTomlDocument reread;
    CheckTrue(reread.Parse(document.ToText()).IsOk() &&
        ASWToml::Detail::IdenticalValues(reread.GetRoot(), document.GetRoot()),
        msg + ": the text should read as the document's values", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentRemove::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentRemove::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentRemove::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentRemove::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentRemove, ArrayElements_OnLinesOfTheirOwn)
{
    // Arrange & Act & Assert
    CheckRemoves("a = [\n  1, # one\n  # about two\n  2, # two\n  3\n]\n", "a[1]", "a = [\n  1, # one\n  3\n]\n",
        "its line, with the comment lines above it and the comment at its end");
    CheckRemoves("a = [\n  1, # one\n  2 # two\n]\n", "a[1]", "a = [\n  1 # one\n]\n",
        "the last one, and the comma before it");
    CheckRemoves("a = [\n  1, # one\n  2, # two\n]\n", "a[1]", "a = [\n  1, # one\n]\n",
        "the last one with a trailing comma, which the one before keeps");
    CheckRemoves("a = [\n  # first\n  1,\n  2,\n]\n", "a[0]", "a = [\n  2,\n]\n", "the first one with its comment");
    CheckRemoves("a = [ # list\n  1,\n  2,\n]\n", "a[0]", "a = [ # list\n  2,\n]\n",
        "a comment after the bracket stays");
    CheckRemoves("a = [\n  1, 2,\n  3\n]\n", "a[1]", "a = [\n  1,\n  3\n]\n", "one after another on their line");
    CheckRemoves("a = [\n  1,\n  2, 3\n]\n", "a[1]", "a = [\n  1,\n  3\n]\n", "one before another on their line");
    CheckRemoves("a = [\n  1,\n  # two\n  2, 3\n]\n", "a[1]", "a = [\n  1,\n  3\n]\n",
        "one before another, with its comment");
    CheckRemoves("a = [\n  1,\n  2]\n", "a[1]", "a = [\n  1\n]\n", "the last one, before the bracket");
    CheckRemoves("a = [\n  1,\n  # two\n  2]\n", "a[1]", "a = [\n  1\n]\n", "the last one with its comment");
    CheckRemoves("a = [\n  1,\n  # not about two\n\n  2,\n]\n", "a[1]", "a = [\n  1,\n  # not about two\n\n]\n",
        "a comment apart from it stays");
    CheckRemoves("a = [\n  1,\n]\n", "a[0]", "a = []\n", "the only one leaves []");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentRemove, ArrayElements_WithOneComma)
{
    // Arrange & Act & Assert
    CheckRemoves("a = [1, 2, 3]\n", "a[0]", "a = [2, 3]\n", "the first one, with the comma after it");
    CheckRemoves("a = [1, 2, 3]\n", "a[1]", "a = [1, 3]\n", "one in the middle");
    CheckRemoves("a = [1, 2, 3]\n", "a[2]", "a = [1, 2]\n", "the last one, with the comma before it");
    CheckRemoves("a = [1, 2, 3,]\n", "a[2]", "a = [1, 2,]\n", "a trailing comma stays");
    CheckRemoves("a = [1,2,3]\n", "a[1]", "a = [1,3]\n", "without blanks");
    CheckRemoves("a = [ 1, 2 ]\n", "a[0]", "a = [ 2 ]\n", "the blanks inside the brackets stay");
    CheckRemoves("a = [ 1 ]\n", "a[0]", "a = []\n", "the only one leaves []");
    CheckRemoves("a = [[1, 2], 3]\n", "a[0][1]", "a = [[1], 3]\n", "in an array in an array");
    CheckRemoves("a = [{ x = 1 }, 2] # c\n", "a[0]", "a = [2] # c\n", "an inline table");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentRemove, ArraysOfTables_ElementsWithTheirTables)
{
    // Arrange & Act & Assert
    CheckRemoves("[[s]]\nx = 1\n\n[s.t]\ny = 1\n\n[[s]]\nx = 2\n", "s[0]", "[[s]]\nx = 2\n",
        "an element with the table under it");
    CheckRemoves("[[s]]\nx = 1\n\n[s.t]\ny = 1\n\n[[s]]\nx = 2\n", "s[1]", "[[s]]\nx = 1\n\n[s.t]\ny = 1\n",
        "the last element");
    CheckRemoves("# servers\n[[s]]\nx = 1\n\n# the second\n[[s]]\nx = 2\n", "s[1]", "# servers\n[[s]]\nx = 1\n",
        "with its comment");
    CheckRemoves("[[s]]\nx = 1\n[[s]]\nx = 2\n\n[u]\n", "s", "[u]\n", "every element of the array of tables");
    CheckRemoves("[t]\n[[t.s]]\nx = 1\n[[t.s]]\nx = 2\n", "t.s[0]", "[t]\n[[t.s]]\nx = 2\n",
        "an element of an array of tables in a table");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentRemove, BlankLines_DontPileUp)
{
    // Arrange & Act & Assert
    CheckRemoves("a = 1\n\nb = 2\n\nc = 3\n", "b", "a = 1\n\nc = 3\n", "between blank lines, one stays");
    CheckRemoves("a = 1\n\nb = 2\n", "b", "a = 1\n", "at the end, the blank line before goes");
    CheckRemoves("b = 2\n\nc = 3\n", "b", "c = 3\n", "at the start, the blank line after goes");
    CheckRemoves("a = 1\n\nb = 2\nc = 3\n", "b", "a = 1\n\nc = 3\n", "a blank line on one side stays");
    CheckRemoves("# file\n\nb = 2\n\n[t]\n", "b", "# file\n\n[t]\n",
        "the file comment and the blank line under it stay");
    CheckRemoves("a = 1\n\n[t]\nx = 1\n\n[u]\n", "t", "a = 1\n\n[u]\n", "a table between blank lines");
    CheckRemoves("a = 1\n\n[t]\nx = 1\n\n", "t", "a = 1\n", "a table at the end");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentRemove, EmptyTables_GoWhenNothingDefinesThem)
{
    // Arrange & Act & Assert
    CheckRemoves("[t]\nx = 1\n", "t.x", "[t]\n", "a table with a header stays");
    CheckRemoves("[[s]]\nx = 1\n", "s[0].x", "[[s]]\n", "an element stays");
    CheckRemoves("a.x = 1\nb = 2\n", "a.x", "b = 2\n", "a dotted-key table goes with its last key");
    CheckRemoves("a.x = 1\n[a.s]\n", "a.x", "[a.s]\n", "but stays while a table under it is left");
    CheckRemoves("[t.a]\nx = 1\n", "t.a", "", "a table only a longer header made goes with it");
    CheckRemoves("[[s]]\nx = 1\n", "s[0]", "", "an array of tables goes with its last element");
    CheckRemoves("p = { a.x = 1 }\n", "p.a.x", "p = {}\n", "a dotted-key table in an inline table");

    // Arrange
    TASWTomlDocument dotted;
    TASWTomlDocument array;
    CheckTrue(dotted.Parse("a.x = 1\n").IsOk() && array.Parse("[[a.s]]\n").IsOk(), "the texts should parse");

    // Act
    bool const dottedRemoved = dotted.Remove("a.x");
    bool const arrayRemoved = array.Remove("a.s[0]");

    // Assert
    CheckTrue(dottedRemoved && dotted.Find("a") == nullptr, "the dotted-key table is gone");
    CheckTrue(arrayRemoved && array.Find("a") == nullptr, "the array of tables and the table it was in are gone");
    CheckTrue(array.GetRoot() == TASWTomlValue(), "the root is empty");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentRemove, InlineTableEntries_WithOneComma)
{
    // Arrange & Act & Assert
    CheckRemoves("p = { a = 1, b = 2, c = 3 }\n", "p.a", "p = { b = 2, c = 3 }\n", "the first one");
    CheckRemoves("p = { a = 1, b = 2, c = 3 }\n", "p.b", "p = { a = 1, c = 3 }\n", "one in the middle");
    CheckRemoves("p = { a = 1, b = 2, c = 3 }\n", "p.c", "p = { a = 1, b = 2 }\n", "the last one");
    CheckRemoves("p = { a = 1 }\n", "p.a", "p = {}\n", "the only one leaves {}");
    CheckRemoves("p = { a.x = 1, b = 2, a.y = 3 }\n", "p.a", "p = { b = 2 }\n",
        "a table of dotted keys, each of them");
    CheckRemoves("p = { a.x = 1, a.y = 2 }\n", "p.a.x", "p = { a.y = 2 }\n", "one dotted key");
    CheckRemoves("p = { q = { r = 1, s = 2 } }\n", "p.q.r", "p = { q = { s = 2 } }\n",
        "in an inline table in an inline table");
    CheckRemoves("a = [{ x = 1, y = 2 }]\n", "a[0].x", "a = [{ y = 2 }]\n", "in an inline table in an array");
    CheckRemoves("p = {\n  a = 1,\n  b = 2,\n}\n", "p.a", "p = {\n  b = 2,\n}\n",
        "an inline table on several lines (TOML 1.1)");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentRemove, KeyLines_WithTheirComments)
{
    // Arrange & Act & Assert
    CheckRemoves("a = 1\nb = 2\nc = 3\n", "b", "a = 1\nc = 3\n", "the key's line");
    CheckRemoves("a = 1\n# about b\n# more\nb = 2 # two\nc = 3\n", "b", "a = 1\nc = 3\n",
        "with the comment lines above it and the comment at its end");
    CheckRemoves("# about a\n\n# about b\nb = 2\n", "b", "# about a\n", "a comment apart from it stays");
    CheckRemoves("[t]\n  x = 1\n  y = 2\n", "t.x", "[t]\n  y = 2\n", "in a header table");
    CheckRemoves("a = [\n  1,\n  2,\n] # c\nb = 1\n", "a", "b = 1\n", "a value on several lines");
    CheckRemoves("p = { x = 1 }\nb = 1\n", "p", "b = 1\n", "an inline table's line");
    CheckRemoves("a.x = 1\na.y = 2\n", "a.x", "a.y = 2\n", "a dotted key");
    CheckRemoves("[[s]]\nx = 1\ny = 2\n", "s[0].x", "[[s]]\ny = 2\n", "a key of an element");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentRemove, LineBreaks_AsTheTextHasThem)
{
    // Arrange & Act & Assert
    CheckRemoves("a = 1\r\nb = 2\r\nc = 3\r\n", "b", "a = 1\r\nc = 3\r\n", "CRLF lines");
    CheckRemoves("a = [\r\n  1,\r\n  2,\r\n]\r\n", "a[0]", "a = [\r\n  2,\r\n]\r\n", "an element on a CRLF line");
    CheckRemoves("a = 1\nb = 2", "b", "a = 1", "a text without a line break at its end keeps that");
    CheckRemoves("x = 1\n\n[t]\ny = 2", "t", "x = 1", "with the blank line before what is removed");
    CheckRemoves(Bom + "a = 1\nb = 2\n", "a", Bom + "b = 2\n", "the byte order mark stays");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentRemove, NothingThere_ReturnsFalse)
{
    // Arrange & Act & Assert
    CheckKeeps("a = 1\n", "", "the root");
    CheckKeeps("a = 1\n", "b", "a missing key");
    CheckKeeps("a = 1\n", "a.b", "a key of a scalar");
    CheckKeeps("a = [1]\n", "a[1]", "an index past the end");
    CheckKeeps("[t]\n", "t[0]", "an index of a table");
    CheckKeeps("a = 1\n", "a..b", "a path that isn't valid");
    CheckKeeps("", "a", "an empty document");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentRemove, Tables_WithTheTablesUnderThem)
{
    // Arrange & Act & Assert
    CheckRemoves("a = 1\n\n# about t\n[t]\nx = 1\n\n# about u\n[u]\ny = 2\n", "t", "a = 1\n\n# about u\n[u]\ny = 2\n",
        "its header with its comment and lines, keeping the next header's comment");
    CheckRemoves("a = 1\n\n[t]\nx = 1\n\n[u]\ny = 2\n\n[t.s]\nz = 3\n", "t", "a = 1\n\n[u]\ny = 2\n",
        "and the tables under it, wherever they are");
    CheckRemoves("[t]\nx = 1\n# y = 2\n\n[u]\n", "t", "[u]\n", "the comment lines at the end of its block");
    CheckRemoves("a.x = 1\nb = 3\na.y = 2\n", "a", "b = 3\n", "a table of dotted keys: each of its lines");
    CheckRemoves("a.x = 1\n[a.s]\ny = 1\n", "a", "", "its dotted keys and a header under it");
    CheckRemoves("a.x = 1\n\n[a.s]\ny = 1", "a", "", "with a blank line between them");
    CheckRemoves("[t.a]\nx = 1\n\n[u]\n\n[t.b]\ny = 2\n", "t", "[u]\n", "a table only longer headers made");
    CheckRemoves("[t]\na.b = 1\nc = 2\n", "t.a", "[t]\nc = 2\n", "a dotted-key table in a header table");
    CheckRemoves("[t]\nx = 1\n", "t", "", "the only table");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DocumentRename
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_DocumentRename::TTest_ASWToml_DocumentRename()
    : inherited("ASWToml_DocumentRename_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_DocumentRename::~TTest_ASWToml_DocumentRename()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentRename::CheckFails(std::string const& text, ASWToml::TASWTomlPath const& path,
    std::string const& newKey, ETomlCode code, std::string const& expectedText, std::string const& msg,
    std::source_location loc)
{
    TASWTomlDocument document;
    CheckTrue(document.Parse(text).IsOk(), msg + ": the text should parse", loc);

    ASWToml::TASWTomlResult const result = document.Rename(path, newKey);

    CheckEquals(code, result.Code, msg + ": the code", loc);
    CheckContains(result.Message, expectedText, msg + ": the message", loc);
    CheckEquals(std::size_t(0), result.Line, msg + ": not about a place in the text", loc);
    CheckEquals(text, document.ToText(), msg + ": the text should be kept", loc);
    CheckFalse(document.IsModified(), msg + ": the document isn't modified", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentRename::CheckRenames(std::string const& text, ASWToml::TASWTomlPath const& path,
    std::string const& newKey, ASWToml::TASWTomlPath const& newPath, std::string const& expected,
    std::string const& msg, std::source_location loc)
{
    TASWTomlDocument document;
    ASWToml::TASWTomlResult const parsed = document.Parse(text);
    CheckTrue(parsed.IsOk(), msg + ": the text should parse: " + parsed.Message, loc);
    TASWTomlValue const* const found = document.Find(path);
    TASWTomlValue const old = found != nullptr ? *found : TASWTomlValue();

    ASWToml::TASWTomlResult const result = document.Rename(path, newKey);

    CheckTrue(result.IsOk(), msg + ": it should be renamed: " + result.Message, loc);
    CheckEquals(Escaped(expected), Escaped(document.ToText()), msg + ": the text", loc);
    TASWTomlValue const* const renamed = document.Find(newPath);
    CheckTrue(found != nullptr && renamed != nullptr && ASWToml::Detail::IdenticalValues(*renamed, old),
        msg + ": the value under its new name", loc);
    CheckTrue(document.Find(path) == nullptr, msg + ": nothing under its old name", loc);
    CheckTrue(document.IsModified(), msg + ": the document is modified", loc);

    TASWTomlDocument reread;
    CheckTrue(reread.Parse(document.ToText()).IsOk() &&
        ASWToml::Detail::IdenticalValues(reread.GetRoot(), document.GetRoot()),
        msg + ": the text should read as the document's values", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentRename::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentRename::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentRename::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DocumentRename::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentRename, Keys_OnlyTheNameChanges)
{
    // Arrange & Act & Assert
    CheckRenames("a = 1 # c\nb = 2\n", "a", "z", "z", "z = 1 # c\nb = 2\n", "its value and comment stay");
    CheckRenames("  a  =  1\n", "a", "b", "b", "  b  =  1\n", "the blanks stay");
    CheckRenames("p = { x = 1, y = 2 }\n", "p.x", "z", "p.z", "p = { z = 1, y = 2 }\n", "a key of an inline table");
    CheckRenames("a.x = 1\n", "a.x", "y", "a.y", "a.y = 1\n", "the last part of a dotted key");
    CheckRenames("[t]\nx = 1\n", "t.x", "y", "t.y", "[t]\ny = 1\n", "a key of a header table");
    CheckRenames("[[s]]\nx = 1\n", "s[0].x", "y", "s[0].y", "[[s]]\ny = 1\n", "a key of an element");
    CheckRenames("a = [1, 2]\n", "a", "b", "b", "b = [1, 2]\n", "an array value");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentRename, NewName_BareBasicOrLiteral)
{
    // Arrange & Act & Assert
    CheckRenames("\"a\" = 1\n", "a", "b", "b", "b = 1\n", "bare when it can be");
    CheckRenames("a = 1\n", "a", "host name", "\"host name\"", "\"host name\" = 1\n", "else a basic string");
    CheckRenames("'a' = 1\n", "a", "host name", "\"host name\"", "'host name' = 1\n", "a literal one stays literal");
    CheckRenames("'a' = 1\n", "a", "it's", "\"it's\"", "\"it's\" = 1\n", "unless a literal string can't hold it");
    CheckRenames("a = 1\n", "a", "", ASWToml::TASWTomlPath({ "" }), "\"\" = 1\n", "the empty key");
    CheckRenames("'a'.x = 1\n\"a\".y = 2\n", "a", "b c", "\"b c\"", "'b c'.x = 1\n\"b c\".y = 2\n",
        "each part in the form it was written in");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentRename, RejectsWhatCantBeRenamed)
{
    // Arrange & Act & Assert
    CheckFails("a = 1\n", "a..b", "c", ETomlCode::InvalidArgument, "isn't valid", "a path that isn't valid");
    CheckFails("a = 1\n", "", "b", ETomlCode::InvalidArgument, "Can't rename the root table: it has no name.",
        "the root");
    CheckFails("a = [1]\n", "a[0]", "b", ETomlCode::InvalidArgument,
        "Can't rename a[0]: an element of an array has no name.", "an element");
    CheckFails("[[s]]\n", "s[0]", "b", ETomlCode::InvalidArgument, "an element of an array has no name.",
        "an element of an array of tables");
    CheckFails("a = 1\n", "a.c", "b", ETomlCode::InvalidArgument,
        "Can't rename a.c: a is a value of type Integer, not a table.", "a key of a scalar");
    CheckFails("a = 1\n", "c", "b", ETomlCode::NotFound, "Can't rename c: c doesn't exist.", "a missing key");
    CheckFails("[t]\n", "t.b.c", "d", ETomlCode::NotFound, "Can't rename t.b.c: t.b doesn't exist.",
        "a missing table on the way");
    CheckFails("a = [{ x = 1 }]\n", "a[2].x", "y", ETomlCode::NotFound, "the array a has 1 element.",
        "an index past the end");
    CheckFails("a = 1\nb = 2\n", "a", "b", ETomlCode::AlreadyExists,
        "Can't rename a: the root table already has a key b.", "onto a key of the root");
    CheckFails("[t]\nx = 1\ny = 2\n", "t.x", "y", ETomlCode::AlreadyExists, "Can't rename t.x: t already has a key y.",
        "onto a key of a table");
    CheckFails("a.x = 1\n[b]\n", "a", "b", ETomlCode::AlreadyExists, "already has a key b.", "onto a table");
    CheckFails("a = 1\n", "a", "\xFF", ETomlCode::InvalidValue, "isn't valid UTF-8", "a name that isn't UTF-8");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentRename, SameName_ChangesNothing)
{
    // Arrange
    std::string const text = "'a' = 1\n[t]\n";
    TASWTomlDocument document;
    CheckTrue(document.Parse(text).IsOk(), "the text should parse");

    // Act
    ASWToml::TASWTomlResult const key = document.Rename("a", "a");
    ASWToml::TASWTomlResult const table = document.Rename("t", "t");

    // Assert
    CheckTrue(key.IsOk() && table.IsOk(), "renaming to the same name succeeds");
    CheckEquals(text, document.ToText(), "the text stays as it was, a quoted key too");
    CheckFalse(document.IsModified(), "the document isn't modified");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DocumentRename, Tables_EveryPlaceThatNamesThem)
{
    // Arrange & Act & Assert
    CheckRenames("[a]\nx = 1\n[a.b]\n[[a.c]]\n", "a", "z", "z", "[z]\nx = 1\n[z.b]\n[[z.c]]\n",
        "a header table and the headers under it");
    CheckRenames("a.x = 1\na.y = 2\n[a.s]\n", "a", "b", "b", "b.x = 1\nb.y = 2\n[b.s]\n", "a table of dotted keys");
    CheckRenames("[[s]]\nx = 1\n[s.t]\n[[s]]\n", "s", "servers", "servers",
        "[[servers]]\nx = 1\n[servers.t]\n[[servers]]\n", "an array of tables, in its elements' headers");
    CheckRenames("[a.b]\nx = 1\n[a.c]\n", "a", "z", "z", "[z.b]\nx = 1\n[z.c]\n", "a table only longer headers made");
    CheckRenames("[a]\n[a.b]\ny = 1\n", "a.b", "c", "a.c", "[a]\n[a.c]\ny = 1\n", "only the part of its own name");
    CheckRenames("p = { a.x = 1, a.y = 2 }\n", "p.a", "b", "p.b", "p = { b.x = 1, b.y = 2 }\n",
        "dotted keys in an inline table");
    CheckRenames("[ a . b ]\n", "a", "c", "c", "[ c . b ]\n", "the blanks around the parts stay");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DocumentAppend, "Document")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DocumentErrors, "Document")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DocumentFuzz, "Document")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DocumentIndex, "Document")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DocumentLookups, "Document")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DocumentModel, "Document")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DocumentParse, "Document")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DocumentRemove, "Document")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DocumentRename, "Document")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DocumentRoundTrip, "Document")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DocumentSetValue, "Document")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DocumentTables, "Document")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DocumentToText, "Document")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_TomlTest, "Document")
