/* **************************************************************************
ASWToml_Value.h
Author: Anthony S. West - ASW Software

A TOML file reader/writer library.

TASWTomlValue, a decoded TOML value detached from any document: a string,
integer, float, boolean, one of the four date and time types, an array or a
table. ETomlType names the type; the date and time types
(TASWTomlLocalTime, TASWTomlLocalDateTime, TASWTomlOffsetDateTime, and
std::chrono::year_month_day for a local date) keep each part as written, so
a leap second and an offset are exact, and convert to std::chrono time
points.

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

#ifndef ASWToml_ValueH
#define ASWToml_ValueH
//---------------------------------------------------------------------------
#include <chrono>
#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>
//---------------------------------------------------------------------------

namespace ASWToml
{

// The type of a TOML value. The values never change (an application may store or compare them).
enum class ETomlType : int
{
    String = 0, // std::string (UTF-8)
    Integer = 1, // std::int64_t
    Float = 2, // double
    Boolean = 3, // bool
    OffsetDateTime = 4, // TASWTomlOffsetDateTime, e.g. 1979-05-27T07:32:00-08:00
    LocalDateTime = 5, // TASWTomlLocalDateTime, e.g. 1979-05-27T07:32:00
    LocalDate = 6, // std::chrono::year_month_day, e.g. 1979-05-27
    LocalTime = 7, // TASWTomlLocalTime, e.g. 07:32:00
    Array = 8, // TASWTomlValue::TValueVector
    Table = 9, // TASWTomlValue::TKeyValueVector
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlLocalTime
//
// A time of day, as TOML's local time and the time part of its date-times: each field as written, so a leap second
// (second 60) is kept at any hour. The default is midnight.
/////////////////////////////////////////////////////////////////////////////
struct TASWTomlLocalTime
{
    std::chrono::hours Hour{ 0 }; // 0 to 23
    std::chrono::minutes Minute{ 0 }; // 0 to 59
    std::chrono::seconds Second{ 0 }; // 0 to 60 (60 is a leap second)
    std::chrono::nanoseconds Fraction{ 0 }; // 0 to 999999999 nanoseconds

    // The time of day of 'sinceMidnight', taken modulo 24 hours (so -1 hour is 23:00); its second is never 60
    static TASWTomlLocalTime FromDuration(std::chrono::nanoseconds sinceMidnight) noexcept;

    // Whether each field is in its range
    bool IsValid() const noexcept;

    // The time since midnight: the fields added up, so second 60 is the next minute's second 0; std::nullopt if the
    // time isn't valid
    std::optional<std::chrono::nanoseconds> ToDuration() const noexcept;

    bool operator==(TASWTomlLocalTime const& other) const = default;
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlLocalDateTime
//
// TOML's local date-time: a date and a time of day without an offset, so not an instant. The default is
// 1970-01-01T00:00:00.
/////////////////////////////////////////////////////////////////////////////
struct TASWTomlLocalDateTime
{
    std::chrono::year_month_day Date{ std::chrono::year{ 1970 }, std::chrono::January, std::chrono::day{ 1 } };
    TASWTomlLocalTime Time;

    // The date and time of 'localTime' (its second is never 60)
    static TASWTomlLocalDateTime FromLocalTime(std::chrono::local_time<std::chrono::nanoseconds> localTime) noexcept;

    // Whether the date is a real date with a year from 0 to 9999 (TOML's four digits) and the time is valid
    bool IsValid() const noexcept;

    // The date and time as a std::chrono::local_time (second 60 is the next minute's second 0); std::nullopt if the
    // value isn't valid or is outside what nanoseconds from 1970 can hold (1677-09-21T00:12:43.145224192 to
    // 2262-04-11T23:47:16.854775807)
    std::optional<std::chrono::local_time<std::chrono::nanoseconds> > ToLocalTime() const noexcept;

    bool operator==(TASWTomlLocalDateTime const& other) const = default;
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlOffsetDateTime
//
// TOML's offset date-time: a date and a time of day at an offset from UTC, so an instant. The date and time are the
// local ones at the offset, as written (1979-05-27T00:32:00-07:00 is Date 1979-05-27, Time 00:32:00, Offset -7
// hours). The default is 1970-01-01T00:00:00Z.
/////////////////////////////////////////////////////////////////////////////
struct TASWTomlOffsetDateTime
{
    std::chrono::year_month_day Date{ std::chrono::year{ 1970 }, std::chrono::January, std::chrono::day{ 1 } };
    TASWTomlLocalTime Time;
    std::chrono::minutes Offset{ 0 }; // From UTC: -23:59 to +23:59 (UTC+1 is 60 minutes)

    // The instant 'utc' as a date and time at 'offset' (their second is never 60); std::nullopt if 'offset' is outside
    // -23:59 to +23:59
    static std::optional<TASWTomlOffsetDateTime> FromSysTime(std::chrono::sys_time<std::chrono::nanoseconds> utc,
        std::chrono::minutes offset = std::chrono::minutes{ 0 }) noexcept;

    // Whether the date is a real date with a year from 0 to 9999, the time is valid and the offset is within
    // -23:59 to +23:59
    bool IsValid() const noexcept;

    // The instant in UTC (second 60 is the next minute's second 0); std::nullopt if the value isn't valid or is outside
    // what nanoseconds from 1970 can hold (1677-09-21T00:12:43.145224192Z to 2262-04-11T23:47:16.854775807Z). Two
    // values with different offsets can be the same instant while operator== tells them apart.
    std::optional<std::chrono::sys_time<std::chrono::nanoseconds> > ToSysTime() const noexcept;

    bool operator==(TASWTomlOffsetDateTime const& other) const = default;
};


struct TASWTomlKeyValue;

/////////////////////////////////////////////////////////////////////////////
// TASWTomlValue
//
// A decoded TOML value of any type (see ETomlType), detached from any document, e.g. the value at a key or a whole
// table. A default value is an empty table. Nothing here throws except std::bad_alloc: AsString() and the others
// return nullptr when the value has another type, and the table and array methods do nothing (nullptr or false) when
// it isn't a table or an array. A pointer to a part of a value (from AsString(), Find() or Set(), for example) is
// valid until that part, or the array or table holding it, is changed. A value isn't checked when it is made (e.g.
// an invalid date); writing it as TOML is.
/////////////////////////////////////////////////////////////////////////////
class TASWTomlValue
{
public:
    // An array's values, in order; their types may differ
    using TValueVector = std::vector<TASWTomlValue>;

    // A table's keys and their values, in the order they were read or set; each key once
    using TKeyValueVector = std::vector<TASWTomlKeyValue>;

private:
    // In the order of ETomlType, so the index is the type
    std::variant<std::string, std::int64_t, double, bool, TASWTomlOffsetDateTime, TASWTomlLocalDateTime,
        std::chrono::year_month_day, TASWTomlLocalTime, TValueVector, TKeyValueVector> m_Data;

    template <typename T>
    T* Get() noexcept;
    template <typename T>
    T const* Get() const noexcept;

public:
    // An empty table
    TASWTomlValue();

    // A string (UTF-8); a null pointer is an empty string
    TASWTomlValue(char const* text);
    TASWTomlValue(std::string text);
    TASWTomlValue(std::string_view text);

    // An integer. Any other integer type whose values all fit in 64 bits converts (not bool, the character types,
    // or a 64-bit unsigned type, whose largest values don't fit).
    TASWTomlValue(std::int64_t number);
    template <std::integral T>
    requires(!std::same_as<T, bool> && !std::same_as<T, char> && !std::same_as<T, wchar_t> &&
        !std::same_as<T, char8_t> && !std::same_as<T, char16_t> && !std::same_as<T, char32_t> &&
        (std::is_signed_v<T> || sizeof(T) < sizeof(std::int64_t)))
    TASWTomlValue(T number) : TASWTomlValue(static_cast<std::int64_t>(number))
    {
    }

    TASWTomlValue(double number);
    TASWTomlValue(bool flag);
    TASWTomlValue(TASWTomlOffsetDateTime const& dateTime);
    TASWTomlValue(TASWTomlLocalDateTime const& dateTime);
    TASWTomlValue(std::chrono::year_month_day const& date);
    TASWTomlValue(TASWTomlLocalTime const& time);
    TASWTomlValue(TValueVector array);
    TASWTomlValue(TKeyValueVector table);

    TASWTomlValue(TASWTomlValue const& other);
    TASWTomlValue(TASWTomlValue&& other) noexcept;
    ~TASWTomlValue();

    TASWTomlValue& operator=(TASWTomlValue const& other);
    TASWTomlValue& operator=(TASWTomlValue&& other) noexcept;

    // Whether both have the same type and value. Floats compare as IEEE 754 numbers (a NaN differs from every value,
    // itself included), arrays element by element in order, and tables by their keys and values in any order (TOML
    // tables are unordered).
    bool operator==(TASWTomlValue const& other) const;

    // Adds 'value' at the end of an array; false (and nothing changes) if this value isn't an array
    bool Append(TASWTomlValue value);

    // The value as each type; nullptr if it has another type
    TValueVector* AsArray() noexcept;
    TValueVector const* AsArray() const noexcept;
    bool* AsBoolean() noexcept;
    bool const* AsBoolean() const noexcept;
    double* AsFloat() noexcept;
    double const* AsFloat() const noexcept;
    std::int64_t* AsInteger() noexcept;
    std::int64_t const* AsInteger() const noexcept;
    std::chrono::year_month_day* AsLocalDate() noexcept;
    std::chrono::year_month_day const* AsLocalDate() const noexcept;
    TASWTomlLocalDateTime* AsLocalDateTime() noexcept;
    TASWTomlLocalDateTime const* AsLocalDateTime() const noexcept;
    TASWTomlLocalTime* AsLocalTime() noexcept;
    TASWTomlLocalTime const* AsLocalTime() const noexcept;
    TASWTomlOffsetDateTime* AsOffsetDateTime() noexcept;
    TASWTomlOffsetDateTime const* AsOffsetDateTime() const noexcept;
    std::string* AsString() noexcept;
    std::string const* AsString() const noexcept;
    TKeyValueVector* AsTable() noexcept;
    TKeyValueVector const* AsTable() const noexcept;

    // The value of 'key' in a table; nullptr if there is no such key or this value isn't a table
    TASWTomlValue* Find(std::string_view key) noexcept;
    TASWTomlValue const* Find(std::string_view key) const noexcept;

    ETomlType GetType() const noexcept;

    // Whether the value has each type
    bool IsArray() const noexcept;
    bool IsBoolean() const noexcept;
    bool IsFloat() const noexcept;
    bool IsInteger() const noexcept;
    bool IsLocalDate() const noexcept;
    bool IsLocalDateTime() const noexcept;
    bool IsLocalTime() const noexcept;
    bool IsOffsetDateTime() const noexcept;
    bool IsString() const noexcept;
    bool IsTable() const noexcept;

    // Removes 'key' from a table, keeping the order of the others; false if there was no such key or this value isn't
    // a table
    bool Remove(std::string_view key);

    // Sets 'key' in a table to 'value': in its place if the key exists, else added at the end. The value as it is now
    // in the table; nullptr (and nothing changes) if this value isn't a table.
    TASWTomlValue* Set(std::string key, TASWTomlValue value);
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlKeyValue
//
// A key of a table and its value
/////////////////////////////////////////////////////////////////////////////
struct TASWTomlKeyValue
{
    std::string Key; // One key part, unquoted (UTF-8), e.g. host name for "host name"
    TASWTomlValue Value;

    bool operator==(TASWTomlKeyValue const& other) const = default;
};

//---------------------------------------------------------------------------

// The type's name, e.g. "LocalDateTime"; "Unknown" for a value that isn't an ETomlType
char const* ToString(ETomlType type) noexcept;

} // namespace ASWToml

//---------------------------------------------------------------------------
#endif // #ifndef ASWToml_ValueH
