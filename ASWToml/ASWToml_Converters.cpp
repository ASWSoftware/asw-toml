/* **************************************************************************
ASWToml_Converters.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "ASWToml_Converters.h"
//---------------------------------------------------------------------------
#include <algorithm>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWToml_File.h"
#include "ASWToml_Scalars.h"
#include "ASWToml_Utils.h"
#include "ASWToml_Writer.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

namespace
{

// The largest float plus half the step from it to the next float up, 2^128 - 2^103: a double from there on rounds to
// infinity, not to the largest float
constexpr double FloatRoundingLimit = 0x1.FFFFFFp127;
static_assert(FloatRoundingLimit - std::numeric_limits<float>::max() == 0x1p103, "half the step above FLT_MAX");

// The days from 1970-01-01 to 0000-01-01 and to 9999-12-31, the first and last dates TOML writes
constexpr std::int64_t FirstDay = -719528;
constexpr std::int64_t LastDay = 2932896;

// The keys of a table, each with its ASCII letters in lower case first (see RepeatedEnumKey())
using TFoldedKeyVector = std::vector<std::pair<std::string, std::string> >;

// The TOML texts of the elements of an array, each with its element's index (see SortByText())
using TTextVector = std::vector<std::pair<std::string, std::size_t> >;

char AsciiLower(char c) noexcept;
std::int64_t CountOfSeconds(std::int64_t seconds, Detail::TDurationUnit const& unit) noexcept;
std::string DateText(std::chrono::year_month_day const& date);
TASWTomlResult DaysToValue(std::int64_t days, TASWTomlValue& value);
std::string EnumNamesProblem(Detail::TNameVector const& names);
TASWTomlResult FailedKey(ETomlCode code, std::string_view key, std::string_view problem);
TASWTomlResult Failure(ETomlCode code, std::string message);
bool HeldExactly(std::int64_t number, int digits) noexcept;
bool KeyLess(TASWTomlKeyValue const& left, TASWTomlKeyValue const& right) noexcept;
std::string KeyPart(std::string_view key);
std::string LocalDateTimeEnd(std::int64_t count, Detail::TDurationUnit const& unit,
    TASWTomlLocalDateTime const& tomlEnd);
bool NearestFloat(double number, float& nearest) noexcept;
TASWTomlResult NotHeldExactly(TASWTomlValue const& value, int digits);
std::string OffsetDateTimeEnd(std::int64_t count, Detail::TDurationUnit const& unit,
    TASWTomlOffsetDateTime const& tomlEnd);
TASWTomlResult OutOfRange(std::string_view text, std::string_view min, std::string_view max);
std::string PrefixedPath(std::string part, std::string const& path);
std::string RangeProblem(std::string_view min, std::string_view max);
TASWTomlResult RealDateFromValue(TASWTomlValue const& value, std::chrono::year_month_day& date);
std::string Shortened(std::string text);
Detail::TWriteStyle TextStyle();
std::string ValidUtf8(std::string_view text);

//---------------------------------------------------------------------------
/*
    AsciiLower

    'c' as a lower-case letter when it is an ASCII upper-case one, else as it is
*/
char AsciiLower(char c) noexcept
{
    if (c >= 'A' && c <= 'Z')
        return static_cast<char>(c - 'A' + 'a');

    return c;
}
//---------------------------------------------------------------------------
/*
    CountOfSeconds

    The smallest count of 'unit' that is 'seconds' seconds (at most a day and a second) or more: seconds * Den / Num
    units, rounded up; 1 for a unit of that many seconds or more
*/
std::int64_t CountOfSeconds(std::int64_t seconds, Detail::TDurationUnit const& unit) noexcept
{
    std::int64_t const scaled = seconds * unit.Den;
    if (unit.Num >= scaled)
        return 1;

    return (scaled + unit.Num - 1) / unit.Num;
}
//---------------------------------------------------------------------------
/*
    DateText

    'date' as a message shows it, as TOML writes a date (YYYY-MM-DD) whether it is real or not: the year with at
    least four digits and its sign when it is negative, the month and the day with two
*/
std::string DateText(std::chrono::year_month_day const& date)
{
    int const year = static_cast<int>(date.year());
    std::string const digits = std::to_string(year < 0 ? -year : year);
    std::string text = year < 0 ? "-" : "";
    if (digits.size() < 4)
        text.append(4 - digits.size(), '0');

    text += digits;
    unsigned const fields[] = { static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()) };
    for (unsigned const field : fields)
    {
        text += field < 10 ? "-0" : "-";
        text += std::to_string(field);
    }

    return text;
}
//---------------------------------------------------------------------------
/*
    DaysToValue

    Sets 'value' to the date 'days' days after 1970-01-01 as a local date, or leaves it as it was and fails with
    ValueOutOfRange when that is outside the years 0 to 9999
*/
TASWTomlResult DaysToValue(std::int64_t days, TASWTomlValue& value)
{
    if (days < FirstDay || days > LastDay)
        return Detail::TimePointOutOfRange();

    value = TASWTomlValue(std::chrono::year_month_day(std::chrono::sys_days(std::chrono::days(days))));

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
/*
    EnumNamesProblem

    What a message says about a name that isn't one of 'names', the names an enum's values are written as: "isn't a
    name of the enum: it must be Red, Green or Blue.", or "isn't a name of the enum: it has no names." for none
*/
std::string EnumNamesProblem(Detail::TNameVector const& names)
{
    if (names.empty())
        return "isn't a name of the enum: it has no names.";

    std::string problem = "isn't a name of the enum: it must be ";
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        if (i > 0)
            problem += i + 1 < names.size() ? ", " : " or ";

        problem += names[i];
    }

    problem += '.';

    return problem;
}
//---------------------------------------------------------------------------
/*
    FailedKey

    A failed result with 'code' and the message "The key <key> <problem>", the key as a path shows it (see KeyPart()),
    cut as ValueText() cuts a value's text, e.g. FailedKey(InvalidValue, "a b", "isn't one word.")
*/
TASWTomlResult FailedKey(ETomlCode code, std::string_view key, std::string_view problem)
{
    std::string message = "The key ";
    message += Shortened(KeyPart(key));
    message += ' ';
    message += problem;

    return Failure(code, std::move(message));
}
//---------------------------------------------------------------------------
/*
    Failure

    A failed result with 'code' and 'message'
*/
TASWTomlResult Failure(ETomlCode code, std::string message)
{
    TASWTomlResult result;
    result.Code = code;
    result.Message = std::move(message);

    return result;
}
//---------------------------------------------------------------------------
/*
    HeldExactly

    Whether a floating-point type with 'digits' significant bits (53 for a double, 24 for a float) holds 'number'
    exactly: whether its significant bits, from its highest set bit to its lowest, are at most 'digits'
*/
bool HeldExactly(std::int64_t number, int digits) noexcept
{
    // The magnitude in unsigned arithmetic, so the smallest integer's negates without overflow; 0 has a bit width of 0
    // and 64 trailing zero bits, so it is held too
    std::uint64_t magnitude = static_cast<std::uint64_t>(number);
    if (number < 0)
        magnitude = 0 - magnitude;

    return static_cast<int>(std::bit_width(magnitude)) - std::countr_zero(magnitude) <= digits;
}
//---------------------------------------------------------------------------
/*
    KeyLess

    Whether the key of 'left' comes before that of 'right', byte by byte (see SortByKey())
*/
bool KeyLess(TASWTomlKeyValue const& left, TASWTomlKeyValue const& right) noexcept
{
    return left.Key < right.Key;
}
//---------------------------------------------------------------------------
/*
    KeyPart

    'key' as a part of a path: bare when it can be, else a basic string, each of its bytes that isn't valid UTF-8 as
    '?' (a key that isn't valid UTF-8 can't be written as TOML, and WriteKey() then appends nothing)
*/
std::string KeyPart(std::string_view key)
{
    std::string part;
    if (!Detail::WriteKey(key, part).IsOk())
        static_cast<void>(Detail::WriteKey(ValidUtf8(key), part));

    return part;
}
//---------------------------------------------------------------------------
/*
    LocalDateTimeEnd

    The text of the local date-time 'count' units of 'unit' after 1970-01-01T00:00:00, the first or last count of the
    unit, or, when that is outside the years 0 to 9999, of 'tomlEnd', TOML's first or last date-time, as the unit
    counts it: rounded down, or up when that is before the year 0
*/
std::string LocalDateTimeEnd(std::int64_t count, Detail::TDurationUnit const& unit,
    TASWTomlLocalDateTime const& tomlEnd)
{
    TASWTomlLocalDateTime dateTime;
    if (!Detail::LocalDateTimeFromCount(count, unit, dateTime))
    {
        static_cast<void>(Detail::LocalDateTimeToCount(tomlEnd, unit, count));
        if (!Detail::LocalDateTimeFromCount(count, unit, dateTime))
            static_cast<void>(Detail::LocalDateTimeFromCount(count + 1, unit, dateTime));
    }

    return Detail::ValueText(dateTime);
}
//---------------------------------------------------------------------------
/*
    NearestFloat

    Sets 'nearest' to the float nearest 'number' (inf and NaN with its sign, 0.0 with its sign for a number too small)
    and returns true, or returns false (and leaves 'nearest' as it was) for a finite number beyond float's range that
    doesn't round to its largest value. A cast is only defined between the largest floats, so this converts beyond
    them itself.
*/
bool NearestFloat(double number, float& nearest) noexcept
{
    float const sign = std::signbit(number) ? -1.0f : 1.0f;
    double const magnitude = std::fabs(number);
    if (std::isnan(number))
        nearest = std::copysign(std::numeric_limits<float>::quiet_NaN(), sign);
    else if (std::isinf(number))
        nearest = std::copysign(std::numeric_limits<float>::infinity(), sign);
    else if (magnitude >= FloatRoundingLimit)
        return false;
    else if (magnitude > std::numeric_limits<float>::max())
        nearest = std::copysign(std::numeric_limits<float>::max(), sign);
    else
        nearest = static_cast<float>(number);

    return true;
}
//---------------------------------------------------------------------------
/*
    NotHeldExactly

    The InvalidValue result of reading 'value', an integer, as a floating-point type with 'digits' significant bits
    that doesn't hold it exactly (see HeldExactly())
*/
TASWTomlResult NotHeldExactly(TASWTomlValue const& value, int digits)
{
    return Detail::ValueFailure(ETomlCode::InvalidValue, value, "can't be held exactly: it has more than " +
        std::to_string(digits) + " significant bits.");
}
//---------------------------------------------------------------------------
/*
    OffsetDateTimeEnd

    As LocalDateTimeEnd(), for an offset date-time in UTC: the text of the instant 'count' units of 'unit' after
    1970-01-01T00:00:00Z, or of 'tomlEnd' as the unit counts it
*/
std::string OffsetDateTimeEnd(std::int64_t count, Detail::TDurationUnit const& unit,
    TASWTomlOffsetDateTime const& tomlEnd)
{
    std::chrono::minutes const utc(0);
    TASWTomlOffsetDateTime dateTime;
    if (!Detail::OffsetDateTimeFromCount(count, unit, utc, dateTime))
    {
        static_cast<void>(Detail::OffsetDateTimeToCount(tomlEnd, unit, count));
        if (!Detail::OffsetDateTimeFromCount(count, unit, utc, dateTime))
            static_cast<void>(Detail::OffsetDateTimeFromCount(count + 1, unit, utc, dateTime));
    }

    return Detail::ValueText(dateTime);
}
//---------------------------------------------------------------------------
/*
    OutOfRange

    The ValueOutOfRange result of a number, shown as 'text', outside the range 'min' to 'max' (as text): "The value
    <text> is out of range: it must be from <min> to <max>."
*/
TASWTomlResult OutOfRange(std::string_view text, std::string_view min, std::string_view max)
{
    std::string message = "The value ";
    message += text;
    message += ' ';
    message += RangeProblem(min, max);

    return Failure(ETomlCode::ValueOutOfRange, std::move(message));
}
//---------------------------------------------------------------------------
/*
    PrefixedPath

    'path' (a path relative to a part of a value, maybe empty) after 'part' (the part's key or [index]): 'part' alone,
    'part' followed by 'path' when that starts with an index, else 'part', '.' and 'path'
*/
std::string PrefixedPath(std::string part, std::string const& path)
{
    if (!path.empty())
    {
        if (path.front() != '[')
            part += '.';

        part += path;
    }

    return part;
}
//---------------------------------------------------------------------------
/*
    RangeProblem

    What a message says about a number outside the range 'min' to 'max' (as text): "is out of range: it must be from
    <min> to <max>."
*/
std::string RangeProblem(std::string_view min, std::string_view max)
{
    std::string problem = "is out of range: it must be from ";
    problem += min;
    problem += " to ";
    problem += max;
    problem += '.';

    return problem;
}
//---------------------------------------------------------------------------
/*
    RealDateFromValue

    Sets 'date' to 'value', a local date, and returns Ok, or leaves it as it was and fails with TypeMismatch for a
    value of another type, InvalidValue for a date that isn't real
*/
TASWTomlResult RealDateFromValue(TASWTomlValue const& value, std::chrono::year_month_day& date)
{
    std::chrono::year_month_day const* const read = value.AsLocalDate();
    if (read == nullptr)
        return Detail::TypeMismatch(value, "a local date");

    if (!read->ok())
        return Detail::ValueFailure(ETomlCode::InvalidValue, value, "isn't a real date.");

    date = *read;

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
/*
    Shortened

    'text' as messages show a value or a key: cut to at most MaxValueTextSize bytes (not inside a UTF-8 sequence) and
    followed by "..." when it is longer
*/
std::string Shortened(std::string text)
{
    if (text.size() > Detail::MaxValueTextSize)
    {
        // Cut before a UTF-8 sequence, never inside one
        std::size_t cut = Detail::MaxValueTextSize;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
            --cut;

        text.resize(cut);
        text += "...";
    }

    return text;
}
//---------------------------------------------------------------------------
/*
    TextStyle

    The style values are written in for messages and for SortByText(): strings as basic strings, which escape every
    control character, nested as deep as a document's values may be
*/
Detail::TWriteStyle TextStyle()
{
    Detail::TWriteStyle style;
    style.Values.StringStyle = EStringStyle::Basic;
    style.MaxDepth = TASWTomlReadingOptions::MaxNestingDepthLimit;

    return style;
}
//---------------------------------------------------------------------------
/*
    ValidUtf8

    'text' with each byte that doesn't belong to a valid UTF-8 sequence as '?'
*/
std::string ValidUtf8(std::string_view text)
{
    std::string valid;
    std::size_t invalid = Detail::FindInvalidUtf8(text);
    while (invalid != std::string_view::npos)
    {
        valid += text.substr(0, invalid);
        valid += '?';
        text.remove_prefix(invalid + 1);
        invalid = Detail::FindInvalidUtf8(text);
    }

    valid += text;

    return valid;
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
TASWTomlResult Detail::DateTimeOutOfRange(TASWTomlValue const& value, TDurationUnit const& unit)
{
    // The first and last values the unit's count holds, each as far as TOML's first and last values go: 0000-01-01 at
    // midnight, the last nanosecond of 9999-12-31, and for a time of day midnight and the last nanosecond of the leap
    // second 23:59:60
    TASWTomlLocalDateTime const firstDateTime{ std::chrono::year(0) / std::chrono::January / 1, TASWTomlLocalTime() };
    TASWTomlLocalDateTime const lastDateTime{ std::chrono::year(9999) / std::chrono::December / 31,
        TASWTomlLocalTime{ std::chrono::hours(23), std::chrono::minutes(59), std::chrono::seconds(59),
            std::chrono::nanoseconds(999999999) } };
    std::string first;
    std::string last;
    if (value.IsOffsetDateTime())
    {
        std::chrono::minutes const utc(0);
        first = OffsetDateTimeEnd(unit.Min, unit, TASWTomlOffsetDateTime{ firstDateTime.Date, firstDateTime.Time, utc });
        last = OffsetDateTimeEnd(unit.Max, unit, TASWTomlOffsetDateTime{ lastDateTime.Date, lastDateTime.Time, utc });
    }
    else if (value.IsLocalDateTime())
    {
        first = LocalDateTimeEnd(unit.Min, unit, firstDateTime);
        last = LocalDateTimeEnd(unit.Max, unit, lastDateTime);
    }
    else
    {
        TASWTomlLocalTime time;
        if (!TimeOfDayFromCount(unit.Max, unit, time))
            static_cast<void>(TimeOfDayFromCount(CountOfSeconds(86401, unit) - 1, unit, time));

        first = "00:00:00";
        last = ValueText(time);
    }

    return OutOfRange(ValueText(value), first, last);
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::ElementFailure(TASWTomlResult result, std::size_t index)
{
    result.Path = PrefixedPath("[" + std::to_string(index) + "]", result.Path);

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::EnumNameToValue(std::string_view name, TASWTomlValue& value)
{
    return TASWTomlConverter<std::string>::ToValue(std::string(name), value, TASWTomlValueOptions());
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::IntegerFromKey(std::string_view key, std::int64_t min, std::uint64_t max, std::uint64_t& bits)
{
    // Only the form a key is written in, so each number has one key: 0, or digits that don't start with 0 after an
    // optional '-'
    std::string_view digits = key;
    bool const negative = !digits.empty() && digits.front() == '-';
    if (negative)
        digits.remove_prefix(1);

    bool const allDigits = !digits.empty() && digits.find_first_not_of("0123456789") == std::string_view::npos;
    bool const leadingZero = allDigits && digits.front() == '0' && (negative || digits.size() > 1);
    if (!allDigits || leadingZero)
    {
        return FailedKey(ETomlCode::InvalidValue, key,
            "isn't a decimal integer (such as 80 or -1, without a + or leading zeros).");
    }

    // The largest magnitude on the number's side of 0: that of a negative 'min' is one more than that of -(min + 1),
    // which can't overflow, and an unsigned type's 'min' of 0 allows none
    std::uint64_t limit = max;
    if (negative)
        limit = static_cast<std::uint64_t>(-(min + 1)) + 1;

    std::uint64_t magnitude = 0;
    std::from_chars_result const read = std::from_chars(digits.data(), digits.data() + digits.size(), magnitude);
    if (read.ec != std::errc() || magnitude > limit)
        return FailedKey(ETomlCode::ValueOutOfRange, key, RangeProblem(std::to_string(min), std::to_string(max)));

    bits = negative ? 0 - magnitude : magnitude;

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::IntegerFromValue(TASWTomlValue const& value, std::int64_t min, std::uint64_t max,
    TASWTomlValueOptions const& options, std::int64_t& out)
{
    std::int64_t const* const number = value.AsInteger();
    if (number == nullptr)
        return TypeMismatch(value, "an integer");

    std::int64_t const highest = static_cast<std::int64_t>(std::min<std::uint64_t>(max,
        std::numeric_limits<std::int64_t>::max()));
    if (*number < min || *number > highest)
    {
        return OutOfRange(ValueText(value, options), ValueText(TASWTomlValue(min), options),
            ValueText(TASWTomlValue(highest), options));
    }

    out = *number;

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::InvalidEnumKey(std::string_view key, TNameVector const& names)
{
    return FailedKey(ETomlCode::InvalidValue, key, EnumNamesProblem(names));
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::InvalidEnumName(TASWTomlValue const& value, TNameVector const& names)
{
    return ValueFailure(ETomlCode::InvalidValue, value, EnumNamesProblem(names));
}
//---------------------------------------------------------------------------
bool Detail::IsEnumName(std::string_view text, std::string_view name) noexcept
{
    if (name.empty() || text.size() != name.size())
        return false;

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (AsciiLower(text[i]) != AsciiLower(name[i]))
            return false;
    }

    return true;
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::KeyFailure(TASWTomlResult result, std::string_view key)
{
    result.Path = PrefixedPath(KeyPart(key), result.Path);

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::LengthMismatch(TASWTomlValue const& value, std::size_t length)
{
    std::size_t const size = value.AsArray()->size();
    std::string problem = "has " + std::to_string(size);
    problem += size == 1 ? " element, not " : " elements, not ";
    problem += std::to_string(length);
    problem += '.';

    return ValueFailure(ETomlCode::InvalidValue, value, problem);
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::MissingKey(std::string_view key)
{
    return Failure(ETomlCode::NotFound, "The table has no key " + Shortened(KeyPart(key)) + ".");
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::RepeatedElement(TASWTomlValue const& element)
{
    return ValueFailure(ETomlCode::InvalidValue, element, "reads as an element the set already has.");
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::RepeatedEnumKey(TASWTomlValue::TKeyValueVector const& table)
{
    // The keys with their letters in lower case, each with the key itself, sorted, so two that are the same are next
    // to each other in byte order, whatever the order of the table (an unordered map's isn't fixed)
    TFoldedKeyVector keys;
    keys.reserve(table.size());
    for (TASWTomlKeyValue const& entry : table)
    {
        std::string folded = entry.Key;
        for (char& c : folded)
            c = AsciiLower(c);

        keys.emplace_back(std::move(folded), entry.Key);
    }

    std::sort(keys.begin(), keys.end());
    for (std::size_t i = 1; i < keys.size(); ++i)
    {
        if (keys[i].first == keys[i - 1].first)
        {
            return FailedKey(ETomlCode::InvalidValue, keys[i].second, "names two values of the enum, which would read "
                "back as one: each value needs a name of its own in ASWTomlEnumNames().");
        }
    }

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::RepeatedFieldKey(TFieldKeyVector const& keys)
{
    // A struct has a few fields, so each key is compared with those before it
    for (std::size_t i = 1; i < keys.size(); ++i)
    {
        for (std::size_t j = 0; j < i; ++j)
        {
            if (keys[j] == keys[i])
            {
                return FailedKey(ETomlCode::InvalidValue, keys[i], "is named by two fields of the struct's "
                    "ASWTomlFields(): each field needs a key of its own.");
            }
        }
    }

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::RepeatedKey(std::string_view key)
{
    return FailedKey(ETomlCode::InvalidValue, key, "reads as a key the map already has.");
}
//---------------------------------------------------------------------------
void Detail::SortByKey(TASWTomlValue::TKeyValueVector& table) noexcept
{
    std::sort(table.begin(), table.end(), KeyLess);
}
//---------------------------------------------------------------------------
void Detail::SortByText(TASWTomlValue::TValueVector& elements)
{
    // The texts first, then the elements moved in their order, so a std::bad_alloc leaves 'elements' as they were;
    // two elements with the same text are the same TOML value, so their order doesn't show
    TWriteStyle const style = TextStyle();
    TTextVector texts;
    texts.reserve(elements.size());
    for (std::size_t i = 0; i < elements.size(); ++i)
    {
        std::string text;
        static_cast<void>(WriteValue(elements[i], style, text));
        texts.emplace_back(std::move(text), i);
    }

    std::sort(texts.begin(), texts.end());
    TASWTomlValue::TValueVector sorted;
    sorted.reserve(elements.size());
    for (TTextVector::value_type const& text : texts)
        sorted.push_back(std::move(elements[text.second]));

    elements = std::move(sorted);
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::TextToKey(std::string_view text, std::string& key)
{
    std::size_t const invalid = FindInvalidUtf8(text);
    if (invalid != std::string_view::npos)
    {
        return Failure(ETomlCode::InvalidValue, "The key can't be written: it isn't valid UTF-8 (at byte " +
            std::to_string(invalid) + "), which TOML needs.");
    }

    key = text;

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
bool Detail::TimeOfDayFromCount(std::int64_t count, TDurationUnit const& unit, TASWTomlLocalTime& time) noexcept
{
    std::int64_t const day = CountOfSeconds(86400, unit);
    if (count < 0 || count >= CountOfSeconds(86401, unit))
        return false;

    // A count from a day on is less than a day and a second, so taken modulo a day it is that second's fraction
    TASWTomlLocalTime result;
    LocalTimeFromCount(count, unit, result);
    if (count >= day)
    {
        result.Hour = std::chrono::hours(23);
        result.Minute = std::chrono::minutes(59);
        result.Second = std::chrono::seconds(60);
    }

    time = result;

    return true;
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::TimeOfDayOutOfRange()
{
    return Failure(ETomlCode::ValueOutOfRange, "The time of day can't be written: it must be at least 0 and less "
        "than a day and a second (24:00:00 is written as the leap second 23:59:60).");
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::TimePointOutOfRange()
{
    return Failure(ETomlCode::ValueOutOfRange, "The time point can't be written: TOML's years are from 0 to 9999.");
}
//---------------------------------------------------------------------------
char const* Detail::TypeName(TASWTomlValue const& value) noexcept
{
    switch (value.GetType())
    {
        case ETomlType::String:
            return "a string";

        case ETomlType::Integer:
            return "an integer";

        case ETomlType::Float:
            return "a float";

        case ETomlType::Boolean:
            return "a boolean";

        case ETomlType::OffsetDateTime:
            return "an offset date-time";

        case ETomlType::LocalDateTime:
            return "a local date-time";

        case ETomlType::LocalDate:
            return "a local date";

        case ETomlType::LocalTime:
            return "a local time";

        case ETomlType::Array:
            return "an array";

        case ETomlType::Table:
            return "a table";
    }

    return "a value";
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::TypeMismatch(TASWTomlValue const& value, std::string_view wanted)
{
    std::string problem = "is ";
    problem += TypeName(value);
    problem += ", not ";
    problem += wanted;
    problem += '.';

    return ValueFailure(ETomlCode::TypeMismatch, value, problem);
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::UnnamedEnumValue(std::string_view number)
{
    std::string message = "The value ";
    message += number;
    message += " has no name in the enum's ASWTomlEnumNames(), so it can't be written.";

    return Failure(ETomlCode::InvalidValue, std::move(message));
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::UnsignedIntegerToValue(std::uint64_t number, TASWTomlValue& value)
{
    constexpr std::int64_t Max = std::numeric_limits<std::int64_t>::max();
    if (number > static_cast<std::uint64_t>(Max))
        return OutOfRange(std::to_string(number), "0", std::to_string(Max));

    value = TASWTomlValue(static_cast<std::int64_t>(number));

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::ValueFailure(ETomlCode code, TASWTomlValue const& value, std::string_view problem)
{
    return ValueFailure(code, value, problem, TASWTomlValueOptions());
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::ValueFailure(ETomlCode code, TASWTomlValue const& value, std::string_view problem,
    TASWTomlValueOptions const& options)
{
    std::string message = "The value ";
    message += ValueText(value, options);
    message += ' ';
    message += problem;

    return Failure(code, std::move(message));
}
//---------------------------------------------------------------------------
std::string Detail::ValueText(TASWTomlValue const& value)
{
    return ValueText(value, TASWTomlValueOptions());
}
//---------------------------------------------------------------------------
std::string Detail::ValueText(TASWTomlValue const& value, TASWTomlValueOptions const& options)
{
    Detail::TWriteStyle style = TextStyle();
    style.Values.IntegerBase = options.IntegerBase;
    std::string text;
    if (!WriteValue(value, style, text).IsOk())
        return std::string("of type ") + ToString(value.GetType());

    return Shortened(std::move(text));
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<TASWTomlLocalDateTime>
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<TASWTomlLocalDateTime>::FromValue(TASWTomlValue const& value,
    TASWTomlLocalDateTime& out, TASWTomlValueOptions const& /*options*/)
{
    TASWTomlLocalDateTime const* const dateTime = value.AsLocalDateTime();
    if (dateTime == nullptr)
        return Detail::TypeMismatch(value, "a local date-time");

    out = *dateTime;

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<TASWTomlLocalDateTime>::ToValue(TASWTomlLocalDateTime const& in,
    TASWTomlValue& value, TASWTomlValueOptions const& /*options*/)
{
    // Written once, as a document writes it, to check that it can be
    std::string text;
    TASWTomlResult const result = Detail::WriteLocalDateTime(in, text);
    if (result.IsOk())
        value = TASWTomlValue(in);

    return result;
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<TASWTomlLocalTime>
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<TASWTomlLocalTime>::FromValue(TASWTomlValue const& value, TASWTomlLocalTime& out,
    TASWTomlValueOptions const& /*options*/)
{
    TASWTomlLocalTime const* const time = value.AsLocalTime();
    if (time == nullptr)
        return Detail::TypeMismatch(value, "a local time");

    out = *time;

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<TASWTomlLocalTime>::ToValue(TASWTomlLocalTime const& in, TASWTomlValue& value,
    TASWTomlValueOptions const& /*options*/)
{
    // Written once, as a document writes it, to check that it can be
    std::string text;
    TASWTomlResult const result = Detail::WriteLocalTime(in, text);
    if (result.IsOk())
        value = TASWTomlValue(in);

    return result;
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<TASWTomlOffsetDateTime>
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<TASWTomlOffsetDateTime>::FromValue(TASWTomlValue const& value,
    TASWTomlOffsetDateTime& out, TASWTomlValueOptions const& /*options*/)
{
    TASWTomlOffsetDateTime const* const dateTime = value.AsOffsetDateTime();
    if (dateTime == nullptr)
        return Detail::TypeMismatch(value, "an offset date-time");

    out = *dateTime;

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<TASWTomlOffsetDateTime>::ToValue(TASWTomlOffsetDateTime const& in,
    TASWTomlValue& value, TASWTomlValueOptions const& /*options*/)
{
    // Written once, as a document writes it, to check that it can be
    std::string text;
    TASWTomlResult const result = Detail::WriteOffsetDateTime(in, text);
    if (result.IsOk())
        value = TASWTomlValue(in);

    return result;
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<TASWTomlValue>
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<TASWTomlValue>::FromValue(TASWTomlValue const& value, TASWTomlValue& out,
    TASWTomlValueOptions const& /*options*/)
{
    // Copied first, so 'out' is unchanged if the copy throws
    TASWTomlValue copy = value;
    out = std::move(copy);

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<TASWTomlValue>::ToValue(TASWTomlValue const& in, TASWTomlValue& value,
    TASWTomlValueOptions const& /*options*/)
{
    TASWTomlValue copy = in;
    value = std::move(copy);

    return TASWTomlResult();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<bool>
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<bool>::FromValue(TASWTomlValue const& value, bool& out,
    TASWTomlValueOptions const& /*options*/)
{
    bool const* const flag = value.AsBoolean();
    if (flag == nullptr)
        return Detail::TypeMismatch(value, "a boolean");

    out = *flag;

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<bool>::ToValue(bool const& in, TASWTomlValue& value,
    TASWTomlValueOptions const& /*options*/)
{
    value = TASWTomlValue(in);

    return TASWTomlResult();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<double>
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<double>::FromValue(TASWTomlValue const& value, double& out,
    TASWTomlValueOptions const& /*options*/)
{
    double const* const number = value.AsFloat();
    if (number != nullptr)
    {
        out = *number;
        return TASWTomlResult();
    }

    std::int64_t const* const integer = value.AsInteger();
    if (integer == nullptr)
        return Detail::TypeMismatch(value, "a float or an integer");

    if (!HeldExactly(*integer, std::numeric_limits<double>::digits))
        return NotHeldExactly(value, std::numeric_limits<double>::digits);

    out = static_cast<double>(*integer);

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<double>::ToValue(double const& in, TASWTomlValue& value,
    TASWTomlValueOptions const& /*options*/)
{
    value = TASWTomlValue(in);

    return TASWTomlResult();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<float>
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<float>::FromValue(TASWTomlValue const& value, float& out,
    TASWTomlValueOptions const& options)
{
    double const* const number = value.AsFloat();
    if (number != nullptr)
    {
        float nearest = 0.0f;
        if (!NearestFloat(*number, nearest))
        {
            // The range as the largest float is written
            TASWTomlValue largest;
            static_cast<void>(ToValue(std::numeric_limits<float>::max(), largest, options));
            std::string const max = Detail::ValueText(largest);

            return OutOfRange(Detail::ValueText(value), "-" + max, max);
        }

        out = nearest;

        return TASWTomlResult();
    }

    std::int64_t const* const integer = value.AsInteger();
    if (integer == nullptr)
        return Detail::TypeMismatch(value, "a float or an integer");

    if (!HeldExactly(*integer, std::numeric_limits<float>::digits))
        return NotHeldExactly(value, std::numeric_limits<float>::digits);

    out = static_cast<float>(*integer);

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<float>::ToValue(float const& in, TASWTomlValue& value,
    TASWTomlValueOptions const& /*options*/)
{
    // Exact; inf and NaN keep their sign
    double written = static_cast<double>(in);
    if (std::isfinite(in))
    {
        // The double nearest the float's shortest text (at most 15 characters, e.g. -1.17549435e-38), when that reads
        // back as the same float
        char buffer[32];
        std::to_chars_result const text = std::to_chars(buffer, buffer + sizeof(buffer), in);
        double shortest = 0.0;
        float readBack = 0.0f;
        if (std::from_chars(buffer, text.ptr, shortest).ec == std::errc() && NearestFloat(shortest, readBack))
        {
            if (readBack == in)
                written = shortest;
        }
    }

    value = TASWTomlValue(written);

    return TASWTomlResult();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<std::chrono::local_days>
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<std::chrono::local_days>::FromValue(TASWTomlValue const& value,
    std::chrono::local_days& out, TASWTomlValueOptions const& /*options*/)
{
    std::chrono::year_month_day date{};
    TASWTomlResult const result = RealDateFromValue(value, date);
    if (result.IsOk())
        out = std::chrono::local_days(date);

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<std::chrono::local_days>::ToValue(std::chrono::local_days const& in,
    TASWTomlValue& value, TASWTomlValueOptions const& /*options*/)
{
    return DaysToValue(static_cast<std::int64_t>(in.time_since_epoch().count()), value);
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<std::chrono::sys_days>
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<std::chrono::sys_days>::FromValue(TASWTomlValue const& value,
    std::chrono::sys_days& out, TASWTomlValueOptions const& /*options*/)
{
    std::chrono::year_month_day date{};
    TASWTomlResult const result = RealDateFromValue(value, date);
    if (result.IsOk())
        out = std::chrono::sys_days(date);

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<std::chrono::sys_days>::ToValue(std::chrono::sys_days const& in,
    TASWTomlValue& value, TASWTomlValueOptions const& /*options*/)
{
    return DaysToValue(static_cast<std::int64_t>(in.time_since_epoch().count()), value);
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<std::chrono::year_month_day>
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<std::chrono::year_month_day>::FromValue(TASWTomlValue const& value,
    std::chrono::year_month_day& out, TASWTomlValueOptions const& /*options*/)
{
    std::chrono::year_month_day const* const date = value.AsLocalDate();
    if (date == nullptr)
        return Detail::TypeMismatch(value, "a local date");

    out = *date;

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<std::chrono::year_month_day>::ToValue(std::chrono::year_month_day const& in,
    TASWTomlValue& value, TASWTomlValueOptions const& /*options*/)
{
    if (!in.ok())
        return Failure(ETomlCode::InvalidValue, "The date " + DateText(in) + " can't be written: it isn't a real date.");

    if (in.year() < std::chrono::year(0) || in.year() > std::chrono::year(9999))
    {
        return Failure(ETomlCode::ValueOutOfRange, "The date " + DateText(in) +
            " can't be written: TOML's years are from 0 to 9999.");
    }

    value = TASWTomlValue(in);

    return TASWTomlResult();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<std::filesystem::path>
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<std::filesystem::path>::FromValue(TASWTomlValue const& value,
    std::filesystem::path& out, TASWTomlValueOptions const& /*options*/)
{
    std::string const* const text = value.AsString();
    if (text == nullptr)
        return Detail::TypeMismatch(value, "a string");

    // Windows would make each invalid sequence U+FFFD, and a POSIX system would take the bytes as they are
    if (!Detail::IsValidUtf8(*text))
        return Detail::ValueFailure(ETomlCode::InvalidValue, value, "isn't a path: it isn't valid UTF-8.");

    out = Utf8ToPath(*text);

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<std::filesystem::path>::ToValue(std::filesystem::path const& in,
    TASWTomlValue& value, TASWTomlValueOptions const& /*options*/)
{
    std::string text = PathToUtf8(in);
    if (!Detail::IsValidUtf8(text) || Utf8ToPath(text) != in)
    {
        return Failure(ETomlCode::InvalidValue,
            "The path can't be written: it isn't valid Unicode, so it wouldn't read back as the same path.");
    }

    value = TASWTomlValue(std::move(text));

    return TASWTomlResult();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<std::string>
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<std::string>::FromValue(TASWTomlValue const& value, std::string& out,
    TASWTomlValueOptions const& /*options*/)
{
    std::string const* const text = value.AsString();
    if (text == nullptr)
        return Detail::TypeMismatch(value, "a string");

    out = *text;

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<std::string>::ToValue(std::string const& in, TASWTomlValue& value,
    TASWTomlValueOptions const& /*options*/)
{
    std::size_t const invalid = Detail::FindInvalidUtf8(in);
    if (invalid != std::string_view::npos)
    {
        return Failure(ETomlCode::InvalidValue, "The string can't be written: it isn't valid UTF-8 (at byte " +
            std::to_string(invalid) + "), which TOML needs.");
    }

    value = TASWTomlValue(in);

    return TASWTomlResult();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<std::u8string>
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<std::u8string>::FromValue(TASWTomlValue const& value, std::u8string& out,
    TASWTomlValueOptions const& /*options*/)
{
    std::string const* const text = value.AsString();
    if (text == nullptr)
        return Detail::TypeMismatch(value, "a string");

    out.assign(text->begin(), text->end());

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<std::u8string>::ToValue(std::u8string const& in, TASWTomlValue& value,
    TASWTomlValueOptions const& options)
{
    return TASWTomlConverter<std::string>::ToValue(std::string(in.begin(), in.end()), value, options);
}
//---------------------------------------------------------------------------

} // namespace ASWToml
