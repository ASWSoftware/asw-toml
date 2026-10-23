/* **************************************************************************
Test_ASWToml_Utils.cpp
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
#include "Test_ASWToml_Utils.h"
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include <cstddef>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWToml_Types.h"
#include "ASWToml_Utils.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

using ASWToml::Detail::EBom;
using ASWToml::ELineBreak;

// The result of FindInvalidUtf8() for valid text
constexpr std::size_t Valid = std::string_view::npos;

std::string Bytes(char const* bytes, std::size_t size);

//---------------------------------------------------------------------------
/*
    Bytes

    A text with embedded NUL bytes or other bytes a C string can't hold, from a literal and its size
*/
std::string Bytes(char const* bytes, std::size_t size)
{
    return std::string(bytes, size);
}

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Utils
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_Utils::TTest_ASWToml_Utils()
    : inherited("ASWToml_Utils_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_Utils::~TTest_ASWToml_Utils()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Utils::CheckLineAndColumn(std::string_view text, std::size_t offset, std::size_t line,
    std::size_t column, std::string const& msg, std::source_location loc)
{
    ASWToml::TASWTomlResult result;
    ASWToml::Detail::SetLineAndColumn(result, text, offset);

    CheckEquals(line, result.Line, msg + ": the line", loc);
    CheckEquals(column, result.Column, msg + ": the column", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Utils::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Utils::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Utils::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Utils::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Utils, BomSize_MatchesEachMark)
{
    CheckEquals(0, static_cast<int>(ASWToml::Detail::BomSize(EBom::None)), "no mark");
    CheckEquals(3, static_cast<int>(ASWToml::Detail::BomSize(EBom::Utf8)), "UTF-8");
    CheckEquals(2, static_cast<int>(ASWToml::Detail::BomSize(EBom::Utf16LE)), "UTF-16 LE");
    CheckEquals(2, static_cast<int>(ASWToml::Detail::BomSize(EBom::Utf16BE)), "UTF-16 BE");
    CheckEquals(4, static_cast<int>(ASWToml::Detail::BomSize(EBom::Utf32LE)), "UTF-32 LE");
    CheckEquals(4, static_cast<int>(ASWToml::Detail::BomSize(EBom::Utf32BE)), "UTF-32 BE");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Utils, CountCodePoints_CountsEachCharacterOnce)
{
    CheckEquals(0, static_cast<int>(ASWToml::Detail::CountCodePoints("")), "an empty text has no characters");
    CheckEquals(9, static_cast<int>(ASWToml::Detail::CountCodePoints("key = \t42")),
        "each ASCII character, a tab included, should count as one");
    CheckEquals(3, static_cast<int>(ASWToml::Detail::CountCodePoints(Bytes("a\0b", 3))),
        "a NUL byte should count as one");
    CheckEquals(4, static_cast<int>(ASWToml::Detail::CountCodePoints("caf\xC3\xA9")),
        "a two-byte sequence (U+00E9) should count as one");
    CheckEquals(3, static_cast<int>(ASWToml::Detail::CountCodePoints("\xE2\x82\xAC" "12")),
        "a three-byte sequence (U+20AC) should count as one");
    CheckEquals(2, static_cast<int>(ASWToml::Detail::CountCodePoints("\xF0\x9F\x98\x80" "!")),
        "a four-byte sequence (U+1F600) should count as one");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Utils, CountCodePoints_InvalidText)
{
    // Every byte that isn't a continuation byte (10xxxxxx) counts as one
    CheckEquals(0, static_cast<int>(ASWToml::Detail::CountCodePoints("\x80\xBF")),
        "continuation bytes without a lead byte should not count");
    CheckEquals(2, static_cast<int>(ASWToml::Detail::CountCodePoints("\xFF\xFE")),
        "bytes that are never valid should count as one each");
    CheckEquals(2, static_cast<int>(ASWToml::Detail::CountCodePoints("a\xE2\x82")),
        "a truncated sequence should count as one");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Utils, DetectBom_FindsEachMark)
{
    CheckEquals(EBom::Utf8, ASWToml::Detail::DetectBom("\xEF\xBB\xBF" "a = 1"), "UTF-8");
    CheckEquals(EBom::Utf16LE, ASWToml::Detail::DetectBom(Bytes("\xFF\xFE" "a\x00", 4)), "UTF-16 LE");
    CheckEquals(EBom::Utf16BE, ASWToml::Detail::DetectBom(Bytes("\xFE\xFF\x00" "a", 4)), "UTF-16 BE");
    CheckEquals(EBom::Utf32LE, ASWToml::Detail::DetectBom(Bytes("\xFF\xFE\x00\x00" "a\x00\x00\x00", 8)),
        "UTF-32 LE should be found before the UTF-16 LE mark it starts with");
    CheckEquals(EBom::Utf32BE, ASWToml::Detail::DetectBom(Bytes("\x00\x00\xFE\xFF\x00\x00\x00" "a", 8)),
        "UTF-32 BE");
    CheckEquals(EBom::None, ASWToml::Detail::DetectBom("a = 1"), "no mark");
    CheckEquals(EBom::None, ASWToml::Detail::DetectBom(""), "an empty text");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Utils, DetectBom_IgnoresPartialMarks)
{
    CheckEquals(EBom::None, ASWToml::Detail::DetectBom("\xEF\xBB"), "two bytes of the UTF-8 mark");
    CheckEquals(EBom::None, ASWToml::Detail::DetectBom("\xFF"), "one byte of the UTF-16 LE mark");
    CheckEquals(EBom::None, ASWToml::Detail::DetectBom(Bytes("\x00\x00\xFE", 3)), "three bytes of the UTF-32 BE mark");
    CheckEquals(EBom::None, ASWToml::Detail::DetectBom("a\xEF\xBB\xBF"), "a mark that isn't at the start");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Utils, FindInvalidUtf8_FindsFirstInvalidSequence)
{
    CheckEquals(Valid, ASWToml::Detail::FindInvalidUtf8(""), "an empty text should be valid");
    CheckEquals(Valid, ASWToml::Detail::FindInvalidUtf8("caf\xC3\xA9 = \xE2\x82\xAC"), "a valid text");
    CheckEquals(std::size_t(0), ASWToml::Detail::FindInvalidUtf8("\x80" "abc"),
        "a continuation byte at the start should be found at 0");
    CheckEquals(std::size_t(4), ASWToml::Detail::FindInvalidUtf8("a = \xFF"),
        "an invalid byte should be found at its offset");
    CheckEquals(std::size_t(2), ASWToml::Detail::FindInvalidUtf8("\xC3\xA9\xE2\x82" "A"),
        "a sequence cut short by an ASCII byte should be found at its lead byte, after a valid sequence");
    CheckEquals(std::size_t(2), ASWToml::Detail::FindInvalidUtf8("ab\xE2\x82"),
        "a sequence cut off at the end should be found at its lead byte");
    CheckEquals(std::size_t(1), ASWToml::Detail::FindInvalidUtf8("a\xED\xA0\x80\xFF"),
        "the first of two invalid sequences should be found");
    CheckEquals(std::size_t(1), ASWToml::Detail::FindInvalidUtf8(Bytes("\0\xC0\xAF", 3)),
        "an overlong form after a NUL should be found");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Utils, IsBlank_SpaceAndTabOnly)
{
    CheckTrue(ASWToml::Detail::IsBlank(' '), "a space should be a blank");
    CheckTrue(ASWToml::Detail::IsBlank('\t'), "a tab should be a blank");
    CheckFalse(ASWToml::Detail::IsBlank('\n'), "LF should not be a blank");
    CheckFalse(ASWToml::Detail::IsBlank('\r'), "CR should not be a blank");
    CheckFalse(ASWToml::Detail::IsBlank('\v'), "a vertical tab should not be a blank");
    CheckFalse(ASWToml::Detail::IsBlank('\f'), "a form feed should not be a blank");
    CheckFalse(ASWToml::Detail::IsBlank('\0'), "NUL should not be a blank");
    CheckFalse(ASWToml::Detail::IsBlank('\xA0'), "the byte A0 should not be a blank");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Utils, IsValidUtf8_AcceptsValidText)
{
    CheckTrue(ASWToml::Detail::IsValidUtf8(""), "an empty text");
    CheckTrue(ASWToml::Detail::IsValidUtf8("plain_ascii = 1"), "ASCII");
    CheckTrue(ASWToml::Detail::IsValidUtf8(Bytes("a\0b", 3)), "a NUL byte is ASCII");
    CheckTrue(ASWToml::Detail::IsValidUtf8("\xC2\x80" "\xDF\xBF"), "two-byte sequences U+0080 and U+07FF");
    CheckTrue(ASWToml::Detail::IsValidUtf8("\xE0\xA0\x80" "\xED\x9F\xBF" "\xEE\x80\x80" "\xEF\xBF\xBF"),
        "three-byte sequences U+0800, U+D7FF, U+E000 and U+FFFF");
    CheckTrue(ASWToml::Detail::IsValidUtf8("\xF0\x90\x80\x80" "\xF4\x8F\xBF\xBF"),
        "four-byte sequences U+10000 and U+10FFFF");
    CheckTrue(ASWToml::Detail::IsValidUtf8("caf" "\xC3\xA9" " = \"\xE2\x82\xAC" "5\""), "mixed ASCII and sequences");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Utils, IsValidUtf8_RejectsInvalidSequences)
{
    CheckFalse(ASWToml::Detail::IsValidUtf8("\x80"), "a continuation byte without a lead byte");
    CheckFalse(ASWToml::Detail::IsValidUtf8("\xC0\xAF"), "an overlong two-byte form (C0)");
    CheckFalse(ASWToml::Detail::IsValidUtf8("\xC1\xBF"), "an overlong two-byte form (C1)");
    CheckFalse(ASWToml::Detail::IsValidUtf8("\xE0\x9F\xBF"), "an overlong three-byte form");
    CheckFalse(ASWToml::Detail::IsValidUtf8("\xF0\x8F\xBF\xBF"), "an overlong four-byte form");
    CheckFalse(ASWToml::Detail::IsValidUtf8("\xED\xA0\x80"), "a surrogate (U+D800)");
    CheckFalse(ASWToml::Detail::IsValidUtf8("\xED\xBF\xBF"), "a surrogate (U+DFFF)");
    CheckFalse(ASWToml::Detail::IsValidUtf8("\xF4\x90\x80\x80"), "above U+10FFFF");
    CheckFalse(ASWToml::Detail::IsValidUtf8("\xF5\x80\x80\x80"), "a lead byte above F4");
    CheckFalse(ASWToml::Detail::IsValidUtf8("\xFF"), "the byte FF");
    CheckFalse(ASWToml::Detail::IsValidUtf8("ab\xE2\x82"), "a sequence cut off at the end");
    CheckFalse(ASWToml::Detail::IsValidUtf8("\xE2\x82" "A"),
        "a sequence with an ASCII byte where a continuation byte should be");
    CheckFalse(ASWToml::Detail::IsValidUtf8("\xF0\x9F\x98" "A"),
        "a four-byte sequence with an ASCII byte where its last continuation byte should be");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Utils, LineBreakText_MatchesEachBreak)
{
    CheckEquals(std::string(""), std::string(ASWToml::Detail::LineBreakText(ELineBreak::None)), "None");
    CheckEquals(std::string("\n"), std::string(ASWToml::Detail::LineBreakText(ELineBreak::LF)), "LF");
    CheckEquals(std::string("\r\n"), std::string(ASWToml::Detail::LineBreakText(ELineBreak::CRLF)), "CRLF");
    CheckEquals(std::string(""), std::string(ASWToml::Detail::LineBreakText(static_cast<ELineBreak>(3))),
        "a value that isn't an ELineBreak should have no characters");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Utils, MessageText_QuotesAndCuts)
{
    // Arrange
    std::string const forty(40, 'x');

    // Act & Assert
    CheckEquals(std::string("\"0x_FF\""), ASWToml::Detail::MessageText("0x_FF"), "a short text in quotes");
    CheckEquals(std::string("\"\""), ASWToml::Detail::MessageText(""), "an empty text");
    CheckEquals(std::string("\"a?b?c?\""), ASWToml::Detail::MessageText(Bytes("a\tb\x7F" "c\0", 6)),
        "control characters (a tab, DEL and NUL) as '?'");
    CheckEquals("\"" + forty + "\"", ASWToml::Detail::MessageText(forty), "40 bytes in full");
    CheckEquals("\"" + forty + "\"...", ASWToml::Detail::MessageText(forty + "y"), "41 bytes cut to 40");
    CheckEquals("\"" + forty.substr(1) + "\"...", ASWToml::Detail::MessageText(forty.substr(1) + "\xC3\xA9"),
        "cut before a UTF-8 sequence that would be split, never inside it");
    CheckEquals("\"" + forty.substr(2) + "\xC3\xA9\"...",
        ASWToml::Detail::MessageText(forty.substr(2) + "\xC3\xA9" "z"), "a UTF-8 sequence that fits is kept");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Utils, NativeLineBreak_MatchesPlatform)
{
    // Act
    ELineBreak const lineBreak = ASWToml::Detail::NativeLineBreak();

    // Assert
#if defined(_WIN32)
    CheckEquals(ELineBreak::CRLF, lineBreak, "Windows should use CRLF");
#else
    CheckEquals(ELineBreak::LF, lineBreak, "other platforms should use LF");
#endif
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Utils, SetLineAndColumn_CountsLinesAndCharacters)
{
    // Arrange
    // Line 2 ends with a CRLF, and line 3 starts with two 2-byte characters (U+00E9)
    std::string const text = "a = 1\nb = 2\r\n\xC3\xA9\xC3\xA9 = 3";

    // Act & Assert
    CheckLineAndColumn(text, 0, 1, 1, "'a'");
    CheckLineAndColumn(text, 4, 1, 5, "'1'");
    CheckLineAndColumn(text, 5, 1, 6, "the first line's LF");
    CheckLineAndColumn(text, 6, 2, 1, "'b'");
    CheckLineAndColumn(text, 11, 2, 6, "the CR of the CRLF");
    CheckLineAndColumn(text, 12, 2, 7, "the LF of the CRLF");
    CheckLineAndColumn(text, 13, 3, 1, "the first U+00E9");
    CheckLineAndColumn(text, 15, 3, 2, "the second U+00E9");
    CheckLineAndColumn(text, 18, 3, 4, "'=', after two characters of four bytes and a space");
    CheckLineAndColumn(text, 21, 3, 7, "the end of the text");
    CheckLineAndColumn(text, 99, 3, 7, "an offset past the end, taken as the end");
    CheckLineAndColumn(std::string_view(), 0, 1, 1, "an empty text");

    // Arrange & Act & Assert
    // A text that is the start of a longer one: nothing after its end is read, not even the line break there
    std::string_view const start = std::string_view("ab\ncd").substr(0, 2);
    CheckLineAndColumn(start, 5, 1, 3, "an offset past the end of a view");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWToml_Utils)
