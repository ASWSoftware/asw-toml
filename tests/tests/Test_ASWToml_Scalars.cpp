/* **************************************************************************
Test_ASWToml_Scalars.cpp
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
#include "Test_ASWToml_Scalars.h"
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWToml_Scalars.h"
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

using namespace std::string_literals;

using ASWToml::EIntegerBase;
using ASWToml::EStringStyle;
using ASWToml::ETomlCode;
using ASWToml::TASWTomlValue;

using TStringStyleVector = std::vector<EStringStyle>;
using TStringVector = std::vector<std::string>;

// Every EStringStyle
TStringStyleVector const AllStyles = { EStringStyle::Auto, EStringStyle::Basic, EStringStyle::Literal };

std::uint64_t Bits(double value) noexcept;
std::string HexByte(unsigned char byte);

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
    HexByte

    'byte' as four hexadecimal digits in upper case, as a message writes a control character: 001B
*/
std::string HexByte(unsigned char byte)
{
    constexpr char Digits[] = "0123456789ABCDEF";

    return "00"s + Digits[byte >> 4] + Digits[byte & 0xF];
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

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_Floats, "Scalars")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_Integers, "Scalars")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_Keys, "Scalars")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_Strings, "Scalars")
