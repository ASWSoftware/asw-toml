/* **************************************************************************
ASWToml_Utils.h
Author: Anthony S. West - ASW Software

A TOML file reader/writer library.

Text helpers of the library: byte order marks, UTF-8 validation, counting
characters, blanks and line breaks. Internal (namespace ASWToml::Detail): not
part of the public interface, and may change in any release.

Requires C++ 20 or higher.

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

#pragma once

#ifndef ASWToml_UtilsH
#define ASWToml_UtilsH
//---------------------------------------------------------------------------
#include <cstddef>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWToml_Types.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

//---------------------------------------------------------------------------
// Internals of the library. Not part of the public interface.
//---------------------------------------------------------------------------
namespace Detail
{

// A byte order mark at the start of a text
enum class EBom
{
    None,
    Utf8, // EF BB BF
    Utf16LE, // FF FE
    Utf16BE, // FE FF
    Utf32LE, // FF FE 00 00
    Utf32BE, // 00 00 FE FF
};

// The size in bytes of 'bom' (0 for EBom::None)
std::size_t BomSize(EBom bom) noexcept;

// The number of characters (Unicode code points) in the UTF-8 text 'text': the bytes that aren't continuation bytes
// (10xxxxxx), so a column can be counted in characters. In text that isn't valid UTF-8, every byte that isn't a
// continuation byte counts as one.
std::size_t CountCodePoints(std::string_view text) noexcept;

// The byte order mark 'text' starts with, if any. A UTF-32 LE mark (FF FE 00 00) is found before the UTF-16 LE mark
// it starts with.
EBom DetectBom(std::string_view text) noexcept;

// The offset of the first byte of the first sequence in 'text' that isn't valid UTF-8 (an overlong form, a surrogate
// (U+D800 to U+DFFF), a value above U+10FFFF, a truncated sequence or a continuation byte without a lead byte), or
// std::string_view::npos when the whole text is valid
std::size_t FindInvalidUtf8(std::string_view text) noexcept;

// Whether 'c' is a space or a tab, TOML's whitespace
inline bool IsBlank(char c) noexcept
{
    return c == ' ' || c == '\t';
}

// Whether 'text' is valid UTF-8 (see FindInvalidUtf8())
inline bool IsValidUtf8(std::string_view text) noexcept
{
    return FindInvalidUtf8(text) == std::string_view::npos;
}

// The characters of 'lineBreak' ("" for ELineBreak::None)
std::string_view LineBreakText(ELineBreak lineBreak) noexcept;

// The platform's line break: CRLF on Windows, LF elsewhere
ELineBreak NativeLineBreak() noexcept;

// Sets result.Line and result.Column to the place of the byte at 'offset' in 'text', both 1-based, the column in
// characters (see CountCodePoints()). Each "\n" ends a line, so the "\r" of a CRLF is the last character of its line.
// An offset past the end of 'text' is taken as its end.
void SetLineAndColumn(TASWTomlResult& result, std::string_view text, std::size_t offset) noexcept;

} // namespace Detail

} // namespace ASWToml

//---------------------------------------------------------------------------
#endif // #ifndef ASWToml_UtilsH
