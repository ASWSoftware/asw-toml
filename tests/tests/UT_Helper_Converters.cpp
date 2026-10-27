/* **************************************************************************
UT_Helper_Converters.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "UT_Helper_Converters.h"
//---------------------------------------------------------------------------
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <random>
#include <string>
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

using ASWToml::TASWTomlValue;

using TArray = ASWToml::TASWTomlValue::TValueVector;
using TTable = ASWToml::TASWTomlValue::TKeyValueVector;

} // namespace

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
std::chrono::year_month_day Date(int year, unsigned month, unsigned day)
{
    return std::chrono::year_month_day{ std::chrono::year{ year }, std::chrono::month{ month },
        std::chrono::day{ day } };
}
//---------------------------------------------------------------------------
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
            escaped += std::string("\\x") + Digits[byte >> 4] + Digits[byte & 0xF];
    }

    return escaped;
}
//---------------------------------------------------------------------------
std::string FirstLine(std::string const& text)
{
    return text.substr(0, text.find_first_of("\r\n"));
}
//---------------------------------------------------------------------------
std::string LfText(std::string const& text)
{
    std::string lf;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != '\r' || i + 1 == text.size() || text[i + 1] != '\n')
            lf += text[i];
    }

    return lf;
}
//---------------------------------------------------------------------------
TArray OneOfEachType()
{
    ASWToml::TASWTomlOffsetDateTime offsetDateTime;
    offsetDateTime.Offset = std::chrono::minutes(-420);
    ASWToml::TASWTomlLocalDateTime const localDateTime;
    std::chrono::year_month_day const date{ std::chrono::year{ 1979 }, std::chrono::May, std::chrono::day{ 27 } };
    ASWToml::TASWTomlLocalTime time;
    time.Hour = std::chrono::hours(7);
    TArray const array = { 1 };
    TTable const table = { { "x", 1 } };

    return TArray{ "abc", 5, 2.5, true, offsetDateTime, localDateTime, date, time, array, table };
}
//---------------------------------------------------------------------------
std::chrono::year_month_day RandomDate(std::mt19937& generator)
{
    int const year = static_cast<int>(generator() % 10000);
    unsigned const month = 1 + generator() % 12;
    std::chrono::year_month_day_last const last{ std::chrono::year{ year },
        std::chrono::month_day_last{ std::chrono::month{ month } } };
    unsigned const day = 1 + generator() % static_cast<unsigned>(last.day());

    return Date(year, month, day);
}
//---------------------------------------------------------------------------
double RandomDouble(std::mt19937& generator)
{
    double const special[] = {
        0.0, -0.0, 0.5, 0.1, 1e300, -1e300, 3.4028234663852886e38, 3.4028235677973366e38, 1e-46, 7.038531e-26,
        16777217.0, 9007199254740993.0, std::numeric_limits<double>::max(), std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(),
        -std::numeric_limits<double>::quiet_NaN(),
    };

    std::uint32_t const kind = generator() % 4;
    if (kind == 0)
    {
        std::uint64_t const high = generator();
        std::uint64_t const low = generator();

        return std::bit_cast<double>(high << 32 | low);
    }

    if (kind == 1)
        return special[generator() % std::size(special)];

    std::int64_t const number = static_cast<std::int64_t>(generator() % 20001) - 10000;

    return static_cast<double>(number) / 64.0;
}
//---------------------------------------------------------------------------
std::int64_t RandomInteger(std::mt19937& generator)
{
    std::int64_t const special[] = {
        std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max(), -2147483649, -2147483648,
        2147483647, 2147483648, 4294967295, 4294967296, -32769, -32768, 32767, 32768, 65535, 65536, -129, -128, 127,
        128, 255, 256, 16777217, 9007199254740993, -1, 0, 1, 2, 7, 40,
    };

    std::uint32_t const kind = generator() % 4;
    if (kind == 0)
    {
        std::uint64_t const high = generator();
        std::uint64_t const low = generator();

        return static_cast<std::int64_t>(high << 32 | low);
    }

    if (kind == 1)
        return special[generator() % std::size(special)];

    return static_cast<std::int64_t>(generator() % 601) - 300;
}
//---------------------------------------------------------------------------
std::string RandomText(std::mt19937& generator, bool validUtf8)
{
    char const* const pieces[] = {
        "a", "b", "Red", "red", "Lime", "host", " ", "\t", "\n", "\x01", "\"", "'", "\\", "#", "C:\\Users",
        "\xC3\xA9", "\xF0\x9F\x98\x80", "\xFF",
    };

    std::size_t const available = validUtf8 ? std::size(pieces) - 1 : std::size(pieces);
    std::size_t const count = generator() % 7;
    std::string text;
    for (std::size_t i = 0; i < count; ++i)
    {
        // The byte that isn't UTF-8 seldom, so most texts can be written
        std::size_t piece = generator() % available;
        if (piece == std::size(pieces) - 1 && generator() % 4 != 0)
            piece = 0;

        text += pieces[piece];
    }

    return text;
}
//---------------------------------------------------------------------------
ASWToml::TASWTomlLocalTime RandomTime(std::mt19937& generator)
{
    int const hour = static_cast<int>(generator() % 24);
    int const minute = static_cast<int>(generator() % 60);
    int const second = static_cast<int>(generator() % 61);
    std::int64_t fraction = 0;
    if (generator() % 2 == 0)
        fraction = generator() % 1000000000;

    return TimeOfDay(hour, minute, second, fraction);
}
//---------------------------------------------------------------------------
TASWTomlValue RandomTomlValue(std::mt19937& generator, std::size_t depth)
{
    std::uint32_t const kind = generator() % (depth < 3 ? 11 : 8);
    if (kind == 0)
        return RandomText(generator, true);

    if (kind == 1)
        return RandomInteger(generator);

    if (kind == 2)
        return RandomDouble(generator);

    if (kind == 3)
        return generator() % 2 == 0;

    if (kind == 4)
    {
        ASWToml::TASWTomlOffsetDateTime dateTime;
        dateTime.Date = RandomDate(generator);
        dateTime.Time = RandomTime(generator);
        std::int64_t const offset = static_cast<std::int64_t>(generator() % 2879) - 1439;
        dateTime.Offset = std::chrono::minutes(offset);

        return dateTime;
    }

    if (kind == 5)
    {
        std::chrono::year_month_day const date = RandomDate(generator);

        return ASWToml::TASWTomlLocalDateTime{ date, RandomTime(generator) };
    }

    if (kind == 6)
        return RandomDate(generator);

    if (kind == 7)
        return RandomTime(generator);

    if (kind < 10)
    {
        TArray elements;
        std::size_t const count = generator() % 4;
        for (std::size_t i = 0; i < count; ++i)
            elements.push_back(RandomTomlValue(generator, depth + 1));

        return elements;
    }

    TASWTomlValue table;
    std::size_t const count = generator() % 4;
    for (std::size_t i = 0; i < count; ++i)
    {
        // The key first, so every compiler draws the numbers in the same order
        std::string const key = FuzzKeys[generator() % std::size(FuzzKeys)];
        table.Set(key, RandomTomlValue(generator, depth + 1));
    }

    return table;
}
//---------------------------------------------------------------------------
ASWToml::TASWTomlLocalTime TimeOfDay(int hour, int minute, int second, std::int64_t nanoseconds)
{
    ASWToml::TASWTomlLocalTime time;
    time.Hour = std::chrono::hours(hour);
    time.Minute = std::chrono::minutes(minute);
    time.Second = std::chrono::seconds(second);
    time.Fraction = std::chrono::nanoseconds(nanoseconds);

    return time;
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests
