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
#include <cstddef>
#include <string>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWToml_Scalars.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

using namespace std::string_literals;

using ASWToml::EStringStyle;
using ASWToml::ETomlCode;

using TStringStyleVector = std::vector<EStringStyle>;
using TStringVector = std::vector<std::string>;

// Every EStringStyle
TStringStyleVector const AllStyles = { EStringStyle::Auto, EStringStyle::Basic, EStringStyle::Literal };

std::string HexByte(unsigned char byte);

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

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_Keys, "Scalars")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_Strings, "Scalars")
