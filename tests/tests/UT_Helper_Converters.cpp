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
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

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
