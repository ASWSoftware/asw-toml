/* **************************************************************************
Test_ASWToml_Scalars.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "Test_ASWToml_Scalars.h"
//---------------------------------------------------------------------------
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWToml_Scalars.h"
#include "ASWToml_Utils.h"
#include "ASWToml_Value.h"
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
using ASWToml::ETomlType;
using ASWToml::TASWTomlLocalDateTime;
using ASWToml::TASWTomlLocalTime;
using ASWToml::TASWTomlOffsetDateTime;
using ASWToml::TASWTomlValue;

using TStringStyleVector = std::vector<EStringStyle>;
using TStringVector = std::vector<std::string>;

// Every EStringStyle
TStringStyleVector const AllStyles = { EStringStyle::Auto, EStringStyle::Basic, EStringStyle::Literal };

// The fuzz tests: the seed of the first random text or value, and how many of each
constexpr std::uint32_t FuzzFirstSeed = 20261023;
constexpr std::size_t FuzzTextCount = 20000;
constexpr std::size_t FuzzValueCount = 20000;

std::uint64_t Bits(double value) noexcept;
bool CanBeWritten(TASWTomlValue const& value);
std::chrono::year_month_day Date(int year, unsigned month, unsigned day);
std::string Escaped(std::string const& text);
std::string HexByte(unsigned char byte);
std::string MutatedText(std::mt19937& generator);
std::chrono::year_month_day RandomDate(std::mt19937& generator);
double RandomFloat(std::mt19937& generator);
std::int64_t RandomInteger(std::mt19937& generator);
ASWToml::TASWTomlValueOptions RandomOptions(std::mt19937& generator);
TASWTomlValue RandomScalar(std::mt19937& generator);
std::string RandomString(std::mt19937& generator);
std::string RandomText(std::mt19937& generator);
TASWTomlLocalTime RandomTime(std::mt19937& generator);
bool SameScalar(TASWTomlValue const& left, TASWTomlValue const& right);
std::string ScalarText(TASWTomlValue const& value);
TASWTomlLocalTime Time(int hour, int minute, int second, std::int64_t nanoseconds = 0);
ASWToml::TASWTomlResult WriteDateTime(TASWTomlValue const& value, std::string& text);
ASWToml::TASWTomlResult WriteScalar(TASWTomlValue const& value, std::string& text,
    ASWToml::TASWTomlValueOptions const& options);

//---------------------------------------------------------------------------
/*
    Bits

    The bits of 'value', so two doubles compare exactly: -0.0 isn't 0.0, and a NaN equals a NaN with the same bits
*/
std::uint64_t Bits(double value) noexcept
{
    return std::bit_cast<std::uint64_t>(value);
}
//---------------------------------------------------------------------------
/*
    CanBeWritten

    Whether the scalar 'value' can be written as TOML: every one but a string that isn't valid UTF-8, and a date or
    time that isn't valid
*/
bool CanBeWritten(TASWTomlValue const& value)
{
    if (value.AsString() != nullptr)
        return ASWToml::Detail::IsValidUtf8(*value.AsString());

    if (value.AsOffsetDateTime() != nullptr)
        return value.AsOffsetDateTime()->IsValid();

    if (value.AsLocalDateTime() != nullptr)
        return value.AsLocalDateTime()->IsValid();

    if (value.AsLocalDate() != nullptr)
        return TASWTomlLocalDateTime{ *value.AsLocalDate(), TASWTomlLocalTime() }.IsValid();

    if (value.AsLocalTime() != nullptr)
        return value.AsLocalTime()->IsValid();

    return true;
}
//---------------------------------------------------------------------------
/*
    Date

    The date 'year'-'month'-'day'
*/
std::chrono::year_month_day Date(int year, unsigned month, unsigned day)
{
    return std::chrono::year_month_day(std::chrono::year(year), std::chrono::month(month), std::chrono::day(day));
}
//---------------------------------------------------------------------------
/*
    Escaped

    'text' for a message: printable ASCII as it is, a backslash as \\, and every other byte as \xHH
*/
std::string Escaped(std::string const& text)
{
    constexpr char Digits[] = "0123456789ABCDEF";
    std::string escaped;

    for (char const c : text)
    {
        unsigned char const byte = static_cast<unsigned char>(c);
        if (byte == '\\')
            escaped += "\\\\";
        else if (byte >= 0x20 && byte < 0x7F)
            escaped += c;
        else
            escaped += "\\x"s + Digits[byte >> 4] + Digits[byte & 0xF];
    }

    return escaped;
}
//---------------------------------------------------------------------------
/*
    HexByte

    'byte' as four hexadecimal digits in upper case, as a message writes a control character: 001B
*/
std::string HexByte(unsigned char byte)
{
    constexpr char Digits[] = "0123456789ABCDEF";

    return "00"s + Digits[byte >> 4] + Digits[byte & 0xF];
}
//---------------------------------------------------------------------------
/*
    MutatedText

    A valid scalar of a random kind with one to three random edits: a byte replaced, inserted or deleted, or the text
    cut off
*/
std::string MutatedText(std::mt19937& generator)
{
    std::string const valid[] = {
        "\"a\\tb\\u00E9\\U0001F600\\x41\\e\"", "'C:\\Users\\me'", "\"\"\"\nline\\\n   more\r\nend\"\"\"",
        "'''a\r\n'b''c'''", "0x7FFFFFFFFFFFFFFF", "-9223372036854775808", "0o755", "0b1010", "1_000_000",
        "3.14e-10", "-0.0", "+inf", "-nan", "5e-324", "1.7976931348623157e308", "true", "false",
        "1979-05-27T07:32:00.999999-07:00", "1979-05-27 07:32Z", "1979-05-27t07:32:00", "2024-02-29", "23:59:60.5",
        "07:32",
    };
    char const bytes[] = {
        '0', '1', '9', '-', '+', ':', '.', '_', 'e', 'x', 'T', 'Z', ' ', '\t', '"', '\'', '\\', '\n', '\r', '#',
        '\x7F', '\xC3', '\xFF', '\0',
    };

    std::string text = valid[generator() % (sizeof(valid) / sizeof(valid[0]))];
    std::size_t const editCount = 1 + generator() % 3;

    for (std::size_t i = 0; i < editCount; ++i)
    {
        std::uint32_t const edit = static_cast<std::uint32_t>(generator() % 4);
        std::size_t const offset = generator() % (text.size() + 1);
        char const byte = bytes[generator() % sizeof(bytes)];

        if (edit == 0 && offset < text.size())
            text[offset] = byte;
        else if (edit == 1)
            text.insert(offset, 1, byte);
        else if (edit == 2 && offset < text.size())
            text.erase(offset, 1);
        else
            text.resize(offset);
    }

    return text;
}
//---------------------------------------------------------------------------
/*
    RandomDate

    A random date: most of them real dates from year 0 to 9999, one in eight with fields that may be outside their
    ranges (a year from -10 to 10009, a month from 0 to 14, a day from 0 to 33)
*/
std::chrono::year_month_day RandomDate(std::mt19937& generator)
{
    bool const wild = generator() % 8 == 0;
    int const year = wild ? static_cast<int>(generator() % 10020) - 10 : static_cast<int>(generator() % 10000);
    unsigned const month = static_cast<unsigned>(wild ? generator() % 15 : 1 + generator() % 12);
    unsigned const day = static_cast<unsigned>(wild ? generator() % 34 : 1 + generator() % 28);

    return Date(year, month, day);
}
//---------------------------------------------------------------------------
/*
    RandomFloat

    A random double: a quarter of them 0, -0, the infinities, NaN, the smallest subnormal or the largest magnitudes,
    the others any bits (so NaNs of either sign and subnormals too)
*/
double RandomFloat(std::mt19937& generator)
{
    if (generator() % 4 == 0)
    {
        double const specials[] = {
            0.0, -0.0, std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::denorm_min(),
            std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest(),
        };

        return specials[generator() % 8];
    }

    // Two calls, in this order, so a seed gives the same value everywhere
    std::uint64_t bits = generator();
    bits = (bits << 32) | generator();

    return std::bit_cast<double>(bits);
}
//---------------------------------------------------------------------------
/*
    RandomInteger

    A random 64-bit integer: a quarter of them the limits, 0, 1 or -1, the others any bits
*/
std::int64_t RandomInteger(std::mt19937& generator)
{
    if (generator() % 4 == 0)
    {
        std::int64_t const specials[] = {
            std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max(), 0, 1, -1,
        };

        return specials[generator() % 5];
    }

    // Two calls, in this order, so a seed gives the same value everywhere
    std::uint64_t bits = generator();
    bits = (bits << 32) | generator();

    return std::bit_cast<std::int64_t>(bits);
}
//---------------------------------------------------------------------------
/*
    RandomOptions

    Value options with a random string style and integer base
*/
ASWToml::TASWTomlValueOptions RandomOptions(std::mt19937& generator)
{
    ASWToml::TASWTomlValueOptions options;
    options.StringStyle = static_cast<EStringStyle>(generator() % 3);
    options.IntegerBase = static_cast<EIntegerBase>(generator() % 5);

    return options;
}
//---------------------------------------------------------------------------
/*
    RandomScalar

    A random scalar of a random type: a string, an integer, a float, a boolean, or a date or time (see RandomString(),
    RandomInteger(), RandomFloat(), RandomDate() and RandomTime()), with an offset from -23:59 to +23:59, or one in
    eight beyond it
*/
TASWTomlValue RandomScalar(std::mt19937& generator)
{
    switch (generator() % 8)
    {
        case 0:
            return RandomString(generator);

        case 1:
            return RandomInteger(generator);

        case 2:
            return RandomFloat(generator);

        case 3:
            return generator() % 2 == 0;

        case 4:
        {
            TASWTomlOffsetDateTime dateTime;
            dateTime.Date = RandomDate(generator);
            dateTime.Time = RandomTime(generator);

            std::uint32_t const form = static_cast<std::uint32_t>(generator() % 8);
            if (form == 0)
                dateTime.Offset = std::chrono::minutes(24 * 60 + static_cast<int>(generator() % 100));
            else if (form == 1)
                dateTime.Offset = std::chrono::minutes(0);
            else
                dateTime.Offset = std::chrono::minutes(static_cast<int>(generator() % (2 * 1439 + 1)) - 1439);

            return dateTime;
        }

        case 5:
        {
            TASWTomlLocalDateTime dateTime;
            dateTime.Date = RandomDate(generator);
            dateTime.Time = RandomTime(generator);

            return dateTime;
        }

        case 6:
            return RandomDate(generator);
    }

    return RandomTime(generator);
}
//---------------------------------------------------------------------------
/*
    RandomString

    A random string of up to 8 pieces: letters, blanks, line breaks, quotes, backslashes, control characters and
    UTF-8 characters; one in sixteen also gets bytes that aren't valid UTF-8
*/
std::string RandomString(std::mt19937& generator)
{
    std::string const pieces[] = {
        "a", "Z", " ", "\t", "\n", "\r", "\r\n", "\"", "'", "''", "\"\"\"", "'''", "\\", "C:\\", "#", "\x1B", "\x7F",
        std::string(1, '\0'), "\xC3\xA9", "\xE2\x82\xAC", "\xF0\x9F\x98\x80",
    };
    std::string const invalid[] = { "\xFF", "\xC3", "\xED\xA0\x80", "\xC0\xAF" };

    std::string text;
    std::size_t const count = generator() % 9;

    for (std::size_t i = 0; i < count; ++i)
        text += pieces[generator() % (sizeof(pieces) / sizeof(pieces[0]))];

    if (generator() % 16 == 0)
    {
        std::size_t const offset = generator() % (text.size() + 1);
        text.insert(offset, invalid[generator() % 4]);
    }

    return text;
}
//---------------------------------------------------------------------------
/*
    RandomText

    A random text of up to 10 pieces of TOML scalars and what may follow them, valid or not
*/
std::string RandomText(std::mt19937& generator)
{
    std::string const pieces[] = {
        "0", "1", "7", "00", "12", "1979", "-", "+", "_", ".", ":", "e", "E", "x", "o", "b", "T", "t", "Z", "z", " ",
        "\t", "inf", "nan", "true", "false", "\"", "'", "\"\"\"", "'''", "\\", "\\u", "\\U", "\\x", "\\e", "D800",
        "\n", "\r\n", "\r", "#", ",", "]", "\xC3\xA9", "\xFF", "\xED\xA0\x80", std::string(1, '\0'), "1979-05-27",
        "07:32:00", "99999999999999999999", "1e400",
    };

    std::string text;
    std::size_t const count = generator() % 11;

    for (std::size_t i = 0; i < count; ++i)
        text += pieces[generator() % (sizeof(pieces) / sizeof(pieces[0]))];

    return text;
}
//---------------------------------------------------------------------------
/*
    RandomTime

    A random time of day: most of them valid, with a fraction of 0, whole milliseconds, any nanoseconds, or a few
    nanoseconds; one in eight with fields that may be outside their ranges (an hour up to 25, a minute up to 61, a
    second up to 62, a fraction of one second)
*/
TASWTomlLocalTime RandomTime(std::mt19937& generator)
{
    bool const wild = generator() % 8 == 0;
    int const hour = static_cast<int>(generator() % (wild ? 26 : 24));
    int const minute = static_cast<int>(generator() % (wild ? 62 : 60));
    int const second = static_cast<int>(generator() % (wild ? 63 : 61));

    std::int64_t fraction = 0;
    std::uint32_t const form = static_cast<std::uint32_t>(generator() % 4);
    if (form == 1)
        fraction = static_cast<std::int64_t>(generator() % 1000) * 1000000;
    else if (form == 2)
        fraction = static_cast<std::int64_t>(generator() % 1000000000);
    else if (form == 3)
        fraction = wild ? 1000000000 : static_cast<std::int64_t>(generator() % 1000);

    return Time(hour, minute, second, fraction);
}
//---------------------------------------------------------------------------
/*
    SameScalar

    Whether 'left' and 'right' are the same value: floats with the same bits, or both NaN with the same sign (a NaN's
    other bits aren't written); the other types as operator== compares them
*/
bool SameScalar(TASWTomlValue const& left, TASWTomlValue const& right)
{
    double const* const leftFloat = left.AsFloat();
    double const* const rightFloat = right.AsFloat();

    if (leftFloat == nullptr || rightFloat == nullptr)
        return left == right;

    if (std::isnan(*leftFloat) && std::isnan(*rightFloat))
        return std::signbit(*leftFloat) == std::signbit(*rightFloat);

    return Bits(*leftFloat) == Bits(*rightFloat);
}
//---------------------------------------------------------------------------
/*
    ScalarText

    The scalar 'value' as text, to compare it in a check: a string as it is, an integer in decimal, and the other
    types as their writers write them
*/
std::string ScalarText(TASWTomlValue const& value)
{
    std::string text;

    if (value.AsString() != nullptr)
        text = *value.AsString();
    else if (value.AsInteger() != nullptr)
        text = std::to_string(*value.AsInteger());
    else if (value.AsFloat() != nullptr)
        ASWToml::Detail::WriteFloat(*value.AsFloat(), text);
    else if (value.AsBoolean() != nullptr)
        ASWToml::Detail::WriteBoolean(*value.AsBoolean(), text);
    else if (!WriteDateTime(value, text).IsOk())
        text = "(not a scalar that can be written)";

    return text;
}
//---------------------------------------------------------------------------
/*
    Time

    The time of day 'hour':'minute':'second' and 'nanoseconds'
*/
TASWTomlLocalTime Time(int hour, int minute, int second, std::int64_t nanoseconds)
{
    TASWTomlLocalTime time;
    time.Hour = std::chrono::hours(hour);
    time.Minute = std::chrono::minutes(minute);
    time.Second = std::chrono::seconds(second);
    time.Fraction = std::chrono::nanoseconds(nanoseconds);

    return time;
}
//---------------------------------------------------------------------------
/*
    WriteDateTime

    Writes the date or time 'value' to 'text' with the writer of its type; InvalidValue for a value of another type
*/
ASWToml::TASWTomlResult WriteDateTime(TASWTomlValue const& value, std::string& text)
{
    if (value.AsOffsetDateTime() != nullptr)
        return ASWToml::Detail::WriteOffsetDateTime(*value.AsOffsetDateTime(), text);

    if (value.AsLocalDateTime() != nullptr)
        return ASWToml::Detail::WriteLocalDateTime(*value.AsLocalDateTime(), text);

    if (value.AsLocalDate() != nullptr)
        return ASWToml::Detail::WriteLocalDate(*value.AsLocalDate(), text);

    if (value.AsLocalTime() != nullptr)
        return ASWToml::Detail::WriteLocalTime(*value.AsLocalTime(), text);

    ASWToml::TASWTomlResult result;
    result.Code = ETomlCode::InvalidValue;
    result.Message = "Not a date or time.";

    return result;
}
//---------------------------------------------------------------------------
/*
    WriteScalar

    Writes the scalar 'value' to 'text' with the writer of its type, as 'options' ask; InvalidValue for an array or a
    table
*/
ASWToml::TASWTomlResult WriteScalar(TASWTomlValue const& value, std::string& text,
    ASWToml::TASWTomlValueOptions const& options)
{
    if (value.AsString() != nullptr)
        return ASWToml::Detail::WriteString(*value.AsString(), text, options.StringStyle);

    if (value.AsInteger() != nullptr)
        ASWToml::Detail::WriteInteger(*value.AsInteger(), text, options.IntegerBase);
    else if (value.AsFloat() != nullptr)
        ASWToml::Detail::WriteFloat(*value.AsFloat(), text);
    else if (value.AsBoolean() != nullptr)
        ASWToml::Detail::WriteBoolean(*value.AsBoolean(), text);
    else
        return WriteDateTime(value, text);

    return ASWToml::TASWTomlResult();
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Strings
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_Strings::TTest_ASWToml_Strings()
    : inherited("ASWToml_Strings_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_Strings::~TTest_ASWToml_Strings()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Strings::CheckReadFails(std::string const& text, ETomlCode code, std::size_t offset,
    std::string const& expectedText, std::string const& msg, std::source_location loc)
{
    std::string value = "unchanged";
    std::size_t position = 0;

    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadString(text, position, value);

    CheckEquals(code, result.Code, msg + ": the code", loc);
    CheckEquals(offset, position, msg + ": the position should be the error's offset", loc);
    CheckContains(result.Message, expectedText, msg + ": the message should name the problem", loc);
    CheckEquals("unchanged"s, value, msg + ": the value should be unchanged", loc);
    CheckTrue(result.Line >= 1 && result.Column >= 1, msg + ": the line and column should be set", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Strings::CheckReads(std::string const& text, std::string const& expected, std::string const& msg,
    std::source_location loc)
{
    CheckReadsUpTo(text, text.size(), expected, msg, loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Strings::CheckReadsUpTo(std::string const& text, std::size_t end, std::string const& expected,
    std::string const& msg, std::source_location loc)
{
    std::string value = "unchanged";
    std::size_t position = 0;

    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadString(text, position, value);

    CheckTrue(result.IsOk(), msg + ": the string should read: " + result.Message, loc);
    CheckEquals(expected, value, msg + ": the value", loc);
    CheckEquals(end, position, msg + ": the position should be after the closing delimiter", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Strings::CheckWrites(std::string const& value, EStringStyle style, std::string const& expected,
    std::string const& msg, std::source_location loc)
{
    std::string text = "x = ";

    ASWToml::TASWTomlResult const result = ASWToml::Detail::WriteString(value, text, style);

    CheckTrue(result.IsOk(), msg + ": the string should be written: " + result.Message, loc);
    CheckEquals("x = " + expected, text, msg + ": the text", loc);

    std::string read;
    std::size_t position = 4;
    ASWToml::TASWTomlResult const readResult = ASWToml::Detail::ReadString(text, position, read);

    CheckTrue(readResult.IsOk(), msg + ": the written string should read back: " + readResult.Message, loc);
    CheckEquals(value, read, msg + ": the string read back", loc);
    CheckEquals(text.size(), position, msg + ": the string read back should end the text", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Strings::CheckWritesMultiLine(std::string const& value, bool literal, ELineBreak lineBreak,
    bool breakFirst, std::string const& expected, std::string const& msg, std::source_location loc)
{
    std::string text = "x = ";

    ASWToml::TASWTomlResult const result =
        ASWToml::Detail::WriteMultiLineString(value, text, literal, lineBreak, breakFirst);

    CheckTrue(result.IsOk(), msg + ": the string should be written: " + result.Message, loc);
    CheckEquals("x = " + expected, text, msg + ": the text", loc);

    std::string read;
    std::size_t position = 4;
    ASWToml::TASWTomlResult const readResult = ASWToml::Detail::ReadString(text, position, read);

    CheckTrue(readResult.IsOk(), msg + ": the written string should read back: " + readResult.Message, loc);
    CheckEquals(value, read, msg + ": the string read back", loc);
    CheckEquals(text.size(), position, msg + ": the string read back should end the text", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Strings::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Strings::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Strings::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Strings::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Strings, ReadString_BasicStrings)
{
    // Arrange & Act & Assert
    CheckReads("\"abc\"", "abc", "a word");
    CheckReads("\"\"", "", "an empty string");
    CheckReads("\" \"", " ", "a space");
    CheckReads("\"I'm a string. # not a comment\"", "I'm a string. # not a comment", "' and # are text");
    CheckReads("\"a\tb\"", "a\tb", "a tab needs no escape");
    CheckReads("\"Jos\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80\"", "Jos\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80",
        "UTF-8 of 2, 3 and 4 bytes");
    CheckReads("\"\xC2\x85\"", "\xC2\x85", "U+0085, a control character outside ASCII, needs no escape");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Strings, ReadString_Escapes)
{
    // Arrange & Act & Assert
    CheckReads("\"\\b\\t\\n\\f\\r\\e\\\"\\\\\"", "\b\t\n\f\r\x1B\"\\", "the escapes of one letter (\\e is TOML 1.1's)");
    CheckReads("\"\\x41\\x7e\"", "A~", "\\xHH, TOML 1.1's, in either case");
    CheckReads("\"Jos\\xE9\"", "Jos\xC3\xA9", "\\xE9 is U+00E9, written as UTF-8");
    CheckReads("\"\\x00\"", "\0"s, "\\x00 is a NUL");
    CheckReads("\"\\u0000\"", "\0"s, "\\u0000 is a NUL");
    CheckReads("\"\\u007F\\u0080\"", "\x7F\xC2\x80", "the last 1-byte and the first 2-byte character");
    CheckReads("\"\\u07ff\\u0800\"", "\xDF\xBF\xE0\xA0\x80", "the last 2-byte and the first 3-byte character");
    CheckReads("\"\\uD7FF\\uE000\"", "\xED\x9F\xBF\xEE\x80\x80", "the characters around the surrogates");
    CheckReads("\"\\u00E9\\u20ac\"", "\xC3\xA9\xE2\x82\xAC", "\\uHHHH in either case");
    CheckReads("\"\\uFFFF\\U00010000\"", "\xEF\xBF\xBF\xF0\x90\x80\x80",
        "the last 3-byte and the first 4-byte character");
    CheckReads("\"\\U0001F600\"", "\xF0\x9F\x98\x80", "\\UHHHHHHHH");
    CheckReads("\"\\U0010FFFF\"", "\xF4\x8F\xBF\xBF", "the last Unicode scalar value");
    CheckReads("\"\\\\x41\"", "\\x41", "an escaped backslash followed by text");
    CheckReads("\"\"\"\\t\\u00E9\\x41\\\"\"\"\"", "\t\xC3\xA9" "A\"", "escapes in a multi-line basic string");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Strings, ReadString_LiteralStrings)
{
    // Arrange & Act & Assert
    CheckReads("'C:\\Users\\nodejs\\templates'", "C:\\Users\\nodejs\\templates", "a Windows path");
    CheckReads("'\\\\ServerX\\admin$\\system32\\'", "\\\\ServerX\\admin$\\system32\\", "a UNC path ending in \\");
    CheckReads("'Tom \"Dubs\" Preston-Werner'", "Tom \"Dubs\" Preston-Werner", "double quotes");
    CheckReads("'<\\i\\c*\\s*>'", "<\\i\\c*\\s*>", "a regular expression");
    CheckReads("''", "", "an empty literal string");
    CheckReads("'a\tb'", "a\tb", "a tab");
    CheckReads("'\\u00E9 \\n'", "\\u00E9 \\n", "no escapes");
    CheckReads("'Jos\xC3\xA9'", "Jos\xC3\xA9", "UTF-8");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Strings, ReadString_MultiLineBasicStrings)
{
    // Arrange
    std::string const quickFox = "The quick brown fox jumps over the lazy dog.";

    // Act & Assert
    CheckReads("\"\"\"\nRoses are red\nViolets are blue\"\"\"", "Roses are red\nViolets are blue",
        "the line break after the opening delimiter left out");
    CheckReads("\"\"\"\r\nRoses are red\r\nViolets are blue\"\"\"", "Roses are red\nViolets are blue",
        "CRLF left out after the opening delimiter, and read as LF");
    CheckReads("\"\"\"\n\nabc\"\"\"", "\nabc", "only the first line break left out");
    CheckReads("\"\"\"abc\r\n\r\n\"\"\"", "abc\n\n", "CRLF before the closing delimiter");
    CheckReads("\"\"\"a\tb\"\"\"", "a\tb", "a tab");

    // Line-ending backslashes: the spec's examples, also with CRLF and tabs
    CheckReads("\"\"\"\nThe quick brown \\\n\n\n  fox jumps over \\\n    the lazy dog.\"\"\"", quickFox,
        "line-ending backslashes followed by blank lines");
    CheckReads("\"\"\"\\\n       The quick brown \\\n       fox jumps over \\\n       the lazy dog.\\\n       \"\"\"",
        quickFox, "line-ending backslashes on every line");
    CheckReads("\"\"\"\\\r\n  The quick brown \\\r\n  fox jumps over \\\r\n  the lazy dog.\\\r\n  \"\"\"", quickFox,
        "line-ending backslashes with CRLF");
    CheckReads("\"\"\"a\\\t \n\t b\"\"\"", "ab", "blanks after a line-ending backslash and on the next line");
    CheckReads("\"\"\"\\\n    \"\"\"", "", "a line-ending backslash before the closing delimiter");
    CheckReads("\"\"\"\\\n   \\\n   \\  \n   \"\"\"", "", "line-ending backslashes, one followed by spaces");
    CheckReads("\"\"\"\nheeee\ngeeee\\  \n\n\n      \"\"\"", "heeee\ngeeee",
        "a line-ending backslash, then blank lines");
    CheckReads("\"\"\"\\\r\n\"\"\"", "", "a line-ending backslash followed by CRLF");

    // Quotes inside and next to the delimiters
    CheckReads("\"\"\"Here are two quotation marks: \"\". Simple enough.\"\"\"",
        "Here are two quotation marks: \"\". Simple enough.", "two quotes inside");
    CheckReads("\"\"\"\"one quote\"\"\"\"", "\"one quote\"", "a quote after and before the delimiters");
    CheckReads("\"\"\"\"\"two quotes\"\"\"\"\"", "\"\"two quotes\"\"", "two quotes after and before the delimiters");
    CheckReads("\"\"\" \"one quote\" \"\"\"", " \"one quote\" ", "quotes with spaces");
    CheckReads("\"\"\"lol\\\"\"\"\"\"\"", "lol\"\"\"", "an escaped quote, then two quotes before the delimiter");
    CheckReads("\"\"\"\"\"\"", "", "an empty multi-line string");
    CheckReads("\"\"\"\nClosing with four quotes\n\"\"\"\"", "Closing with four quotes\n\"", "closing with four quotes");
    CheckReads("\"\"\"\nClosing with five quotes\n\"\"\"\"\"", "Closing with five quotes\n\"\"",
        "closing with five quotes");
    CheckReads("\"\"\"aaa'''bbb\"\"\"", "aaa'''bbb", "apostrophes");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Strings, ReadString_MultiLineLiteralStrings)
{
    // Arrange & Act & Assert
    CheckReads("'''I [dw]on't need \\d{2} apples'''", "I [dw]on't need \\d{2} apples", "an apostrophe and a backslash");
    CheckReads("'''\nThe first newline is\ntrimmed in raw strings.\n   All other whitespace\n   is preserved.\n'''",
        "The first newline is\ntrimmed in raw strings.\n   All other whitespace\n   is preserved.\n",
        "the spec's example");
    CheckReads("'''\r\nfirst\r\nsecond\r\n'''", "first\nsecond\n", "CRLF left out after the delimiter, read as LF");
    CheckReads("'''a\\\nb'''", "a\\\nb", "a backslash at the end of a line is text");
    CheckReads("'''a\tb'''", "a\tb", "a tab");
    CheckReads("''''one quote''''", "'one quote'", "an apostrophe after and before the delimiters");
    CheckReads("'''''two quotes'''''", "''two quotes''", "two apostrophes after and before the delimiters");
    CheckReads("'''' there's one already\n'' two more\n'''''", "' there's one already\n'' two more\n''",
        "apostrophes on several lines");
    CheckReads("''''''", "", "an empty multi-line literal string");
    CheckReads("'''aaa\"\"\"bbb'''", "aaa\"\"\"bbb", "quotes");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Strings, ReadString_RejectsControlCharacters)
{
    // Arrange
    TStringVector const delimiters = { "\"", "'", "\"\"\"", "'''" };
    std::string const controls = "\0\x01\x08\x0B\x0C\x1F\x7F"s;

    for (std::string const& delimiter : delimiters)
    {
        for (char const control : controls)
        {
            std::string const text = delimiter + "ab" + control + "cd" + delimiter;
            std::string const name = HexByte(static_cast<unsigned char>(control));

            // Act & Assert
            CheckReadFails(text, ETomlCode::ParseFailed, delimiter.size() + 2, "control character U+" + name,
                delimiter + " with U+" + name);
        }

        // Act & Assert
        // A carriage return without a line feed: a control character on one line, else not a line break
        std::string const text = delimiter + "ab\rcd" + delimiter;
        std::string const expectedText = delimiter.size() == 1 ? "control character U+000D" : "a line feed";
        CheckReadFails(text, ETomlCode::ParseFailed, delimiter.size() + 2, expectedText, delimiter + " with a lone CR");
    }

    // Arrange & Act & Assert
    // The messages say how to write the character
    CheckReadFails("\"\x07\"", ETomlCode::ParseFailed, 1, "; write it as \\u0007.", "the basic string's message");
    CheckReadFails("'\x07'", ETomlCode::ParseFailed, 1, "; a basic string can write it as \\u0007.",
        "the literal string's message");

    // A lone CR right after the opening delimiter, and at the end of the text
    CheckReadFails("\"\"\"\rabc\"\"\"", ETomlCode::ParseFailed, 3, "a line feed", "a CR after the delimiter");
    CheckReadFails("'''abc\r", ETomlCode::ParseFailed, 6, "a line feed", "a CR at the end of the text");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Strings, ReadString_RejectsInvalidEscapes)
{
    // Arrange
    // The escapes toml-test's invalid files have, each followed by " z" so a missing digit isn't one of the text's
    TStringVector const delimiters = { "\"", "\"\"\"" };
    TStringVector const escapes = { "\\a", "\\/", "\\N", "\\x0g", "\\xG0", "\\x", "\\ue", "\\Ux", "\\U0000000",
        "\\U0000", "\\Ugggggggg", "\\uD800", "\\uD801", "\\uDFFF", "\\uabag", "\\UFFFFFFFF", "\\U00D80000",
        "\\U00110000" };
    TStringVector const expectedTexts = { "The escape \\a isn't one of TOML's", "The escape \\/ isn't",
        "The escape \\N isn't", "The escape \\x0g needs 2 hexadecimal digits", "The escape \\xG0 needs 2",
        "The escape \\x needs 2", "The escape \\ue needs 4", "The escape \\Ux needs 8",
        "The escape \\U0000000 needs 8", "The escape \\U0000 needs 8", "The escape \\Ugggggggg needs 8",
        "The escape \\uD800 isn't a Unicode scalar value", "The escape \\uD801 isn't a Unicode scalar value",
        "The escape \\uDFFF isn't a Unicode", "The escape \\uabag needs 4", "The escape \\UFFFFFFFF isn't a Unicode",
        "The escape \\U00D80000 isn't a Unicode", "The escape \\U00110000 isn't a Unicode" };

    for (std::string const& delimiter : delimiters)
    {
        for (std::size_t i = 0; i < escapes.size(); ++i)
        {
            std::string const text = delimiter + "ab" + escapes[i] + " z" + delimiter;

            // Act & Assert
            CheckReadFails(text, ETomlCode::ParseFailed, delimiter.size() + 2, expectedTexts[i],
                delimiter + " with " + escapes[i]);
        }
    }

    // Arrange & Act & Assert
    // A backslash followed by something that isn't a letter
    CheckReadFails("\"a\\\xC3\xA9\"", ETomlCode::ParseFailed, 2, "A backslash in a string must start one of TOML's",
        "a backslash before U+00E9");
    CheckReadFails("\"a\\ b\"", ETomlCode::ParseFailed, 2, "A backslash in a string must start",
        "a backslash and a space");
    CheckReadFails("\"a\\\nb\"", ETomlCode::ParseFailed, 2, "A backslash in a string must start",
        "a line-ending backslash in a basic string");
    CheckReadFails("\"ab\\", ETomlCode::ParseFailed, 3, "A backslash in a string must start", "a backslash at the end");
    CheckReadFails("\"\\u12", ETomlCode::ParseFailed, 1, "The escape \\u12 needs 4", "digits cut off by the end");

    // The message quotes the escape as long as it should be, not the text after it
    CheckReadFails("\"\\x4G1\"", ETomlCode::ParseFailed, 1, "The escape \\x4G needs 2 hexadecimal digits.",
        "a letter and a digit after the escape");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Strings, ReadString_RejectsInvalidUtf8)
{
    // Arrange & Act & Assert
    std::string const expectedText = "holds bytes that aren't valid UTF-8";
    CheckReadFails("\"a\xFF" "b\"", ETomlCode::InvalidUtf8, 2, expectedText, "the byte FF");
    CheckReadFails("\"\x80\"", ETomlCode::InvalidUtf8, 1, expectedText, "a continuation byte on its own");
    CheckReadFails("'a\xC3'", ETomlCode::InvalidUtf8, 2, expectedText, "a sequence cut off by the closing quote");
    CheckReadFails("\"\xED\xA0\x80\"", ETomlCode::InvalidUtf8, 1, expectedText, "a surrogate");
    CheckReadFails("\"\xC3\xA9\xFF\"", ETomlCode::InvalidUtf8, 3, expectedText, "after a valid character");
    CheckReadFails("\"\"\"x\n\xC0\xAF\"\"\"", ETomlCode::InvalidUtf8, 5, expectedText, "an overlong form");
    CheckReadFails("'''\xF4\x90\x80\x80'''", ETomlCode::InvalidUtf8, 3, expectedText, "above U+10FFFF");
    CheckReadFails("\"\xE2\x82", ETomlCode::InvalidUtf8, 1, expectedText, "a sequence cut off by the end");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Strings, ReadString_RejectsLineEndingBackslashWithText)
{
    // Arrange & Act & Assert
    // A backslash followed by blanks in a multi-line basic string must end its line
    std::string const expectedText = "A backslash followed by spaces or tabs must be the last character on its line";
    CheckReadFails("\"\"\"t\\ t\"\"\"", ETomlCode::ParseFailed, 4, expectedText, "text after the space");
    CheckReadFails("\"\"\"t\\ \"\"\"", ETomlCode::ParseFailed, 4, expectedText, "the closing delimiter after the space");
    CheckReadFails("\"\"\"a\\\t\tb\"\"\"", ETomlCode::ParseFailed, 4, expectedText, "text after tabs");
    CheckReadFails("\"\"\"\n  foo \\ \\n\n  bar\"\"\"", ETomlCode::ParseFailed, 10, expectedText,
        "toml-test's multiline-escape-space-01");
    CheckReadFails("\"\"\"\nhee \\\n\ngee \\   \"\"\"", ETomlCode::ParseFailed, 15, expectedText,
        "toml-test's multiline-escape-space-02, after a line-ending backslash");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Strings, ReadString_RejectsTextThatIsNotAString)
{
    // Arrange
    TStringVector const texts = { "abc", "", "`a`", "x = 1" };

    for (std::string const& text : texts)
    {
        // Act & Assert
        CheckReadFails(text, ETomlCode::ParseFailed, 0, "A string must start with \" or '", "\"" + text + "\"");
    }

    // Arrange
    std::string value = "unchanged";
    std::size_t position = 9;

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadString("\"a\"", position, value);

    // Assert
    CheckEquals(ETomlCode::ParseFailed, result.Code, "a position past the end should fail");
    CheckEquals(std::size_t(9), position, "a position past the end should stay");
    CheckEquals("unchanged"s, value, "a position past the end should leave the value");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Strings, ReadString_RejectsUnclosedStrings)
{
    // Arrange & Act & Assert
    // Reported at the opening delimiter
    CheckReadFails("\"abc", ETomlCode::ParseFailed, 0, "The string has no closing \".", "the end of the text");
    CheckReadFails("\"", ETomlCode::ParseFailed, 0, "The string has no closing \".", "a quote on its own");
    CheckReadFails("\"abc\nx\"", ETomlCode::ParseFailed, 0, "The string has no closing \" on its line.", "an LF");
    CheckReadFails("\"abc\r\nx\"", ETomlCode::ParseFailed, 0, "The string has no closing \" on its line.", "a CRLF");
    CheckReadFails("\"abc\\\"", ETomlCode::ParseFailed, 0, "The string has no closing \".", "an escaped quote");
    CheckReadFails("\"abc'", ETomlCode::ParseFailed, 0, "The string has no closing \".", "an apostrophe");
    CheckReadFails("'abc", ETomlCode::ParseFailed, 0, "The literal string has no closing '.", "a literal string");
    CheckReadFails("'", ETomlCode::ParseFailed, 0, "The literal string has no closing '.", "an apostrophe alone");
    CheckReadFails("'abc\n'", ETomlCode::ParseFailed, 0, "The literal string has no closing ' on its line.",
        "a literal string and an LF");
    CheckReadFails("\"\"\"", ETomlCode::ParseFailed, 0, "The multi-line string has no closing \"\"\".",
        "a multi-line string's opening delimiter");
    CheckReadFails("\"\"\"abc\"\"", ETomlCode::ParseFailed, 0, "The multi-line string has no closing \"\"\".",
        "two quotes");
    CheckReadFails("\"\"\"\nhee\ngee \"\"", ETomlCode::ParseFailed, 0, "The multi-line string has no closing",
        "several lines");
    CheckReadFails("\"\"\"\\\"\"\"", ETomlCode::ParseFailed, 0, "The multi-line string has no closing",
        "an escaped quote and two quotes");
    CheckReadFails("\"\"\"a\\\n  ", ETomlCode::ParseFailed, 0, "The multi-line string has no closing",
        "a line-ending backslash at the end");
    CheckReadFails("'''abc''", ETomlCode::ParseFailed, 0, "The multi-line literal string has no closing '''.",
        "a multi-line literal string");
    CheckReadFails("'''\n", ETomlCode::ParseFailed, 0, "The multi-line literal string has no closing '''.",
        "a multi-line literal string's opening delimiter and a line break");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Strings, ReadString_ReportsLineAndColumn)
{
    // Arrange
    // The escape \q is on line 2, after six characters (one of two bytes)
    std::string const text = "a = 1\nb = \"\xC3\xA9\\q\"\n";
    std::string value;
    std::size_t position = 10;

    // Act
    ASWToml::TASWTomlResult result = ASWToml::Detail::ReadString(text, position, value);

    // Assert
    CheckEquals(ETomlCode::ParseFailed, result.Code, "\\q should fail");
    CheckEquals(std::size_t(13), position, "the position should be the backslash's");
    CheckEquals(std::size_t(2), result.Line, "the backslash's line");
    CheckEquals(std::size_t(7), result.Column, "the backslash's column, in characters");

    // Arrange
    // A multi-line string that isn't closed is reported where it starts
    std::string const unclosed = "a = 1\r\nb = \"\"\"x\r\ny\r\n";
    position = 11;

    // Act
    result = ASWToml::Detail::ReadString(unclosed, position, value);

    // Assert
    CheckEquals(ETomlCode::ParseFailed, result.Code, "the unclosed string should fail");
    CheckEquals(std::size_t(11), position, "the position should be the opening delimiter's");
    CheckEquals(std::size_t(2), result.Line, "the opening delimiter's line");
    CheckEquals(std::size_t(5), result.Column, "the opening delimiter's column");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Strings, ReadString_StartsAndStopsAtItsToken)
{
    // Arrange
    std::string const line = "key = \"value\" # comment";
    std::string value;
    std::size_t position = 6;

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadString(line, position, value);

    // Assert
    CheckTrue(result.IsOk(), "the string in the line should read: " + result.Message);
    CheckEquals("value"s, value, "the string in the line");
    CheckEquals(std::size_t(13), position, "the position should be after the closing quote");

    // Arrange & Act & Assert
    // The text after a string is the parser's to check: here, what is left of too many quotes
    CheckReadsUpTo("\"\"x", 2, "", "an empty basic string");
    CheckReadsUpTo("''x", 2, "", "an empty literal string");
    CheckReadsUpTo("'a' = 1", 3, "a", "a literal string as a key");
    CheckReadsUpTo("\"\"\"a\"\"\"\"\"\"", 9, "a\"\"", "six quotes after the text: two of them and the delimiter");
    CheckReadsUpTo("'''''''", 7, "'", "seven apostrophes: one inside the delimiters");
    CheckReadsUpTo("\"\"\"\"\"\"\"\"", 8, "\"\"", "eight quotes: two inside the delimiters");
    CheckReadsUpTo("\"\"\"\"\"\"\"\"\"", 8, "\"\"", "nine quotes: one left over");
    CheckReadsUpTo("''''''''''''", 8, "''", "twelve apostrophes: four left over");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Strings, WriteMultiLineString_BasicStrings)
{
    // Arrange & Act & Assert
    CheckWritesMultiLine("one\ntwo", false, ELineBreak::LF, false, "\"\"\"one\ntwo\"\"\"", "a line feed as it is");
    CheckWritesMultiLine("one\ntwo", false, ELineBreak::CRLF, false, "\"\"\"one\r\ntwo\"\"\"",
        "a line feed as the line break asked for");
    CheckWritesMultiLine("one\ntwo", false, ELineBreak::None, false, "\"\"\"one\ntwo\"\"\"", "None writes LF");
    CheckWritesMultiLine("one", false, ELineBreak::CRLF, true, "\"\"\"\r\none\"\"\"",
        "a line break after the opening delimiter");
    CheckWritesMultiLine("\none", false, ELineBreak::LF, false, "\"\"\"\n\none\"\"\"",
        "a text that starts with a line feed gets one after the delimiter, which isn't read");
    CheckWritesMultiLine("", false, ELineBreak::LF, false, "\"\"\"\"\"\"", "an empty string");
    CheckWritesMultiLine("a\tb", false, ELineBreak::LF, false, "\"\"\"a\tb\"\"\"", "a tab as it is");
    CheckWritesMultiLine("C:\\x\b\f\r", false, ELineBreak::LF, false, "\"\"\"C:\\\\x\\b\\f\\r\"\"\"",
        "a backslash and the escapes of one letter");
    CheckWritesMultiLine("a\r\nb", false, ELineBreak::CRLF, false, "\"\"\"a\\r\r\nb\"\"\"",
        "a carriage return is escaped, as a line break reads as a line feed");
    CheckWritesMultiLine("\0\x1B\x7F"s, false, ELineBreak::LF, false, "\"\"\"\\u0000\\u001B\\u007F\"\"\"",
        "other control characters as \\u00XX");
    CheckWritesMultiLine("say \"hi\"", false, ELineBreak::LF, false, "\"\"\"say \"hi\"\"\"\"", "quotes as they are");
    CheckWritesMultiLine("\"\"\"", false, ELineBreak::LF, false, "\"\"\"\"\"\\\"\"\"\"", "the third quote of a run");
    CheckWritesMultiLine("\"\"\"\"\"\"\"", false, ELineBreak::LF, false,
        "\"\"\"\"\"\\\"\"\"\\\"\"\"\"\"", "every third quote of a long run");
    CheckWritesMultiLine("\"\"", false, ELineBreak::LF, false, "\"\"\"\"\"\"\"\"",
        "two quotes just inside the delimiters");
    CheckWritesMultiLine("Jos\xC3\xA9", false, ELineBreak::LF, false, "\"\"\"Jos\xC3\xA9\"\"\"", "UTF-8 as it is");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Strings, WriteMultiLineString_LiteralStrings)
{
    // Arrange & Act & Assert
    CheckWritesMultiLine("C:\\x\nC:\\y", true, ELineBreak::LF, false, "'''C:\\x\nC:\\y'''", "a backslash as it is");
    CheckWritesMultiLine("one\ntwo", true, ELineBreak::CRLF, true, "'''\r\none\r\ntwo'''",
        "line breaks, and one after the opening delimiter");
    CheckWritesMultiLine("\none", true, ELineBreak::LF, false, "'''\n\none'''",
        "a text that starts with a line feed gets one after the delimiter");
    CheckWritesMultiLine("it's 'quoted'", true, ELineBreak::LF, false, "'''it's 'quoted''''",
        "single apostrophes, one just inside the closing delimiter");
    CheckWritesMultiLine("''", true, ELineBreak::LF, false, "''''''''", "two apostrophes inside the delimiters");
    CheckWritesMultiLine("a\tb", true, ELineBreak::LF, false, "'''a\tb'''", "a tab");

    // A text a multi-line literal string can't hold is a multi-line basic string
    CheckWritesMultiLine("'''", true, ELineBreak::LF, false, "\"\"\"'''\"\"\"", "three apostrophes");
    CheckWritesMultiLine("a\r\nb", true, ELineBreak::LF, false, "\"\"\"a\\r\nb\"\"\"", "a carriage return");
    CheckWritesMultiLine("\x1B", true, ELineBreak::LF, false, "\"\"\"\\u001B\"\"\"", "an escape character");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Strings, WriteMultiLineString_ReadsBackEveryCharacter)
{
    // Arrange
    // Every ASCII character, characters of 2, 3 and 4 bytes, and runs of delimiters at the start, inside and the end
    std::string everyAscii;
    for (int c = 0; c < 0x80; ++c)
        everyAscii += static_cast<char>(c);

    std::string const multiByte = "\xC2\x80\xDF\xBF\xE0\xA0\x80\xEF\xBF\xBF\xF0\x90\x80\x80\xF4\x8F\xBF\xBF";
    TStringVector const values = { everyAscii, multiByte, "'", "''", "'''", "''''", "a'", "a''", "'a", "''a", "\"",
        "\"\"", "\"\"\"", "\"\"\"\"", "\"\"\"\"\"", "a\"", "a\"\"", "\"a", "\"\"a", "\n", "\n\n", "a\n", "\r\n", "\\",
        "a\\", "\\\n", "" };

    for (std::string const& value : values)
    {
        for (bool const literal : { false, true })
        {
            for (ELineBreak const lineBreak : { ELineBreak::LF, ELineBreak::CRLF })
            {
                for (bool const breakFirst : { false, true })
                {
                    std::string text;
                    std::string read;
                    std::size_t position = 0;

                    // Act
                    ASWToml::TASWTomlResult const writeResult =
                        ASWToml::Detail::WriteMultiLineString(value, text, literal, lineBreak, breakFirst);
                    ASWToml::TASWTomlResult const readResult = ASWToml::Detail::ReadString(text, position, read);

                    // Assert
                    CheckTrue(writeResult.IsOk(), text + " should be written: " + writeResult.Message);
                    CheckTrue(readResult.IsOk(), text + " should read back: " + readResult.Message);
                    CheckEquals(value, read, text + ": the value read back");
                    CheckEquals(text.size(), position, text + ": the string should be the whole text");
                }
            }
        }
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Strings, WriteMultiLineString_RejectsInvalidUtf8)
{
    // Arrange
    TStringVector const values = { "a\xFF", "\xED\xA0\x80", "\xC3" };

    for (std::string const& value : values)
    {
        for (bool const literal : { false, true })
        {
            std::string text = "x";

            // Act
            ASWToml::TASWTomlResult const result =
                ASWToml::Detail::WriteMultiLineString(value, text, literal, ELineBreak::LF, false);

            // Assert
            CheckEquals(ASWToml::ETomlCode::InvalidValue, result.Code, "invalid UTF-8 can't be written");
            CheckEquals("x"s, text, "nothing should be appended");
        }
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Strings, WriteString_AppendsToText)
{
    // Arrange
    std::string text = "a = ";

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::WriteString("b", text, EStringStyle::Basic);

    // Assert
    CheckTrue(result.IsOk(), "the string should be written: " + result.Message);
    CheckEquals("a = \"b\""s, text, "the string should be appended to the text");

    // Arrange & Act & Assert
    // A style that isn't one of the enumerators writes a basic string
    text.clear();
    CheckTrue(ASWToml::Detail::WriteString("C:\\x", text, static_cast<EStringStyle>(9)).IsOk(),
        "an unknown style should write");
    CheckEquals("\"C:\\\\x\""s, text, "an unknown style should write a basic string");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Strings, WriteString_AutoStyle)
{
    // Arrange & Act & Assert
    // A literal string when the text holds a backslash and a literal string can hold it, else a basic one
    CheckWrites("plain", EStringStyle::Auto, "\"plain\"", "no backslash");
    CheckWrites("", EStringStyle::Auto, "\"\"", "an empty string");
    CheckWrites("C:\\Users\\me", EStringStyle::Auto, "'C:\\Users\\me'", "a Windows path");
    CheckWrites("\\d+\t\\s", EStringStyle::Auto, "'\\d+\t\\s'", "a backslash and a tab");
    CheckWrites("a\\b'c", EStringStyle::Auto, "\"a\\\\b'c\"", "a backslash and an apostrophe");
    CheckWrites("a\\b\nc", EStringStyle::Auto, "\"a\\\\b\\nc\"", "a backslash and a line break");
    CheckWrites("It's \"quoted\"", EStringStyle::Auto, "\"It's \\\"quoted\\\"\"", "quotes without a backslash");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Strings, WriteString_BasicStyle)
{
    // Arrange & Act & Assert
    CheckWrites("C:\\Users\\me", EStringStyle::Basic, "\"C:\\\\Users\\\\me\"", "a Windows path");
    CheckWrites("\"\\\b\t\n\f\r", EStringStyle::Basic, "\"\\\"\\\\\\b\\t\\n\\f\\r\"", "the escapes of one letter");
    CheckWrites("\0"s, EStringStyle::Basic, "\"\\u0000\"", "a NUL");
    CheckWrites("\x01\x1B\x1F\x7F", EStringStyle::Basic, "\"\\u0001\\u001B\\u001F\\u007F\"",
        "other control characters as \\u00XX (TOML 1.0 has no \\e)");
    CheckWrites("Jos\xC3\xA9 \xC2\x85 \xF0\x9F\x98\x80", EStringStyle::Basic,
        "\"Jos\xC3\xA9 \xC2\x85 \xF0\x9F\x98\x80\"", "UTF-8 as it is");
    CheckWrites("'", EStringStyle::Basic, "\"'\"", "an apostrophe");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Strings, WriteString_LiteralStyle)
{
    // Arrange & Act & Assert
    // A literal string when it can hold the text, else a basic one
    CheckWrites("plain", EStringStyle::Literal, "'plain'", "no backslash");
    CheckWrites("", EStringStyle::Literal, "''", "an empty string");
    CheckWrites("C:\\x", EStringStyle::Literal, "'C:\\x'", "a backslash");
    CheckWrites("\"", EStringStyle::Literal, "'\"'", "a quote");
    CheckWrites("a\tb", EStringStyle::Literal, "'a\tb'", "a tab");
    CheckWrites("Jos\xC3\xA9", EStringStyle::Literal, "'Jos\xC3\xA9'", "UTF-8");
    CheckWrites("it's", EStringStyle::Literal, "\"it's\"", "an apostrophe");
    CheckWrites("a\nb", EStringStyle::Literal, "\"a\\nb\"", "a line break");
    CheckWrites("\x7F", EStringStyle::Literal, "\"\\u007F\"", "a DEL");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Strings, WriteString_ReadsBackEveryCharacter)
{
    // Arrange
    // Every ASCII character, characters of 2, 3 and 4 bytes, and delimiters of every form
    std::string everyAscii;
    for (int c = 0; c < 0x80; ++c)
        everyAscii += static_cast<char>(c);

    std::string const multiByte = "\xC2\x80\xDF\xBF\xE0\xA0\x80\xEF\xBF\xBF\xF0\x90\x80\x80\xF4\x8F\xBF\xBF";
    TStringVector const values = { everyAscii, multiByte, "'''", "\"\"\"", "a\\", "\\", "'", "\"", "\r\n", "\t" };

    for (std::string const& value : values)
    {
        for (EStringStyle const style : AllStyles)
        {
            std::string text;
            std::string read;
            std::size_t position = 0;

            // Act
            ASWToml::TASWTomlResult const writeResult = ASWToml::Detail::WriteString(value, text, style);
            ASWToml::TASWTomlResult const readResult = ASWToml::Detail::ReadString(text, position, read);

            // Assert
            std::string const name = text + " (" + ASWToml::ToString(style) + ")";
            CheckTrue(writeResult.IsOk(), name + " should be written: " + writeResult.Message);
            CheckTrue(readResult.IsOk(), name + " should read back: " + readResult.Message);
            CheckEquals(value, read, name + ": the value read back");
            CheckEquals(text.size(), position, name + ": the string should be the whole text");
        }
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Strings, WriteString_RejectsInvalidUtf8)
{
    // Arrange
    TStringVector const values = { "a\xFF", "\xED\xA0\x80", "\xC3" };

    for (std::string const& value : values)
    {
        for (EStringStyle const style : AllStyles)
        {
            std::string text = "x = ";

            // Act
            ASWToml::TASWTomlResult const result = ASWToml::Detail::WriteString(value, text, style);

            // Assert
            std::string const name = std::string("with ") + ASWToml::ToString(style);
            CheckEquals(ETomlCode::InvalidValue, result.Code, name + ": the code");
            CheckContains(result.Message, "isn't valid UTF-8", name + ": the message");
            CheckEquals("x = "s, text, name + ": nothing should be appended");
        }
    }
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Keys
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_Keys::TTest_ASWToml_Keys()
    : inherited("ASWToml_Keys_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_Keys::~TTest_ASWToml_Keys()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Keys::CheckReadFails(std::string const& text, ETomlCode code, std::size_t offset,
    std::string const& expectedText, std::string const& msg, std::source_location loc)
{
    std::string key = "unchanged";
    std::size_t position = 0;

    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadKey(text, position, key);

    CheckEquals(code, result.Code, msg + ": the code", loc);
    CheckEquals(offset, position, msg + ": the position should be the error's offset", loc);
    CheckContains(result.Message, expectedText, msg + ": the message should name the problem", loc);
    CheckEquals(std::size_t(1), result.Line, msg + ": the line", loc);
    CheckEquals(offset + 1, result.Column, msg + ": the column", loc);
    CheckEquals("unchanged"s, key, msg + ": the key should be left as it was", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Keys::CheckReads(std::string const& text, std::string const& key, std::size_t end,
    std::string const& msg, std::source_location loc)
{
    std::string read = "unchanged";
    std::size_t position = 0;

    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadKey(text, position, read);

    CheckTrue(result.IsOk(), msg + ": the key should be read: " + result.Message, loc);
    CheckEquals(key, read, msg + ": the key", loc);
    CheckEquals(end, position, msg + ": the position should be after the key", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Keys::CheckWrites(std::string const& key, std::string const& expected, std::string const& msg,
    std::source_location loc)
{
    std::string text = "[";

    ASWToml::TASWTomlResult const result = ASWToml::Detail::WriteKey(key, text);

    CheckTrue(result.IsOk(), msg + ": the key should be written: " + result.Message, loc);
    CheckEquals("[" + expected, text, msg + ": the text", loc);

    // A quoted key reads back as the key
    if (!expected.empty() && expected[0] == '"')
    {
        std::string read;
        std::size_t position = 1;
        ASWToml::TASWTomlResult const readResult = ASWToml::Detail::ReadString(text, position, read);

        CheckTrue(readResult.IsOk(), msg + ": the quoted key should read back: " + readResult.Message, loc);
        CheckEquals(key, read, msg + ": the key read back", loc);
    }
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Keys::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Keys::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Keys::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Keys::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Keys, ReadKey_BareKeys)
{
    // Arrange & Act & Assert
    CheckReads("port", "port", 4, "a word to the end of the text");
    CheckReads("bare_key-1 = 2", "bare_key-1", 10, "letters, digits, '_' and '-', up to a blank");
    CheckReads("ABCxyz09=", "ABCxyz09", 8, "up to '='");
    CheckReads("1234", "1234", 4, "digits");
    CheckReads("-", "-", 1, "a hyphen alone");
    CheckReads("server.port", "server", 6, "one part of a dotted key, up to the dot");
    CheckReads("true]", "true", 4, "a word that is a value elsewhere, up to ']'");
    CheckReads("1.5", "1", 1, "digits with a dot are two parts");
    CheckReads("a\xC3\xA9", "a", 1, "up to a character outside ASCII");

    // From an offset inside a text
    std::string key;
    std::size_t position = 2;
    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadKey("a.b.c", position, key);

    CheckTrue(result.IsOk(), "a part after a dot should be read: " + result.Message);
    CheckEquals("b"s, key, "the part after the first dot");
    CheckEquals(std::size_t(3), position, "the position should be after that part");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Keys, ReadKey_QuotedKeys)
{
    // Arrange & Act & Assert
    CheckReads("\"host name\" = 1", "host name", 11, "a basic string");
    CheckReads("'C:\\x'.y", "C:\\x", 6, "a literal string, up to the dot");
    CheckReads("\"a.b\"", "a.b", 5, "a dot in a quoted key");
    CheckReads("\"\"", "", 2, "an empty basic string");
    CheckReads("''", "", 2, "an empty literal string");
    CheckReads("\"a\\tb\\u00E9\"", "a\tb\xC3\xA9", 12, "escapes");
    CheckReads("\"\xC3\xA9\"", "\xC3\xA9", 4, "a character outside ASCII");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Keys, ReadKey_RejectsTextThatIsNotAKey)
{
    // Arrange & Act & Assert
    std::string const notAKey = "A key must be a bare key (A-Z, a-z, 0-9, '-' and '_') or a quoted string.";
    CheckReadFails("", ETomlCode::ParseFailed, 0, notAKey, "an empty text");
    CheckReadFails(" a", ETomlCode::ParseFailed, 0, notAKey, "a blank");
    CheckReadFails("= 1", ETomlCode::ParseFailed, 0, notAKey, "an equals sign");
    CheckReadFails(".a", ETomlCode::ParseFailed, 0, notAKey, "a dot");
    CheckReadFails("[a]", ETomlCode::ParseFailed, 0, notAKey, "a bracket");
    CheckReadFails("\xC3\xA9", ETomlCode::ParseFailed, 0, notAKey, "a character outside ASCII, unquoted");

    std::string const multiLine = "A key can't be a multi-line string";
    CheckReadFails("\"\"\"a\"\"\"", ETomlCode::ParseFailed, 0, multiLine, "a multi-line basic string");
    CheckReadFails("'''a'''", ETomlCode::ParseFailed, 0, multiLine, "a multi-line literal string");
    CheckReadFails("\"\"\"", ETomlCode::ParseFailed, 0, multiLine, "three quotes, which an empty key can't start");

    // What ReadString() reports
    CheckReadFails("\"a", ETomlCode::ParseFailed, 0, "The string has no closing \"", "an unclosed basic string");
    CheckReadFails("'a\nb' = 1", ETomlCode::ParseFailed, 0, "has no closing ' on its line", "a line break");
    CheckReadFails("\"a\\q\"", ETomlCode::ParseFailed, 2, "\\q", "an escape TOML doesn't have");
    CheckReadFails("\"a\xFF\"", ETomlCode::InvalidUtf8, 2, "holds bytes that aren't valid UTF-8",
        "bytes that aren't UTF-8");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Keys, WriteKey_BareKeys)
{
    // Arrange & Act & Assert
    CheckWrites("port", "port", "a word");
    CheckWrites("bare_key", "bare_key", "an underscore");
    CheckWrites("bare-key", "bare-key", "a hyphen");
    CheckWrites("1234", "1234", "digits");
    CheckWrites("-", "-", "a hyphen alone");
    CheckWrites("_", "_", "an underscore alone");
    CheckWrites("ABCxyz09", "ABCxyz09", "letters of both cases and digits");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Keys, WriteKey_QuotedKeys)
{
    // Arrange & Act & Assert
    // A basic string, never a literal one
    CheckWrites("", "\"\"", "an empty key");
    CheckWrites("host name", "\"host name\"", "a space");
    CheckWrites("a.b", "\"a.b\"", "a dot, which would make a dotted key");
    CheckWrites("\xC3\xA9", "\"\xC3\xA9\"", "a character outside ASCII");
    CheckWrites("a\"b", "\"a\\\"b\"", "a quote");
    CheckWrites("C:\\x", "\"C:\\\\x\"", "a backslash");
    CheckWrites("\t", "\"\\t\"", "a tab");
    CheckWrites("a\x7F", "\"a\\u007F\"", "a DEL");
    CheckWrites("=", "\"=\"", "an equals sign");
    CheckWrites("key#", "\"key#\"", "a hash");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Keys, WriteKey_RejectsInvalidUtf8)
{
    // Arrange
    std::string text = "[";

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::WriteKey("a\xFF", text);

    // Assert
    CheckEquals(ETomlCode::InvalidValue, result.Code, "the code");
    CheckContains(result.Message, "The key can't be written: it isn't valid UTF-8.", "the message");
    CheckEquals("["s, text, "nothing should be appended");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Integers
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_Integers::TTest_ASWToml_Integers()
    : inherited("ASWToml_Integers_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_Integers::~TTest_ASWToml_Integers()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Integers::CheckReadFails(std::string const& text, ETomlCode code, std::size_t offset,
    std::string const& expectedText, std::string const& msg, std::source_location loc)
{
    TASWTomlValue value = "unchanged";
    std::size_t position = 0;

    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadNumber(text, position, value);

    CheckEquals(code, result.Code, msg + ": the code", loc);
    CheckEquals(offset, position, msg + ": the position should be the error's offset", loc);
    CheckContains(result.Message, expectedText, msg + ": the message should name the problem", loc);
    CheckTrue(value == TASWTomlValue("unchanged"), msg + ": the value should be unchanged", loc);
    CheckTrue(result.Line >= 1 && result.Column >= 1, msg + ": the line and column should be set", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Integers::CheckReads(std::string const& text, std::int64_t expected, std::string const& msg,
    std::source_location loc)
{
    CheckReadsUpTo(text, 0, text.size(), expected, msg, loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Integers::CheckReadsUpTo(std::string const& text, std::size_t start, std::size_t end,
    std::int64_t expected, std::string const& msg, std::source_location loc)
{
    TASWTomlValue value;
    std::size_t position = start;

    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadNumber(text, position, value);

    CheckTrue(result.IsOk(), msg + ": the number should read: " + result.Message, loc);
    CheckTrue(value.AsInteger() != nullptr, msg + ": the value should be an integer", loc);
    CheckEquals(expected, value.AsInteger() != nullptr ? *value.AsInteger() : -1, msg + ": the value", loc);
    CheckEquals(end, position, msg + ": the position should be after the number", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Integers::CheckWrites(std::int64_t value, EIntegerBase base, std::string const& expected,
    std::string const& msg, std::source_location loc)
{
    std::string text;
    ASWToml::Detail::WriteInteger(value, text, base);

    CheckEquals(expected, text, msg + ": the text", loc);
    CheckReads(text, value, msg + ": read back", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Integers::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Integers::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Integers::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Integers::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Integers, ReadNumber_DecimalIntegers)
{
    // Arrange & Act & Assert
    CheckReads("0", 0, "zero");
    CheckReads("+0", 0, "plus zero");
    CheckReads("-0", 0, "minus zero is zero");
    CheckReads("42", 42, "a number");
    CheckReads("+99", 99, "a plus sign");
    CheckReads("-17", -17, "a minus sign");
    CheckReads("1_000", 1000, "an underscore");
    CheckReads("5_349_221", 5349221, "underscores");
    CheckReads("1_2_3_4_5", 12345, "an underscore between every digit");
    CheckReads("9223372036854775807", std::numeric_limits<std::int64_t>::max(), "the largest 64-bit integer");
    CheckReads("9_223_372_036_854_775_807", std::numeric_limits<std::int64_t>::max(), "the largest, with underscores");
    CheckReads("-9223372036854775808", std::numeric_limits<std::int64_t>::min(), "the smallest 64-bit integer");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Integers, ReadNumber_PrefixedIntegers)
{
    // Arrange & Act & Assert
    CheckReads("0xDEADBEEF", 0xDEADBEEF, "hexadecimal in upper case");
    CheckReads("0xdeadbeef", 0xDEADBEEF, "hexadecimal in lower case");
    CheckReads("0xdead_beef", 0xDEADBEEF, "hexadecimal with an underscore");
    CheckReads("0x00987", 0x987, "hexadecimal with leading zeros");
    CheckReads("0x0", 0, "hexadecimal zero");
    CheckReads("0x7FFFFFFFFFFFFFFF", std::numeric_limits<std::int64_t>::max(), "the largest in hexadecimal");
    CheckReads("0x00000000000000000000001", 1, "more leading zeros than 64 bits");
    CheckReads("0o01234567", 01234567, "octal");
    CheckReads("0o755", 0755, "octal permissions");
    CheckReads("0o7_6_5", 0765, "octal with underscores");
    CheckReads("0o777777777777777777777", std::numeric_limits<std::int64_t>::max(), "the largest in octal");
    CheckReads("0b11010110", 0xD6, "binary");
    CheckReads("0b1_0_1", 5, "binary with underscores");
    CheckReads("0b0", 0, "binary zero");
    CheckReads("0b" + std::string(63, '1'), std::numeric_limits<std::int64_t>::max(), "the largest in binary");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Integers, ReadNumber_QuotesLongNumbersInMessages)
{
    // Arrange
    // 50 digits and a letter: the message quotes the first 40 characters
    std::string const text = std::string(50, '1') + "x";

    // Act & Assert
    CheckReadFails(text, ETomlCode::ParseFailed, 50, "The number \"" + std::string(40, '1') + "\"... isn't valid: "
        "unexpected 'x'.", "a long number");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Integers, ReadNumber_RejectsInvalidIntegers)
{
    // Arrange & Act & Assert
    // toml-test's invalid integers, each at the character that breaks the grammar
    std::string const underscore = "an underscore must be between two digits";
    std::string const sign = "a hexadecimal, octal or binary integer can't have a sign";
    std::string const leadingZero = "a decimal number can't start with 0 followed by more digits";
    std::string const digitFirst = "a digit, inf or nan must come first";

    CheckReadFails("0B0", ETomlCode::ParseFailed, 1, "unexpected 'B'", "a capital B");
    CheckReadFails("0X1", ETomlCode::ParseFailed, 1, "unexpected 'X'", "a capital X");
    CheckReadFails("0O0", ETomlCode::ParseFailed, 1, "unexpected 'O'", "a capital O");
    CheckReadFails("--99", ETomlCode::ParseFailed, 1, digitFirst, "two minus signs");
    CheckReadFails("++99", ETomlCode::ParseFailed, 1, digitFirst, "two plus signs");
    CheckReadFails("1__23", ETomlCode::ParseFailed, 1, underscore, "two underscores");
    CheckReadFails("0b", ETomlCode::ParseFailed, 2, "a binary digit must follow 0b", "0b alone");
    CheckReadFails("0x", ETomlCode::ParseFailed, 2, "a hexadecimal digit must follow 0x", "0x alone");
    CheckReadFails("0o", ETomlCode::ParseFailed, 2, "an octal digit must follow 0o", "0o alone");
    CheckReadFails("0b0012", ETomlCode::ParseFailed, 5, "'2' isn't a binary digit", "2 in binary");
    CheckReadFails("0xaafz", ETomlCode::ParseFailed, 5, "'z' isn't a hexadecimal digit", "z in hexadecimal");
    CheckReadFails("0xgabba00f1", ETomlCode::ParseFailed, 2, "a hexadecimal digit must follow 0x", "g after 0x");
    CheckReadFails("0x-1", ETomlCode::ParseFailed, 2, "a hexadecimal digit must follow 0x", "a sign after 0x");
    CheckReadFails("0o778", ETomlCode::ParseFailed, 4, "'8' isn't an octal digit", "8 in octal");
    CheckReadFails("_123", ETomlCode::ParseFailed, 0, underscore, "a leading underscore");
    CheckReadFails("_0b1", ETomlCode::ParseFailed, 0, underscore, "a leading underscore before 0b");
    CheckReadFails("_0x1", ETomlCode::ParseFailed, 0, underscore, "a leading underscore before 0x");
    CheckReadFails("_0o1", ETomlCode::ParseFailed, 0, underscore, "a leading underscore before 0o");
    CheckReadFails("01", ETomlCode::ParseFailed, 1, leadingZero, "a leading zero");
    CheckReadFails("00", ETomlCode::ParseFailed, 1, leadingZero, "two zeros");
    CheckReadFails("0_0", ETomlCode::ParseFailed, 1, leadingZero, "zeros with an underscore");
    CheckReadFails("-01", ETomlCode::ParseFailed, 2, leadingZero, "a leading zero after a minus sign");
    CheckReadFails("+01", ETomlCode::ParseFailed, 2, leadingZero, "a leading zero after a plus sign");
    CheckReadFails("+0_1", ETomlCode::ParseFailed, 2, leadingZero, "a leading zero and an underscore");
    CheckReadFails("-0b11010110", ETomlCode::ParseFailed, 0, sign, "negative binary");
    CheckReadFails("-0xff", ETomlCode::ParseFailed, 0, sign, "negative hexadecimal");
    CheckReadFails("-0o755", ETomlCode::ParseFailed, 0, sign, "negative octal");
    CheckReadFails("+0b11010110", ETomlCode::ParseFailed, 0, sign, "positive binary");
    CheckReadFails("+0xff", ETomlCode::ParseFailed, 0, sign, "positive hexadecimal");
    CheckReadFails("+0o755", ETomlCode::ParseFailed, 0, sign, "positive octal");
    CheckReadFails("123_", ETomlCode::ParseFailed, 3, underscore, "a trailing underscore");
    CheckReadFails("0b1_", ETomlCode::ParseFailed, 3, underscore, "a trailing underscore in binary");
    CheckReadFails("0x1_", ETomlCode::ParseFailed, 3, underscore, "a trailing underscore in hexadecimal");
    CheckReadFails("0o1_", ETomlCode::ParseFailed, 3, underscore, "a trailing underscore in octal");
    CheckReadFails("0b_1", ETomlCode::ParseFailed, 2, underscore, "an underscore after 0b");
    CheckReadFails("0x_1", ETomlCode::ParseFailed, 2, underscore, "an underscore after 0x");
    CheckReadFails("0o_1", ETomlCode::ParseFailed, 2, underscore, "an underscore after 0o");
    CheckReadFails("42abc", ETomlCode::ParseFailed, 2, "unexpected 'a'", "letters after the digits");
    CheckReadFails("1-2", ETomlCode::ParseFailed, 1, "unexpected '-'", "a minus sign after a digit");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Integers, ReadNumber_RejectsOutOfRangeIntegers)
{
    // Arrange & Act & Assert
    // Reported at the start of the number
    std::string const outside = "is outside the 64-bit range, -9223372036854775808 to 9223372036854775807.";
    std::string const larger = "is larger than the largest 64-bit integer, 0x7FFFFFFFFFFFFFFF.";

    CheckReadFails("9223372036854775808", ETomlCode::ValueOutOfRange, 0, "The integer \"9223372036854775808\" " +
        outside, "one more than the largest");
    CheckReadFails("-9223372036854775809", ETomlCode::ValueOutOfRange, 0, outside, "one less than the smallest");
    CheckReadFails("99999999999999999999999", ETomlCode::ValueOutOfRange, 0, outside, "far more than the largest");
    CheckReadFails("0x8000000000000000", ETomlCode::ValueOutOfRange, 0, larger, "2^63 in hexadecimal");
    CheckReadFails("0xFFFFFFFFFFFFFFFF", ETomlCode::ValueOutOfRange, 0, larger, "2^64 - 1 in hexadecimal");
    CheckReadFails("0o1000000000000000000000", ETomlCode::ValueOutOfRange, 0, larger, "2^63 in octal");
    CheckReadFails("0b1" + std::string(63, '0'), ETomlCode::ValueOutOfRange, 0, larger, "2^63 in binary");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Integers, ReadNumber_RejectsTextThatIsNotANumber)
{
    // Arrange
    TStringVector const texts = { "", "#", " 1", "\"1\"", "[1]" };

    for (std::string const& text : texts)
    {
        // Act & Assert
        CheckReadFails(text, ETomlCode::ParseFailed, 0, "A number must start with a digit, '+', '-', inf or nan.",
            "\"" + text + "\"");
    }

    // Arrange
    TASWTomlValue value = "unchanged";
    std::size_t position = 9;

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadNumber("1", position, value);

    // Assert
    CheckEquals(ETomlCode::ParseFailed, result.Code, "a position past the end should fail");
    CheckEquals(std::size_t(9), position, "a position past the end should stay");
    CheckTrue(value == TASWTomlValue("unchanged"), "a position past the end should leave the value");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Integers, ReadNumber_ReportsLineAndColumn)
{
    // Arrange
    std::string const text = "a = 1\nb = 1__2";
    TASWTomlValue value;
    std::size_t position = 10;

    // Act
    ASWToml::TASWTomlResult result = ASWToml::Detail::ReadNumber(text, position, value);

    // Assert
    CheckEquals(ETomlCode::ParseFailed, result.Code, "1__2 should fail");
    CheckEquals(std::size_t(11), position, "the position should be the first underscore's");
    CheckEquals(std::size_t(2), result.Line, "the underscore's line");
    CheckEquals(std::size_t(6), result.Column, "the underscore's column");

    // Arrange
    // A number out of range is reported where it starts
    std::string const outOfRange = "a = 1\r\nb = 99999999999999999999";
    position = 11;

    // Act
    result = ASWToml::Detail::ReadNumber(outOfRange, position, value);

    // Assert
    CheckEquals(ETomlCode::ValueOutOfRange, result.Code, "the number should be out of range");
    CheckEquals(std::size_t(11), position, "the position should be unchanged");
    CheckEquals(std::size_t(2), result.Line, "the number's line");
    CheckEquals(std::size_t(5), result.Column, "the number's column");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Integers, ReadNumber_StopsAtTheEndOfItsRun)
{
    // Arrange & Act & Assert
    // What follows the number is the parser's to check, e.g. " the ultimate answer?" (toml-test's text-after-integer)
    CheckReadsUpTo("answer = 42 the ultimate answer?", 9, 11, 42, "a space after the number");
    CheckReadsUpTo("[1,2]", 1, 2, 1, "a comma");
    CheckReadsUpTo("x=-7}", 2, 4, -7, "a closing brace");
    CheckReadsUpTo("3#c", 0, 1, 3, "a comment");
    CheckReadsUpTo("a = 0x1F]\n", 4, 8, 31, "a closing bracket");
    CheckReadsUpTo("a = 1\r\n", 4, 5, 1, "a line break");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Integers, WriteInteger_AppendsToText)
{
    // Arrange
    std::string text = "a = ";

    // Act
    ASWToml::Detail::WriteInteger(-5, text, EIntegerBase::Auto);

    // Assert
    CheckEquals("a = -5"s, text, "the integer should be appended to the text");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Integers, WriteInteger_Bases)
{
    // Arrange
    std::int64_t const largest = std::numeric_limits<std::int64_t>::max();
    std::int64_t const smallest = std::numeric_limits<std::int64_t>::min();

    // Act & Assert
    CheckWrites(0, EIntegerBase::Auto, "0", "zero");
    CheckWrites(255, EIntegerBase::Auto, "255", "Auto is decimal");
    CheckWrites(255, EIntegerBase::Decimal, "255", "decimal");
    CheckWrites(255, EIntegerBase::Hexadecimal, "0xFF", "hexadecimal, in upper case");
    CheckWrites(0xABCDEF, EIntegerBase::Hexadecimal, "0xABCDEF", "every hexadecimal letter in upper case");
    CheckWrites(255, EIntegerBase::Octal, "0o377", "octal");
    CheckWrites(255, EIntegerBase::Binary, "0b11111111", "binary");
    CheckWrites(0, EIntegerBase::Hexadecimal, "0x0", "zero in hexadecimal");
    CheckWrites(0, EIntegerBase::Binary, "0b0", "zero in binary");
    CheckWrites(largest, EIntegerBase::Decimal, "9223372036854775807", "the largest in decimal");
    CheckWrites(largest, EIntegerBase::Hexadecimal, "0x7FFFFFFFFFFFFFFF", "the largest in hexadecimal");
    CheckWrites(largest, EIntegerBase::Octal, "0o777777777777777777777", "the largest in octal");
    CheckWrites(largest, EIntegerBase::Binary, "0b" + std::string(63, '1'), "the largest in binary");
    CheckWrites(smallest, EIntegerBase::Hexadecimal, "-9223372036854775808", "the smallest, always decimal");
    CheckWrites(-1, EIntegerBase::Binary, "-1", "a negative number in binary is decimal");
    CheckWrites(-255, EIntegerBase::Octal, "-255", "a negative number in octal is decimal");
    CheckWrites(255, static_cast<EIntegerBase>(9), "255", "a base that isn't an EIntegerBase is decimal");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Floats
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_Floats::TTest_ASWToml_Floats()
    : inherited("ASWToml_Floats_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_Floats::~TTest_ASWToml_Floats()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Floats::CheckReadFails(std::string const& text, ETomlCode code, std::size_t offset,
    std::string const& expectedText, std::string const& msg, std::source_location loc)
{
    TASWTomlValue value = "unchanged";
    std::size_t position = 0;

    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadNumber(text, position, value);

    CheckEquals(code, result.Code, msg + ": the code", loc);
    CheckEquals(offset, position, msg + ": the position should be the error's offset", loc);
    CheckContains(result.Message, expectedText, msg + ": the message should name the problem", loc);
    CheckTrue(value == TASWTomlValue("unchanged"), msg + ": the value should be unchanged", loc);
    CheckTrue(result.Line >= 1 && result.Column >= 1, msg + ": the line and column should be set", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Floats::CheckReads(std::string const& text, double expected, std::string const& msg,
    std::source_location loc)
{
    TASWTomlValue value;
    std::size_t position = 0;

    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadNumber(text, position, value);

    CheckTrue(result.IsOk(), msg + ": the number should read: " + result.Message, loc);
    CheckTrue(value.AsFloat() != nullptr, msg + ": the value should be a float", loc);
    CheckEquals(Bits(expected), value.AsFloat() != nullptr ? Bits(*value.AsFloat()) : 0,
        msg + ": the value's bits", loc);
    CheckEquals(text.size(), position, msg + ": the position should be after the number", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Floats::CheckWrites(double value, std::string const& expected, std::string const& msg,
    std::source_location loc)
{
    std::string text;
    ASWToml::Detail::WriteFloat(value, text);

    CheckEquals(expected, text, msg + ": the text", loc);
    CheckReads(text, value, msg + ": read back", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Floats::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Floats::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Floats::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Floats::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Floats, ReadNumber_Floats)
{
    // Arrange & Act & Assert
    CheckReads("+1.0", 1.0, "a plus sign");
    CheckReads("3.1415", 3.1415, "a fraction");
    CheckReads("-0.01", -0.01, "a minus sign");
    CheckReads("5e+22", 5e+22, "an exponent with a plus sign");
    CheckReads("1e06", 1e06, "an exponent with a leading zero");
    CheckReads("-2E-2", -2E-2, "a capital E");
    CheckReads("6.626e-34", 6.626e-34, "a fraction and an exponent");
    CheckReads("3e2", 3e2, "an exponent without a fraction");
    CheckReads("3E+2", 3E+2, "a capital E and a plus sign");
    CheckReads("3e0", 3.0, "a zero exponent");
    CheckReads("9_224_617.445_991_228_313", 9224617.445991228313, "underscores in both parts");
    CheckReads("1_000.0_1e1_0", 1000.01e10, "underscores in every part");
    CheckReads("9_007_199_254_740_991.0", 9007199254740991.0, "the largest safe integer");
    CheckReads("0.0", 0.0, "zero");
    CheckReads("+0.0", 0.0, "plus zero");
    CheckReads("-0.0", -0.0, "minus zero keeps its sign");
    CheckReads("0e0", 0.0, "zero with an exponent");
    CheckReads("1.7976931348623157e308", std::numeric_limits<double>::max(), "the largest double");
    CheckReads("4.9e-324", std::numeric_limits<double>::denorm_min(), "the smallest double");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Floats, ReadNumber_InfAndNan)
{
    // Arrange
    double const infinity = std::numeric_limits<double>::infinity();
    double const nan = std::numeric_limits<double>::quiet_NaN();

    // Act & Assert
    CheckReads("inf", infinity, "inf");
    CheckReads("+inf", infinity, "+inf");
    CheckReads("-inf", -infinity, "-inf");
    CheckReads("nan", nan, "nan");
    CheckReads("+nan", nan, "+nan");
    CheckReads("-nan", std::copysign(nan, -1.0), "-nan keeps its sign bit");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Floats, ReadNumber_RejectsInvalidFloats)
{
    // Arrange & Act & Assert
    // toml-test's invalid floats, each at the character that breaks the grammar
    std::string const underscore = "an underscore must be between two digits";
    std::string const leadingZero = "a decimal number can't start with 0 followed by more digits";
    std::string const digitFirst = "a digit, inf or nan must come first";
    std::string const fraction = "a digit must follow the decimal point";
    std::string const exponent = "the exponent needs digits";

    CheckReadFails("0..1", ETomlCode::ParseFailed, 2, fraction, "two dots");
    CheckReadFails("0.1.2", ETomlCode::ParseFailed, 3, "unexpected '.'", "a second fraction");
    CheckReadFails("1e2.3", ETomlCode::ParseFailed, 3, "unexpected '.'", "a fraction after the exponent");
    CheckReadFails("1.e2", ETomlCode::ParseFailed, 2, fraction, "no digit between the dot and the exponent");
    CheckReadFails("3.e+20", ETomlCode::ParseFailed, 2, fraction, "no digit before a signed exponent");
    CheckReadFails("1ee2", ETomlCode::ParseFailed, 2, exponent, "two e's");
    CheckReadFails("1e2e3", ETomlCode::ParseFailed, 3, "unexpected 'e'", "two exponents");
    CheckReadFails("1e__23", ETomlCode::ParseFailed, 2, underscore, "two underscores in the exponent");
    CheckReadFails("1e_23", ETomlCode::ParseFailed, 2, underscore, "an underscore starting the exponent");
    CheckReadFails("1e23_", ETomlCode::ParseFailed, 4, underscore, "an underscore ending the exponent");
    CheckReadFails("1_e2", ETomlCode::ParseFailed, 1, underscore, "an underscore before the e");
    CheckReadFails("1.2_e2", ETomlCode::ParseFailed, 3, underscore, "an underscore after the fraction");
    CheckReadFails("Inf", ETomlCode::ParseFailed, 0, digitFirst, "Inf in capitals");
    CheckReadFails("in", ETomlCode::ParseFailed, 0, digitFirst, "in");
    CheckReadFails("+in", ETomlCode::ParseFailed, 1, digitFirst, "+in");
    CheckReadFails("-in", ETomlCode::ParseFailed, 1, digitFirst, "-in");
    CheckReadFails("in_f", ETomlCode::ParseFailed, 0, digitFirst, "in_f");
    CheckReadFails("infinity", ETomlCode::ParseFailed, 0, digitFirst, "infinity");
    CheckReadFails("NaN", ETomlCode::ParseFailed, 0, digitFirst, "NaN in capitals");
    CheckReadFails("na", ETomlCode::ParseFailed, 0, digitFirst, "na");
    CheckReadFails("+na", ETomlCode::ParseFailed, 1, digitFirst, "+na");
    CheckReadFails("-na", ETomlCode::ParseFailed, 1, digitFirst, "-na");
    CheckReadFails("na_n", ETomlCode::ParseFailed, 0, digitFirst, "na_n");
    CheckReadFails(".12345", ETomlCode::ParseFailed, 0, digitFirst, "a leading dot");
    CheckReadFails("-.12345", ETomlCode::ParseFailed, 1, digitFirst, "a leading dot after a minus sign");
    CheckReadFails("+.12345", ETomlCode::ParseFailed, 1, digitFirst, "a leading dot after a plus sign");
    CheckReadFails("_1.2", ETomlCode::ParseFailed, 0, underscore, "a leading underscore");
    CheckReadFails("03.14", ETomlCode::ParseFailed, 1, leadingZero, "a leading zero");
    CheckReadFails("-03.14", ETomlCode::ParseFailed, 2, leadingZero, "a leading zero after a minus sign");
    CheckReadFails("+03.14", ETomlCode::ParseFailed, 2, leadingZero, "a leading zero after a plus sign");
    CheckReadFails("1.", ETomlCode::ParseFailed, 2, fraction, "a trailing dot");
    CheckReadFails("-1.", ETomlCode::ParseFailed, 3, fraction, "a trailing dot after a minus sign");
    CheckReadFails("+1.", ETomlCode::ParseFailed, 3, fraction, "a trailing dot after a plus sign");
    CheckReadFails("0.0E", ETomlCode::ParseFailed, 4, exponent, "a trailing E");
    CheckReadFails("0.e", ETomlCode::ParseFailed, 2, fraction, "a dot and a trailing e");
    CheckReadFails("0.0e-", ETomlCode::ParseFailed, 5, exponent, "a trailing e-");
    CheckReadFails("0.0e+", ETomlCode::ParseFailed, 5, exponent, "a trailing e+");
    CheckReadFails("1.2_", ETomlCode::ParseFailed, 3, underscore, "a trailing underscore");
    CheckReadFails("1._2", ETomlCode::ParseFailed, 2, underscore, "an underscore after the dot");
    CheckReadFails("1_.2", ETomlCode::ParseFailed, 1, underscore, "an underscore before the dot");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Floats, ReadNumber_RejectsOutOfRangeFloats)
{
    // Arrange & Act & Assert
    // Reported at the start of the number; never read as inf
    std::string const outside = "is outside the range of a double, about plus or minus 1.8e308.";

    CheckReadFails("1e400", ETomlCode::ValueOutOfRange, 0, "The float \"1e400\" " + outside, "1e400");
    CheckReadFails("-1e400", ETomlCode::ValueOutOfRange, 0, outside, "-1e400");
    CheckReadFails("1.7976931348623159e308", ETomlCode::ValueOutOfRange, 0, outside, "just above the largest double");
    CheckReadFails("1e99999999999999999999", ETomlCode::ValueOutOfRange, 0, outside, "a huge exponent");
    CheckReadFails("1" + std::string(400, '0') + ".0", ETomlCode::ValueOutOfRange, 0, outside, "401 digits");
    CheckReadFails("0.000001e315", ETomlCode::ValueOutOfRange, 0, outside, "1e309 written with a fraction");
    CheckReadFails("1" + std::string(400, '0') + ".0e-50", ETomlCode::ValueOutOfRange, 0, outside,
        "1e350 written with a negative exponent");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Floats, ReadNumber_UnderflowReadsAsZero)
{
    // Arrange & Act & Assert
    // A number below the smallest double rounds to 0.0, keeping its sign
    CheckReads("1e-400", 0.0, "1e-400");
    CheckReads("-1e-400", -0.0, "-1e-400 reads as -0.0");
    CheckReads("2.4703282292062327e-324", 0.0, "just below half the smallest double");
    CheckReads("2.4703282292062328e-324", std::numeric_limits<double>::denorm_min(),
        "just above half the smallest double rounds up");
    CheckReads("1e-99999999999999999999", 0.0, "a huge negative exponent");
    CheckReads("0." + std::string(400, '0') + "1", 0.0, "400 zeros after the dot");
    CheckReads("100000e-330", 0.0, "1e-325 written with digits before the dot");
    CheckReads("-0." + std::string(400, '0') + "1e50", -0.0, "-1e-351 written with a positive exponent");
    CheckReads("0e99999999999999999999", 0.0, "zero with a huge exponent");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Floats, WriteFloat_InfAndNan)
{
    // Arrange
    double const infinity = std::numeric_limits<double>::infinity();
    double const nan = std::numeric_limits<double>::quiet_NaN();

    // Act & Assert
    CheckWrites(infinity, "inf", "inf");
    CheckWrites(-infinity, "-inf", "-inf");
    CheckWrites(nan, "nan", "nan");
    CheckWrites(std::copysign(nan, -1.0), "-nan", "a NaN with its sign bit set");

    // Arrange
    std::string text = "a = ";

    // Act
    ASWToml::Detail::WriteFloat(2.5, text);

    // Assert
    CheckEquals("a = 2.5"s, text, "the float should be appended to the text");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Floats, WriteFloat_ReadsBackEveryExponent)
{
    // Arrange
    // Every power of 2 a double has, subnormals included, and a value with more bits next to each
    for (int exponent = -1074; exponent <= 1023; ++exponent)
    {
        double const power = std::ldexp(1.0, exponent);
        double const values[] = { power, -power, 1.1 * power, std::nextafter(power, 0.0) };

        for (double const value : values)
        {
            std::string text;
            TASWTomlValue read;
            std::size_t position = 0;

            // Act
            ASWToml::Detail::WriteFloat(value, text);
            ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadNumber(text, position, read);

            // Assert
            CheckTrue(result.IsOk() && read.AsFloat() != nullptr && Bits(*read.AsFloat()) == Bits(value),
                text + " should read back as the double it was written from: " + result.Message);
        }
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Floats, WriteFloat_ShortestText)
{
    // Arrange & Act & Assert
    // std::to_chars' shortest text, with ".0" when it has neither a dot nor an exponent
    CheckWrites(1.0, "1.0", "one");
    CheckWrites(0.0, "0.0", "zero");
    CheckWrites(-0.0, "-0.0", "minus zero");
    CheckWrites(100.0, "100.0", "a hundred");
    CheckWrites(0.1, "0.1", "a tenth");
    CheckWrites(0.1 + 0.2, "0.30000000000000004", "a sum that isn't 0.3");
    CheckWrites(3.141592653589793, "3.141592653589793", "pi");
    CheckWrites(-1.5e-10, "-1.5e-10", "a small negative number");
    CheckWrites(1e15, "1e+15", "1e15, shorter with an exponent");
    CheckWrites(1e16, "1e+16", "1e16");
    CheckWrites(1e-7, "1e-07", "1e-7, with a two-digit exponent");
    CheckWrites(123456789012345680.0, "123456789012345680.0", "a large number without an exponent");
    CheckWrites(9007199254740991.0, "9007199254740991.0", "the largest safe integer");
    CheckWrites(std::numeric_limits<double>::max(), "1.7976931348623157e+308", "the largest double");
    CheckWrites(std::numeric_limits<double>::min(), "2.2250738585072014e-308", "the smallest normal double");
    CheckWrites(std::numeric_limits<double>::denorm_min(), "5e-324", "the smallest double");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Booleans
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_Booleans::TTest_ASWToml_Booleans()
    : inherited("ASWToml_Booleans_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_Booleans::~TTest_ASWToml_Booleans()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Booleans::CheckReadsUpTo(std::string const& text, std::size_t start, std::size_t end,
    bool expected, std::string const& msg, std::source_location loc)
{
    TASWTomlValue value;
    std::size_t position = start;

    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadScalar(text, position, value);

    CheckTrue(result.IsOk(), msg + ": the boolean should read: " + result.Message, loc);
    CheckTrue(value.AsBoolean() != nullptr, msg + ": the value should be a boolean", loc);
    CheckEquals(expected, value.AsBoolean() != nullptr ? *value.AsBoolean() : !expected, msg + ": the value", loc);
    CheckEquals(end, position, msg + ": the position should be after the boolean", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Booleans::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Booleans::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Booleans::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Booleans::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Booleans, ReadScalar_Booleans)
{
    // Arrange & Act & Assert
    CheckReadsUpTo("true", 0, 4, true, "true");
    CheckReadsUpTo("false", 0, 5, false, "false");
    CheckReadsUpTo("a = true # comment", 4, 8, true, "true before a comment");
    CheckReadsUpTo("[true,false]", 1, 5, true, "true before a comma");
    CheckReadsUpTo("[true,false]", 6, 11, false, "false before a closing bracket");
    CheckReadsUpTo("{a=false}", 3, 8, false, "false before a closing brace");
    CheckReadsUpTo("b = false\r\n", 4, 9, false, "false before a line break");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Booleans, ReadScalar_RejectsOtherWords)
{
    // Arrange
    // toml-test's invalid booleans, and other words that aren't inf or nan
    TStringVector const words = { "True", "FALSE", "trUe", "falsE", "tru", "fals", "t", "f", "truthy", "falsey",
        "true_", "true-1", "yes", "John", "infinity", "nan2", "T", "TZ", "T." };

    for (std::string const& word : words)
    {
        TASWTomlValue value = "unchanged";
        std::size_t position = 0;

        // Act
        ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadScalar(word + " # comment", position, value);

        // Assert
        CheckEquals(ETomlCode::ParseFailed, result.Code, word + " should fail");
        CheckEquals(std::size_t(0), position, word + ": the position should be the word's start");
        CheckEquals("The value \"" + word + "\" isn't valid: a string must be in quotes, and a boolean is true or "
            "false (in lower case).", result.Message, word + ": the message should quote the word");
        CheckTrue(value == TASWTomlValue("unchanged"), word + ": the value should be unchanged");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Booleans, WriteBoolean_ReadsBack)
{
    // Arrange
    std::string text = "a = ";

    // Act
    ASWToml::Detail::WriteBoolean(true, text);

    // Assert
    CheckEquals("a = true"s, text, "true should be appended to the text");
    CheckReadsUpTo(text, 4, 8, true, "true read back");

    // Arrange
    text.clear();

    // Act
    ASWToml::Detail::WriteBoolean(false, text);

    // Assert
    CheckEquals("false"s, text, "false");
    CheckReadsUpTo(text, 0, 5, false, "false read back");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DateTimeTexts
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_DateTimeTexts::TTest_ASWToml_DateTimeTexts()
    : inherited("ASWToml_DateTimeTexts_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_DateTimeTexts::~TTest_ASWToml_DateTimeTexts()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DateTimeTexts::CheckReadFails(std::string const& text, std::size_t offset,
    std::string const& expectedText, std::string const& msg, std::source_location loc)
{
    TASWTomlValue value = "unchanged";
    std::size_t position = 0;

    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadDateTime(text, position, value);

    CheckEquals(ETomlCode::ParseFailed, result.Code, msg + ": the code", loc);
    CheckEquals(offset, position, msg + ": the position should be the error's offset", loc);
    CheckContains(result.Message, expectedText, msg + ": the message should name the problem", loc);
    CheckTrue(value == TASWTomlValue("unchanged"), msg + ": the value should be unchanged", loc);
    CheckTrue(result.Line >= 1 && result.Column >= 1, msg + ": the line and column should be set", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DateTimeTexts::CheckReads(std::string const& text, TASWTomlValue const& expected,
    std::string const& msg, std::source_location loc)
{
    CheckReadsUpTo(text, 0, text.size(), expected, msg, loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DateTimeTexts::CheckReadsUpTo(std::string const& text, std::size_t start, std::size_t end,
    TASWTomlValue const& expected, std::string const& msg, std::source_location loc)
{
    TASWTomlValue value;
    std::size_t position = start;

    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadDateTime(text, position, value);

    CheckTrue(result.IsOk(), msg + ": the date or time should read: " + result.Message, loc);
    CheckEquals(ASWToml::ToString(expected.GetType()), ASWToml::ToString(value.GetType()),
        msg + ": the type", loc);
    CheckTrue(value == expected, msg + ": the value should be " + ScalarText(expected) + ", not " + ScalarText(value),
        loc);
    CheckEquals(end, position, msg + ": the position should be after the date or time", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DateTimeTexts::CheckWriteFails(TASWTomlValue const& value, std::string const& expectedText,
    std::string const& msg, std::source_location loc)
{
    std::string text = "unchanged";

    ASWToml::TASWTomlResult const result = WriteDateTime(value, text);

    CheckEquals(ETomlCode::InvalidValue, result.Code, msg + ": the code", loc);
    CheckContains(result.Message, expectedText, msg + ": the message should name the problem", loc);
    CheckEquals("unchanged"s, text, msg + ": nothing should be appended", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DateTimeTexts::CheckWrites(TASWTomlValue const& value, std::string const& expected,
    std::string const& msg, std::source_location loc)
{
    std::string text;

    ASWToml::TASWTomlResult const result = WriteDateTime(value, text);

    CheckTrue(result.IsOk(), msg + ": the value should be written: " + result.Message, loc);
    CheckEquals(expected, text, msg + ": the text", loc);
    CheckReads(text, value, msg + ": read back", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DateTimeTexts::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DateTimeTexts::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DateTimeTexts::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DateTimeTexts::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeTexts, ReadDateTime_FractionsOfASecond)
{
    // Arrange & Act & Assert
    // Kept to nanoseconds; more digits are cut off, not rounded
    CheckReads("00:00:00.5", Time(0, 0, 0, 500000000), "one digit");
    CheckReads("10:32:00.555", Time(10, 32, 0, 555000000), "milliseconds");
    CheckReads("00:32:00.999999", Time(0, 32, 0, 999999000), "microseconds");
    CheckReads("00:00:00.123456789", Time(0, 0, 0, 123456789), "nanoseconds");
    CheckReads("00:00:00.000000001", Time(0, 0, 0, 1), "one nanosecond");
    CheckReads("00:00:00.000", Time(0, 0, 0, 0), "zeros");
    CheckReads("00:00:00.1234567899", Time(0, 0, 0, 123456789), "a tenth digit is cut off");
    CheckReads("00:00:00.9999999999999", Time(0, 0, 0, 999999999), "nines after the ninth digit aren't rounded up");
    CheckReads("00:00:00.0000000009", Time(0, 0, 0, 0), "less than a nanosecond is 0");
    CheckReads("1979-05-27T00:32:00.999999", TASWTomlLocalDateTime{ Date(1979, 5, 27), Time(0, 32, 0, 999999000) },
        "in a local date-time");
    CheckReads("1979-05-27T00:32:00.5-07:00", TASWTomlOffsetDateTime{ Date(1979, 5, 27), Time(0, 32, 0, 500000000),
        std::chrono::minutes(-7 * 60) }, "in an offset date-time");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeTexts, ReadDateTime_LeapSeconds)
{
    // Arrange & Act & Assert
    // Second 60 reads at any time, kept as written
    CheckReads("23:59:60", Time(23, 59, 60), "at the end of a day");
    CheckReads("12:00:60.5", Time(12, 0, 60, 500000000), "at any hour, with a fraction");
    CheckReads("2016-12-31 23:59:60", TASWTomlLocalDateTime{ Date(2016, 12, 31), Time(23, 59, 60) },
        "in a local date-time");
    CheckReads("1990-12-31T23:59:60Z", TASWTomlOffsetDateTime{ Date(1990, 12, 31), Time(23, 59, 60),
        std::chrono::minutes(0) }, "in UTC");
    CheckReads("1990-12-31T15:59:60-08:00", TASWTomlOffsetDateTime{ Date(1990, 12, 31), Time(15, 59, 60),
        std::chrono::minutes(-8 * 60) }, "at an offset (RFC 3339's example)");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeTexts, ReadDateTime_LocalDates)
{
    // Arrange & Act & Assert
    CheckReads("1979-05-27", Date(1979, 5, 27), "a date");
    CheckReads("0000-01-01", Date(0, 1, 1), "year 0");
    CheckReads("0001-01-01", Date(1, 1, 1), "the first year");
    CheckReads("9999-12-31", Date(9999, 12, 31), "the last date");
    CheckReads("2000-02-29", Date(2000, 2, 29), "February 29 in 2000");
    CheckReads("2024-02-29", Date(2024, 2, 29), "February 29 in a leap year");
    CheckReads("2023-04-30", Date(2023, 4, 30), "the last day of April");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeTexts, ReadDateTime_LocalDateTimes)
{
    // Arrange
    TASWTomlLocalDateTime const expected{ Date(1979, 5, 27), Time(7, 32, 0) };

    // Act & Assert
    CheckReads("1979-05-27T07:32:00", expected, "a T");
    CheckReads("1979-05-27t07:32:00", expected, "a t");
    CheckReads("1979-05-27 07:32:00", expected, "a space");
    CheckReads("1979-05-27T07:32", expected, "without seconds");
    CheckReads("1979-05-27 07:32", expected, "a space and no seconds");
    CheckReads("0001-01-01 00:00:00", TASWTomlLocalDateTime{ Date(1, 1, 1), Time(0, 0, 0) }, "the first");
    CheckReads("9999-12-31 23:59:59", TASWTomlLocalDateTime{ Date(9999, 12, 31), Time(23, 59, 59) }, "the last");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeTexts, ReadDateTime_LocalTimes)
{
    // Arrange & Act & Assert
    CheckReads("07:32:00", Time(7, 32, 0), "a time");
    CheckReads("00:00:00", Time(0, 0, 0), "midnight");
    CheckReads("23:59:59", Time(23, 59, 59), "the last second of a day");
    CheckReads("13:37", Time(13, 37, 0), "without seconds");
    CheckReads("00:00", Time(0, 0, 0), "midnight without seconds");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeTexts, ReadDateTime_OffsetDateTimes)
{
    // Arrange
    auto const at = [](int minutes)
        {
            return TASWTomlOffsetDateTime{ Date(1979, 5, 27), Time(7, 32, 0), std::chrono::minutes(minutes) };
        };

    // Act & Assert
    CheckReads("1979-05-27T07:32:00Z", at(0), "Z");
    CheckReads("1979-05-27t07:32:00z", at(0), "t and z");
    CheckReads("1979-05-27 07:32:00Z", at(0), "a space");
    CheckReads("1979-05-27T07:32:00-00:00", at(0), "-00:00 is UTC");
    CheckReads("1979-05-27T07:32:00+00:00", at(0), "+00:00 is UTC");
    CheckReads("1979-05-27T07:32:00-07:00", at(-7 * 60), "a negative offset");
    CheckReads("1979-05-27T07:32:00+12:00", at(12 * 60), "a positive offset");
    CheckReads("1979-05-27T07:32:00+05:30", at(5 * 60 + 30), "an offset with minutes");
    CheckReads("1979-05-27T07:32:00-00:30", at(-30), "a negative offset below an hour");
    CheckReads("1979-05-27T07:32:00+23:59", at(23 * 60 + 59), "the largest offset");
    CheckReads("1979-05-27T07:32:00-23:59", at(-(23 * 60 + 59)), "the smallest offset");
    CheckReads("1979-05-27 07:32Z", at(0), "without seconds");
    CheckReads("1979-05-27 07:32-07:00", at(-7 * 60), "without seconds, with an offset");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeTexts, ReadDateTime_RejectsInvalidDates)
{
    // Arrange & Act & Assert
    // A field out of range is reported at its start, a field of the wrong size where it starts
    std::string const year = "the year must have four digits";
    std::string const month = "the month must be 01 to 12";

    CheckReadFails("2023-02-29", 8, "The date or time \"2023-02-29\" isn't valid: the day must be 01 to 28 in "
        "2023-02.", "February 29 in a common year");
    CheckReadFails("2100-02-29", 8, "the day must be 01 to 28 in 2100-02", "2100 isn't a leap year");
    CheckReadFails("1988-02-30", 8, "the day must be 01 to 29 in 1988-02", "February 30 in a leap year");
    CheckReadFails("2006-04-31", 8, "the day must be 01 to 30 in 2006-04", "April 31");
    CheckReadFails("2006-01-32", 8, "the day must be 01 to 31 in 2006-01", "January 32");
    CheckReadFails("2006-01-00", 8, "the day must be 01 to 31 in 2006-01", "day 0");
    CheckReadFails("1997-09-00T09:09:09.09Z", 8, "the day must be 01 to 30 in 1997-09", "day 0 in a date-time");
    CheckReadFails("2006-13-01", 5, month, "month 13");
    CheckReadFails("2007-00-01", 5, month, "month 0");
    CheckReadFails("199-09-09", 0, year, "three digits");
    CheckReadFails("10000-01-01", 0, year, "five digits");
    CheckReadFails("199709-09", 0, year, "no '-' after the year");
    CheckReadFails("1-2", 0, year, "one digit");
    CheckReadFails("1987-7-05", 5, "the month must have two digits", "a one-digit month");
    CheckReadFails("1987-07-5", 8, "the day must have two digits", "a one-digit day");
    CheckReadFails("1987-07-0517:45:00", 8, "the day must have two digits", "no T between the date and the time");
    CheckReadFails("1997-09", 7, "a '-' and the day must follow the month", "no day");
    CheckReadFails("1997-09-", 8, "the day must have two digits", "no day after the '-'");
    CheckReadFails("2006-01-30T", 11, "the hour must have two digits", "a T without a time");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeTexts, ReadDateTime_RejectsInvalidOffsets)
{
    // Arrange & Act & Assert
    std::string const noOffset = "a time without a date can't have an offset";

    CheckReadFails("1985-06-18 17:04:07+25:00", 20, "the offset's hour must be 00 to 23", "hour 25");
    CheckReadFails("1985-06-18 17:04:07-24:00", 20, "the offset's hour must be 00 to 23", "hour 24");
    CheckReadFails("1985-06-18 17:04:07+12:60", 23, "the offset's minute must be 00 to 59", "minute 60");
    CheckReadFails("1997-09-09T09:09:09.09+09:9", 26, "the offset's minute must have two digits", "a one-digit minute");
    CheckReadFails("1997-09-09T09:09:09.09+", 23, "the offset's hour must have two digits", "a sign alone");
    CheckReadFails("1997-09-09T09:09:09.09-0909", 23, "the offset's hour must have two digits", "no ':'");
    CheckReadFails("1997-09-09T09:09:09.09+09", 25, "a ':' and the minute must follow the offset's hour", "no minute");
    CheckReadFails("07:32:00Z", 8, noOffset, "Z after a time");
    CheckReadFails("07:32:00+01:00", 8, noOffset, "+01:00 after a time");
    CheckReadFails("07:32:00-01:00", 8, noOffset, "-01:00 after a time");
    CheckReadFails("07:32z", 5, noOffset, "z after a time without seconds");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeTexts, ReadDateTime_RejectsInvalidTimes)
{
    // Arrange & Act & Assert
    std::string const hourDigits = "the hour must have two digits";
    std::string const noDigit = "a digit must follow the decimal point";

    CheckReadFails("24:00:00", 0, "The date or time \"24:00:00\" isn't valid: the hour must be 00 to 23.", "hour 24");
    CheckReadFails("00:60:00", 3, "the minute must be 00 to 59", "minute 60");
    CheckReadFails("00:00:61", 6, "the second must be 00 to 60 (60 is a leap second)", "second 61");
    CheckReadFails("2006-01-01T24:00:00", 11, "the hour must be 00 to 23", "hour 24 in a date-time");
    CheckReadFails("1:32:00", 0, hourDigits, "a one-digit hour");
    CheckReadFails("123:00:00", 0, hourDigits, "a three-digit hour");
    CheckReadFails("2023-10-01T1:32:00Z", 11, hourDigits, "a one-digit hour after a T");
    CheckReadFails("1979-05-27 7:32:00", 11, hourDigits, "a one-digit hour after a space");
    CheckReadFails("01:2:00", 3, "the minute must have two digits", "a one-digit minute");
    CheckReadFails("07:", 3, "the minute must have two digits", "no minute");
    CheckReadFails("01:32:0", 6, "the second must have two digits", "a one-digit second");
    CheckReadFails("01:32:000", 6, "the second must have two digits", "a three-digit second");
    CheckReadFails("07:32:", 6, "the second must have two digits", "no second after the ':'");
    CheckReadFails("1997-09-09T09", 13, "a ':' and the minute must follow the hour", "no minute after a T");
    CheckReadFails("1997-09-09T09-30", 13, "a ':' and the minute must follow the hour", "a '-' after the hour");
    CheckReadFails("12:13:14.", 9, noDigit, "a trailing dot");
    CheckReadFails("12:13:14..", 9, noDigit, "two dots");
    CheckReadFails("2016-09-09T09:09:09.Z", 20, noDigit, "a dot before Z");
    CheckReadFails("07:32.5", 5, "a fraction of a second needs the seconds before it", "a fraction without seconds");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeTexts, ReadDateTime_RejectsTextThatIsNotADateOrTime)
{
    // Arrange
    TStringVector const texts = { "", "x", "07", "1979", "1e5", "-1979-05-27", "+07:00", " 07:32:00", "T07:32:00" };

    for (std::string const& text : texts)
    {
        // Act & Assert
        CheckReadFails(text, 0, "A date must start with a four-digit year and '-', a time with a two-digit hour and "
            "':'.", "\"" + text + "\"");
    }

    // Arrange
    TASWTomlValue value = "unchanged";
    std::size_t position = 20;

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadDateTime("1979-05-27", position, value);

    // Assert
    CheckEquals(ETomlCode::ParseFailed, result.Code, "a position past the end should fail");
    CheckEquals(std::size_t(20), position, "a position past the end should stay");
    CheckTrue(value == TASWTomlValue("unchanged"), "a position past the end should leave the value");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeTexts, ReadDateTime_RejectsTrailingCharacters)
{
    // Arrange & Act & Assert
    // A character a date or time can hold, right after one
    CheckReadFails("2020-01-01x", 10, "The date or time \"2020-01-01x\" isn't valid: unexpected 'x'.", "an x");
    CheckReadFails("1979-05-27_", 10, "unexpected '_'", "an underscore");
    CheckReadFails("1979-05-27T07:32:00Zx", 20, "unexpected 'x'", "an x after Z");
    CheckReadFails("1979-05-27T07:32:00Z5", 20, "unexpected '5'", "a digit after Z");
    CheckReadFails("07:32:00:00", 8, "unexpected ':'", "a fourth field");
    CheckReadFails("1979-05-27T07:32:00+01:00:00", 25, "unexpected ':'", "seconds in the offset");
    CheckReadFails("1979-05-27T07:32:00-07:00-", 25, "unexpected '-'", "a '-' after the offset");
    CheckReadFails("1979-05-27 07:32:00.5.5", 21, "The date or time \"1979-05-27 07:32:00.5.5\" isn't valid: "
        "unexpected '.'.", "a second fraction, quoted with the date");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeTexts, ReadDateTime_ReportsLineAndColumn)
{
    // Arrange
    // The day 29 of 2023-02-29 is on line 2, column 13
    std::string const text = "a = 1\nb = 2023-02-29\n";
    TASWTomlValue value;
    std::size_t position = 10;

    // Act
    ASWToml::TASWTomlResult result = ASWToml::Detail::ReadDateTime(text, position, value);

    // Assert
    CheckEquals(ETomlCode::ParseFailed, result.Code, "February 29 in 2023 should fail");
    CheckEquals(std::size_t(18), position, "the position should be the day's");
    CheckEquals(std::size_t(2), result.Line, "the day's line");
    CheckEquals(std::size_t(13), result.Column, "the day's column");

    // Arrange
    std::string const offset = "a = 1\r\nb = 07:32:00Z";
    position = 11;

    // Act
    result = ASWToml::Detail::ReadDateTime(offset, position, value);

    // Assert
    CheckEquals(ETomlCode::ParseFailed, result.Code, "an offset after a time should fail");
    CheckEquals(std::size_t(19), position, "the position should be the Z's");
    CheckEquals(std::size_t(2), result.Line, "the Z's line");
    CheckEquals(std::size_t(13), result.Column, "the Z's column");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeTexts, ReadDateTime_StopsAtTheEndOfItsToken)
{
    // Arrange
    std::chrono::year_month_day const date = Date(1979, 5, 27);

    // Act & Assert
    // What follows the date or time is the parser's to check
    CheckReadsUpTo("d = 1979-05-27 # comment", 4, 14, date, "a space and a comment after a date");
    CheckReadsUpTo("d = 1979-05-27 x", 4, 14, date, "a space and a letter after a date");
    CheckReadsUpTo("d = 1979-05-27 07", 4, 14, date, "a space and digits without ':'");
    CheckReadsUpTo("d = 1979-05-27\t07:32:00", 4, 14, date, "a tab isn't a separator");
    CheckReadsUpTo("d = 1979-05-27  07:32:00", 4, 14, date, "nor are two spaces");
    CheckReadsUpTo("[1979-05-27,07:32:00]", 1, 11, date, "a comma");
    CheckReadsUpTo("[1979-05-27,07:32:00]", 12, 20, Time(7, 32, 0), "a closing bracket");
    CheckReadsUpTo("{t=1979-05-27T07:32:00Z}", 3, 23, TASWTomlOffsetDateTime{ date, Time(7, 32, 0),
        std::chrono::minutes(0) }, "a closing brace");
    CheckReadsUpTo("t = 07:32:00\r\n", 4, 12, Time(7, 32, 0), "a line break");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeTexts, WriteDateTimes_AppendToText)
{
    // Arrange
    std::string text = "a = ";

    // Act
    ASWToml::TASWTomlResult result = ASWToml::Detail::WriteLocalDate(Date(1979, 5, 27), text);

    // Assert
    CheckTrue(result.IsOk(), "the date should be written: " + result.Message);
    CheckEquals("a = 1979-05-27"s, text, "the date should be appended to the text");

    // Arrange
    text = "b = ";

    // Act
    result = ASWToml::Detail::WriteLocalTime(Time(7, 32, 0), text);

    // Assert
    CheckTrue(result.IsOk(), "the time should be written: " + result.Message);
    CheckEquals("b = 07:32:00"s, text, "the time should be appended to the text");

    // Arrange
    text = "c = ";

    // Act
    result = ASWToml::Detail::WriteLocalDateTime(TASWTomlLocalDateTime(), text);

    // Assert
    CheckTrue(result.IsOk(), "the local date-time should be written: " + result.Message);
    CheckEquals("c = 1970-01-01T00:00:00"s, text, "the local date-time should be appended to the text");

    // Arrange
    text = "d = ";

    // Act
    result = ASWToml::Detail::WriteOffsetDateTime(TASWTomlOffsetDateTime(), text);

    // Assert
    CheckTrue(result.IsOk(), "the offset date-time should be written: " + result.Message);
    CheckEquals("d = 1970-01-01T00:00:00Z"s, text, "the offset date-time should be appended to the text");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeTexts, WriteDateTimes_RejectInvalidValues)
{
    // Arrange
    std::string const date = "The date can't be written: it must be a real date with a year from 0 to 9999.";
    std::string const time = "The time can't be written: it must have an hour from 0 to 23, a minute from 0 to 59, "
        "a second from 0 to 60 and a fraction of a second from 0 to 999999999 nanoseconds.";
    std::string const offset = "The offset can't be written: it must be from -23:59 to +23:59.";
    std::chrono::year_month_day const goodDate = Date(2023, 1, 1);

    // Act & Assert
    CheckWriteFails(Date(2023, 2, 29), date, "February 29 in a common year");
    CheckWriteFails(Date(2023, 13, 1), date, "month 13");
    CheckWriteFails(Date(10000, 1, 1), date, "year 10000");
    CheckWriteFails(Date(-1, 12, 31), date, "year -1");
    CheckWriteFails(std::chrono::year_month_day{}, date, "a value-initialized date");
    CheckWriteFails(Time(24, 0, 0), time, "hour 24");
    CheckWriteFails(Time(-1, 0, 0), time, "hour -1");
    CheckWriteFails(Time(0, 60, 0), time, "minute 60");
    CheckWriteFails(Time(0, 0, 61), time, "second 61");
    CheckWriteFails(Time(0, 0, 0, 1000000000), time, "a fraction of one second");
    CheckWriteFails(Time(0, 0, 0, -1), time, "a negative fraction");
    CheckWriteFails(TASWTomlLocalDateTime{ Date(2023, 2, 29), Time(0, 0, 0) }, date, "a local date-time's date");
    CheckWriteFails(TASWTomlLocalDateTime{ goodDate, Time(0, 0, 61) }, time, "a local date-time's time");
    CheckWriteFails(TASWTomlOffsetDateTime{ Date(2023, 2, 29), Time(0, 0, 0), std::chrono::minutes(0) }, date,
        "an offset date-time's date");
    CheckWriteFails(TASWTomlOffsetDateTime{ goodDate, Time(24, 0, 0), std::chrono::minutes(0) }, time,
        "an offset date-time's time");
    CheckWriteFails(TASWTomlOffsetDateTime{ goodDate, Time(0, 0, 0), std::chrono::minutes(24 * 60) }, offset,
        "an offset of 24 hours");
    CheckWriteFails(TASWTomlOffsetDateTime{ goodDate, Time(0, 0, 0), std::chrono::minutes(-24 * 60) }, offset,
        "an offset of -24 hours");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeTexts, WriteLocalDate_Dates)
{
    // Arrange & Act & Assert
    CheckWrites(Date(1979, 5, 27), "1979-05-27", "a date");
    CheckWrites(Date(0, 1, 1), "0000-01-01", "year 0");
    CheckWrites(Date(1, 1, 1), "0001-01-01", "the first year");
    CheckWrites(Date(999, 12, 31), "0999-12-31", "a year of three digits, with a zero before it");
    CheckWrites(Date(9999, 12, 31), "9999-12-31", "the last date");
    CheckWrites(Date(2024, 2, 29), "2024-02-29", "a leap day");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeTexts, WriteLocalDateTime_DateTimes)
{
    // Arrange & Act & Assert
    // Always with a T and the seconds
    CheckWrites(TASWTomlLocalDateTime{ Date(1979, 5, 27), Time(7, 32, 0) }, "1979-05-27T07:32:00", "a date-time");
    CheckWrites(TASWTomlLocalDateTime{ Date(1979, 5, 27), Time(0, 32, 0, 999999000) }, "1979-05-27T00:32:00.999999",
        "with a fraction");
    CheckWrites(TASWTomlLocalDateTime{ Date(2016, 12, 31), Time(23, 59, 60) }, "2016-12-31T23:59:60",
        "a leap second");
    CheckWrites(TASWTomlLocalDateTime(), "1970-01-01T00:00:00", "the default");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeTexts, WriteLocalTime_Times)
{
    // Arrange & Act & Assert
    // The seconds always; a fraction only when it isn't 0, with as few digits as it needs
    CheckWrites(Time(7, 32, 0), "07:32:00", "a time");
    CheckWrites(Time(0, 0, 0), "00:00:00", "midnight");
    CheckWrites(Time(23, 59, 59), "23:59:59", "the last second of a day");
    CheckWrites(Time(23, 59, 60), "23:59:60", "a leap second");
    CheckWrites(Time(15, 59, 60), "15:59:60", "a leap second at another hour");
    CheckWrites(Time(0, 0, 0, 500000000), "00:00:00.5", "half a second");
    CheckWrites(Time(0, 0, 0, 120000000), "00:00:00.12", "two digits");
    CheckWrites(Time(0, 0, 0, 123000000), "00:00:00.123", "milliseconds");
    CheckWrites(Time(0, 0, 0, 100000), "00:00:00.0001", "zeros before a digit");
    CheckWrites(Time(0, 0, 0, 1000), "00:00:00.000001", "a microsecond");
    CheckWrites(Time(0, 0, 0, 1), "00:00:00.000000001", "a nanosecond");
    CheckWrites(Time(0, 0, 0, 999999999), "00:00:00.999999999", "the largest fraction");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeTexts, WriteOffsetDateTime_Offsets)
{
    // Arrange
    auto const at = [](int minutes, std::int64_t nanoseconds = 0)
        {
            return TASWTomlOffsetDateTime{ Date(1979, 5, 27), Time(0, 32, 0, nanoseconds),
                std::chrono::minutes(minutes) };
        };

    // Act & Assert
    // Z for offset 0, else +HH:MM or -HH:MM
    CheckWrites(at(0), "1979-05-27T00:32:00Z", "UTC");
    CheckWrites(at(-7 * 60), "1979-05-27T00:32:00-07:00", "a negative offset");
    CheckWrites(at(5 * 60 + 30), "1979-05-27T00:32:00+05:30", "a positive offset with minutes");
    CheckWrites(at(-30), "1979-05-27T00:32:00-00:30", "a negative offset below an hour");
    CheckWrites(at(23 * 60 + 59), "1979-05-27T00:32:00+23:59", "the largest offset");
    CheckWrites(at(-(23 * 60 + 59)), "1979-05-27T00:32:00-23:59", "the smallest offset");
    CheckWrites(at(-7 * 60, 999999000), "1979-05-27T00:32:00.999999-07:00", "a fraction and an offset");
    CheckWrites(TASWTomlOffsetDateTime(), "1970-01-01T00:00:00Z", "the default");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_ScalarFuzz
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_ScalarFuzz::TTest_ASWToml_ScalarFuzz()
    : inherited("ASWToml_ScalarFuzz_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_ScalarFuzz::~TTest_ASWToml_ScalarFuzz()
{
}
//---------------------------------------------------------------------------
bool TTest_ASWToml_ScalarFuzz::CheckReadOrFail(std::string const& text, std::size_t start,
    ASWToml::TASWTomlValueOptions const& options, std::string const& name, std::source_location loc)
{
    TASWTomlValue value = "unchanged";
    std::size_t position = start;

    ASWToml::TASWTomlResult const read = ASWToml::Detail::ReadScalar(text, position, value);
    if (!read.IsOk())
    {
        bool const known = read.Code == ETomlCode::ParseFailed || read.Code == ETomlCode::InvalidUtf8 ||
            read.Code == ETomlCode::ValueOutOfRange;
        bool const inText = position == start || (position > start && position <= text.size());

        if (!known || read.Message.empty() || read.Line == 0 || read.Column == 0 || !inText ||
            !(value == TASWTomlValue("unchanged")))
        {
            CheckTrue(false, name + ": a text that doesn't read should fail with ParseFailed, InvalidUtf8 or "
                "ValueOutOfRange, a message, a line and a column, and leave the value as it was and the position in "
                "the text (" + ASWToml::ToString(read.Code) + " at " + std::to_string(position) + ": " + read.Message +
                ")", loc);
        }

        return false;
    }

    std::string written;
    std::string again;
    TASWTomlValue readBack;
    std::size_t end = 0;

    ASWToml::TASWTomlResult const write = WriteScalar(value, written, options);
    ASWToml::TASWTomlResult const reread = ASWToml::Detail::ReadScalar(written, end, readBack);
    ASWToml::TASWTomlResult const rewrite = WriteScalar(readBack, again, options);

    if (position <= start || position > text.size() || !write.IsOk() || !reread.IsOk() || !rewrite.IsOk() ||
        end != written.size() || !SameScalar(value, readBack) || again != written)
    {
        CheckTrue(false, name + ": the value read (up to offset " + std::to_string(position) + ") should be written as "
            "a text (\"" + Escaped(written) + "\") that reads back as the same value and is written the same again" +
            write.Message + reread.Message + rewrite.Message, loc);
    }

    return true;
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ScalarFuzz::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ScalarFuzz::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ScalarFuzz::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ScalarFuzz::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ScalarFuzz, MutatedTexts_ReadOrFailCleanly)
{
    // Arrange
    std::size_t accepted = 0;

    for (std::size_t i = 0; i < FuzzTextCount; ++i)
    {
        std::uint32_t const seed = FuzzFirstSeed + static_cast<std::uint32_t>(i);
        std::mt19937 generator(seed);
        ASWToml::TASWTomlValueOptions const options = RandomOptions(generator);
        std::string const text = MutatedText(generator);

        // Act & Assert
        if (CheckReadOrFail(text, 0, options, "seed " + std::to_string(seed) + ", \"" + Escaped(text) + "\""))
            ++accepted;
    }

    // Assert
    CheckTrue(accepted > 0, "some mutated texts should read");
    CheckTrue(accepted < FuzzTextCount, "some mutated texts should fail");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ScalarFuzz, RandomTexts_ReadOrFailCleanly)
{
    // Arrange
    std::size_t accepted = 0;

    for (std::size_t i = 0; i < FuzzTextCount; ++i)
    {
        std::uint32_t const seed = FuzzFirstSeed + static_cast<std::uint32_t>(i);
        std::mt19937 generator(seed);
        ASWToml::TASWTomlValueOptions const options = RandomOptions(generator);
        std::string const text = RandomText(generator);

        // From any offset, or one past the end
        std::size_t const start = generator() % (text.size() + 2);
        std::string const name = "seed " + std::to_string(seed) + ", \"" + Escaped(text) + "\" from " +
            std::to_string(start);

        // Act & Assert
        if (CheckReadOrFail(text, start, options, name))
            ++accepted;
    }

    // Assert
    CheckTrue(accepted > 0, "some random texts should read");
    CheckTrue(accepted < FuzzTextCount, "some random texts should fail");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ScalarFuzz, RandomValues_ReadBack)
{
    // Arrange
    // What may come before and after a value in a document
    TStringVector const prefixes = { "", "a = ", "[1, ", "{ x = 1, y = " };
    TStringVector const suffixes = { "", " # comment", ",", "]", " }", "\n", "\r\n", " " };
    std::size_t writtenCount = 0;

    for (std::size_t i = 0; i < FuzzValueCount; ++i)
    {
        std::uint32_t const seed = FuzzFirstSeed + static_cast<std::uint32_t>(i);
        std::mt19937 generator(seed);
        ASWToml::TASWTomlValueOptions const options = RandomOptions(generator);
        TASWTomlValue const value = RandomScalar(generator);
        std::string const& prefix = prefixes[generator() % prefixes.size()];
        std::string const& suffix = suffixes[generator() % suffixes.size()];
        std::string const name = "seed " + std::to_string(seed) + ", " + ASWToml::ToString(value.GetType());
        std::string text = prefix;

        // Act
        ASWToml::TASWTomlResult const write = WriteScalar(value, text, options);

        // Assert
        bool const canBeWritten = CanBeWritten(value);
        if (!write.IsOk())
        {
            if (canBeWritten || write.Code != ETomlCode::InvalidValue || write.Message.empty() || text != prefix)
            {
                CheckTrue(false, name + ": only a value that isn't valid should fail, with InvalidValue and a "
                    "message, appending nothing (" + write.Message + ")");
            }

            continue;
        }

        ++writtenCount;
        std::size_t const end = text.size();
        text += suffix;

        TASWTomlValue readBack;
        std::size_t position = prefix.size();
        ASWToml::TASWTomlResult const read = ASWToml::Detail::ReadScalar(text, position, readBack);

        if (!canBeWritten || !read.IsOk() || position != end || !SameScalar(value, readBack))
        {
            CheckTrue(false, name + ": the text \"" + Escaped(text) + "\" should read back as the same value, up to "
                "the suffix (" + read.Message + ")");
        }
    }

    // Assert
    CheckTrue(writtenCount > FuzzValueCount / 2, "most random values should be written");
    CheckTrue(writtenCount < FuzzValueCount, "some random values should be invalid");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_ScalarKinds
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_ScalarKinds::TTest_ASWToml_ScalarKinds()
    : inherited("ASWToml_ScalarKinds_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_ScalarKinds::~TTest_ASWToml_ScalarKinds()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ScalarKinds::CheckReadFails(std::string const& text, ETomlCode code, std::size_t offset,
    std::string const& expectedText, std::string const& msg, std::source_location loc)
{
    TASWTomlValue value = "unchanged";
    std::size_t position = 0;

    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadScalar(text, position, value);

    CheckEquals(code, result.Code, msg + ": the code", loc);
    CheckEquals(offset, position, msg + ": the position should be the error's offset", loc);
    CheckContains(result.Message, expectedText, msg + ": the message should name the problem", loc);
    CheckTrue(value == TASWTomlValue("unchanged"), msg + ": the value should be unchanged", loc);
    CheckTrue(result.Line >= 1 && result.Column >= 1, msg + ": the line and column should be set", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ScalarKinds::CheckReadsUpTo(std::string const& text, std::size_t start, std::size_t end,
    ETomlType type, std::string const& expected, std::string const& msg, std::source_location loc)
{
    TASWTomlValue value;
    std::size_t position = start;

    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadScalar(text, position, value);

    CheckTrue(result.IsOk(), msg + ": the value should read: " + result.Message, loc);
    CheckEquals(ASWToml::ToString(type), ASWToml::ToString(value.GetType()),
        msg + ": the type", loc);
    CheckEquals(expected, ScalarText(value), msg + ": the value", loc);
    CheckEquals(end, position, msg + ": the position should be after the value", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ScalarKinds::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ScalarKinds::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ScalarKinds::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ScalarKinds::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ScalarKinds, ReadScalar_ChoosesTheKind)
{
    // Arrange & Act & Assert
    CheckReadsUpTo("\"a\"", 0, 3, ETomlType::String, "a", "a basic string");
    CheckReadsUpTo("'C:\\x'", 0, 6, ETomlType::String, "C:\\x", "a literal string");
    CheckReadsUpTo("\"\"\"a\nb\"\"\"", 0, 9, ETomlType::String, "a\nb", "a multi-line string");
    CheckReadsUpTo("'''a'''", 0, 7, ETomlType::String, "a", "a multi-line literal string");
    CheckReadsUpTo("42", 0, 2, ETomlType::Integer, "42", "an integer");
    CheckReadsUpTo("0", 0, 1, ETomlType::Integer, "0", "zero");
    CheckReadsUpTo("-17", 0, 3, ETomlType::Integer, "-17", "a negative integer");
    CheckReadsUpTo("+99", 0, 3, ETomlType::Integer, "99", "a plus sign");
    CheckReadsUpTo("1_000", 0, 5, ETomlType::Integer, "1000", "an underscore");
    CheckReadsUpTo("0x1F", 0, 4, ETomlType::Integer, "31", "hexadecimal");
    CheckReadsUpTo("0o17", 0, 4, ETomlType::Integer, "15", "octal");
    CheckReadsUpTo("0b101", 0, 5, ETomlType::Integer, "5", "binary");
    CheckReadsUpTo("3.14", 0, 4, ETomlType::Float, "3.14", "a float");
    CheckReadsUpTo("-0.0", 0, 4, ETomlType::Float, "-0.0", "minus zero");
    CheckReadsUpTo("+1.5", 0, 4, ETomlType::Float, "1.5", "a float with a plus sign");
    CheckReadsUpTo("1e3", 0, 3, ETomlType::Float, "1000.0", "an exponent");
    CheckReadsUpTo("inf", 0, 3, ETomlType::Float, "inf", "inf");
    CheckReadsUpTo("+inf", 0, 4, ETomlType::Float, "inf", "+inf");
    CheckReadsUpTo("-inf", 0, 4, ETomlType::Float, "-inf", "-inf");
    CheckReadsUpTo("nan", 0, 3, ETomlType::Float, "nan", "nan");
    CheckReadsUpTo("+nan", 0, 4, ETomlType::Float, "nan", "+nan");
    CheckReadsUpTo("-nan", 0, 4, ETomlType::Float, "-nan", "-nan");
    CheckReadsUpTo("true", 0, 4, ETomlType::Boolean, "true", "true");
    CheckReadsUpTo("false", 0, 5, ETomlType::Boolean, "false", "false");
    CheckReadsUpTo("1979-05-27", 0, 10, ETomlType::LocalDate, "1979-05-27", "a local date");
    CheckReadsUpTo("07:32:00", 0, 8, ETomlType::LocalTime, "07:32:00", "a local time");
    CheckReadsUpTo("07:32", 0, 5, ETomlType::LocalTime, "07:32:00", "a local time without seconds");
    CheckReadsUpTo("1979-05-27T07:32:00", 0, 19, ETomlType::LocalDateTime, "1979-05-27T07:32:00",
        "a local date-time");
    CheckReadsUpTo("1979-05-27 07:32:00-07:00", 0, 25, ETomlType::OffsetDateTime, "1979-05-27T07:32:00-07:00",
        "an offset date-time");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ScalarKinds, ReadScalar_FailsAsTheKindsReader)
{
    // Arrange & Act & Assert
    // Each kind's reader reports the error, at its offset
    std::string const year = "the year must have four digits";

    CheckReadFails("\"abc", ETomlCode::ParseFailed, 0, "The string has no closing \".", "an unclosed string");
    CheckReadFails("'a\x01'", ETomlCode::ParseFailed, 2, "control character U+0001", "a control character");
    CheckReadFails("\"\xC3\"", ETomlCode::InvalidUtf8, 1, "aren't valid UTF-8", "invalid UTF-8 in a string");
    CheckReadFails("1__2", ETomlCode::ParseFailed, 1, "an underscore must be between two digits", "an integer");
    CheckReadFails("_1", ETomlCode::ParseFailed, 0, "an underscore must be between two digits", "an underscore first");
    CheckReadFails(".5", ETomlCode::ParseFailed, 0, "a digit, inf or nan must come first", "a float without 0");
    CheckReadFails("+infinity", ETomlCode::ParseFailed, 1, "a digit, inf or nan must come first", "a sign and a word");
    CheckReadFails("9223372036854775808", ETomlCode::ValueOutOfRange, 0, "is outside the 64-bit range",
        "an integer out of range");
    CheckReadFails("1e400", ETomlCode::ValueOutOfRange, 0, "is outside the range of a double", "a float out of range");
    CheckReadFails("2023-02-29", ETomlCode::ParseFailed, 8, "the day must be 01 to 28 in 2023-02", "a date");
    CheckReadFails("24:00:00", ETomlCode::ParseFailed, 0, "the hour must be 00 to 23", "a time");
    CheckReadFails("1:32:00", ETomlCode::ParseFailed, 0, "the hour must have two digits", "digits and ':' as a time");
    CheckReadFails("1-2", ETomlCode::ParseFailed, 0, year, "digits and '-' as a date");
    CheckReadFails("10000-01-01", ETomlCode::ParseFailed, 0, year, "a year of five digits");
    CheckReadFails("True", ETomlCode::ParseFailed, 0, "The value \"True\" isn't valid: a string must be in quotes, "
        "and a boolean is true or false (in lower case).", "a word");
    CheckReadFails("infinity", ETomlCode::ParseFailed, 0, "The value \"infinity\" isn't valid", "a word like inf");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ScalarKinds, ReadScalar_RejectsTextThatIsNotAScalar)
{
    // Arrange
    // Arrays and inline tables are the parser's to read
    TStringVector const texts = { "", " 1", "\t1", "#", "[1]", "{a=1}", "=", "@", ")", "\r\n" };

    for (std::string const& text : texts)
    {
        // Act & Assert
        CheckReadFails(text, ETomlCode::ParseFailed, 0, "A value must be a string, a number, true, false, a date or a "
            "time.", "\"" + text + "\"");
    }

    // Arrange
    TASWTomlValue value = "unchanged";
    std::size_t position = 9;

    // Act
    ASWToml::TASWTomlResult const result = ASWToml::Detail::ReadScalar("true", position, value);

    // Assert
    CheckEquals(ETomlCode::ParseFailed, result.Code, "a position past the end should fail");
    CheckEquals(std::size_t(9), position, "a position past the end should stay");
    CheckTrue(value == TASWTomlValue("unchanged"), "a position past the end should leave the value");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ScalarKinds, ReadScalar_StartsAndStopsAtItsToken)
{
    // Arrange
    std::string const line = "a = [\"x\", 1, 2.5, true, 1979-05-27, 07:32:00, 'y'] # c";

    // Act & Assert
    CheckReadsUpTo(line, 5, 8, ETomlType::String, "x", "a string");
    CheckReadsUpTo(line, 10, 11, ETomlType::Integer, "1", "an integer");
    CheckReadsUpTo(line, 13, 16, ETomlType::Float, "2.5", "a float");
    CheckReadsUpTo(line, 18, 22, ETomlType::Boolean, "true", "a boolean");
    CheckReadsUpTo(line, 24, 34, ETomlType::LocalDate, "1979-05-27", "a date");
    CheckReadsUpTo(line, 36, 44, ETomlType::LocalTime, "07:32:00", "a time");
    CheckReadsUpTo(line, 46, 49, ETomlType::String, "y", "a literal string");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_Booleans, "Scalars")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DateTimeTexts, "Scalars")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_Floats, "Scalars")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_Integers, "Scalars")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_Keys, "Scalars")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_ScalarFuzz, "Scalars")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_ScalarKinds, "Scalars")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_Strings, "Scalars")
