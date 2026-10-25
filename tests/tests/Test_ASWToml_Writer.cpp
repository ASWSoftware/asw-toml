/* **************************************************************************
Test_ASWToml_Writer.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "Test_ASWToml_Writer.h"
//---------------------------------------------------------------------------
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWToml_Document.h"
#include "ASWToml_Options.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
#include "ASWToml_Writer.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

using namespace std::string_literals;

using ASWToml::EIntegerBase;
using ASWToml::ELineBreak;
using ASWToml::EStringStyle;
using ASWToml::ETomlCode;
using ASWToml::TASWTomlDocument;
using ASWToml::TASWTomlKeyValue;
using ASWToml::TASWTomlLocalDateTime;
using ASWToml::TASWTomlLocalTime;
using ASWToml::TASWTomlOffsetDateTime;
using ASWToml::TASWTomlResult;
using ASWToml::TASWTomlValue;
using ASWToml::Detail::TWriteStyle;
using TKeyValueVector = TASWTomlValue::TKeyValueVector;
using TValueVector = TASWTomlValue::TValueVector;

// The fuzz tests: the seed of the first random value, and how many
constexpr std::uint32_t FuzzFirstSeed = 20261024;
constexpr std::size_t FuzzValueCount = 3000;

TASWTomlValue Array(TValueVector elements);
bool IsBareKey(std::string_view key) noexcept;
std::size_t RandomBelow(std::mt19937& generator, std::size_t count);
TASWTomlValue RandomScalar(std::mt19937& generator);
std::string RandomString(std::mt19937& generator);
TASWTomlValue RandomTable(std::mt19937& generator, std::size_t depth);
TASWTomlValue RandomValue(std::mt19937& generator, std::size_t depth);
TWriteStyle Style(std::size_t maxDepth = 100, ELineBreak lineBreak = ELineBreak::LF);
TASWTomlValue Table(TKeyValueVector entries);

//---------------------------------------------------------------------------
/*
    Array

    An array value of 'elements'
*/
TASWTomlValue Array(TValueVector elements)
{
    return TASWTomlValue(std::move(elements));
}
//---------------------------------------------------------------------------
/*
    IsBareKey

    Whether 'key' is a bare key: not empty, and only A-Z, a-z, 0-9, '-' and '_'
*/
bool IsBareKey(std::string_view key) noexcept
{
    if (key.empty())
        return false;

    for (char const c : key)
    {
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return false;
    }

    return true;
}
//---------------------------------------------------------------------------
/*
    RandomBelow

    A random number from 0 to 'count' - 1, from 'generator' only (std::uniform_int_distribution differs between
    standard libraries)
*/
std::size_t RandomBelow(std::mt19937& generator, std::size_t count)
{
    return static_cast<std::size_t>(generator() % count);
}
//---------------------------------------------------------------------------
/*
    RandomScalar

    A random scalar value of any type, every one valid (a NaN never: it doesn't equal itself)
*/
TASWTomlValue RandomScalar(std::mt19937& generator)
{
    using namespace std::chrono;

    std::int64_t const integers[] = { 0, 1, -1, 255, -4096, std::numeric_limits<std::int64_t>::max(),
        std::numeric_limits<std::int64_t>::min() };
    double const floats[] = { 0.5, -0.0, 1e300, -1.5e-300, 1.0 / 3, 123456789.0, 5e-324,
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() };

    year_month_day const date{ year{ static_cast<int>(RandomBelow(generator, 10000)) },
        month{ static_cast<unsigned>(1 + RandomBelow(generator, 12)) },
        day{ static_cast<unsigned>(1 + RandomBelow(generator, 28)) } };
    TASWTomlLocalTime time;
    time.Hour = hours{ static_cast<int>(RandomBelow(generator, 24)) };
    time.Minute = minutes{ static_cast<int>(RandomBelow(generator, 60)) };
    time.Second = seconds{ static_cast<int>(RandomBelow(generator, 61)) };
    if (RandomBelow(generator, 2) == 0)
        time.Fraction = nanoseconds{ static_cast<int>(RandomBelow(generator, 1000000000)) };

    switch (RandomBelow(generator, 8))
    {
        case 0:
            return TASWTomlValue(RandomString(generator));

        case 1:
            return TASWTomlValue(integers[RandomBelow(generator, std::size(integers))]);

        case 2:
            return TASWTomlValue(floats[RandomBelow(generator, std::size(floats))]);

        case 3:
            return TASWTomlValue(RandomBelow(generator, 2) == 0);

        case 4:
            return TASWTomlValue(date);

        case 5:
            return TASWTomlValue(time);

        case 6:
            return TASWTomlValue(TASWTomlLocalDateTime{ date, time });
    }

    TASWTomlOffsetDateTime dateTime;
    dateTime.Date = date;
    dateTime.Time = time;
    dateTime.Offset = minutes{ static_cast<int>(RandomBelow(generator, 2879)) - 1439 };

    return TASWTomlValue(dateTime);
}
//---------------------------------------------------------------------------
/*
    RandomString

    A random string of up to 8 pieces: letters, digits, blanks, quotes, a backslash, line breaks, control characters,
    TOML's punctuation and UTF-8 of 2 to 4 bytes
*/
std::string RandomString(std::mt19937& generator)
{
    char const* const pieces[] = { "a", "Z", "0", " ", "\\", "\"", "'", "\n", "\t", "\r", "\x01", "\x7F", "#", "=",
        "[", "]", "{", "}", ".", ",", "\xC3\xA9", "\xE2\x82\xAC", "\xF0\x9F\x98\x80", "'''", "\"\"\"" };

    std::string text;
    std::size_t const count = RandomBelow(generator, 9);
    for (std::size_t i = 0; i < count; ++i)
        text += pieces[RandomBelow(generator, std::size(pieces))];

    return text;
}
//---------------------------------------------------------------------------
/*
    RandomTable

    A random table of up to 4 keys (each from a small pool, so some need quotes), its values as RandomValue() makes
    them, nested at most 'depth' more levels
*/
TASWTomlValue RandomTable(std::mt19937& generator, std::size_t depth)
{
    char const* const keys[] = { "a", "b", "port", "key with blanks", "", "x.y", "\xC3\xA9", "1", "-", "#", "'" };

    TASWTomlValue table;
    std::size_t const count = RandomBelow(generator, 5);
    for (std::size_t i = 0; i < count; ++i)
    {
        std::string const key = keys[RandomBelow(generator, std::size(keys))];
        if (table.Find(key) == nullptr)
            table.Set(key, RandomValue(generator, depth));
    }

    return table;
}
//---------------------------------------------------------------------------
/*
    RandomValue

    A random value: a scalar, or, when 'depth' allows (it is how many more levels of arrays and tables may be nested),
    an array of values, an array of tables or a table
*/
TASWTomlValue RandomValue(std::mt19937& generator, std::size_t depth)
{
    std::size_t const kind = depth == 0 ? 0 : RandomBelow(generator, 4);
    if (kind == 0)
        return RandomScalar(generator);

    if (kind == 3)
        return RandomTable(generator, depth - 1);

    // An array of values, or of tables (which needs a level more for its tables)
    TASWTomlValue array = Array({});
    std::size_t const count = RandomBelow(generator, 4);
    for (std::size_t i = 0; i < count; ++i)
    {
        if (kind == 2 && depth >= 2)
            array.Append(RandomTable(generator, depth - 2));
        else
            array.Append(RandomValue(generator, depth - 1));
    }

    return array;
}
//---------------------------------------------------------------------------
/*
    Style

    A style with the default value options, 'maxDepth' and 'lineBreak'
*/
TWriteStyle Style(std::size_t maxDepth, ELineBreak lineBreak)
{
    TWriteStyle style;
    style.MaxDepth = maxDepth;
    style.LineBreak = lineBreak;

    return style;
}
//---------------------------------------------------------------------------
/*
    Table

    A table value of 'entries'
*/
TASWTomlValue Table(TKeyValueVector entries)
{
    return TASWTomlValue(std::move(entries));
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Writer
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_Writer::TTest_ASWToml_Writer()
    : inherited("ASWToml_Writer_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_Writer::~TTest_ASWToml_Writer()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Writer::CheckReadsAs(std::string const& text, TASWTomlValue const& expected,
    std::string const& msg, std::source_location loc)
{
    TASWTomlDocument document;
    TASWTomlResult const result = document.Parse(text);

    CheckTrue(result.IsOk(), msg + ": the text should parse: " + result.Message + " (" + text + ")", loc);
    CheckTrue(document.GetRoot() == expected, msg + ": the text should read back as the value (" + text + ")", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Writer::CheckWritesBlocks(std::string_view path, bool isElement, bool blankLineFirst,
    TASWTomlValue const& table, TWriteStyle const& style, std::string const& expected, std::string const& msg,
    std::source_location loc)
{
    std::string text;
    TASWTomlResult const result =
        ASWToml::Detail::WriteTableBlocks(path, isElement, blankLineFirst, table, style, text);

    CheckTrue(result.IsOk(), msg + ": the table should be written: " + result.Message, loc);
    CheckEquals(expected, text, msg + ": the text", loc);

    if (path.empty())
        CheckReadsAs(text, table, msg, loc);
    else if (IsBareKey(path))
        CheckReadsAs(text, Table({ { std::string(path), isElement ? Array({ table }) : table } }), msg, loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Writer::CheckWritesDotted(std::string_view prefix, TASWTomlValue const& table,
    TWriteStyle const& style, std::string const& expected, std::string const& msg, std::source_location loc)
{
    std::string text;
    TASWTomlResult const result = ASWToml::Detail::WriteDottedKeys("", prefix, table, style, text);

    CheckTrue(result.IsOk(), msg + ": the table should be written: " + result.Message, loc);
    CheckEquals(expected, text, msg + ": the text", loc);
    CheckReadsAs(text, Table({ { std::string(prefix), table } }), msg, loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Writer::CheckWritesLike(TASWTomlValue const& value, std::string_view oldText,
    TWriteStyle const& style, std::string const& expected, std::string const& msg, std::source_location loc)
{
    std::string text = "x = ";
    TASWTomlResult const result = ASWToml::Detail::WriteValueLike(value, oldText, style, text);

    CheckTrue(result.IsOk(), msg + ": the value should be written: " + result.Message, loc);
    CheckEquals("x = " + expected, text, msg + ": the text", loc);
    CheckReadsAs(text, Table({ { "x", value } }), msg, loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Writer::CheckWritesValue(TASWTomlValue const& value, TWriteStyle const& style,
    std::string const& expected, std::string const& msg, std::source_location loc)
{
    std::string text = "x = ";
    TASWTomlResult const result = ASWToml::Detail::WriteValue(value, style, text);

    CheckTrue(result.IsOk(), msg + ": the value should be written: " + result.Message, loc);
    CheckEquals("x = " + expected, text, msg + ": the text", loc);
    CheckReadsAs(text, Table({ { "x", value } }), msg, loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Writer::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Writer::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Writer::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Writer::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Writer, WriteDottedKeys_EmptyTables)
{
    // Arrange & Act & Assert
    CheckWritesDotted("server", Table({}), Style(), "server = {}\n", "an empty table is an empty inline table");
    CheckWritesDotted("server", Table({ { "tls", Table({}) } }), Style(), "server.tls = {}\n",
        "an empty table in it");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Writer, WriteDottedKeys_NestedTables)
{
    // Arrange
    TASWTomlValue const table = Table({ { "host", "example.com" },
        { "tls", Table({ { "port", 443 }, { "on", true } }) }, { "port", 8080 },
        { "key name", Table({ { "x", 1 } }) } });

    // Act & Assert
    CheckWritesDotted("server", table, Style(),
        "server.host = \"example.com\"\nserver.tls.port = 443\nserver.tls.on = true\nserver.port = 8080\n"
        "server.\"key name\".x = 1\n", "each key in the order of the table, depth first, a key quoted where needed");

    // Arrange & Act & Assert
    // An array of tables is an array of inline tables, and the indentation and line break are the ones asked for
    std::string text;
    TASWTomlValue const servers = Table({ { "list", Array({ Table({ { "x", 1 } }), Table({}) }) } });
    TASWTomlResult const result =
        ASWToml::Detail::WriteDottedKeys("  ", "a.\"b c\"", servers, Style(100, ELineBreak::CRLF), text);
    CheckTrue(result.IsOk(), "the table should be written: " + result.Message);
    CheckEquals("  a.\"b c\".list = [{ x = 1 }, {}]\r\n"s, text, "an array of tables, indented, CRLF");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Writer, WriteDottedKeys_RejectsWhatCantBeWritten)
{
    // Arrange
    TASWTomlValue const nested = Table({ { "b", Table({ { "c", 1 } }) } });

    // Act & Assert
    std::string text;
    CheckEquals(ETomlCode::InvalidValue, ASWToml::Detail::WriteDottedKeys("", "a", 1, Style(), text).Code,
        "a value that isn't a table");
    CheckEquals(ETomlCode::Ok, ASWToml::Detail::WriteDottedKeys("", "a", nested, Style(2), text).Code,
        "two tables within a limit of 2");
    CheckEquals(ETomlCode::NestingTooDeep, ASWToml::Detail::WriteDottedKeys("", "a", nested, Style(1), text).Code,
        "two tables beyond a limit of 1");
    CheckEquals(ETomlCode::InvalidValue,
        ASWToml::Detail::WriteDottedKeys("", "a", Table({ { "\xFF", 1 } }), Style(), text).Code,
        "a key that isn't valid UTF-8");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Writer, WriteKeyValueLine_WritesALine)
{
    // Arrange
    std::string text = "# start\n";

    // Act
    TASWTomlResult const first = ASWToml::Detail::WriteKeyValueLine("    ", "a.\"b c\"", Array({ 1, "x" }), Style(),
        text);
    TASWTomlResult const second = ASWToml::Detail::WriteKeyValueLine("", "port", 80, Style(100, ELineBreak::CRLF),
        text);
    TASWTomlResult const third = ASWToml::Detail::WriteKeyValueLine("", "x", "y", Style(100, ELineBreak::None), text);

    // Assert
    CheckTrue(first.IsOk() && second.IsOk() && third.IsOk(), "the lines should be written");
    CheckEquals("# start\n    a.\"b c\" = [1, \"x\"]\nport = 80\r\nx = \"y\"\n"s, text,
        "the indentation, the key as given, ' = ', the value and the line break (LF for None)");

    // Arrange & Act & Assert
    CheckEquals(ETomlCode::NestingTooDeep, ASWToml::Detail::WriteKeyValueLine("", "x", Array({}), Style(0), text).Code,
        "the limit counts the value");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Writer, WriteTableBlocks_ArraysOfTables)
{
    // Arrange
    TASWTomlValue const root = Table({ { "title", "x" },
        { "servers", Array({ Table({ { "host", "a" }, { "tls", Table({ { "on", true } }) } }), Table({}),
            Table({ { "ports", Array({ Table({ { "n", 1 } }), Table({ { "n", 2 } }) }) } }) }) },
        { "empty", Array({}) }, { "mixed", Array({ Table({}), 1 }) } });

    // Act & Assert
    CheckWritesBlocks("", false, false, root, Style(),
        "title = \"x\"\nempty = []\nmixed = [{}, 1]\n"
        "\n[[servers]]\nhost = \"a\"\n\n[servers.tls]\non = true\n"
        "\n[[servers]]\n"
        "\n[[servers]]\n\n[[servers.ports]]\nn = 1\n\n[[servers.ports]]\nn = 2\n",
        "every element a [[...]] header, even an empty one or one with only sub-tables; an empty array and an array "
        "that isn't only tables as key lines");

    // Arrange & Act & Assert
    // A new element of an array of tables
    CheckWritesBlocks("servers", true, true, Table({ { "host", "b" } }), Style(), "\n[[servers]]\nhost = \"b\"\n",
        "an element");
    CheckWritesBlocks("servers", true, false, Table({}), Style(), "[[servers]]\n", "an empty element");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Writer, WriteTableBlocks_BlankLines)
{
    // Arrange
    TASWTomlValue const table = Table({ { "x", 1 }, { "b", Table({ { "y", 2 } }) } });
    TWriteStyle noBlankLines = Style();
    noBlankLines.BlankLineBeforeTable = false;

    // Act & Assert
    CheckWritesBlocks("a", false, false, table, Style(), "[a]\nx = 1\n\n[a.b]\ny = 2\n",
        "no blank line before the first line, one before each other header");
    CheckWritesBlocks("a", false, true, table, Style(), "\n[a]\nx = 1\n\n[a.b]\ny = 2\n",
        "a blank line before the first header when asked for");
    CheckWritesBlocks("a", false, true, table, noBlankLines, "[a]\nx = 1\n[a.b]\ny = 2\n",
        "no blank lines without BlankLineBeforeTable");
    CheckWritesBlocks("a", false, false, table, Style(100, ELineBreak::CRLF), "[a]\r\nx = 1\r\n\r\n[a.b]\r\ny = 2\r\n",
        "CRLF line breaks");
    CheckWritesBlocks("", false, true, Table({ { "x", 1 }, { "b", Table({}) } }), Style(), "x = 1\n\n[b]\n",
        "the root's lines come first, without a blank line");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Writer, WriteTableBlocks_HeadersAndLines)
{
    // Arrange
    TASWTomlValue const table = Table({ { "host", "example.com" }, { "tls", Table({ { "port", 443 } }) },
        { "ports", Array({ 80, 8080 }) }, { "limits", Table({ { "min", 1 }, { "inner", Table({ { "z", 0 } }) } }) },
        { "point", Array({ Table({ { "x", 1 } }) }) }, { "port", 80 } });

    // Act & Assert
    CheckWritesBlocks("server", false, false, table, Style(),
        "[server]\nhost = \"example.com\"\nports = [80, 8080]\nport = 80\n"
        "\n[server.tls]\nport = 443\n"
        "\n[server.limits]\nmin = 1\n\n[server.limits.inner]\nz = 0\n"
        "\n[[server.point]]\nx = 1\n",
        "the key lines first, then each table and array of tables in order, depth first");

    // Arrange & Act & Assert
    // The path as written by the caller, and new keys quoted where needed
    CheckWritesBlocks("a.\"b c\"", false, false, Table({ { "key name", 1 }, { "d e", Table({ { "x", 2 } }) } }),
        Style(), "[a.\"b c\"]\n\"key name\" = 1\n\n[a.\"b c\".\"d e\"]\nx = 2\n", "quoted keys");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Writer, WriteTableBlocks_ImplicitAndEmptyTables)
{
    // Arrange & Act & Assert
    CheckWritesBlocks("a", false, false, Table({ { "b", Table({ { "c", Table({ { "x", 1 } }) } }) } }), Style(),
        "[a.b.c]\nx = 1\n", "tables with only sub-tables get no header");
    CheckWritesBlocks("a", false, false, Table({}), Style(), "[a]\n", "an empty table gets a header");
    CheckWritesBlocks("a", false, false, Table({ { "b", Table({}) } }), Style(), "[a.b]\n",
        "an empty sub-table gets one");
    CheckWritesBlocks("a", false, false, Table({ { "s", Array({ Table({}) }) } }), Style(), "[[a.s]]\n",
        "a table with only an array of tables");
    CheckWritesBlocks("", false, false, Table({}), Style(), "", "the root never gets a header");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Writer, WriteTableBlocks_ReadsBackRandomTables)
{
    for (std::size_t i = 0; i < FuzzValueCount; ++i)
    {
        // Arrange
        std::mt19937 generator(FuzzFirstSeed + static_cast<std::uint32_t>(i));
        TASWTomlValue const table = RandomTable(generator, 4);
        TWriteStyle style = Style(4, RandomBelow(generator, 2) == 0 ? ELineBreak::LF : ELineBreak::CRLF);
        style.BlankLineBeforeTable = RandomBelow(generator, 2) == 0;

        // Act
        std::string text;
        TASWTomlResult const result = ASWToml::Detail::WriteTableBlocks("", false, false, table, style, text);

        // Assert
        std::string const name = "seed " + std::to_string(FuzzFirstSeed + i);
        CheckTrue(result.IsOk(), name + ": the table should be written: " + result.Message);
        CheckReadsAs(text, table, name);
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Writer, WriteTableBlocks_RejectsWhatCantBeWritten)
{
    // Arrange
    TASWTomlValue const withArray = Table({ { "x", Array({ 1 }) } });
    TASWTomlValue const withArrayOfTables = Table({ { "s", Array({ Table({}) }) } });

    // Act & Assert
    std::string text;
    CheckEquals(ETomlCode::InvalidValue, ASWToml::Detail::WriteTableBlocks("a", false, false, 1, Style(), text).Code,
        "a value that isn't a table");
    CheckEquals(ETomlCode::NestingTooDeep,
        ASWToml::Detail::WriteTableBlocks("a", false, false, Table({}), Style(0), text).Code, "the table counts");
    CheckEquals(ETomlCode::Ok, ASWToml::Detail::WriteTableBlocks("a", false, false, withArray, Style(2), text).Code,
        "a table and an array within 2");
    CheckEquals(ETomlCode::NestingTooDeep,
        ASWToml::Detail::WriteTableBlocks("a", false, false, withArray, Style(1), text).Code,
        "a table and an array beyond 1");
    CheckEquals(ETomlCode::Ok,
        ASWToml::Detail::WriteTableBlocks("a", false, false, withArrayOfTables, Style(3), text).Code,
        "a table, an array of tables and its element within 3");
    CheckEquals(ETomlCode::NestingTooDeep,
        ASWToml::Detail::WriteTableBlocks("a", false, false, withArrayOfTables, Style(2), text).Code,
        "a table, an array of tables and its element beyond 2");
    TASWTomlValue const badKey = Table({ { "b", Table({ { "\xFF", 1 } }) } });
    CheckEquals(ETomlCode::InvalidValue, ASWToml::Detail::WriteTableBlocks("a", false, false, badKey, Style(),
        text).Code, "a key that isn't valid UTF-8");
    CheckEquals(ETomlCode::InvalidValue,
        ASWToml::Detail::WriteTableBlocks("a", false, false, Table({ { "x", "\xFF" } }), Style(), text).Code,
        "a string that isn't valid UTF-8");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Writer, WriteValue_ArraysAndInlineTables)
{
    // Arrange & Act & Assert
    CheckWritesValue(Array({ 1, 2, 3 }), Style(), "[1, 2, 3]", "an array on one line");
    CheckWritesValue(Array({}), Style(), "[]", "an empty array");
    CheckWritesValue(Array({ Array({ 1 }), Array({}), "x" }), Style(), "[[1], [], \"x\"]", "arrays in an array");
    CheckWritesValue(Table({ { "x", 1 }, { "y", 2 } }), Style(), "{ x = 1, y = 2 }", "an inline table");
    CheckWritesValue(Table({}), Style(), "{}", "an empty inline table");
    CheckWritesValue(Table({ { "key name", Table({ { "", Array({ 1.5 }) } }) } }), Style(),
        "{ \"key name\" = { \"\" = [1.5] } }", "keys quoted where needed, and values nested");
    CheckWritesValue(Array({ Table({ { "x", 1 } }), Table({}) }), Style(), "[{ x = 1 }, {}]",
        "an array of tables on one line");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Writer, WriteValue_NestingLimit)
{
    // Arrange
    struct TCase
    {
        TASWTomlValue Value;
        std::size_t Depth; // How deep its arrays and tables are nested
    };
    std::vector<TCase> const cases = { { 1, 0 }, { Array({}), 1 }, { Array({ 1 }), 1 }, { Table({ { "x", 1 } }), 1 },
        { Array({ Array({ 1 }) }), 2 }, { Table({ { "x", Array({}) } }), 2 },
        { Array({ Table({ { "x", Array({ 1 }) } }) }), 3 } };

    for (TCase const& c : cases)
    {
        std::string text;

        // Act
        TASWTomlResult const within = ASWToml::Detail::WriteValue(c.Value, Style(c.Depth), text);
        TASWTomlResult const beyond = ASWToml::Detail::WriteValue(c.Value, Style(c.Depth - 1), text);

        // Assert
        CheckEquals(ETomlCode::Ok, within.Code, text + ": written within its depth " + std::to_string(c.Depth));
        if (c.Depth > 0)
        {
            CheckEquals(ETomlCode::NestingTooDeep, beyond.Code, text + ": not written beyond its depth");
            CheckContains(beyond.Message, "Reading.MaxNestingDepth", "the message names the option");
            CheckEquals(std::size_t(0), beyond.Line, "a writer's error has no line");
        }
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Writer, WriteValue_ReadsBackRandomValues)
{
    for (std::size_t i = 0; i < FuzzValueCount; ++i)
    {
        // Arrange
        std::mt19937 generator(FuzzFirstSeed + static_cast<std::uint32_t>(i));
        TASWTomlValue const value = RandomValue(generator, 4);
        TWriteStyle style = Style(4);
        style.Values.StringStyle = static_cast<EStringStyle>(RandomBelow(generator, 3));
        style.Values.IntegerBase = static_cast<EIntegerBase>(RandomBelow(generator, 5));

        // Act
        std::string text = "x = ";
        TASWTomlResult const result = ASWToml::Detail::WriteValue(value, style, text);

        // Assert
        std::string const name = "seed " + std::to_string(FuzzFirstSeed + i);
        CheckTrue(result.IsOk(), name + ": the value should be written: " + result.Message);
        CheckReadsAs(text, Table({ { "x", value } }), name);
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Writer, WriteValue_RejectsWhatCantBeWritten)
{
    // Arrange
    TASWTomlOffsetDateTime badOffset;
    badOffset.Offset = std::chrono::minutes{ 24 * 60 };
    TASWTomlLocalTime badTime;
    badTime.Hour = std::chrono::hours{ 24 };
    std::chrono::year_month_day const badDate{ std::chrono::year{ 2023 }, std::chrono::February,
        std::chrono::day{ 29 } };
    std::vector<TASWTomlValue> const values = { "\xFF", Array({ 1, "\xC3" }), Table({ { "\xFF", 1 } }),
        Table({ { "x", "\xFF" } }), badDate, badTime, TASWTomlLocalDateTime{ {}, badTime }, badOffset };

    for (std::size_t i = 0; i < values.size(); ++i)
    {
        std::string text;

        // Act
        TASWTomlResult const result = ASWToml::Detail::WriteValue(values[i], Style(), text);

        // Assert
        CheckEquals(ETomlCode::InvalidValue, result.Code, "value " + std::to_string(i) + " can't be written");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Writer, WriteValue_Scalars)
{
    // Arrange
    TWriteStyle literal = Style();
    literal.Values.StringStyle = EStringStyle::Literal;
    TWriteStyle hexadecimal = Style();
    hexadecimal.Values.IntegerBase = EIntegerBase::Hexadecimal;
    TASWTomlOffsetDateTime dateTime;
    dateTime.Offset = std::chrono::minutes{ -90 };

    // Act & Assert
    CheckWritesValue("text", Style(), "\"text\"", "a basic string");
    CheckWritesValue("C:\\x", Style(), "'C:\\x'", "Auto: a literal string for a backslash");
    CheckWritesValue("text", literal, "'text'", "the style asked for");
    CheckWritesValue(255, Style(), "255", "a decimal integer");
    CheckWritesValue(255, hexadecimal, "0xFF", "the base asked for");
    CheckWritesValue(-1, hexadecimal, "-1", "a negative number in decimal");
    CheckWritesValue(0.5, Style(), "0.5", "a float");
    CheckWritesValue(3.0, Style(), "3.0", "a float with .0");
    CheckWritesValue(true, Style(), "true", "a boolean");
    CheckWritesValue(std::chrono::year_month_day{ std::chrono::year{ 1979 }, std::chrono::May, std::chrono::day{ 27 } },
        Style(), "1979-05-27", "a local date");
    CheckWritesValue(TASWTomlLocalTime{}, Style(), "00:00:00", "a local time");
    CheckWritesValue(TASWTomlLocalDateTime{}, Style(), "1970-01-01T00:00:00", "a local date-time");
    CheckWritesValue(dateTime, Style(), "1970-01-01T00:00:00-01:30", "an offset date-time");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Writer, WriteValueLike_IntegerBases)
{
    // Arrange
    TWriteStyle decimal = Style();
    decimal.Values.IntegerBase = EIntegerBase::Decimal;
    TWriteStyle binary = Style();
    binary.Values.IntegerBase = EIntegerBase::Binary;

    // Act & Assert
    CheckWritesLike(255, "0xff", Style(), "0xFF", "hexadecimal stays hexadecimal, upper case");
    CheckWritesLike(8, "0o17", Style(), "0o10", "octal stays octal");
    CheckWritesLike(5, "0b1_0", Style(), "0b101", "binary stays binary, without '_'");
    CheckWritesLike(-5, "0xFF", Style(), "-5", "a negative number is decimal");
    CheckWritesLike(255, "1_000", Style(), "255", "decimal stays decimal");
    CheckWritesLike(255, "0xFF", decimal, "255", "an explicit base wins over the old one");
    CheckWritesLike(5, "0xFF", binary, "0b101", "any explicit base");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Writer, WriteValueLike_OtherValues)
{
    // Arrange & Act & Assert
    CheckWritesLike(255, "'text'", Style(), "255", "an integer for an old string");
    CheckWritesLike("text", "0xFF", Style(), "\"text\"", "a string for an old integer");
    CheckWritesLike("C:\\x", "1.5", Style(), "'C:\\x'", "a new string's form for an old float");
    CheckWritesLike(1.5, "0x1", Style(), "1.5", "a float for an old hexadecimal integer");
    CheckWritesLike(Array({ "a" }), "'''x'''", Style(), "[\"a\"]", "an array for an old string");
    CheckWritesLike("x", "", Style(), "\"x\"", "no old text");

    // Arrange & Act & Assert
    std::string text;
    CheckEquals(ETomlCode::InvalidValue, ASWToml::Detail::WriteValueLike("\xFF", "'''x'''", Style(), text).Code,
        "a multi-line string that isn't valid UTF-8");
    CheckEquals(ETomlCode::NestingTooDeep, ASWToml::Detail::WriteValueLike(Array({}), "1", Style(0), text).Code,
        "the limit");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Writer, WriteValueLike_StringForms)
{
    // Arrange
    TWriteStyle basic = Style();
    basic.Values.StringStyle = EStringStyle::Basic;
    TWriteStyle literal = Style();
    literal.Values.StringStyle = EStringStyle::Literal;

    // Act & Assert
    CheckWritesLike("C:\\new", "\"C:\\\\old\"", Style(), "\"C:\\\\new\"", "a basic string stays basic");
    CheckWritesLike("new", "'old'", Style(), "'new'", "a literal string stays literal");
    CheckWritesLike("it's", "'old'", Style(), "\"it's\"", "a literal string that can't hold the text: basic");
    CheckWritesLike("a\nb", "\"\"\"old\"\"\"", Style(), "\"\"\"a\nb\"\"\"", "a multi-line basic string");
    CheckWritesLike("a\nb", "\"\"\"\nold\"\"\"", Style(100, ELineBreak::CRLF), "\"\"\"\r\na\r\nb\"\"\"",
        "a line break after the delimiter kept, and the line break asked for");
    CheckWritesLike("a", "\"\"\"\r\nold\"\"\"", Style(), "\"\"\"\na\"\"\"", "an old CRLF after the delimiter");
    CheckWritesLike("C:\\x", "'''old'''", Style(), "'''C:\\x'''", "a multi-line literal string");
    CheckWritesLike("'''", "'''\nold'''", Style(), "\"\"\"\n'''\"\"\"",
        "a multi-line literal string that can't hold the text: multi-line basic");
    CheckWritesLike("C:\\x", "'''old'''", basic, "\"C:\\\\x\"", "an explicit style wins over the old form");
    CheckWritesLike("x", "\"old\"", literal, "'x'", "any explicit style");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWToml_Writer)
