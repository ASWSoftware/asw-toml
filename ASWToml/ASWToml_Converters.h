/* **************************************************************************
ASWToml_Converters.h
Author: Anthony S. West - ASW Software

A TOML file reader/writer library.

TASWTomlConverter<T>, which converts a value of an application's type T to
and from a TASWTomlValue, and the concept TomlConvertible, which says that a
type has one. The library has converters for TASWTomlValue itself,
std::string, std::u8string, std::filesystem::path, bool, the integer types,
float and double; an application adds its own by specializing
TASWTomlConverter for its type.

Requires C++ 20 or higher.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

#pragma once

#ifndef ASWToml_ConvertersH
#define ASWToml_ConvertersH
//---------------------------------------------------------------------------
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
//---------------------------------------------------------------------------
#include "ASWToml_Options.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter
//
// Converts a value of type T to and from a TASWTomlValue, e.g. the value of a key of a document. A converter has two
// static functions:
//
//     static TASWTomlResult FromValue(TASWTomlValue const& value, T& out, TASWTomlValueOptions const& options);
//     static TASWTomlResult ToValue(T const& in, TASWTomlValue& value, TASWTomlValueOptions const& options);
//
// FromValue() reads 'value' into 'out'; ToValue() sets 'value' (whatever it held) to 'in'. On failure they leave 'out'
// or 'value' as it was, so a caller can set a default first. A value of a TOML type that T doesn't read fails with
// ETomlCode::TypeMismatch, a number outside T's range with ValueOutOfRange, and a value T doesn't accept (or one that
// TOML can't hold, so it couldn't be written) with InvalidValue, each with a message that shows the value as TOML
// text. A failure inside a value (an element of an array, a key of a table) has the place in its result's Path,
// relative to the value converted, e.g. [1].port. The options are passed on to the converters of the parts of a value,
// for converters that need them; the library's own converters don't read them yet. Nothing throws but
// std::bad_alloc, and then 'out' or 'value' is as it was.
// An application adds a converter for its own type by specializing the template in namespace ASWToml, for one type or,
// as a partial specialization with a requires clause, for a group of types; a full specialization wins over a partial
// one. Declare it before the first use of TASWTomlConverter<T> or TomlConvertible<T> for the type, since a concept's
// result for a type may be kept. The template itself is only declared, so a type without a converter is a compile
// error.
/////////////////////////////////////////////////////////////////////////////
template <typename T>
struct TASWTomlConverter;

//---------------------------------------------------------------------------
// Internals of the library: the helpers of the converters' templates. Not part of the public interface.
//---------------------------------------------------------------------------
namespace Detail
{

// The types TASWTomlConverter<T>'s FromValue() and ToValue() return, called as TASWTomlConverter describes them (for
// TomlConvertible)
template <typename T>
using TFromValueResult = decltype(TASWTomlConverter<T>::FromValue(std::declval<TASWTomlValue const&>(),
    std::declval<T&>(), std::declval<TASWTomlValueOptions const&>()));
template <typename T>
using TToValueResult = decltype(TASWTomlConverter<T>::ToValue(std::declval<T const&>(),
    std::declval<TASWTomlValue&>(), std::declval<TASWTomlValueOptions const&>()));

// The longest part of a value's text a message shows (see ValueText())
constexpr std::size_t MaxValueTextSize = 40;

// Whether T is one of the integer types TASWTomlConverter converts as TOML integers: an integral type of at most 64
// bits, without const or volatile, other than bool, plain char and the wide and Unicode character types
template <typename T>
concept ConvertibleInteger = std::integral<T> && std::same_as<T, std::remove_cv_t<T> > &&
    sizeof(T) <= sizeof(std::int64_t) && !std::same_as<T, bool> && !std::same_as<T, char> &&
    !std::same_as<T, wchar_t> && !std::same_as<T, char8_t> && !std::same_as<T, char16_t> && !std::same_as<T, char32_t>;

// 'result' (a failure of a part of a value) as the failure of the array that holds it as its element 'index':
// "[index]" put in front of its Path, e.g. [2] or [2].port
TASWTomlResult ElementFailure(TASWTomlResult result, std::size_t index);

// Reads 'value' as an integer type with the range 'min' to 'max' reads it (see TASWTomlConverter<integer types>)
// into 'out', which is left as it was on failure: TypeMismatch for a value that isn't an integer, ValueOutOfRange
// outside the range, whose message gives it ('max' as at most INT64_MAX, the largest TOML integer)
TASWTomlResult IntegerFromValue(TASWTomlValue const& value, std::int64_t min, std::uint64_t max, std::int64_t& out);

// 'result' (a failure of a part of a value) as the failure of the table that holds it at 'key': the key, bare when it
// can be, else a basic string (each byte of it that isn't valid UTF-8 as '?'), put in front of its Path, e.g. port,
// "host name" or servers[2]
TASWTomlResult KeyFailure(TASWTomlResult result, std::string_view key);

// The TOML type of 'value' with an article, as messages name it: "a string", "an integer", "a float", "a boolean", "an
// offset date-time", "a local date-time", "a local date", "a local time", "an array" or "a table"
char const* TypeName(TASWTomlValue const& value) noexcept;

// The TypeMismatch result of reading 'value' as a type that reads 'wanted' (e.g. "a string"): "The value 5 is an
// integer, not a string."
TASWTomlResult TypeMismatch(TASWTomlValue const& value, std::string_view wanted);

// Sets 'value' to 'number' as an unsigned integer type writes it (see TASWTomlConverter<integer types>), or leaves it
// as it was and fails with ValueOutOfRange when 'number' is above INT64_MAX, which a TOML integer can't hold
TASWTomlResult UnsignedIntegerToValue(std::uint64_t number, TASWTomlValue& value);

// A failed result with 'code' and the message "The value <text> <problem>", the text as ValueText() gives it, e.g.
// ValueFailure(InvalidValue, value, "isn't one of the names: Red, Green, Blue.")
TASWTomlResult ValueFailure(ETomlCode code, TASWTomlValue const& value, std::string_view problem);

// 'value' as messages show it: as TOML text on one line (a string as a basic string, its control characters escaped),
// cut to at most MaxValueTextSize bytes (not inside a UTF-8 sequence) followed by "..." when it is longer; "of type
// <type>" (e.g. of type String) for a value that can't be written, such as a string that isn't valid UTF-8
std::string ValueText(TASWTomlValue const& value);

} // namespace Detail

// Whether T has a converter: TASWTomlConverter<T> is defined, with FromValue() and ToValue() as TASWTomlConverter
// describes them
template <typename T>
concept TomlConvertible = std::same_as<Detail::TFromValueResult<T>, TASWTomlResult> &&
    std::same_as<Detail::TToValueResult<T>, TASWTomlResult>;


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<TASWTomlValue>
//
// The value as it is, both ways: it never fails (but for std::bad_alloc), so a value of any type can be read and
// written, e.g. a table an application reads itself
/////////////////////////////////////////////////////////////////////////////
template <>
struct TASWTomlConverter<TASWTomlValue>
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, TASWTomlValue& out,
        TASWTomlValueOptions const& options);
    static TASWTomlResult ToValue(TASWTomlValue const& in, TASWTomlValue& value, TASWTomlValueOptions const& options);
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<bool>
//
// A boolean, both ways; any other type (1, "true") is TypeMismatch
/////////////////////////////////////////////////////////////////////////////
template <>
struct TASWTomlConverter<bool>
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, bool& out, TASWTomlValueOptions const& options);
    static TASWTomlResult ToValue(bool const& in, TASWTomlValue& value, TASWTomlValueOptions const& options);
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<double>
//
// Reads a float as it is, or an integer that a double holds exactly: one with at most 53 significant bits (from its
// highest set bit to its lowest), so every integer from -2^53 to 2^53 and larger ones with enough trailing zero bits;
// ratio = 1 reads as 1.0. Another integer is InvalidValue, any other type TypeMismatch. Writes a float; inf and nan
// keep their sign.
/////////////////////////////////////////////////////////////////////////////
template <>
struct TASWTomlConverter<double>
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, double& out, TASWTomlValueOptions const& options);
    static TASWTomlResult ToValue(double const& in, TASWTomlValue& value, TASWTomlValueOptions const& options);
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<float>
//
// As TASWTomlConverter<double>, with float's range and precision. A TOML float (a double) reads as the nearest float,
// 0.0 for one too small (with its sign); one beyond float's range that doesn't round to its largest value is
// ValueOutOfRange. An integer must have at most 24 significant bits. A float is written as the double nearest its
// shortest text, so 0.1f is written as 0.1, not 0.10000000149011612, and reads back as the same float; the one pair
// of floats for which that double would read back as another float, 7.038531e-26 and its negative, is written as its
// exact value instead. inf and nan keep their sign.
/////////////////////////////////////////////////////////////////////////////
template <>
struct TASWTomlConverter<float>
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, float& out, TASWTomlValueOptions const& options);
    static TASWTomlResult ToValue(float const& in, TASWTomlValue& value, TASWTomlValueOptions const& options);
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<integer types>
//
// Every integral type of at most 64 bits but bool, plain char and the wide and Unicode character types (wchar_t,
// char8_t, char16_t, char32_t), so signed char and unsigned char (std::int8_t, std::uint8_t) are numbers. Reads an
// integer only (3.0 is a float, so TypeMismatch); one outside the type's range is ValueOutOfRange, with the range in
// the message: "The value 300 is out of range: it must be from 0 to 255.". Writes an integer. TOML integers are 64-bit
// signed, so an unsigned 64-bit type's range is 0 to 9223372036854775807 (INT64_MAX) both ways: a larger value can't
// be written (ValueOutOfRange).
/////////////////////////////////////////////////////////////////////////////
template <Detail::ConvertibleInteger T>
struct TASWTomlConverter<T>
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, T& out, TASWTomlValueOptions const& /*options*/)
    {
        std::int64_t number = 0;
        TASWTomlResult result = Detail::IntegerFromValue(value, static_cast<std::int64_t>(std::numeric_limits<T>::min()),
            static_cast<std::uint64_t>(std::numeric_limits<T>::max()), number);
        if (result.IsOk())
            out = static_cast<T>(number);

        return result;
    }

    static TASWTomlResult ToValue(T const& in, TASWTomlValue& value, TASWTomlValueOptions const& /*options*/)
    {
        if constexpr (std::is_signed_v<T>)
        {
            value = TASWTomlValue(static_cast<std::int64_t>(in));
            return TASWTomlResult();
        }
        else
        {
            return Detail::UnsignedIntegerToValue(static_cast<std::uint64_t>(in), value);
        }
    }
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<std::filesystem::path>
//
// A string, which is the path in UTF-8 on every platform (see PathToUtf8() and Utf8ToPath()), with the separators as
// the path holds them, so a Windows path is written as 'C:\Users\me' (a literal string) with the default value
// options. Any other type is TypeMismatch. A string that isn't valid UTF-8 is InvalidValue, and so is a path whose
// UTF-8 form wouldn't read back as the same path (an unpaired surrogate in a Windows path, or bytes that aren't UTF-8
// in a POSIX one), so what is written always reads back the same.
/////////////////////////////////////////////////////////////////////////////
template <>
struct TASWTomlConverter<std::filesystem::path>
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, std::filesystem::path& out,
        TASWTomlValueOptions const& options);
    static TASWTomlResult ToValue(std::filesystem::path const& in, TASWTomlValue& value,
        TASWTomlValueOptions const& options);
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<std::string>
//
// A string (UTF-8), both ways; any other type (80, true) is TypeMismatch. Writing text that isn't valid UTF-8 is
// InvalidValue, as TOML can't hold it.
/////////////////////////////////////////////////////////////////////////////
template <>
struct TASWTomlConverter<std::string>
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, std::string& out, TASWTomlValueOptions const& options);
    static TASWTomlResult ToValue(std::string const& in, TASWTomlValue& value, TASWTomlValueOptions const& options);
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<std::u8string>
//
// As TASWTomlConverter<std::string>, the string's bytes as the char8_t of a std::u8string
/////////////////////////////////////////////////////////////////////////////
template <>
struct TASWTomlConverter<std::u8string>
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, std::u8string& out,
        TASWTomlValueOptions const& options);
    static TASWTomlResult ToValue(std::u8string const& in, TASWTomlValue& value, TASWTomlValueOptions const& options);
};

} // namespace ASWToml

//---------------------------------------------------------------------------
#endif // #ifndef ASWToml_ConvertersH
