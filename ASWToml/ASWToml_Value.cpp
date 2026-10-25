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
#include <cstdint>
#include <limits>
#include <utility>
//---------------------------------------------------------------------------

namespace ASWToml
{

namespace
{

// The seconds from 1970-01-01T00:00:00 to the first and last seconds TOML writes: 0000-01-01T00:00:00 and
// 9999-12-31T23:59:59
constexpr std::int64_t FirstSecond = -62167219200;
constexpr std::int64_t LastSecond = 253402300799;

// The largest offset from UTC, in minutes: 23:59
constexpr std::int64_t MaxOffsetMinutes = 23 * 60 + 59;

constexpr std::int64_t NanosecondsPerSecond = 1000000000;
constexpr std::int64_t SecondsPerDay = 24 * 60 * 60;

bool CountFromSeconds(std::int64_t seconds, std::int64_t nanoseconds, Detail::TDurationUnit const& unit,
    std::int64_t& count) noexcept;
std::int64_t FloorDivide(std::int64_t value, std::int64_t divisor) noexcept;
bool IsValidDate(std::chrono::year_month_day const& date) noexcept;
std::int64_t LocalSeconds(std::chrono::year_month_day const& date, TASWTomlLocalTime const& time) noexcept;
bool SecondsFromCount(std::int64_t count, Detail::TDurationUnit const& unit, std::int64_t& seconds,
    std::int64_t& nanoseconds) noexcept;
bool SplitSeconds(std::int64_t seconds, std::int64_t nanoseconds, std::chrono::year_month_day& date,
    TASWTomlLocalTime& time) noexcept;
TASWTomlLocalTime TimeOfDay(std::int64_t secondOfDay, std::int64_t nanoseconds) noexcept;

//---------------------------------------------------------------------------
/*
    CountFromSeconds

    Sets 'count' to 'seconds' plus 'nanoseconds' (0 to 999999999) since 1970 as a count of 'unit', rounded down; false
    (and 'count' unchanged) when the count is outside the unit's range
*/
bool CountFromSeconds(std::int64_t seconds, std::int64_t nanoseconds, Detail::TDurationUnit const& unit,
    std::int64_t& count) noexcept
{
    std::int64_t result = 0;
    if (unit.Den == 1)
    {
        // A unit of whole seconds: the fraction (under a second) can't change the seconds rounded down to it
        result = FloorDivide(seconds, unit.Num);
    }
    else
    {
        // A fraction of a second: seconds * Den plus the units in 'nanoseconds', if that is from Min to Max, worked
        // out without overflowing
        std::int64_t const units = nanoseconds / (NanosecondsPerSecond / unit.Den);
        if (seconds >= 0)
        {
            std::int64_t const last = unit.Max / unit.Den;
            if (seconds > last || (seconds == last && units > unit.Max % unit.Den))
                return false;

            result = seconds * unit.Den + units;
        }
        else
        {
            // Min is first * Den + Min % Den (a remainder from 1 - Den to 0), so the second before 'first' still fits
            // when its units make up for the remainder
            std::int64_t const first = unit.Min / unit.Den;
            if (seconds < first - 1 || (seconds == first - 1 && units - unit.Min % unit.Den < unit.Den))
                return false;

            // From the second above, since seconds * Den itself may not fit
            result = (seconds + 1) * unit.Den + (units - unit.Den);
        }
    }

    if (result < unit.Min || result > unit.Max)
        return false;

    count = result;

    return true;
}
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
    LocalSeconds

