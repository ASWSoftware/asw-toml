/* **************************************************************************
ASWToml_Scalars.cpp
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
#include "ASWToml_Scalars.h"
//---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
//---------------------------------------------------------------------------
#include "ASWToml_Utils.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

namespace Detail
{

namespace
{

// TOML's escapes, for the messages about one that isn't
constexpr char const* EscapeList = "\\b, \\t, \\n, \\f, \\r, \\e, \\\", \\\\, \\xHH, \\uHHHH and \\UHHHHHHHH";

void AppendBasicString(std::string_view value, std::string& text);
void AppendCodePoint(std::uint32_t codePoint, std::string& text);
void AppendHex(std::uint32_t value, std::size_t digitCount, std::string& text);
bool CanBeLiteral(std::string_view value) noexcept;
TASWTomlResult Error(ETomlCode code, std::string message);
TASWTomlResult ErrorAt(ETomlCode code, std::string message, std::string_view text, std::size_t offset);
char EscapedCharacter(char letter) noexcept;
std::string EscapeForMessage(std::string_view text, std::size_t backslash, std::size_t digitCount);
char EscapeLetter(char c) noexcept;
int HexDigitValue(char c) noexcept;
bool IsBareKey(std::string_view key) noexcept;
bool IsControlCharacter(unsigned char byte) noexcept;
std::size_t LineBreakSize(std::string_view text, std::size_t offset) noexcept;
TASWTomlResult ReadEscape(std::string_view text, std::size_t& offset, std::string& value);

//---------------------------------------------------------------------------
/*
    AppendBasicString

    Appends 'value' to 'text' as a basic string, with the escapes TOML 1.0 reads: \", \\, \b, \t, \n, \f and \r, and
    \u00XX for the other control characters
*/
void AppendBasicString(std::string_view value, std::string& text)
{
    text += '"';

    for (char const c : value)
    {
        char const letter = EscapeLetter(c);

        if (letter != '\0')
        {
            text += '\\';
            text += letter;
        }
        else if (IsControlCharacter(static_cast<unsigned char>(c)))
        {
            text += "\\u";
            AppendHex(static_cast<unsigned char>(c), 4, text);
        }
        else
        {
            text += c;
        }
    }

    text += '"';
}
//---------------------------------------------------------------------------
/*
    AppendCodePoint

    Appends the UTF-8 bytes of 'codePoint' (a Unicode scalar value) to 'text'
*/
void AppendCodePoint(std::uint32_t codePoint, std::string& text)
{
    if (codePoint < 0x80)
    {
        text += static_cast<char>(codePoint);
    }
    else if (codePoint < 0x800)
    {
        text += static_cast<char>(0xC0 | (codePoint >> 6));
        text += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
    else if (codePoint < 0x10000)
    {
        text += static_cast<char>(0xE0 | (codePoint >> 12));
        text += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        text += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
    else
    {
        text += static_cast<char>(0xF0 | (codePoint >> 18));
        text += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
        text += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        text += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
}
//---------------------------------------------------------------------------
/*
    AppendHex

    Appends 'value' to 'text' as 'digitCount' hexadecimal digits in upper case, e.g. 001B
*/
void AppendHex(std::uint32_t value, std::size_t digitCount, std::string& text)
{
    constexpr char Digits[] = "0123456789ABCDEF";

    for (std::size_t i = digitCount; i > 0; --i)
        text += Digits[(value >> ((i - 1) * 4)) & 0xF];
}
//---------------------------------------------------------------------------
/*
    CanBeLiteral

    Whether a literal string can hold 'value': it has no ' and no control character other than a tab
*/
bool CanBeLiteral(std::string_view value) noexcept
{
    for (char const c : value)
    {
        if (c == '\'' || (c != '\t' && IsControlCharacter(static_cast<unsigned char>(c))))
            return false;
    }

    return true;
}
//---------------------------------------------------------------------------
/*
    Error

    A result with 'code' and 'message'
*/
TASWTomlResult Error(ETomlCode code, std::string message)
{
    TASWTomlResult result;
    result.Code = code;
    result.Message = std::move(message);

    return result;
}
//---------------------------------------------------------------------------
/*
    ErrorAt

    A result with 'code' and 'message', and the Line and Column of 'offset' in 'text'
*/
TASWTomlResult ErrorAt(ETomlCode code, std::string message, std::string_view text, std::size_t offset)
{
    TASWTomlResult result = Error(code, std::move(message));
    SetLineAndColumn(result, text, offset);

    return result;
}
//---------------------------------------------------------------------------
/*
    EscapedCharacter

    The character that the escape of one letter, a backslash and 'letter', stands for: b, t, n, f, r, e (U+001B), "
    or a backslash; '\0' for a letter that isn't one of them
*/
char EscapedCharacter(char letter) noexcept
{
    switch (letter)
    {
        case 'b':
            return '\b';

        case 't':
            return '\t';

        case 'n':
            return '\n';

        case 'f':
            return '\f';

        case 'r':
            return '\r';

        case 'e':
            return '\x1B';

        case '"':
        case '\\':
            return letter;
    }

    return '\0';
}
//---------------------------------------------------------------------------
/*
    EscapeForMessage

    The escape that starts with the backslash at 'backslash' in 'text', for a message: the backslash, its letter, and
    up to 'digitCount' ASCII letters and digits after it, e.g. \uD801 or \x0g
*/
std::string EscapeForMessage(std::string_view text, std::size_t backslash, std::size_t digitCount)
{
    std::string escape(text.substr(backslash, 2));

    for (std::size_t i = backslash + 2; i < text.size() && i < backslash + 2 + digitCount; ++i)
    {
        char const c = text[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')))
            break;

        escape += c;
    }

    return escape;
}
//---------------------------------------------------------------------------
/*
    EscapeLetter

    The letter of the escape a basic string writes 'c' with: b, t, n, f, r, " or a backslash; '\0' for a character
    that is written as it is or as \u00XX (\e, TOML 1.1's escape for U+001B, isn't written)
*/
char EscapeLetter(char c) noexcept
{
    switch (c)
    {
        case '\b':
            return 'b';

        case '\t':
            return 't';

        case '\n':
            return 'n';

        case '\f':
            return 'f';

        case '\r':
            return 'r';

        case '"':
        case '\\':
            return c;
    }

    return '\0';
}
//---------------------------------------------------------------------------
/*
    HexDigitValue

    The value of the hexadecimal digit 'c' (in either case), or -1 when it isn't one
*/
int HexDigitValue(char c) noexcept
{
    if (c >= '0' && c <= '9')
        return c - '0';

    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;

    return -1;
}
//---------------------------------------------------------------------------
/*
    IsBareKey

    Whether 'key' can be written as a bare key: not empty, and only A-Z, a-z, 0-9, '-' and '_'
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
    IsControlCharacter

    Whether 'byte' is a control character: U+0000 to U+001F (the tab included) or U+007F
*/
bool IsControlCharacter(unsigned char byte) noexcept
{
    return byte < 0x20 || byte == 0x7F;
}
//---------------------------------------------------------------------------
/*
    LineBreakSize

    The size of the line break at 'offset' in 'text': 1 for "\n", 2 for "\r\n", 0 for anything else (a carriage
    return without a line feed included)
*/
std::size_t LineBreakSize(std::string_view text, std::size_t offset) noexcept
{
    if (offset < text.size() && text[offset] == '\n')
        return 1;

    if (offset + 1 < text.size() && text[offset] == '\r' && text[offset + 1] == '\n')
        return 2;

    return 0;
}
//---------------------------------------------------------------------------
/*
    ReadEscape

    Reads the escape whose backslash is at 'offset' in 'text' (not a line-ending backslash), appends the character it
    stands for to 'value' as UTF-8, and moves 'offset' past it. Fails with ParseFailed, leaving 'offset' at the
    backslash, for an escape TOML doesn't have or one that isn't a Unicode scalar value.
*/
TASWTomlResult ReadEscape(std::string_view text, std::size_t& offset, std::string& value)
{
    char const letter = offset + 1 < text.size() ? text[offset + 1] : '\0';

    char const character = EscapedCharacter(letter);
    if (character != '\0')
    {
        value += character;
        offset += 2;

        return TASWTomlResult();
    }

    std::size_t digitCount = 0;
    if (letter == 'x')
    {
        digitCount = 2;
    }
    else if (letter == 'u')
    {
        digitCount = 4;
    }
    else if (letter == 'U')
    {
        digitCount = 8;
    }
    else if (letter > ' ' && letter <= '~')
    {
        return ErrorAt(ETomlCode::ParseFailed, "The escape \\" + std::string(1, letter) + " isn't one of TOML's: " +
            EscapeList + ".", text, offset);
    }
    else
    {
        return ErrorAt(ETomlCode::ParseFailed,
            std::string("A backslash in a string must start one of TOML's escapes: ") + EscapeList + ".", text, offset);
    }

    std::uint32_t codePoint = 0;
    for (std::size_t i = offset + 2; i < offset + 2 + digitCount; ++i)
    {
        int const digit = i < text.size() ? HexDigitValue(text[i]) : -1;
        if (digit < 0)
        {
            return ErrorAt(ETomlCode::ParseFailed, "The escape " + EscapeForMessage(text, offset, digitCount) +
                " needs " + std::to_string(digitCount) + " hexadecimal digits.", text, offset);
        }

        codePoint = codePoint * 16 + static_cast<std::uint32_t>(digit);
    }

    if (codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
    {
        return ErrorAt(ETomlCode::ParseFailed, "The escape " + EscapeForMessage(text, offset, digitCount) +
            " isn't a Unicode scalar value (U+D800 to U+DFFF and values above U+10FFFF aren't).", text, offset);
    }

    AppendCodePoint(codePoint, value);
    offset += 2 + digitCount;

    return TASWTomlResult();
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
TASWTomlResult ReadString(std::string_view text, std::size_t& position, std::string& value)
{
    std::size_t const start = position;
    if (start >= text.size() || (text[start] != '"' && text[start] != '\''))
        return ErrorAt(ETomlCode::ParseFailed, "A string must start with \" or '.", text, start);

    char const quote = text[start];
    bool const literal = quote == '\'';
    bool const multiLine = text.substr(start, 3) == (literal ? "'''" : "\"\"\"");
    std::string const delimiter(multiLine ? 3 : 1, quote);
    std::string const kind = std::string(multiLine ? "multi-line " : "") + (literal ? "literal string" : "string");

    // An error at 'offset', which 'position' is set to
    auto const fail = [&position, text](ETomlCode code, std::string message, std::size_t offset)
        {
            position = offset;
            return ErrorAt(code, std::move(message), text, offset);
        };

    std::size_t i = start + delimiter.size();
    if (multiLine)
        i += LineBreakSize(text, i);

    std::string read;
    while (true)
    {
        if (i >= text.size())
            return fail(ETomlCode::ParseFailed, "The " + kind + " has no closing " + delimiter + ".", start);

        unsigned char const byte = static_cast<unsigned char>(text[i]);

        if (byte == static_cast<unsigned char>(quote))
        {
            // In a multi-line string, one or two quotes are text, and so are up to two before the closing delimiter
            std::size_t count = 1;
            if (multiLine)
            {
                while (count < 5 && i + count < text.size() && text[i + count] == quote)
                    ++count;

                if (count < 3)
                {
                    read.append(count, quote);
                    i += count;
                    continue;
                }

                read.append(count - 3, quote);
            }

            position = i + count;
            value = std::move(read);

            return TASWTomlResult();
        }

        if (byte == '\\' && !literal)
        {
            if (multiLine)
            {
                // A line-ending backslash: it is left out with the blanks and line breaks after it
                std::size_t next = i + 1;
                while (next < text.size() && IsBlank(text[next]))
                    ++next;

                if (LineBreakSize(text, next) != 0)
                {
                    while (next < text.size() && (IsBlank(text[next]) || LineBreakSize(text, next) != 0))
                        next += IsBlank(text[next]) ? 1 : LineBreakSize(text, next);

                    i = next;
                    continue;
                }

                if (next > i + 1)
                {
                    return fail(ETomlCode::ParseFailed,
                        "A backslash followed by spaces or tabs must be the last character on its line.", i);
                }
            }

            TASWTomlResult result = ReadEscape(text, i, read);
            if (!result.IsOk())
            {
                position = i;
                return result;
            }

            continue;
        }

        std::size_t const lineBreak = LineBreakSize(text, i);
        if (lineBreak != 0)
        {
            if (!multiLine)
            {
                return fail(ETomlCode::ParseFailed, "The " + kind + " has no closing " + delimiter + " on its line.",
                    start);
            }

            read += '\n';
            i += lineBreak;
            continue;
        }

        if (byte == '\r' && multiLine)
        {
            return fail(ETomlCode::ParseFailed, "A carriage return in a " + kind + " must be followed by a line feed.",
                i);
        }

        if (byte != '\t' && IsControlCharacter(byte))
        {
            std::string message = "A " + kind + " can't hold the control character U+";
            AppendHex(byte, 4, message);
            message += literal ? "; a basic string can write it as \\u" : "; write it as \\u";
            AppendHex(byte, 4, message);
            message += '.';

            return fail(ETomlCode::ParseFailed, std::move(message), i);
        }

        if (byte >= 0x80)
        {
            // A run of non-ASCII bytes, which valid UTF-8 never splits a character across
            std::size_t end = i;
            while (end < text.size() && static_cast<unsigned char>(text[end]) >= 0x80)
                ++end;

            std::string_view const run = text.substr(i, end - i);
            std::size_t const invalid = FindInvalidUtf8(run);
            if (invalid != std::string_view::npos)
            {
                return fail(ETomlCode::InvalidUtf8, "The " + kind + " holds bytes that aren't valid UTF-8.",
                    i + invalid);
            }

            read += run;
            i = end;
            continue;
        }

        read += static_cast<char>(byte);
        ++i;
    }
}
//---------------------------------------------------------------------------
TASWTomlResult WriteKey(std::string_view key, std::string& text)
{
    if (!IsValidUtf8(key))
        return Error(ETomlCode::InvalidValue, "The key can't be written: it isn't valid UTF-8.");

    if (IsBareKey(key))
        text += key;
    else
        AppendBasicString(key, text);

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult WriteString(std::string_view value, std::string& text, EStringStyle style)
{
    if (!IsValidUtf8(value))
        return Error(ETomlCode::InvalidValue, "The string can't be written: it isn't valid UTF-8.");

    bool literal = false;
    if (style == EStringStyle::Auto)
        literal = value.find('\\') != std::string_view::npos && CanBeLiteral(value);
    else if (style == EStringStyle::Literal)
        literal = CanBeLiteral(value);

    if (literal)
    {
        text += '\'';
        text += value;
        text += '\'';
    }
    else
    {
        AppendBasicString(value, text);
    }

    return TASWTomlResult();
}
//---------------------------------------------------------------------------

} // namespace Detail

} // namespace ASWToml
