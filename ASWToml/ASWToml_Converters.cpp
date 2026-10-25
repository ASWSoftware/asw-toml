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
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
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

TASWTomlResult Failure(ETomlCode code, std::string message);
bool HeldExactly(std::int64_t number, int digits) noexcept;
bool NearestFloat(double number, float& nearest) noexcept;
TASWTomlResult NotHeldExactly(TASWTomlValue const& value, int digits);
TASWTomlResult OutOfRange(std::string_view text, std::string_view min, std::string_view max);
std::string PrefixedPath(std::string part, std::string const& path);
std::string ValidUtf8(std::string_view text);

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
    OutOfRange

    The ValueOutOfRange result of a number, shown as 'text', outside the range 'min' to 'max' (as text): "The value
    <text> is out of range: it must be from <min> to <max>."
*/
TASWTomlResult OutOfRange(std::string_view text, std::string_view min, std::string_view max)
{
    std::string message = "The value ";
    message += text;
    message += " is out of range: it must be from ";
    message += min;
    message += " to ";
    message += max;
    message += '.';

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
TASWTomlResult Detail::ElementFailure(TASWTomlResult result, std::size_t index)
{
    result.Path = PrefixedPath("[" + std::to_string(index) + "]", result.Path);

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::IntegerFromValue(TASWTomlValue const& value, std::int64_t min, std::uint64_t max,
    std::int64_t& out)
{
    std::int64_t const* const number = value.AsInteger();
    if (number == nullptr)
        return TypeMismatch(value, "an integer");

    std::int64_t const highest = static_cast<std::int64_t>(std::min<std::uint64_t>(max,
        std::numeric_limits<std::int64_t>::max()));
    if (*number < min || *number > highest)
        return OutOfRange(ValueText(value), std::to_string(min), std::to_string(highest));

    out = *number;

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::KeyFailure(TASWTomlResult result, std::string_view key)
{
    // A key that isn't valid UTF-8 can't be written as TOML (WriteKey() then appends nothing), so its bytes that
    // aren't are shown as '?'
    std::string part;
    if (!WriteKey(key, part).IsOk())
        static_cast<void>(WriteKey(ValidUtf8(key), part));

    result.Path = PrefixedPath(std::move(part), result.Path);

    return result;
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
    std::string message = "The value ";
    message += ValueText(value);
    message += ' ';
    message += problem;

    return Failure(code, std::move(message));
}
//---------------------------------------------------------------------------
std::string Detail::ValueText(TASWTomlValue const& value)
{
    // Strings as basic strings, which escape every control character; nested as deep as a document's values may be
    TWriteStyle style;
    style.Values.StringStyle = EStringStyle::Basic;
    style.MaxDepth = TASWTomlReadingOptions::MaxNestingDepthLimit;

    std::string text;
    if (!WriteValue(value, style, text).IsOk())
        return std::string("of type ") + ToString(value.GetType());

    if (text.size() > MaxValueTextSize)
    {
        // Cut before a UTF-8 sequence, never inside one
        std::size_t cut = MaxValueTextSize;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
            --cut;

        text.resize(cut);
        text += "...";
    }

    return text;
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