    The seconds from 1970-01-01T00:00:00 to 'date' at 'time', a valid date and time (second 60 is the next minute's
    second 0)
*/
std::int64_t LocalSeconds(std::chrono::year_month_day const& date, TASWTomlLocalTime const& time) noexcept
{
    // In 64 bits: std::chrono::days, hours and minutes count in 32 bits with libc++ and MSVC
    std::int64_t const dayCount = static_cast<std::int64_t>(std::chrono::sys_days(date).time_since_epoch().count());

    return dayCount * SecondsPerDay + static_cast<std::int64_t>(time.Hour.count()) * 3600 +
        static_cast<std::int64_t>(time.Minute.count()) * 60 + static_cast<std::int64_t>(time.Second.count());
}
//---------------------------------------------------------------------------
/*
    SecondsFromCount

    Sets 'seconds' and 'nanoseconds' (0 to 999999999) to the time 'count' units of 'unit' are, rounded down to whole
    seconds, and what is left of it; false (and both unchanged) when the seconds don't fit in 64 bits
*/
bool SecondsFromCount(std::int64_t count, Detail::TDurationUnit const& unit, std::int64_t& seconds,
    std::int64_t& nanoseconds) noexcept
{
    if (unit.Den == 1)
    {
        if (count > std::numeric_limits<std::int64_t>::max() / unit.Num ||
            count < std::numeric_limits<std::int64_t>::min() / unit.Num)
        {
            return false;
        }

        seconds = count * unit.Num;
        nanoseconds = 0;

        return true;
    }

    // Without multiplying back (the seconds times Den may not fit)
    std::int64_t whole = count / unit.Den;
    std::int64_t rest = count % unit.Den;
    if (rest < 0)
    {
        rest += unit.Den;
        --whole;
    }

    seconds = whole;
    nanoseconds = rest * (NanosecondsPerSecond / unit.Den);

    return true;
}
//---------------------------------------------------------------------------
/*
    SplitSeconds

    Sets 'date' and 'time' to the date and time of day 'seconds' plus 'nanoseconds' (0 to 999999999) after
    1970-01-01T00:00:00; false (and both unchanged) when that is outside the years 0 to 9999
*/
bool SplitSeconds(std::int64_t seconds, std::int64_t nanoseconds, std::chrono::year_month_day& date,
    TASWTomlLocalTime& time) noexcept
{
    if (seconds < FirstSecond || seconds > LastSecond)
        return false;

    std::int64_t const dayCount = FloorDivide(seconds, SecondsPerDay);
    date = std::chrono::year_month_day(std::chrono::sys_days(
        std::chrono::days(static_cast<std::chrono::days::rep>(dayCount))));
    time = TimeOfDay(seconds - dayCount * SecondsPerDay, nanoseconds);

    return true;
}
//---------------------------------------------------------------------------
/*
    TimeOfDay

    The time 'secondOfDay' (0 to 86399) plus 'nanoseconds' (0 to 999999999) after midnight
*/
TASWTomlLocalTime TimeOfDay(std::int64_t secondOfDay, std::int64_t nanoseconds) noexcept
{
    TASWTomlLocalTime time;
    time.Hour = std::chrono::hours(static_cast<std::chrono::hours::rep>(secondOfDay / 3600));
    time.Minute = std::chrono::minutes(static_cast<std::chrono::minutes::rep>(secondOfDay / 60 % 60));
    time.Second = std::chrono::seconds(static_cast<std::chrono::seconds::rep>(secondOfDay % 60));
    time.Fraction = std::chrono::nanoseconds(static_cast<std::chrono::nanoseconds::rep>(nanoseconds));

    return time;
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
bool Detail::LocalDateTimeFromCount(std::int64_t count, TDurationUnit const& unit,
    TASWTomlLocalDateTime& dateTime) noexcept
{
    std::int64_t seconds = 0;
    std::int64_t nanoseconds = 0;
    if (!SecondsFromCount(count, unit, seconds, nanoseconds))
        return false;

    TASWTomlLocalDateTime result;
    if (!SplitSeconds(seconds, nanoseconds, result.Date, result.Time))
        return false;

    dateTime = result;

    return true;
}
//---------------------------------------------------------------------------
bool Detail::LocalDateTimeToCount(TASWTomlLocalDateTime const& dateTime, TDurationUnit const& unit,
    std::int64_t& count) noexcept
{
    if (!dateTime.IsValid())
        return false;

    return CountFromSeconds(LocalSeconds(dateTime.Date, dateTime.Time),
        static_cast<std::int64_t>(dateTime.Time.Fraction.count()), unit, count);
}
//---------------------------------------------------------------------------
void Detail::LocalTimeFromCount(std::int64_t count, TDurationUnit const& unit, TASWTomlLocalTime& time) noexcept
{
    std::int64_t secondOfDay = 0;
    std::int64_t nanoseconds = 0;
    if (unit.Den == 1)
    {
        // count * Num modulo a day, from the remainders of both, since the product itself may not fit
        secondOfDay = count % SecondsPerDay * (unit.Num % SecondsPerDay) % SecondsPerDay;
    }
    else
    {
        // A fraction of a second always gives seconds that fit
        std::int64_t seconds = 0;
        static_cast<void>(SecondsFromCount(count, unit, seconds, nanoseconds));
        secondOfDay = seconds % SecondsPerDay;
    }

    if (secondOfDay < 0)
        secondOfDay += SecondsPerDay;

    time = TimeOfDay(secondOfDay, nanoseconds);
}
//---------------------------------------------------------------------------
bool Detail::LocalTimeToCount(TASWTomlLocalTime const& time, TDurationUnit const& unit, std::int64_t& count) noexcept
{
    if (!time.IsValid())
        return false;

    std::int64_t const seconds = static_cast<std::int64_t>(time.Hour.count()) * 3600 +
        static_cast<std::int64_t>(time.Minute.count()) * 60 + static_cast<std::int64_t>(time.Second.count());

    return CountFromSeconds(seconds, static_cast<std::int64_t>(time.Fraction.count()), unit, count);
}
//---------------------------------------------------------------------------
bool Detail::OffsetDateTimeFromCount(std::int64_t count, TDurationUnit const& unit, std::chrono::minutes offset,
    TASWTomlOffsetDateTime& dateTime) noexcept
{
    std::int64_t const offsetMinutes = static_cast<std::int64_t>(offset.count());
    if (offsetMinutes < -MaxOffsetMinutes || offsetMinutes > MaxOffsetMinutes)
        return false;

    std::int64_t seconds = 0;
    std::int64_t nanoseconds = 0;
    if (!SecondsFromCount(count, unit, seconds, nanoseconds))
        return false;

    // Far outside the years 0 to 9999 even at the offset, which then can't overflow when it is added
    if (seconds < FirstSecond - SecondsPerDay || seconds > LastSecond + SecondsPerDay)
        return false;

    TASWTomlOffsetDateTime result;
    if (!SplitSeconds(seconds + offsetMinutes * 60, nanoseconds, result.Date, result.Time))
        return false;

    result.Offset = offset;
    dateTime = result;

    return true;
}
//---------------------------------------------------------------------------
bool Detail::OffsetDateTimeToCount(TASWTomlOffsetDateTime const& dateTime, TDurationUnit const& unit,
    std::int64_t& count) noexcept
{
    if (!dateTime.IsValid())
        return false;

    std::int64_t const seconds = LocalSeconds(dateTime.Date, dateTime.Time) -
        static_cast<std::int64_t>(dateTime.Offset.count()) * 60;

    return CountFromSeconds(seconds, static_cast<std::int64_t>(dateTime.Time.Fraction.count()), unit, count);
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlLocalTime
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
bool TASWTomlLocalTime::IsValid() const noexcept
{
    return Hour >= std::chrono::hours(0) && Hour <= std::chrono::hours(23) && Minute >= std::chrono::minutes(0) &&
        Minute <= std::chrono::minutes(59) && Second >= std::chrono::seconds(0) && Second <= std::chrono::seconds(60) &&
        Fraction >= std::chrono::nanoseconds(0) && Fraction < std::chrono::seconds(1);
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlLocalDateTime
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
bool TASWTomlLocalDateTime::IsValid() const noexcept
{
    return IsValidDate(Date) && Time.IsValid();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlOffsetDateTime
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
bool TASWTomlOffsetDateTime::IsValid() const noexcept
{
    return IsValidDate(Date) && Time.IsValid() && Offset.count() >= -MaxOffsetMinutes &&
        Offset.count() <= MaxOffsetMinutes;
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
