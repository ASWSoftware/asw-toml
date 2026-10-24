/* **************************************************************************
ASWToml_Value.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------
#include <cmath>
#include <cstddef>
#include <utility>
//---------------------------------------------------------------------------

namespace ASWToml
{

namespace
{

// The largest offset from UTC, in minutes: 23:59
constexpr std::int64_t MaxOffsetMinutes = 23 * 60 + 59;

constexpr std::int64_t NanosecondsPerSecond = 1000000000;
constexpr std::int64_t SecondsPerDay = 24 * 60 * 60;

std::int64_t FloorDivide(std::int64_t value, std::int64_t divisor) noexcept;
bool IsValidDate(std::chrono::year_month_day const& date) noexcept;
std::optional<std::chrono::nanoseconds> SecondsToNanoseconds(std::int64_t seconds, std::int64_t fraction) noexcept;
void SplitNanoseconds(std::int64_t nanoseconds, std::int64_t& seconds, std::int64_t& fraction) noexcept;
void SplitSeconds(std::int64_t seconds, std::int64_t fraction, std::chrono::year_month_day& date,
    TASWTomlLocalTime& time) noexcept;

//---------------------------------------------------------------------------
/*
    FloorDivide

    'value' divided by the positive 'divisor', rounded down (so -1 / 86400 is -1, not 0)
*/
std::int64_t FloorDivide(std::int64_t value, std::int64_t divisor) noexcept
{
    std::int64_t quotient = value / divisor;
    if (value % divisor < 0)
        --quotient;

    return quotient;
}
//---------------------------------------------------------------------------
/*
    IsValidDate

    Whether 'date' is a real date with a year TOML's four digits can write (0 to 9999)
*/
bool IsValidDate(std::chrono::year_month_day const& date) noexcept
{
    int const year = static_cast<int>(date.year());

    return date.ok() && year >= 0 && year <= 9999;
}
//---------------------------------------------------------------------------
/*
    SecondsToNanoseconds

    'seconds' plus 'fraction' nanoseconds (0 to 999999999) as one count of nanoseconds; std::nullopt if it doesn't fit
    in std::chrono::nanoseconds
*/
std::optional<std::chrono::nanoseconds> SecondsToNanoseconds(std::int64_t seconds, std::int64_t fraction) noexcept
{
    // The limits as whole seconds and a fraction, worked out without overflowing
    std::int64_t maxSeconds = 0;
    std::int64_t maxFraction = 0;
    std::int64_t minSeconds = 0;
    std::int64_t minFraction = 0;
    SplitNanoseconds(std::chrono::nanoseconds::max().count(), maxSeconds, maxFraction);
    SplitNanoseconds(std::chrono::nanoseconds::min().count(), minSeconds, minFraction);

    if (seconds > maxSeconds || (seconds == maxSeconds && fraction > maxFraction))
        return std::nullopt;

    if (seconds < minSeconds || (seconds == minSeconds && fraction < minFraction))
        return std::nullopt;

    // A negative count is built from the second above it, since minSeconds * 10^9 itself doesn't fit
    if (seconds < 0)
        return std::chrono::nanoseconds((seconds + 1) * NanosecondsPerSecond + (fraction - NanosecondsPerSecond));

    return std::chrono::nanoseconds(seconds * NanosecondsPerSecond + fraction);
}
//---------------------------------------------------------------------------
/*
    SplitNanoseconds

    'nanoseconds' as whole seconds, rounded down, and the nanoseconds left over (0 to 999999999)
*/
void SplitNanoseconds(std::int64_t nanoseconds, std::int64_t& seconds, std::int64_t& fraction) noexcept
{
    seconds = nanoseconds / NanosecondsPerSecond;
    fraction = nanoseconds % NanosecondsPerSecond;
    if (fraction < 0)
    {
        fraction += NanosecondsPerSecond;
        --seconds;
    }
}
//---------------------------------------------------------------------------
/*
    SplitSeconds

    The date and time of day 'seconds' plus 'fraction' nanoseconds after 1970-01-01T00:00:00
*/
void SplitSeconds(std::int64_t seconds, std::int64_t fraction, std::chrono::year_month_day& date,
    TASWTomlLocalTime& time) noexcept
{
    std::int64_t const dayCount = FloorDivide(seconds, SecondsPerDay);
    std::int64_t const secondOfDay = seconds - dayCount * SecondsPerDay;

    date = std::chrono::year_month_day(std::chrono::sys_days(
        std::chrono::days(static_cast<std::chrono::days::rep>(dayCount))));
    time = TASWTomlLocalTime::FromDuration(std::chrono::seconds(secondOfDay) + std::chrono::nanoseconds(fraction));
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
char const* ToString(ETomlType type) noexcept
{
    switch (type)
    {
        case ETomlType::String:
            return "String";

        case ETomlType::Integer:
            return "Integer";

        case ETomlType::Float:
            return "Float";

        case ETomlType::Boolean:
            return "Boolean";

        case ETomlType::OffsetDateTime:
            return "OffsetDateTime";

        case ETomlType::LocalDateTime:
            return "LocalDateTime";

        case ETomlType::LocalDate:
            return "LocalDate";

        case ETomlType::LocalTime:
            return "LocalTime";

        case ETomlType::Array:
            return "Array";

        case ETomlType::Table:
            return "Table";
    }

    return "Unknown";
}
//---------------------------------------------------------------------------
bool Detail::IdenticalValues(TASWTomlValue const& left, TASWTomlValue const& right) noexcept
{
    if (left.GetType() != right.GetType())
        return false;

    double const* const leftFloat = left.AsFloat();
    if (leftFloat != nullptr)
    {
        double const rightFloat = *right.AsFloat();
        if (std::signbit(*leftFloat) != std::signbit(rightFloat))
            return false;

        return std::isnan(*leftFloat) ? std::isnan(rightFloat) : *leftFloat == rightFloat;
    }

    TASWTomlValue::TValueVector const* const leftArray = left.AsArray();
    if (leftArray != nullptr)
    {
        TASWTomlValue::TValueVector const& rightArray = *right.AsArray();
        if (leftArray->size() != rightArray.size())
            return false;

        for (std::size_t i = 0; i < leftArray->size(); ++i)
        {
            if (!IdenticalValues((*leftArray)[i], rightArray[i]))
                return false;
        }

        return true;
    }

    TASWTomlValue::TKeyValueVector const* const leftTable = left.AsTable();
    if (leftTable != nullptr)
    {
        if (leftTable->size() != right.AsTable()->size())
            return false;

        for (TASWTomlKeyValue const& entry : *leftTable)
        {
            TASWTomlValue const* const rightValue = right.Find(entry.Key);
            if (rightValue == nullptr || !IdenticalValues(entry.Value, *rightValue))
                return false;
        }

        return true;
    }

    // The other types have no float in them
    return left == right;
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlLocalTime
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlLocalTime TASWTomlLocalTime::FromDuration(std::chrono::nanoseconds sinceMidnight) noexcept
{
    std::chrono::nanoseconds const day = std::chrono::hours(24);
    std::chrono::nanoseconds rest = sinceMidnight % day;
    if (rest < std::chrono::nanoseconds::zero())
        rest += day;

    TASWTomlLocalTime time;
    time.Hour = std::chrono::duration_cast<std::chrono::hours>(rest);
    rest -= time.Hour;
    time.Minute = std::chrono::duration_cast<std::chrono::minutes>(rest);
    rest -= time.Minute;
    time.Second = std::chrono::duration_cast<std::chrono::seconds>(rest);
    rest -= time.Second;
    time.Fraction = rest;

    return time;
}
//---------------------------------------------------------------------------
bool TASWTomlLocalTime::IsValid() const noexcept
{
    return Hour >= std::chrono::hours(0) && Hour <= std::chrono::hours(23) && Minute >= std::chrono::minutes(0) &&
        Minute <= std::chrono::minutes(59) && Second >= std::chrono::seconds(0) && Second <= std::chrono::seconds(60) &&
        Fraction >= std::chrono::nanoseconds(0) && Fraction < std::chrono::seconds(1);
}
//---------------------------------------------------------------------------
std::optional<std::chrono::nanoseconds> TASWTomlLocalTime::ToDuration() const noexcept
{
    if (!IsValid())
        return std::nullopt;

    return Hour + Minute + Second + Fraction;
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlLocalDateTime
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlLocalDateTime TASWTomlLocalDateTime::FromLocalTime(
    std::chrono::local_time<std::chrono::nanoseconds> localTime) noexcept
{
    std::int64_t seconds = 0;
    std::int64_t fraction = 0;
    SplitNanoseconds(localTime.time_since_epoch().count(), seconds, fraction);

    TASWTomlLocalDateTime dateTime;
    SplitSeconds(seconds, fraction, dateTime.Date, dateTime.Time);

    return dateTime;
}
//---------------------------------------------------------------------------
bool TASWTomlLocalDateTime::IsValid() const noexcept
{
    return IsValidDate(Date) && Time.IsValid();
}
//---------------------------------------------------------------------------
std::optional<std::chrono::local_time<std::chrono::nanoseconds> > TASWTomlLocalDateTime::ToLocalTime() const noexcept
{
    if (!IsValid())
        return std::nullopt;

    std::int64_t const dayCount = std::chrono::local_days(Date).time_since_epoch().count();
    std::int64_t const seconds = dayCount * SecondsPerDay + Time.Hour.count() * 3600 + Time.Minute.count() * 60 +
        Time.Second.count();
    std::optional<std::chrono::nanoseconds> const sinceEpoch = SecondsToNanoseconds(seconds, Time.Fraction.count());
    if (!sinceEpoch)
        return std::nullopt;

    return std::chrono::local_time<std::chrono::nanoseconds>(*sinceEpoch);
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlOffsetDateTime
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
std::optional<TASWTomlOffsetDateTime> TASWTomlOffsetDateTime::FromSysTime(
    std::chrono::sys_time<std::chrono::nanoseconds> utc, std::chrono::minutes offset) noexcept
{
    if (offset.count() < -MaxOffsetMinutes || offset.count() > MaxOffsetMinutes)
        return std::nullopt;

    std::int64_t seconds = 0;
    std::int64_t fraction = 0;
    SplitNanoseconds(utc.time_since_epoch().count(), seconds, fraction);

    TASWTomlOffsetDateTime dateTime;
    SplitSeconds(seconds + offset.count() * 60, fraction, dateTime.Date, dateTime.Time);
    dateTime.Offset = offset;

    return dateTime;
}
//---------------------------------------------------------------------------
bool TASWTomlOffsetDateTime::IsValid() const noexcept
{
    return IsValidDate(Date) && Time.IsValid() && Offset.count() >= -MaxOffsetMinutes &&
        Offset.count() <= MaxOffsetMinutes;
}
//---------------------------------------------------------------------------
std::optional<std::chrono::sys_time<std::chrono::nanoseconds> > TASWTomlOffsetDateTime::ToSysTime() const noexcept
{
    if (!IsValid())
        return std::nullopt;

    std::int64_t const dayCount = std::chrono::sys_days(Date).time_since_epoch().count();
    std::int64_t const seconds = dayCount * SecondsPerDay + Time.Hour.count() * 3600 + Time.Minute.count() * 60 +
        Time.Second.count() - Offset.count() * 60;
    std::optional<std::chrono::nanoseconds> const sinceEpoch = SecondsToNanoseconds(seconds, Time.Fraction.count());
    if (!sinceEpoch)
        return std::nullopt;

    return std::chrono::sys_time<std::chrono::nanoseconds>(*sinceEpoch);
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlValue
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlValue::TASWTomlValue()
    : m_Data(std::in_place_type<TKeyValueVector>)
{
}
//---------------------------------------------------------------------------
TASWTomlValue::TASWTomlValue(char const* text)
    : m_Data(std::in_place_type<std::string>, text != nullptr ? text : "")
{
}
//---------------------------------------------------------------------------
TASWTomlValue::TASWTomlValue(std::string text)
    : m_Data(std::in_place_type<std::string>, std::move(text))
{
}
//---------------------------------------------------------------------------
TASWTomlValue::TASWTomlValue(std::string_view text)
    : m_Data(std::in_place_type<std::string>, text)
{
}
//---------------------------------------------------------------------------
TASWTomlValue::TASWTomlValue(std::int64_t number)
    : m_Data(std::in_place_type<std::int64_t>, number)
{
}
//---------------------------------------------------------------------------
TASWTomlValue::TASWTomlValue(double number)
    : m_Data(std::in_place_type<double>, number)
{
}
//---------------------------------------------------------------------------
TASWTomlValue::TASWTomlValue(bool flag)
    : m_Data(std::in_place_type<bool>, flag)
{
}
//---------------------------------------------------------------------------
TASWTomlValue::TASWTomlValue(TASWTomlOffsetDateTime const& dateTime)
    : m_Data(std::in_place_type<TASWTomlOffsetDateTime>, dateTime)
{
}
//---------------------------------------------------------------------------
TASWTomlValue::TASWTomlValue(TASWTomlLocalDateTime const& dateTime)
    : m_Data(std::in_place_type<TASWTomlLocalDateTime>, dateTime)
{
}
//---------------------------------------------------------------------------
TASWTomlValue::TASWTomlValue(std::chrono::year_month_day const& date)
    : m_Data(std::in_place_type<std::chrono::year_month_day>, date)
{
}
//---------------------------------------------------------------------------
TASWTomlValue::TASWTomlValue(TASWTomlLocalTime const& time)
    : m_Data(std::in_place_type<TASWTomlLocalTime>, time)
{
}
//---------------------------------------------------------------------------
TASWTomlValue::TASWTomlValue(TValueVector array)
    : m_Data(std::in_place_type<TValueVector>, std::move(array))
{
}
//---------------------------------------------------------------------------
TASWTomlValue::TASWTomlValue(TKeyValueVector table)
    : m_Data(std::in_place_type<TKeyValueVector>, std::move(table))
{
}
//---------------------------------------------------------------------------
TASWTomlValue::TASWTomlValue(TASWTomlValue const& other)
    : m_Data(std::in_place_type<bool>, false)
{
    // An array or a table is copied element by element here rather than by the variant's copy, whose visitation
    // takes several times the stack per nesting level in an unoptimized build
    if (TValueVector const* const array = other.AsArray())
    {
        TValueVector& copy = m_Data.emplace<TValueVector>();
        copy.reserve(array->size());

        for (TASWTomlValue const& element : *array)
            copy.emplace_back(element);
    }
    else if (TKeyValueVector const* const table = other.AsTable())
    {
        TKeyValueVector& copy = m_Data.emplace<TKeyValueVector>();
        copy.reserve(table->size());

        for (TASWTomlKeyValue const& entry : *table)
            copy.push_back(entry);
    }
    else
    {
        m_Data = other.m_Data;
    }
}
//---------------------------------------------------------------------------
TASWTomlValue::TASWTomlValue(TASWTomlValue&& other) noexcept = default;
//---------------------------------------------------------------------------
TASWTomlValue::~TASWTomlValue()
{
    // An array's or a table's elements are destroyed by a local vector, so the variant's destructor, which takes
    // several times the stack per nesting level in an unoptimized build, has none to destroy
    if (TValueVector* const array = AsArray())
    {
        TValueVector const elements = std::move(*array);
    }
    else if (TKeyValueVector* const table = AsTable())
    {
        TKeyValueVector const entries = std::move(*table);
    }
}
//---------------------------------------------------------------------------
TASWTomlValue& TASWTomlValue::operator=(TASWTomlValue const& other)
{
    // Through the copy constructor, for its stack use (see there)
    if (this != &other)
    {
        TASWTomlValue copy(other);
        m_Data = std::move(copy.m_Data);
    }

    return *this;
}
//---------------------------------------------------------------------------
TASWTomlValue& TASWTomlValue::operator=(TASWTomlValue&& other) noexcept = default;
//---------------------------------------------------------------------------
bool TASWTomlValue::operator==(TASWTomlValue const& other) const
{
    TKeyValueVector const* const table = AsTable();
    TKeyValueVector const* const otherTable = other.AsTable();
    if (table == nullptr || otherTable == nullptr)
    {
        // Arrays element by element here rather than by the variant's operator== (see the copy constructor)
        TValueVector const* const array = AsArray();
        TValueVector const* const otherArray = other.AsArray();
        if (array == nullptr || otherArray == nullptr)
            return m_Data == other.m_Data;

        if (array->size() != otherArray->size())
            return false;

        for (std::size_t i = 0; i < array->size(); ++i)
        {
            if (!((*array)[i] == (*otherArray)[i]))
                return false;
        }

        return true;
    }

    // Tables are unordered: the same keys, each with an equal value
    if (table->size() != otherTable->size())
        return false;

    for (TASWTomlKeyValue const& entry : *table)
    {
        TASWTomlValue const* const otherValue = other.Find(entry.Key);
        if (otherValue == nullptr || !(*otherValue == entry.Value))
            return false;
    }

    return true;
}
//---------------------------------------------------------------------------
bool TASWTomlValue::Append(TASWTomlValue value)
{
    TValueVector* const array = AsArray();
    if (array == nullptr)
        return false;

    array->push_back(std::move(value));

    return true;
}
//---------------------------------------------------------------------------
TASWTomlValue::TValueVector* TASWTomlValue::AsArray() noexcept
{
    return Get<TValueVector>();
}
//---------------------------------------------------------------------------
TASWTomlValue::TValueVector const* TASWTomlValue::AsArray() const noexcept
{
    return Get<TValueVector>();
}
//---------------------------------------------------------------------------
bool* TASWTomlValue::AsBoolean() noexcept
{
    return Get<bool>();
}
//---------------------------------------------------------------------------
bool const* TASWTomlValue::AsBoolean() const noexcept
{
    return Get<bool>();
}
//---------------------------------------------------------------------------
double* TASWTomlValue::AsFloat() noexcept
{
    return Get<double>();
}
//---------------------------------------------------------------------------
double const* TASWTomlValue::AsFloat() const noexcept
{
    return Get<double>();
}
//---------------------------------------------------------------------------
std::int64_t* TASWTomlValue::AsInteger() noexcept
{
    return Get<std::int64_t>();
}
//---------------------------------------------------------------------------
std::int64_t const* TASWTomlValue::AsInteger() const noexcept
{
    return Get<std::int64_t>();
}
//---------------------------------------------------------------------------
std::chrono::year_month_day* TASWTomlValue::AsLocalDate() noexcept
{
    return Get<std::chrono::year_month_day>();
}
//---------------------------------------------------------------------------
std::chrono::year_month_day const* TASWTomlValue::AsLocalDate() const noexcept
{
    return Get<std::chrono::year_month_day>();
}
//---------------------------------------------------------------------------
TASWTomlLocalDateTime* TASWTomlValue::AsLocalDateTime() noexcept
{
    return Get<TASWTomlLocalDateTime>();
}
//---------------------------------------------------------------------------
TASWTomlLocalDateTime const* TASWTomlValue::AsLocalDateTime() const noexcept
{
    return Get<TASWTomlLocalDateTime>();
}
//---------------------------------------------------------------------------
TASWTomlLocalTime* TASWTomlValue::AsLocalTime() noexcept
{
    return Get<TASWTomlLocalTime>();
}
//---------------------------------------------------------------------------
TASWTomlLocalTime const* TASWTomlValue::AsLocalTime() const noexcept
{
    return Get<TASWTomlLocalTime>();
}
//---------------------------------------------------------------------------
TASWTomlOffsetDateTime* TASWTomlValue::AsOffsetDateTime() noexcept
{
    return Get<TASWTomlOffsetDateTime>();
}
//---------------------------------------------------------------------------
TASWTomlOffsetDateTime const* TASWTomlValue::AsOffsetDateTime() const noexcept
{
    return Get<TASWTomlOffsetDateTime>();
}
//---------------------------------------------------------------------------
std::string* TASWTomlValue::AsString() noexcept
{
    return Get<std::string>();
}
//---------------------------------------------------------------------------
std::string const* TASWTomlValue::AsString() const noexcept
{
    return Get<std::string>();
}
//---------------------------------------------------------------------------
TASWTomlValue::TKeyValueVector* TASWTomlValue::AsTable() noexcept
{
    return Get<TKeyValueVector>();
}
//---------------------------------------------------------------------------
TASWTomlValue::TKeyValueVector const* TASWTomlValue::AsTable() const noexcept
{
    return Get<TKeyValueVector>();
}
//---------------------------------------------------------------------------
TASWTomlValue* TASWTomlValue::Find(std::string_view key) noexcept
{
    TKeyValueVector* const table = AsTable();
    if (table == nullptr)
        return nullptr;

    for (TASWTomlKeyValue& entry : *table)
    {
        if (entry.Key == key)
            return &entry.Value;
    }

    return nullptr;
}
//---------------------------------------------------------------------------
TASWTomlValue const* TASWTomlValue::Find(std::string_view key) const noexcept
{
    TKeyValueVector const* const table = AsTable();
    if (table == nullptr)
        return nullptr;

    for (TASWTomlKeyValue const& entry : *table)
    {
        if (entry.Key == key)
            return &entry.Value;
    }

    return nullptr;
}
//---------------------------------------------------------------------------
template <typename T>
T* TASWTomlValue::Get() noexcept
{
    return std::get_if<T>(&m_Data);
}
//---------------------------------------------------------------------------
template <typename T>
T const* TASWTomlValue::Get() const noexcept
{
    return std::get_if<T>(&m_Data);
}
//---------------------------------------------------------------------------
ETomlType TASWTomlValue::GetType() const noexcept
{
    return static_cast<ETomlType>(m_Data.index());
}
//---------------------------------------------------------------------------
bool TASWTomlValue::IsArray() const noexcept
{
    return GetType() == ETomlType::Array;
}
//---------------------------------------------------------------------------
bool TASWTomlValue::IsBoolean() const noexcept
{
    return GetType() == ETomlType::Boolean;
}
//---------------------------------------------------------------------------
bool TASWTomlValue::IsFloat() const noexcept
{
    return GetType() == ETomlType::Float;
}
//---------------------------------------------------------------------------
bool TASWTomlValue::IsInteger() const noexcept
{
    return GetType() == ETomlType::Integer;
}
//---------------------------------------------------------------------------
bool TASWTomlValue::IsLocalDate() const noexcept
{
    return GetType() == ETomlType::LocalDate;
}
//---------------------------------------------------------------------------
bool TASWTomlValue::IsLocalDateTime() const noexcept
{
    return GetType() == ETomlType::LocalDateTime;
}
//---------------------------------------------------------------------------
bool TASWTomlValue::IsLocalTime() const noexcept
{
    return GetType() == ETomlType::LocalTime;
}
//---------------------------------------------------------------------------
bool TASWTomlValue::IsOffsetDateTime() const noexcept
{
    return GetType() == ETomlType::OffsetDateTime;
}
//---------------------------------------------------------------------------
bool TASWTomlValue::IsString() const noexcept
{
    return GetType() == ETomlType::String;
}
//---------------------------------------------------------------------------
bool TASWTomlValue::IsTable() const noexcept
{
    return GetType() == ETomlType::Table;
}
//---------------------------------------------------------------------------
bool TASWTomlValue::Remove(std::string_view key)
{
    TKeyValueVector* const table = AsTable();
    if (table == nullptr)
        return false;

    for (TKeyValueVector::iterator it = table->begin(); it != table->end(); ++it)
    {
        if (it->Key == key)
        {
            table->erase(it);
            return true;
        }
    }

    return false;
}
//---------------------------------------------------------------------------
TASWTomlValue* TASWTomlValue::Set(std::string key, TASWTomlValue value)
{
    TKeyValueVector* const table = AsTable();
    if (table == nullptr)
        return nullptr;

    TASWTomlValue* const existing = Find(key);
    if (existing != nullptr)
    {
        *existing = std::move(value);
        return existing;
    }

    table->push_back(TASWTomlKeyValue{ std::move(key), std::move(value) });

    return &table->back().Value;
}
//---------------------------------------------------------------------------

} // namespace ASWToml
