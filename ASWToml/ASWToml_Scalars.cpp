/* **************************************************************************
ASWToml_Scalars.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "ASWToml_Scalars.h"
//---------------------------------------------------------------------------
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
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

// The largest exponent DecimalExponent() counts; any larger one is far beyond a double's range
constexpr long long MaxExponent = 1000000;

// The reason a number with an underscore that isn't between two digits isn't valid
constexpr char const* UnderscoreReason = "an underscore must be between two digits";

//---------------------------------------------------------------------------

void AppendBasicString(std::string_view value, std::string& text);
void AppendCodePoint(std::uint32_t codePoint, std::string& text);
void AppendDate(std::chrono::year_month_day const& date, std::string& text);
void AppendDigits(std::int64_t value, std::size_t digitCount, std::string& text);
void AppendHex(std::uint32_t value, std::size_t digitCount, std::string& text);
void AppendTime(TASWTomlLocalTime const& time, std::string& text);
bool CanBeLiteral(std::string_view value) noexcept;
bool CanBeMultiLineLiteral(std::string_view value) noexcept;
char CharacterAfterDigits(std::string_view text, std::size_t offset) noexcept;
TASWTomlResult CheckDate(std::chrono::year_month_day const& date);
TASWTomlResult CheckTime(TASWTomlLocalTime const& time);
std::size_t CountDigits(std::string_view text, std::size_t offset) noexcept;
std::string_view DateTimeToken(std::string_view text, std::size_t start) noexcept;
long long DecimalExponent(std::string_view number) noexcept;
int DigitValue(char c, int base) noexcept;
TASWTomlResult Error(ETomlCode code, std::string message);
TASWTomlResult ErrorAt(ETomlCode code, std::string message, std::string_view text, std::size_t offset);
char EscapedCharacter(char letter) noexcept;
std::string EscapeForMessage(std::string_view text, std::size_t backslash, std::size_t digitCount);
char EscapeLetter(char c) noexcept;
bool IsBareKey(std::string_view key) noexcept;
bool IsBareKeyCharacter(char c) noexcept;
bool IsControlCharacter(unsigned char byte) noexcept;
bool IsDateTimeCharacter(char c) noexcept;
bool IsLetter(char c) noexcept;
bool IsNumberCharacter(char c) noexcept;
std::size_t LineBreakSize(std::string_view text, std::size_t offset) noexcept;
bool ReadDigits(std::string_view digits, int base, std::uint64_t limit, std::uint64_t& value) noexcept;
TASWTomlResult ReadEscape(std::string_view text, std::size_t& offset, std::string& value);
bool ReadField(std::string_view text, std::size_t offset, std::size_t digitCount, int& field) noexcept;
std::size_t ScanDigits(std::string_view token, std::size_t offset, int base, std::size_t& underscore) noexcept;

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
    AppendDate

    Appends the valid 'date' to 'text' as YYYY-MM-DD
*/
void AppendDate(std::chrono::year_month_day const& date, std::string& text)
{
    AppendDigits(static_cast<int>(date.year()), 4, text);
    text += '-';
    AppendDigits(static_cast<unsigned>(date.month()), 2, text);
    text += '-';
    AppendDigits(static_cast<unsigned>(date.day()), 2, text);
}
//---------------------------------------------------------------------------
/*
    AppendDigits

    Appends 'value' (not negative) to 'text' in decimal, with zeros before it to make 'digitCount' digits
*/
void AppendDigits(std::int64_t value, std::size_t digitCount, std::string& text)
{
    std::string const digits = std::to_string(value);
    if (digits.size() < digitCount)
        text.append(digitCount - digits.size(), '0');

    text += digits;
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
    AppendTime

    Appends the valid 'time' to 'text' as HH:MM:SS, followed by the fraction of a second when it isn't 0, with as few
    digits as it needs (.5, .000001)
*/
void AppendTime(TASWTomlLocalTime const& time, std::string& text)
{
    AppendDigits(time.Hour.count(), 2, text);
    text += ':';
    AppendDigits(time.Minute.count(), 2, text);
    text += ':';
    AppendDigits(time.Second.count(), 2, text);

    std::int64_t fraction = time.Fraction.count();
    if (fraction != 0)
    {
        std::size_t digitCount = 9;
        while (fraction % 10 == 0)
        {
            fraction /= 10;
            --digitCount;
        }

        text += '.';
        AppendDigits(fraction, digitCount, text);
    }
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
    CanBeMultiLineLiteral

    Whether a multi-line literal string can hold 'value': it has no run of three ' (which would close it) and no
    control character other than a tab and a line feed
*/
bool CanBeMultiLineLiteral(std::string_view value) noexcept
{
    if (value.find("'''") != std::string_view::npos)
        return false;

    for (char const c : value)
    {
        if (c != '\t' && c != '\n' && IsControlCharacter(static_cast<unsigned char>(c)))
            return false;
    }

    return true;
}
//---------------------------------------------------------------------------
/*
    CharacterAfterDigits

    The character after the decimal digits that start at 'offset' in 'text' ('-' after a date's year, ':' after a
    time's hour); '\0' when no digit is there or nothing follows them
*/
char CharacterAfterDigits(std::string_view text, std::size_t offset) noexcept
{
    std::size_t const digitCount = CountDigits(text, offset);
    if (digitCount == 0 || offset + digitCount >= text.size())
        return '\0';

    return text[offset + digitCount];
}
//---------------------------------------------------------------------------
/*
    CheckDate

    Ok if 'date' can be written, else InvalidValue: it must be a real date with a year from 0 to 9999
*/
TASWTomlResult CheckDate(std::chrono::year_month_day const& date)
{
    // The rule a local date-time has for its date
    if (!TASWTomlLocalDateTime{ date, TASWTomlLocalTime() }.IsValid())
    {
        return Error(ETomlCode::InvalidValue,
            "The date can't be written: it must be a real date with a year from 0 to 9999.");
    }

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
/*
    CheckTime

    Ok if 'time' can be written, else InvalidValue: each field must be in its range
*/
TASWTomlResult CheckTime(TASWTomlLocalTime const& time)
{
    if (!time.IsValid())
    {
        return Error(ETomlCode::InvalidValue, "The time can't be written: it must have an hour from 0 to 23, a minute "
            "from 0 to 59, a second from 0 to 60 and a fraction of a second from 0 to 999999999 nanoseconds.");
    }

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
/*
    CountDigits

    The number of decimal digits in a row from 'offset' in 'text' (0 when 'offset' is at or past its end)
*/
std::size_t CountDigits(std::string_view text, std::size_t offset) noexcept
{
    std::size_t i = offset;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9')
        ++i;

    return i - offset;
}
//---------------------------------------------------------------------------
/*
    DateTimeToken

    The date or time that starts at 'start' in 'text', as a message quotes it: the run of characters a date or time
    can hold, and when a space and a time follow it (digits and ':'), the space and the time's run too
*/
std::string_view DateTimeToken(std::string_view text, std::size_t start) noexcept
{
    std::size_t end = start;
    while (end < text.size() && IsDateTimeCharacter(text[end]))
        ++end;

    if (end < text.size() && text[end] == ' ' && CharacterAfterDigits(text, end + 1) == ':')
    {
        ++end;
        while (end < text.size() && IsDateTimeCharacter(text[end]))
            ++end;
    }

    return text.substr(start, end - start);
}
//---------------------------------------------------------------------------
/*
    DecimalExponent

    The decimal exponent of the first digit of 'number' that isn't 0, a float's text without underscores, e.g. 400
    for 1e400, -3 for 0.0025 and 2 for -123.4: what tells a float above a double's range from one below it. An
    exponent beyond MaxExponent counts as MaxExponent (or -MaxExponent). A number without such a digit gives its
    exponent.
*/
long long DecimalExponent(std::string_view number) noexcept
{
    std::size_t const e = number.find_first_of("eE");
    std::string_view const mantissa = number.substr(0, e);

    long long exponent = 0;
    if (e != std::string_view::npos)
    {
        std::size_t i = e + 1;
        bool const negative = i < number.size() && number[i] == '-';

        if (i < number.size() && (number[i] == '+' || number[i] == '-'))
            ++i;

        for (; i < number.size() && exponent < MaxExponent; ++i)
            exponent = exponent * 10 + (number[i] - '0');

        if (exponent > MaxExponent)
            exponent = MaxExponent;

        if (negative)
            exponent = -exponent;
    }

    std::size_t const first = mantissa.find_first_of("123456789");
    if (first == std::string_view::npos)
        return exponent;

    std::size_t point = mantissa.find('.');
    if (point == std::string_view::npos)
        point = mantissa.size();

    long long firstExponent = 0;
    if (first < point)
        firstExponent = static_cast<long long>(point - first - 1);
    else
        firstExponent = -static_cast<long long>(first - point);

    return firstExponent + exponent;
}
//---------------------------------------------------------------------------
/*
    DigitValue

    The value of 'c' as a digit of 'base' (2, 8, 10 or 16; hexadecimal digits in either case), or -1 when it isn't one
*/
int DigitValue(char c, int base) noexcept
{
    int value = -1;

    if (c >= '0' && c <= '9')
        value = c - '0';
    else if (c >= 'A' && c <= 'F')
        value = c - 'A' + 10;
    else if (c >= 'a' && c <= 'f')
        value = c - 'a' + 10;

    return value < base ? value : -1;
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
    IsBareKey

    Whether 'key' can be written as a bare key: not empty, and only A-Z, a-z, 0-9, '-' and '_'
*/
bool IsBareKey(std::string_view key) noexcept
{
    if (key.empty())
        return false;

    for (char const c : key)
    {
        if (!IsBareKeyCharacter(c))
            return false;
    }

    return true;
}
//---------------------------------------------------------------------------
/*
    IsBareKeyCharacter

    Whether 'c' can be part of a bare key: A-Z, a-z, 0-9, '-' or '_'
*/
bool IsBareKeyCharacter(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
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
    IsDateTimeCharacter

    Whether 'c' can be part of a date or time, or be mistaken for one of its parts: what a number can hold (see
    IsNumberCharacter()) and ':'
*/
bool IsDateTimeCharacter(char c) noexcept
{
    return IsNumberCharacter(c) || c == ':';
}
//---------------------------------------------------------------------------
/*
    IsLetter

    Whether 'c' is an ASCII letter: A-Z or a-z
*/
bool IsLetter(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
//---------------------------------------------------------------------------
/*
    IsNumberCharacter

    Whether 'c' can be part of the run of characters ReadNumber() reads: A-Z, a-z, 0-9, '_', '.', '+' or '-'
*/
bool IsNumberCharacter(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
        c == '+' || c == '-';
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
    ReadDigits

    Sets 'value' to the number the digits of 'base' in 'digits' make, skipping underscores, and returns true; returns
    false, leaving 'value' unspecified, when the number is larger than 'limit'
*/
bool ReadDigits(std::string_view digits, int base, std::uint64_t limit, std::uint64_t& value) noexcept
{
    std::uint64_t const radix = static_cast<std::uint64_t>(base);

    value = 0;
    for (char const c : digits)
    {
        if (c == '_')
            continue;

        std::uint64_t const digit = static_cast<std::uint64_t>(DigitValue(c, base));
        if (value > (limit - digit) / radix)
            return false;

        value = value * radix + digit;
    }

    return true;
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
        int const digit = i < text.size() ? DigitValue(text[i], 16) : -1;
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
/*
    ReadField

    Sets 'field' to the number the 'digitCount' decimal digits at 'offset' in 'text' make, and returns true; returns
    false, leaving 'field' as it was, when the digits in a row there aren't exactly that many
*/
bool ReadField(std::string_view text, std::size_t offset, std::size_t digitCount, int& field) noexcept
{
    if (CountDigits(text, offset) != digitCount)
        return false;

    int read = 0;
    for (char const c : text.substr(offset, digitCount))
        read = read * 10 + (c - '0');

    field = read;

    return true;
}
//---------------------------------------------------------------------------
/*
    ScanDigits

    The offset after the digits of 'base' that start at 'offset' in 'token', with each '_' between two of them:
    'offset' itself when no digit is there. Sets 'underscore' to the offset of a '_' that isn't between two digits
    (the scan stops at it), else to npos.
*/
std::size_t ScanDigits(std::string_view token, std::size_t offset, int base, std::size_t& underscore) noexcept
{
    underscore = std::string_view::npos;

    std::size_t i = offset;
    while (i < token.size())
    {
        if (token[i] == '_')
        {
            if (i == offset || i + 1 >= token.size() || DigitValue(token[i + 1], base) < 0)
            {
                underscore = i;
                break;
            }
        }
        else if (DigitValue(token[i], base) < 0)
        {
            break;
        }

        ++i;
    }

    return i;
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
TASWTomlResult ReadDateTime(std::string_view text, std::size_t& position, TASWTomlValue& value)
{
    std::size_t const start = position;
    char const afterDigits = CharacterAfterDigits(text, start);
    if (afterDigits != '-' && afterDigits != ':')
    {
        return ErrorAt(ETomlCode::ParseFailed,
            "A date must start with a four-digit year and '-', a time with a two-digit hour and ':'.", text, start);
    }

    std::string_view const token = DateTimeToken(text, start);

    // An error at 'offset', which 'position' is set to
    auto const fail = [&position, text, token](std::size_t offset, std::string const& reason)
        {
            position = offset;
            return ErrorAt(ETomlCode::ParseFailed, "The date or time " + MessageText(token) + " isn't valid: " + reason +
                ".", text, offset);
        };

    // Whether the character at 'offset' is one of 'characters'
    auto const isAt = [text](std::size_t offset, std::string_view characters)
        {
            return offset < text.size() && characters.find(text[offset]) != std::string_view::npos;
        };

    bool const hasDate = afterDigits == '-';
    bool hasTime = !hasDate;
    std::chrono::year_month_day date{};
    std::size_t i = start;

    if (hasDate)
    {
        int year = 0;
        int month = 0;
        int day = 0;

        if (!ReadField(text, i, 4, year))
            return fail(i, "the year must have four digits");

        i += 5;
        if (!ReadField(text, i, 2, month))
            return fail(i, "the month must have two digits");

        if (month < 1 || month > 12)
            return fail(i, "the month must be 01 to 12");

        i += 2;
        if (!isAt(i, "-"))
            return fail(i, "a '-' and the day must follow the month");

        ++i;
        if (!ReadField(text, i, 2, day))
            return fail(i, "the day must have two digits");

        date = std::chrono::year_month_day(std::chrono::year(year), std::chrono::month(static_cast<unsigned>(month)),
            std::chrono::day(static_cast<unsigned>(day)));
        if (!date.ok())
        {
            std::chrono::year_month_day_last const last(date.year(), std::chrono::month_day_last(date.month()));

            return fail(i, "the day must be 01 to " + std::to_string(static_cast<unsigned>(last.day())) + " in " +
                std::string(text.substr(start, 7)));
        }

        i += 2;

        // A T or t starts the time; a space does only when the time's digits and ':' follow it
        if (isAt(i, "Tt") || (isAt(i, " ") && CharacterAfterDigits(text, i + 1) == ':'))
        {
            hasTime = true;
            ++i;
        }
    }

    TASWTomlLocalTime time;
    bool hasOffset = false;
    std::chrono::minutes utcOffset{ 0 };

    if (hasTime)
    {
        int hour = 0;
        int minute = 0;
        int second = 0;

        if (!ReadField(text, i, 2, hour))
            return fail(i, "the hour must have two digits");

        if (hour > 23)
            return fail(i, "the hour must be 00 to 23");

        i += 2;
        if (!isAt(i, ":"))
            return fail(i, "a ':' and the minute must follow the hour");

        ++i;
        if (!ReadField(text, i, 2, minute))
            return fail(i, "the minute must have two digits");

        if (minute > 59)
            return fail(i, "the minute must be 00 to 59");

        i += 2;
        if (isAt(i, "."))
            return fail(i, "a fraction of a second needs the seconds before it");

        // The seconds are optional (TOML 1.1), and so is their fraction
        if (isAt(i, ":"))
        {
            ++i;
            if (!ReadField(text, i, 2, second))
                return fail(i, "the second must have two digits");

            if (second > 60)
                return fail(i, "the second must be 00 to 60 (60 is a leap second)");

            i += 2;
            if (isAt(i, "."))
            {
                ++i;
                std::size_t const digitCount = CountDigits(text, i);
                if (digitCount == 0)
                    return fail(i, "a digit must follow the decimal point");

                // Nanoseconds: the digits after the ninth are cut off, not rounded
                std::int64_t nanoseconds = 0;
                for (std::size_t digit = 0; digit < 9; ++digit)
                    nanoseconds = nanoseconds * 10 + (digit < digitCount ? text[i + digit] - '0' : 0);

                time.Fraction = std::chrono::nanoseconds(nanoseconds);
                i += digitCount;
            }
        }

        time.Hour = std::chrono::hours(hour);
        time.Minute = std::chrono::minutes(minute);
        time.Second = std::chrono::seconds(second);

        if (!hasDate && isAt(i, "Zz+-"))
            return fail(i, "a time without a date can't have an offset");

        if (hasDate && isAt(i, "Zz"))
        {
            hasOffset = true;
            ++i;
        }
        else if (hasDate && isAt(i, "+-"))
        {
            bool const negative = text[i] == '-';
            int offsetHour = 0;
            int offsetMinute = 0;

            ++i;
            if (!ReadField(text, i, 2, offsetHour))
                return fail(i, "the offset's hour must have two digits");

            if (offsetHour > 23)
                return fail(i, "the offset's hour must be 00 to 23");

            i += 2;
            if (!isAt(i, ":"))
                return fail(i, "a ':' and the minute must follow the offset's hour");

            ++i;
            if (!ReadField(text, i, 2, offsetMinute))
                return fail(i, "the offset's minute must have two digits");

            if (offsetMinute > 59)
                return fail(i, "the offset's minute must be 00 to 59");

            int const minutes = offsetHour * 60 + offsetMinute;

            i += 2;
            hasOffset = true;
            utcOffset = std::chrono::minutes(negative ? -minutes : minutes);
        }
    }

    if (i < text.size() && IsDateTimeCharacter(text[i]))
        return fail(i, "unexpected '" + std::string(1, text[i]) + "'");

    if (!hasTime)
        value = date;
    else if (!hasDate)
        value = time;
    else if (hasOffset)
        value = TASWTomlOffsetDateTime{ date, time, utcOffset };
    else
        value = TASWTomlLocalDateTime{ date, time };

    position = i;

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult ReadKey(std::string_view text, std::size_t& position, std::string& key)
{
    std::size_t const start = position;
    char const first = start < text.size() ? text[start] : '\0';

    if (first == '"' || first == '\'')
    {
        if (text.substr(start, 3) == (first == '"' ? "\"\"\"" : "'''"))
        {
            return ErrorAt(ETomlCode::ParseFailed, "A key can't be a multi-line string; write it in \" or '.", text,
                start);
        }

        return ReadString(text, position, key);
    }

    std::size_t end = start;
    while (end < text.size() && IsBareKeyCharacter(text[end]))
        ++end;

    if (end == start)
    {
        return ErrorAt(ETomlCode::ParseFailed,
            "A key must be a bare key (A-Z, a-z, 0-9, '-' and '_') or a quoted string.", text, start);
    }

    key = text.substr(start, end - start);
    position = end;

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult ReadNumber(std::string_view text, std::size_t& position, TASWTomlValue& value)
{
    std::size_t const start = position;
    std::size_t end = start;
    while (end < text.size() && IsNumberCharacter(text[end]))
        ++end;

    if (end == start)
        return ErrorAt(ETomlCode::ParseFailed, "A number must start with a digit, '+', '-', inf or nan.", text, start);

    std::string_view const token = text.substr(start, end - start);

    // An error at 'offset' in the token, which 'position' is set to
    auto const fail = [&position, start, text, token](std::size_t offset, std::string const& reason)
        {
            position = start + offset;
            return ErrorAt(ETomlCode::ParseFailed, "The number " + MessageText(token) + " isn't valid: " + reason + ".",
                text, start + offset);
        };

    bool const negative = token[0] == '-';
    std::size_t const signSize = negative || token[0] == '+' ? 1 : 0;
    std::string_view const body = token.substr(signSize);

    if (body == "inf" || body == "nan")
    {
        double special = std::numeric_limits<double>::infinity();

        if (body == "nan")
            special = std::numeric_limits<double>::quiet_NaN();

        value = std::copysign(special, negative ? -1.0 : 1.0);
        position = end;

        return TASWTomlResult();
    }

    // A hexadecimal, octal or binary integer
    int base = 10;
    if (body.size() >= 2 && body[0] == '0')
    {
        if (body[1] == 'x')
            base = 16;
        else if (body[1] == 'o')
            base = 8;
        else if (body[1] == 'b')
            base = 2;
    }

    std::size_t underscore = std::string_view::npos;
    if (base != 10)
    {
        if (signSize != 0)
            return fail(0, "a hexadecimal, octal or binary integer can't have a sign");

        std::string const aDigit = base == 16 ? "a hexadecimal digit" : base == 8 ? "an octal digit" : "a binary digit";
        std::size_t const digitsEnd = ScanDigits(token, 2, base, underscore);

        if (underscore != std::string_view::npos)
            return fail(underscore, UnderscoreReason);

        if (digitsEnd == 2)
            return fail(2, aDigit + " must follow " + std::string(token.substr(0, 2)));

        if (digitsEnd != token.size())
            return fail(digitsEnd, "'" + std::string(1, token[digitsEnd]) + "' isn't " + aDigit);

        std::uint64_t magnitude = 0;

        if (!ReadDigits(token.substr(2), base, std::numeric_limits<std::int64_t>::max(), magnitude))
        {
            return ErrorAt(ETomlCode::ValueOutOfRange, "The integer " + MessageText(token) +
                " is larger than the largest 64-bit integer, 0x7FFFFFFFFFFFFFFF.", text, start);
        }

        value = static_cast<std::int64_t>(magnitude);
        position = end;

        return TASWTomlResult();
    }

    // A decimal integer or a float: an integer part, then a fraction, an exponent or both for a float
    std::size_t const integerEnd = ScanDigits(token, signSize, 10, underscore);
    if (underscore != std::string_view::npos)
        return fail(underscore, UnderscoreReason);

    if (integerEnd == signSize)
        return fail(signSize, "a digit, inf or nan must come first");

    if (token[signSize] == '0' && integerEnd > signSize + 1)
        return fail(signSize + 1, "a decimal number can't start with 0 followed by more digits");

    bool isFloat = false;
    std::size_t i = integerEnd;

    if (i < token.size() && token[i] == '.')
    {
        std::size_t const fractionEnd = ScanDigits(token, i + 1, 10, underscore);

        if (underscore != std::string_view::npos)
            return fail(underscore, UnderscoreReason);

        if (fractionEnd == i + 1)
            return fail(i + 1, "a digit must follow the decimal point");

        i = fractionEnd;
        isFloat = true;
    }

    if (i < token.size() && (token[i] == 'e' || token[i] == 'E'))
    {
        std::size_t exponentStart = i + 1;

        if (exponentStart < token.size() && (token[exponentStart] == '+' || token[exponentStart] == '-'))
            ++exponentStart;

        std::size_t const exponentEnd = ScanDigits(token, exponentStart, 10, underscore);

        if (underscore != std::string_view::npos)
            return fail(underscore, UnderscoreReason);

        if (exponentEnd == exponentStart)
            return fail(exponentStart, "the exponent needs digits");

        i = exponentEnd;
        isFloat = true;
    }

    if (i != token.size())
        return fail(i, "unexpected '" + std::string(1, token[i]) + "'");

    if (!isFloat)
    {
        // The magnitude of the smallest 64-bit integer is one more than the largest's
        std::uint64_t const largest = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        std::uint64_t magnitude = 0;

        if (!ReadDigits(token.substr(signSize), 10, negative ? largest + 1 : largest, magnitude))
        {
            return ErrorAt(ETomlCode::ValueOutOfRange, "The integer " + MessageText(token) +
                " is outside the 64-bit range, -9223372036854775808 to 9223372036854775807.", text, start);
        }

        // In unsigned arithmetic, so the smallest integer's magnitude negates without overflow
        value = static_cast<std::int64_t>(negative ? 0 - magnitude : magnitude);
        position = end;

        return TASWTomlResult();
    }

    // std::from_chars reads neither '_' nor a leading '+'
    std::string number;
    for (char const c : token.substr(token[0] == '+' ? 1 : 0))
    {
        if (c != '_')
            number += c;
    }

    double read = 0.0;
    std::from_chars_result const result = std::from_chars(number.data(), number.data() + number.size(), read);
    if (result.ec == std::errc::result_out_of_range)
    {
        // Above a double's range, or below its smallest number (0.0 is the nearest double then); libstdc++ leaves
        // 'read' as it was either way
        if (DecimalExponent(number) > 0)
        {
            return ErrorAt(ETomlCode::ValueOutOfRange, "The float " + MessageText(token) +
                " is outside the range of a double, about plus or minus 1.8e308.", text, start);
        }

        read = negative ? -0.0 : 0.0;
    }
    else if (result.ec != std::errc() || result.ptr != number.data() + number.size())
    {
        return fail(0, "it can't be read as a double");
    }

    value = read;
    position = end;

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult ReadScalar(std::string_view text, std::size_t& position, TASWTomlValue& value)
{
    std::size_t const start = position;
    char const first = start < text.size() ? text[start] : '\0';

    if (first == '"' || first == '\'')
    {
        std::string read;
        TASWTomlResult result = ReadString(text, position, read);
        if (result.IsOk())
            value = std::move(read);

        return result;
    }

    char const afterDigits = CharacterAfterDigits(text, start);
    if (afterDigits == '-' || afterDigits == ':')
        return ReadDateTime(text, position, value);

    if (IsLetter(first))
    {
        // A word: true, false, or inf or nan for ReadNumber()
        std::size_t end = start;
        while (end < text.size() && IsNumberCharacter(text[end]))
            ++end;

        std::string_view const word = text.substr(start, end - start);
        if (word == "true" || word == "false")
        {
            value = word == "true";
            position = end;

            return TASWTomlResult();
        }

        if (word != "inf" && word != "nan")
        {
            return ErrorAt(ETomlCode::ParseFailed, "The value " + MessageText(word) + " isn't valid: a string must be "
                "in quotes, and a boolean is true or false (in lower case).", text, start);
        }
    }
    else if (!IsNumberCharacter(first))
    {
        return ErrorAt(ETomlCode::ParseFailed, "A value must be a string, a number, true, false, a date or a time.",
            text, start);
    }

    return ReadNumber(text, position, value);
}
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
void WriteBoolean(bool value, std::string& text)
{
    text += value ? "true" : "false";
}
//---------------------------------------------------------------------------
void WriteFloat(double value, std::string& text)
{
    if (std::isnan(value))
    {
        text += std::signbit(value) ? "-nan" : "nan";
        return;
    }

    if (std::isinf(value))
    {
        text += value < 0 ? "-inf" : "inf";
        return;
    }

    // The longest shortest form of a double, e.g. -2.2250738585072014e-308, has 24 characters
    char buffer[32];
    std::to_chars_result const result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    std::string_view const written(buffer, static_cast<std::size_t>(result.ptr - buffer));

    text += written;
    if (written.find_first_of(".e") == std::string_view::npos)
        text += ".0";
}
//---------------------------------------------------------------------------
void WriteInteger(std::int64_t value, std::string& text, EIntegerBase base)
{
    int radix = 10;
    if (value >= 0 && base == EIntegerBase::Hexadecimal)
    {
        radix = 16;
        text += "0x";
    }
    else if (value >= 0 && base == EIntegerBase::Octal)
    {
        radix = 8;
        text += "0o";
    }
    else if (value >= 0 && base == EIntegerBase::Binary)
    {
        radix = 2;
        text += "0b";
    }

    // 64 binary digits, or a sign and 19 decimal digits
    char buffer[72];
    std::to_chars_result const result = std::to_chars(buffer, buffer + sizeof(buffer), value, radix);

    for (char const* c = buffer; c != result.ptr; ++c)
        text += *c >= 'a' && *c <= 'f' ? static_cast<char>(*c - 'a' + 'A') : *c;
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
TASWTomlResult WriteLocalDate(std::chrono::year_month_day const& value, std::string& text)
{
    TASWTomlResult result = CheckDate(value);
    if (!result.IsOk())
        return result;

    AppendDate(value, text);

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult WriteLocalDateTime(TASWTomlLocalDateTime const& value, std::string& text)
{
    TASWTomlResult result = CheckDate(value.Date);
    if (result.IsOk())
        result = CheckTime(value.Time);

    if (!result.IsOk())
        return result;

    AppendDate(value.Date, text);
    text += 'T';
    AppendTime(value.Time, text);

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult WriteLocalTime(TASWTomlLocalTime const& value, std::string& text)
{
    TASWTomlResult result = CheckTime(value);
    if (!result.IsOk())
        return result;

    AppendTime(value, text);

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult WriteMultiLineString(std::string_view value, std::string& text, bool literal, ELineBreak lineBreak,
    bool breakFirst)
{
    if (!IsValidUtf8(value))
        return Error(ETomlCode::InvalidValue, "The string can't be written: it isn't valid UTF-8.");

    std::string_view const newLine = lineBreak == ELineBreak::None ? "\n" : LineBreakText(lineBreak);
    literal = literal && CanBeMultiLineLiteral(value);

    text += literal ? "'''" : "\"\"\"";
    if (breakFirst || (!value.empty() && value.front() == '\n'))
        text += newLine;

    // How many " in a row were written as they are, so a third one is escaped and can't close the string
    std::size_t quotes = 0;

    for (char const c : value)
    {
        if (c == '\n')
        {
            text += newLine;
        }
        else if (literal || c == '\t')
        {
            text += c;
        }
        else if (c == '"')
        {
            quotes = quotes == 2 ? 0 : quotes + 1;
            text += quotes == 0 ? "\\\"" : "\"";
            continue;
        }
        else if (c == '\\' || c == '\b' || c == '\f' || c == '\r')
        {
            text += '\\';
            text += EscapeLetter(c);
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

        quotes = 0;
    }

    text += literal ? "'''" : "\"\"\"";

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult WriteOffsetDateTime(TASWTomlOffsetDateTime const& value, std::string& text)
{
    TASWTomlResult result = CheckDate(value.Date);
    if (result.IsOk())
        result = CheckTime(value.Time);

    if (!result.IsOk())
        return result;

    // The rule of TASWTomlOffsetDateTime::IsValid() for the offset, the only part not checked yet
    if (!value.IsValid())
        return Error(ETomlCode::InvalidValue, "The offset can't be written: it must be from -23:59 to +23:59.");

    AppendDate(value.Date, text);
    text += 'T';
    AppendTime(value.Time, text);

    std::int64_t const minutes = value.Offset.count();
    if (minutes == 0)
    {
        text += 'Z';
    }
    else
    {
        std::int64_t const size = minutes < 0 ? -minutes : minutes;

        text += minutes < 0 ? '-' : '+';
        AppendDigits(size / 60, 2, text);
        text += ':';
        AppendDigits(size % 60, 2, text);
    }

    return result;
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
