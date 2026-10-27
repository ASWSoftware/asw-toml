/* **************************************************************************
ASWToml_Utils.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "ASWToml_Utils.h"
//---------------------------------------------------------------------------
#include <cstddef>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------

namespace ASWToml
{

namespace Detail
{

namespace
{

// The longest part of a text a message quotes (see MessageText())
constexpr std::size_t MaxMessageTextSize = 40;

} // namespace

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
std::size_t BomSize(EBom bom) noexcept
{
    switch (bom)
    {
        case EBom::None:
            return 0;

        case EBom::Utf8:
            return 3;

        case EBom::Utf16LE:
        case EBom::Utf16BE:
            return 2;

        case EBom::Utf32LE:
        case EBom::Utf32BE:
            return 4;
    }

    return 0;
}
//---------------------------------------------------------------------------
std::size_t CountCodePoints(std::string_view text) noexcept
{
    std::size_t count = 0;

    for (char const c : text)
    {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
            ++count;
    }

    return count;
}
//---------------------------------------------------------------------------
EBom DetectBom(std::string_view text) noexcept
{
    auto const startsWith = [text](char const* bytes, std::size_t size)
        {
            return text.size() >= size && text.compare(0, size, std::string_view(bytes, size)) == 0;
        };

    // UTF-32 LE before UTF-16 LE, whose mark it starts with
    if (startsWith("\xFF\xFE\x00\x00", 4))
        return EBom::Utf32LE;

    if (startsWith("\x00\x00\xFE\xFF", 4))
        return EBom::Utf32BE;

    if (startsWith("\xEF\xBB\xBF", 3))
        return EBom::Utf8;

    if (startsWith("\xFF\xFE", 2))
        return EBom::Utf16LE;

    if (startsWith("\xFE\xFF", 2))
        return EBom::Utf16BE;

    return EBom::None;
}
//---------------------------------------------------------------------------
std::size_t FindInvalidUtf8(std::string_view text) noexcept
{
    std::size_t i = 0;

    while (i < text.size())
    {
        unsigned char const lead = static_cast<unsigned char>(text[i]);

        if (lead < 0x80)
        {
            ++i;
            continue;
        }

        // The number of continuation bytes, and the range the second byte must be in to rule out overlong forms,
        // surrogates and values above U+10FFFF (RFC 3629's table)
        std::size_t continuationCount;
        unsigned char secondMin = 0x80;
        unsigned char secondMax = 0xBF;

        if (lead >= 0xC2 && lead <= 0xDF)
        {
            continuationCount = 1;
        }
        else if (lead >= 0xE0 && lead <= 0xEF)
        {
            continuationCount = 2;
            if (lead == 0xE0)
                secondMin = 0xA0; // Overlong below U+0800
            else if (lead == 0xED)
                secondMax = 0x9F; // Surrogates U+D800 to U+DFFF
        }
        else if (lead >= 0xF0 && lead <= 0xF4)
        {
            continuationCount = 3;
            if (lead == 0xF0)
                secondMin = 0x90; // Overlong below U+10000
            else if (lead == 0xF4)
                secondMax = 0x8F; // Above U+10FFFF
        }
        else
        {
            return i; // A continuation byte without a lead byte, an overlong C0/C1 lead, or F5 to FF
        }

        if (text.size() - i <= continuationCount)
            return i; // Truncated

        unsigned char const second = static_cast<unsigned char>(text[i + 1]);
        if (second < secondMin || second > secondMax)
            return i;

        for (std::size_t j = 2; j <= continuationCount; ++j)
        {
            unsigned char const continuation = static_cast<unsigned char>(text[i + j]);
            if (continuation < 0x80 || continuation > 0xBF)
                return i;
        }

        i += continuationCount + 1;
    }

    return std::string_view::npos;
}
//---------------------------------------------------------------------------
std::string_view LineBreakText(ELineBreak lineBreak) noexcept
{
    switch (lineBreak)
    {
        case ELineBreak::None:
            return std::string_view();

        case ELineBreak::LF:
            return std::string_view("\n", 1);

        case ELineBreak::CRLF:
            return std::string_view("\r\n", 2);
    }

    return std::string_view();
}
//---------------------------------------------------------------------------
std::string MessageText(std::string_view text)
{
    std::size_t size = text.size();
    if (size > MaxMessageTextSize)
    {
        size = MaxMessageTextSize;
        while (size > 0 && (static_cast<unsigned char>(text[size]) & 0xC0) == 0x80)
            --size;
    }

    std::string quoted = "\"";
    for (char const c : text.substr(0, size))
    {
        unsigned char const byte = static_cast<unsigned char>(c);
        quoted += byte < 0x20 || byte == 0x7F ? '?' : c;
    }

    quoted += '"';
    if (size < text.size())
        quoted += "...";

    return quoted;
}
//---------------------------------------------------------------------------
ELineBreak NativeLineBreak() noexcept
{
#if defined(_WIN32)
    return ELineBreak::CRLF;
#else
    return ELineBreak::LF;
#endif
}
//---------------------------------------------------------------------------
void SetLineAndColumn(TASWTomlResult& result, std::string_view text, std::size_t offset) noexcept
{
    if (offset > text.size())
        offset = text.size();

    std::size_t line = 1;
    std::size_t lineStart = 0;

    for (std::size_t i = 0; i < offset; ++i)
    {
        if (text[i] == '\n')
        {
            ++line;
            lineStart = i + 1;
        }
    }

    result.Line = line;
    result.Column = CountCodePoints(text.substr(lineStart, offset - lineStart)) + 1;
}
//---------------------------------------------------------------------------

} // namespace Detail

} // namespace ASWToml
