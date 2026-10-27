/* **************************************************************************
UT_Helper_TomlTest.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "UT_Helper_TomlTest.h"
//---------------------------------------------------------------------------
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
//---------------------------------------------------------------------------
#include "ASWToml_Scalars.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

using ASWToml::TASWTomlValue;

// An array and a table of values
using TArray = TASWTomlValue::TValueVector;
using TTable = TASWTomlValue::TKeyValueVector;

// The expected values of a toml-test file: each path's type and value, as the corpus writes them
using TExpectedMap = std::map<std::string, std::pair<std::string, std::string> >;

// The values of a document by their paths, in the corpus's form
using TValueMap = std::map<std::string, TASWTomlValue const*>;

//---------------------------------------------------------------------------

std::string CorpusEscaped(std::string_view text);
char const* CorpusType(TASWTomlValue const& value) noexcept;
TExpectedMap ExpectedValues(std::string_view expected);
void FlattenValues(TASWTomlValue const& value, std::string const& path, TValueMap& values);
bool SameValue(std::string const& expected, TASWTomlValue const& value);

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

} // namespace

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
std::string ValuesDiffer(TTomlTestFile const& file, ASWToml::TASWTomlValue const& root)
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

} // namespace ASWUnitTests
