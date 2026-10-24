/* **************************************************************************
Test_ASWToml_Value.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "Test_ASWToml_Value.h"
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------

// The integer types a value is made from: those whose values all fit in 64 bits, but not bool, the character types
// or a 64-bit unsigned type (they would be ambiguous between the integer, float and bool constructors)
static_assert(std::is_constructible_v<ASWToml::TASWTomlValue, int>, "an int should make a value");
static_assert(std::is_constructible_v<ASWToml::TASWTomlValue, unsigned int>, "an unsigned int should make a value");
static_assert(std::is_constructible_v<ASWToml::TASWTomlValue, long long>, "a long long should make a value");
static_assert(!std::is_constructible_v<ASWToml::TASWTomlValue, unsigned long long>,
    "an unsigned long long should not make a value");
static_assert(!std::is_constructible_v<ASWToml::TASWTomlValue, char>, "a char should not make a value");
static_assert(!std::is_constructible_v<ASWToml::TASWTomlValue, char8_t>, "a char8_t should not make a value");

namespace ASWUnitTests
{

namespace
{

using namespace std::chrono_literals;

using ASWToml::ETomlType;
using ASWToml::TASWTomlLocalDateTime;
using ASWToml::TASWTomlLocalTime;
using ASWToml::TASWTomlOffsetDateTime;
using ASWToml::TASWTomlValue;

using TNameSet = std::set<std::string>;
using TTomlTypeVector = std::vector<ETomlType>;

TTomlTypeVector AllTomlTypes();
std::chrono::year_month_day Date(int year, unsigned month, unsigned day);
TASWTomlLocalTime Time(int hour, int minute, int second, std::int64_t fraction = 0);

//---------------------------------------------------------------------------
/*
    AllTomlTypes

    Every ETomlType, in declaration order
*/
TTomlTypeVector AllTomlTypes()
{
    return TTomlTypeVector{
        ETomlType::String,
        ETomlType::Integer,
        ETomlType::Float,
        ETomlType::Boolean,
        ETomlType::OffsetDateTime,
        ETomlType::LocalDateTime,
        ETomlType::LocalDate,
        ETomlType::LocalTime,
        ETomlType::Array,
        ETomlType::Table,
    };
}
//---------------------------------------------------------------------------
/*
    Date

    The date 'year'-'month'-'day'
*/
std::chrono::year_month_day Date(int year, unsigned month, unsigned day)
{
    return std::chrono::year_month_day(std::chrono::year(year), std::chrono::month(month), std::chrono::day(day));
}
//---------------------------------------------------------------------------
/*
    Time

    The time 'hour':'minute':'second' plus 'fraction' nanoseconds
*/
TASWTomlLocalTime Time(int hour, int minute, int second, std::int64_t fraction)
{
    TASWTomlLocalTime time;
    time.Hour = std::chrono::hours(hour);
    time.Minute = std::chrono::minutes(minute);
    time.Second = std::chrono::seconds(second);
    time.Fraction = std::chrono::nanoseconds(fraction);

    return time;
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DateTimes
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_DateTimes::TTest_ASWToml_DateTimes()
    : inherited("ASWToml_DateTimes_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_DateTimes::~TTest_ASWToml_DateTimes()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DateTimes::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DateTimes::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DateTimes::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DateTimes::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimes, LocalDateTime_DefaultsToEpoch)
{
    // Act
    TASWTomlLocalDateTime const dateTime;

    // Assert
    CheckTrue(dateTime.Date == Date(1970, 1, 1), "the date should be 1970-01-01");
    CheckTrue(dateTime.Time == TASWTomlLocalTime(), "the time should be midnight");
    CheckTrue(dateTime.IsValid(), "the default should be valid");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimes, LocalDateTime_FromLocalTime_SplitsDateAndTime)
{
    // Arrange
    std::chrono::local_time<std::chrono::nanoseconds> const before1970 =
        std::chrono::local_days(Date(1969, 12, 31)) + 23h + 59min + 59s + 500ms;
    std::chrono::local_time<std::chrono::nanoseconds> const after1970 =
        std::chrono::local_days(Date(1979, 5, 27)) + 7h + 32min + 1ns;

    // Act
    TASWTomlLocalDateTime const early = TASWTomlLocalDateTime::FromLocalTime(before1970);
    TASWTomlLocalDateTime const late = TASWTomlLocalDateTime::FromLocalTime(after1970);

    // Assert
    CheckTrue(early.Date == Date(1969, 12, 31), "a time before 1970 should keep its date");
    CheckTrue(early.Time == Time(23, 59, 59, 500000000), "a time before 1970 should keep its time");
    CheckTrue(late.Date == Date(1979, 5, 27), "the date");
    CheckTrue(late.Time == Time(7, 32, 0, 1), "the time, to the nanosecond");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimes, LocalDateTime_IsValid_ChecksDateAndTime)
{
    // Arrange
    TASWTomlLocalDateTime dateTime;

    // Act & Assert
    dateTime.Date = Date(2024, 2, 29);
    CheckTrue(dateTime.IsValid(), "Feb 29 in a leap year should be valid");

    dateTime.Date = Date(2023, 2, 29);
    CheckFalse(dateTime.IsValid(), "Feb 29 in a common year should not be valid");

    dateTime.Date = Date(0, 1, 1);
    CheckTrue(dateTime.IsValid(), "year 0 should be valid");

    dateTime.Date = Date(9999, 12, 31);
    CheckTrue(dateTime.IsValid(), "year 9999 should be valid");

    dateTime.Date = Date(10000, 1, 1);
    CheckFalse(dateTime.IsValid(), "year 10000 should not be valid (TOML writes four digits)");

    dateTime.Date = Date(-1, 1, 1);
    CheckFalse(dateTime.IsValid(), "year -1 should not be valid");

    dateTime.Date = Date(2024, 1, 1);
    dateTime.Time = Time(24, 0, 0);
    CheckFalse(dateTime.IsValid(), "an invalid time should make the date-time invalid");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimes, LocalDateTime_ToLocalTime_AddsDateAndTime)
{
    // Arrange
    TASWTomlLocalDateTime dateTime;
    dateTime.Date = Date(1979, 5, 27);
    dateTime.Time = Time(7, 32, 0, 999999);
    TASWTomlLocalDateTime leapSecond;
    leapSecond.Date = Date(2016, 12, 31);
    leapSecond.Time = Time(23, 59, 60, 500000000);

    // Act
    std::optional<std::chrono::local_time<std::chrono::nanoseconds> > const localTime = dateTime.ToLocalTime();
    std::optional<std::chrono::local_time<std::chrono::nanoseconds> > const afterLeap = leapSecond.ToLocalTime();

    // Assert
    AssertTrue(localTime.has_value(), "the date-time should convert");
    CheckTrue(*localTime == std::chrono::local_days(Date(1979, 5, 27)) + 7h + 32min + 999999ns,
        "the time point should be the date plus the time");
    AssertTrue(afterLeap.has_value(), "a leap second should convert");
    CheckTrue(*afterLeap == std::chrono::local_days(Date(2017, 1, 1)) + 500ms,
        "a leap second should become the next day's first second");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimes, LocalDateTime_ToLocalTime_RefusesOutOfRange)
{
    // Arrange
    // The first and last nanoseconds std::chrono::nanoseconds can count from 1970, and one past each
    TASWTomlLocalDateTime first;
    first.Date = Date(1677, 9, 21);
    first.Time = Time(0, 12, 43, 145224192);
    TASWTomlLocalDateTime beforeFirst = first;
    beforeFirst.Time.Fraction -= 1ns;
    TASWTomlLocalDateTime last;
    last.Date = Date(2262, 4, 11);
    last.Time = Time(23, 47, 16, 854775807);
    TASWTomlLocalDateTime afterLast = last;
    afterLast.Time.Fraction += 1ns;
    TASWTomlLocalDateTime farFuture;
    farFuture.Date = Date(9999, 12, 31);
    TASWTomlLocalDateTime invalid;
    invalid.Date = Date(2023, 2, 30);

    // Act & Assert
    std::optional<std::chrono::local_time<std::chrono::nanoseconds> > const firstTime = first.ToLocalTime();
    AssertTrue(firstTime.has_value(), "the first time nanoseconds can hold should convert");
    CheckEquals(std::numeric_limits<std::int64_t>::min(), static_cast<std::int64_t>(
        firstTime->time_since_epoch().count()), "the first time should be the smallest count");
    std::optional<std::chrono::local_time<std::chrono::nanoseconds> > const lastTime = last.ToLocalTime();
    AssertTrue(lastTime.has_value(), "the last time nanoseconds can hold should convert");
    CheckEquals(std::numeric_limits<std::int64_t>::max(), static_cast<std::int64_t>(
        lastTime->time_since_epoch().count()), "the last time should be the largest count");
    CheckFalse(beforeFirst.ToLocalTime().has_value(), "one nanosecond before the first should not convert");
    CheckFalse(afterLast.ToLocalTime().has_value(), "one nanosecond after the last should not convert");
    CheckFalse(farFuture.ToLocalTime().has_value(), "9999-12-31 should not convert");
    CheckFalse(invalid.ToLocalTime().has_value(), "an invalid date should not convert");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimes, LocalTime_DefaultsToMidnight)
{
    // Act
    TASWTomlLocalTime const time;

    // Assert
    CheckTrue(time == Time(0, 0, 0), "every field should be 0");
    CheckTrue(time.IsValid(), "midnight should be valid");
    CheckTrue(time.ToDuration() == std::optional<std::chrono::nanoseconds>(0ns), "midnight should be 0 since midnight");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimes, LocalTime_FromDuration_SplitsAndWraps)
{
    CheckTrue(TASWTomlLocalTime::FromDuration(7h + 32min + 500ms) == Time(7, 32, 0, 500000000),
        "a time should split into its fields");
    CheckTrue(TASWTomlLocalTime::FromDuration(23h + 59min + 59s + 999999999ns) == Time(23, 59, 59, 999999999),
        "the last nanosecond of a day");
    CheckTrue(TASWTomlLocalTime::FromDuration(24h) == Time(0, 0, 0), "24 hours should wrap to midnight");
    CheckTrue(TASWTomlLocalTime::FromDuration(25h + 1s) == Time(1, 0, 1), "25 hours should wrap to 01:00");
    CheckTrue(TASWTomlLocalTime::FromDuration(-1h) == Time(23, 0, 0), "-1 hour should be 23:00");
    CheckTrue(TASWTomlLocalTime::FromDuration(-1ns) == Time(23, 59, 59, 999999999),
        "-1 nanosecond should be the day's last nanosecond");
    CheckTrue(TASWTomlLocalTime::FromDuration(std::chrono::nanoseconds::min()).IsValid(),
        "the smallest duration should give a valid time");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimes, LocalTime_IsValid_ChecksEachField)
{
    CheckTrue(Time(23, 59, 59, 999999999).IsValid(), "the last nanosecond of a day should be valid");
    CheckTrue(Time(15, 59, 60).IsValid(), "a leap second (second 60) should be valid at any hour");
    CheckFalse(Time(24, 0, 0).IsValid(), "hour 24 should not be valid");
    CheckFalse(Time(-1, 0, 0).IsValid(), "hour -1 should not be valid");
    CheckFalse(Time(0, 60, 0).IsValid(), "minute 60 should not be valid");
    CheckFalse(Time(0, -1, 0).IsValid(), "minute -1 should not be valid");
    CheckFalse(Time(0, 0, 61).IsValid(), "second 61 should not be valid");
    CheckFalse(Time(0, 0, -1).IsValid(), "second -1 should not be valid");
    CheckFalse(Time(0, 0, 0, 1000000000).IsValid(), "a fraction of a whole second should not be valid");
    CheckFalse(Time(0, 0, 0, -1).IsValid(), "a negative fraction should not be valid");
    CheckFalse(Time(24, 0, 0).ToDuration().has_value(), "an invalid time should have no duration");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimes, LocalTime_ToDuration_AddsFields)
{
    CheckTrue(Time(7, 32, 0, 500000000).ToDuration() == std::optional<std::chrono::nanoseconds>(7h + 32min + 500ms),
        "the fields should add up");
    CheckTrue(Time(23, 59, 60).ToDuration() == std::optional<std::chrono::nanoseconds>(24h),
        "second 60 should be the next minute's second 0");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimes, OffsetDateTime_Equality_ComparesFields)
{
    // Arrange
    // 1979-05-27T00:32:00-07:00 and 1979-05-27T07:32:00Z: the same instant, written differently
    TASWTomlOffsetDateTime pacific;
    pacific.Date = Date(1979, 5, 27);
    pacific.Time = Time(0, 32, 0);
    pacific.Offset = -7h;
    TASWTomlOffsetDateTime utc;
    utc.Date = Date(1979, 5, 27);
    utc.Time = Time(7, 32, 0);

    // Act & Assert
    CheckTrue(pacific.ToSysTime() == utc.ToSysTime(), "both should be the same instant");
    CheckFalse(pacific == utc, "operator== should tell the two apart");
    CheckTrue(pacific == pacific, "a value should equal itself");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimes, OffsetDateTime_FromSysTime_AppliesOffset)
{
    // Arrange
    std::chrono::sys_time<std::chrono::nanoseconds> const instant = std::chrono::sys_days(Date(1979, 5, 27)) + 7h +
        32min;

    // Act
    std::optional<TASWTomlOffsetDateTime> const pacific = TASWTomlOffsetDateTime::FromSysTime(instant, -7h);
    std::optional<TASWTomlOffsetDateTime> const tokyo = TASWTomlOffsetDateTime::FromSysTime(instant, 9h);
    std::optional<TASWTomlOffsetDateTime> const utc = TASWTomlOffsetDateTime::FromSysTime(instant);
    std::optional<TASWTomlOffsetDateTime> const tooFar = TASWTomlOffsetDateTime::FromSysTime(instant, 24h);

    // Assert
    AssertTrue(pacific.has_value() && tokyo.has_value() && utc.has_value(), "the offsets should be accepted");
    CheckTrue(pacific->Date == Date(1979, 5, 27) && pacific->Time == Time(0, 32, 0), "UTC-7 should be 00:32");
    CheckEquals(static_cast<std::int64_t>(-420), static_cast<std::int64_t>(pacific->Offset.count()),
        "the offset should be kept, in minutes");
    CheckTrue(tokyo->Date == Date(1979, 5, 27) && tokyo->Time == Time(16, 32, 0), "UTC+9 should be 16:32");
    CheckTrue(utc->Time == Time(7, 32, 0) && utc->Offset == 0min, "no offset should be UTC");
    CheckFalse(tooFar.has_value(), "an offset of 24 hours should be refused");
    CheckTrue(pacific->ToSysTime() == std::optional<std::chrono::sys_time<std::chrono::nanoseconds> >(instant),
        "converting back should give the same instant");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimes, OffsetDateTime_IsValid_ChecksOffset)
{
    // Arrange
    TASWTomlOffsetDateTime dateTime;

    // Act & Assert
    CheckTrue(dateTime.IsValid(), "the default (1970-01-01T00:00:00Z) should be valid");

    dateTime.Offset = 23h + 59min;
    CheckTrue(dateTime.IsValid(), "+23:59 should be valid");

    dateTime.Offset = -(23h + 59min);
    CheckTrue(dateTime.IsValid(), "-23:59 should be valid");

    dateTime.Offset = 24h;
    CheckFalse(dateTime.IsValid(), "+24:00 should not be valid");
    CheckFalse(dateTime.ToSysTime().has_value(), "an invalid value should have no instant");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimes, OffsetDateTime_ToSysTime_SubtractsOffset)
{
    // Arrange
    // TOML's own example: 1979-05-27T00:32:00.999999-07:00
    TASWTomlOffsetDateTime dateTime;
    dateTime.Date = Date(1979, 5, 27);
    dateTime.Time = Time(0, 32, 0, 999999000);
    dateTime.Offset = -7h;

    // Act
    std::optional<std::chrono::sys_time<std::chrono::nanoseconds> > const instant = dateTime.ToSysTime();

    // Assert
    AssertTrue(instant.has_value(), "the value should convert");
    CheckEquals(static_cast<std::int64_t>(296638320999999000), static_cast<std::int64_t>(
        instant->time_since_epoch().count()), "the instant should be 07:32:00.999999 UTC");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_ValueModel
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_ValueModel::TTest_ASWToml_ValueModel()
    : inherited("ASWToml_ValueModel_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_ValueModel::~TTest_ASWToml_ValueModel()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ValueModel::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ValueModel::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ValueModel::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ValueModel::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ValueModel, Array_Append)
{
    // Arrange
    TASWTomlValue array = TASWTomlValue::TValueVector{ 1 };
    TASWTomlValue notArray = "text";

    // Act
    bool const appended = array.Append("two");
    bool const appendedToItself = array.Append(array);
    bool const appendedToString = notArray.Append(3);

    // Assert
    CheckTrue(appended && appendedToItself, "values should be appended to an array");
    AssertEquals(3, static_cast<int>(array.AsArray()->size()), "the array should hold three values");
    CheckTrue((*array.AsArray())[1] == TASWTomlValue("two"), "the values may have different types");
    CheckEquals(2, static_cast<int>((*array.AsArray())[2].AsArray()->size()),
        "appending the array to itself should append a copy of it as it was");
    CheckFalse(appendedToString, "nothing should be appended to a string");
    CheckTrue(notArray == TASWTomlValue("text"), "the string should be unchanged");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ValueModel, As_ReturnsNullForOtherTypes)
{
    // Arrange
    TASWTomlValue value = 42;

    // Act & Assert
    AssertTrue(value.AsInteger() != nullptr, "the integer should be there");
    CheckEquals(static_cast<std::int64_t>(42), *value.AsInteger(), "the integer's value");
    CheckTrue(value.AsString() == nullptr && value.AsFloat() == nullptr && value.AsBoolean() == nullptr &&
        value.AsArray() == nullptr && value.AsTable() == nullptr && value.AsLocalDate() == nullptr &&
        value.AsLocalTime() == nullptr && value.AsLocalDateTime() == nullptr && value.AsOffsetDateTime() == nullptr,
        "every other type should be nullptr");
    CheckTrue(value.Find("x") == nullptr, "Find() on an integer should be nullptr");
    CheckTrue(value.Set("x", 1) == nullptr, "Set() on an integer should be nullptr");
    CheckFalse(value.Remove("x"), "Remove() on an integer should be false");

    *value.AsInteger() = 7;
    CheckTrue(value == TASWTomlValue(7), "the value should be changeable through its pointer");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ValueModel, Constructors_SetEachType)
{
    // Arrange
    char const* const nullText = nullptr;

    // Act & Assert
    CheckEquals(ETomlType::String, TASWTomlValue("text").GetType(), "a string literal");
    CheckEquals(std::string("text"), *TASWTomlValue(std::string_view("text")).AsString(), "a std::string_view");
    CheckEquals(std::string("text"), *TASWTomlValue(std::string("text")).AsString(), "a std::string");
    CheckEquals(std::string(""), *TASWTomlValue(nullText).AsString(), "a null pointer should be an empty string");
    CheckEquals(ETomlType::Integer, TASWTomlValue(42).GetType(), "an int");
    CheckEquals(ETomlType::Integer, TASWTomlValue(static_cast<unsigned short>(42)).GetType(), "an unsigned short");
    CheckEquals(ETomlType::Integer, TASWTomlValue(static_cast<std::int8_t>(-5)).GetType(), "a std::int8_t");
    CheckEquals(static_cast<std::int64_t>(4294967295), *TASWTomlValue(4294967295u).AsInteger(),
        "the largest unsigned int should keep its value");
    CheckEquals(static_cast<std::int64_t>(std::numeric_limits<std::int64_t>::min()),
        *TASWTomlValue(std::numeric_limits<long long>::min()).AsInteger(), "the smallest long long");
    CheckEquals(ETomlType::Float, TASWTomlValue(2.5).GetType(), "a double");
    CheckEquals(ETomlType::Float, TASWTomlValue(1.5f).GetType(), "a float");
    CheckEquals(ETomlType::Boolean, TASWTomlValue(true).GetType(), "a bool");
    CheckEquals(ETomlType::OffsetDateTime, TASWTomlValue(TASWTomlOffsetDateTime()).GetType(), "an offset date-time");
    CheckEquals(ETomlType::LocalDateTime, TASWTomlValue(TASWTomlLocalDateTime()).GetType(), "a local date-time");
    CheckEquals(ETomlType::LocalDate, TASWTomlValue(Date(1979, 5, 27)).GetType(), "a local date");
    CheckEquals(ETomlType::LocalTime, TASWTomlValue(Time(7, 32, 0)).GetType(), "a local time");
    CheckEquals(ETomlType::Array, TASWTomlValue(TASWTomlValue::TValueVector()).GetType(), "an array");
    CheckEquals(ETomlType::Table, TASWTomlValue(TASWTomlValue::TKeyValueVector()).GetType(), "a table");
    CheckTrue(TASWTomlValue(true).IsBoolean() && TASWTomlValue(1).IsInteger() && TASWTomlValue(1.0).IsFloat() &&
        TASWTomlValue("").IsString() && TASWTomlValue().IsTable(), "Is...() should match the type");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ValueModel, Assignment_IsDeep)
{
    // Arrange
    TASWTomlValue original;
    original.Set("server", TASWTomlValue())->Set("ports", TASWTomlValue::TValueVector{ 80, 443 });
    TASWTomlValue const array = TASWTomlValue::TValueVector{ 1, TASWTomlValue::TValueVector{ 2 } };
    TASWTomlValue const text = "text";
    TASWTomlValue table = 1;
    TASWTomlValue arrayCopy = "other";
    TASWTomlValue scalar = TASWTomlValue::TValueVector{ 3 };
    TASWTomlValue self = TASWTomlValue::TValueVector{ 4 };
    TASWTomlValue const& selfReference = self;

    // Act
    table = original;
    arrayCopy = array;
    scalar = text;
    self = selfReference;

    // Assert
    AssertTrue(table == original, "a table assigned over an integer should equal it");
    table.Find("server")->Find("ports")->Append(8080);
    CheckEquals(2, static_cast<int>(original.Find("server")->Find("ports")->AsArray()->size()),
        "the original's nested array should be unchanged: the assigned table is a deep copy");
    CheckTrue(arrayCopy == array, "an array assigned over a string should equal it");
    CheckTrue(scalar == text, "a string assigned over an array should equal it");
    CheckTrue(self == TASWTomlValue(TASWTomlValue::TValueVector{ 4 }), "a value assigned to itself should be kept");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ValueModel, Copy_IsDeep)
{
    // Arrange
    TASWTomlValue original;
    original.Set("server", TASWTomlValue())->Set("ports", TASWTomlValue::TValueVector{ 80, 443 });

    // Act
    TASWTomlValue copy = original;
    copy.Find("server")->Find("ports")->Append(8080);

    // Assert
    CheckEquals(2, static_cast<int>(original.Find("server")->Find("ports")->AsArray()->size()),
        "the original's nested array should be unchanged");
    CheckEquals(3, static_cast<int>(copy.Find("server")->Find("ports")->AsArray()->size()),
        "the copy's nested array should have the new value");
    CheckFalse(copy == original, "the two should now differ");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ValueModel, Default_IsEmptyTable)
{
    // Act
    TASWTomlValue const value;

    // Assert
    CheckEquals(ETomlType::Table, value.GetType(), "a default value should be a table");
    AssertTrue(value.AsTable() != nullptr, "the table should be there");
    CheckEmpty(*value.AsTable(), "the table should be empty");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ValueModel, Equality_ComparesTypeAndValue)
{
    double const nan = std::numeric_limits<double>::quiet_NaN();

    CheckTrue(TASWTomlValue(1) == TASWTomlValue(1), "equal integers");
    CheckFalse(TASWTomlValue(1) == TASWTomlValue(1.0), "an integer and a float should differ, even of equal value");
    CheckFalse(TASWTomlValue("1") == TASWTomlValue(1), "a string and an integer should differ");
    CheckFalse(TASWTomlValue(nan) == TASWTomlValue(nan), "a NaN should differ from itself (IEEE 754)");
    CheckTrue(TASWTomlValue(0.0) == TASWTomlValue(-0.0), "0.0 and -0.0 should be equal (IEEE 754)");
    CheckTrue(TASWTomlValue(TASWTomlValue::TValueVector{ 1, "a" }) == TASWTomlValue(TASWTomlValue::TValueVector{ 1,
        "a" }), "equal arrays");
    CheckFalse(TASWTomlValue(TASWTomlValue::TValueVector{ 1, 2 }) == TASWTomlValue(TASWTomlValue::TValueVector{ 2,
        1 }), "arrays in another order should differ");
    CheckTrue(TASWTomlValue(Time(7, 32, 0)) == TASWTomlValue(Time(7, 32, 0)), "equal times");
    CheckFalse(TASWTomlValue(Time(7, 32, 0)) == TASWTomlValue(Time(7, 32, 1)), "different times");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ValueModel, IdenticalValues_FloatsBySign)
{
    // Arrange
    double const nan = std::numeric_limits<double>::quiet_NaN();
    double const negativeNan = std::copysign(nan, -1.0);
    TASWTomlValue first;
    first.Set("a", TASWTomlValue::TValueVector{ nan, 1 });
    first.Set("b", -0.0);
    TASWTomlValue second;
    second.Set("b", -0.0);
    second.Set("a", TASWTomlValue::TValueVector{ nan, 1 });
    TASWTomlValue third = second;
    third.Find("a")->AsArray()->front() = negativeNan;

    // Act & Assert
    CheckTrue(ASWToml::Detail::IdenticalValues(nan, nan), "a NaN is identical to a NaN of the same sign");
    CheckTrue(ASWToml::Detail::IdenticalValues(negativeNan, std::copysign(std::nan("1"), -1.0)),
        "whatever their other bits, as TOML text writes no more");
    CheckFalse(ASWToml::Detail::IdenticalValues(nan, negativeNan), "but not to one of the other sign");
    CheckFalse(ASWToml::Detail::IdenticalValues(nan, 1.0), "nor to a number");
    CheckFalse(ASWToml::Detail::IdenticalValues(0.0, -0.0), "0.0 and -0.0 differ");
    CheckTrue(ASWToml::Detail::IdenticalValues(1.5, 1.5), "equal floats");
    CheckFalse(ASWToml::Detail::IdenticalValues(1, 1.0), "an integer and a float differ");
    CheckTrue(ASWToml::Detail::IdenticalValues("x", "x"), "equal strings");
    CheckTrue(ASWToml::Detail::IdenticalValues(Time(7, 32, 0), Time(7, 32, 0)), "equal times");
    CheckTrue(ASWToml::Detail::IdenticalValues(first, second), "tables in any order, with NaNs in arrays");
    CheckFalse(ASWToml::Detail::IdenticalValues(first, third), "a NaN of the other sign deep inside");
    CheckFalse(ASWToml::Detail::IdenticalValues(TASWTomlValue::TValueVector{ 1, 2 },
        TASWTomlValue::TValueVector{ 2, 1 }), "arrays in another order differ");
    CheckFalse(ASWToml::Detail::IdenticalValues(TASWTomlValue::TValueVector{ 1 }, TASWTomlValue::TValueVector{ 1, 2 }),
        "arrays of other sizes differ");
    second.Set("c", 1);
    CheckFalse(ASWToml::Detail::IdenticalValues(first, second), "a table with another key differs");
    CheckFalse(ASWToml::Detail::IdenticalValues(second, first), "both ways");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ValueModel, Equality_TablesIgnoreOrder)
{
    // Arrange
    TASWTomlValue first;
    first.Set("a", 1);
    first.Set("b", TASWTomlValue())->Set("c", "x");
    TASWTomlValue second;
    second.Set("b", TASWTomlValue())->Set("c", "x");
    second.Set("a", 1);
    TASWTomlValue third = second;
    third.Set("d", true);

    // Act & Assert
    CheckTrue(first == second, "tables with the same keys and values in another order should be equal");
    CheckFalse(first == third, "a table with another key should differ");
    CheckFalse(third == first, "the comparison should work both ways");

    first.Find("b")->Set("c", "y");
    CheckFalse(first == second, "a nested value that differs should make the tables differ");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ValueModel, Table_SetFindRemove)
{
    // Arrange
    TASWTomlValue table;

    // Act
    table.Set("name", "app");
    table.Set("port", 8080);
    table.Set("debug", false);
    TASWTomlValue* const replaced = table.Set("port", 9090);
    bool const returnedTableValue = replaced == table.Find("port");
    bool const removed = table.Remove("name");
    bool const removedMissing = table.Remove("missing");

    // Assert
    TASWTomlValue::TKeyValueVector const& entries = *table.AsTable();
    AssertEquals(2, static_cast<int>(entries.size()), "two keys should be left");
    CheckEquals(std::string("port"), entries[0].Key, "a replaced key should keep its place");
    CheckEquals(std::string("debug"), entries[1].Key, "the other keys should keep their order");
    CheckTrue(returnedTableValue, "Set() should return the value in the table");
    CheckTrue(*table.Find("port") == TASWTomlValue(9090), "the replaced value");
    CheckTrue(removed, "Remove() should report the removed key");
    CheckFalse(removedMissing, "Remove() of a missing key should be false");
    CheckTrue(table.Find("name") == nullptr, "a removed key should not be found");
    CheckTrue(table.Find("Port") == nullptr, "keys should be compared case-sensitively");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ValueModel, TomlType_ValuesNeverChange)
{
    // Arrange
    // Applications may store or compare the values, numbered in declaration order from 0, which is also the order of
    // the value's variant
    TTomlTypeVector const types = AllTomlTypes();

    // Act & Assert
    for (std::size_t i = 0; i < types.size(); ++i)
    {
        CheckEquals(static_cast<int>(i), static_cast<int>(types[i]),
            std::string("ETomlType::") + ASWToml::ToString(types[i]) + " should keep its value");
    }

    CheckEquals(std::string("Unknown"), std::string(ASWToml::ToString(static_cast<ETomlType>(types.size()))),
        "AllTomlTypes() should list every type");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ValueModel, ToString_TomlTypeNames)
{
    // Arrange
    TNameSet names;

    for (ETomlType const type : AllTomlTypes())
    {
        // Act
        std::string const name = ASWToml::ToString(type);

        // Assert
        CheckNotEquals(std::string("Unknown"), name, "type " + std::to_string(static_cast<int>(type)) +
            " should have a name");
        CheckTrue(names.insert(name).second, "the name \"" + name + "\" should be unique");
    }

    CheckEquals(std::string("LocalDateTime"), std::string(ASWToml::ToString(ETomlType::LocalDateTime)),
        "the name should be the enumerator's");
    CheckEquals(std::string("Unknown"), std::string(ASWToml::ToString(static_cast<ETomlType>(-1))),
        "an unknown type should be \"Unknown\"");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DateTimes, "Value")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_ValueModel, "Value")
