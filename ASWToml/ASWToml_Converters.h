/* **************************************************************************
ASWToml_Converters.h
Author: Anthony S. West - ASW Software

A TOML file reader/writer library.

TASWTomlConverter<T>, which converts a value of an application's type T to
and from a TASWTomlValue, and the concept TomlConvertible, which says that a
type has one. The library has converters for TASWTomlValue itself,
std::string, std::u8string, std::filesystem::path, bool, the integer types,
float, double, enums (by name when the application gives the enum names,
see TASWTomlEnumName, else as integers), the TOML date and time types, the
std::chrono dates, time points and times of day, and std::vector, std::array,
std::set, std::unordered_set, std::pair and std::tuple (as arrays) of the
types with converters; an application adds its own by specializing
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
#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>
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


/////////////////////////////////////////////////////////////////////////////
// TASWTomlEnumName
//
// One name of a value of the enum T, in the names an application gives an enum so that it is converted by name (see
// TASWTomlConverter<enums with names>). The application declares a function ASWTomlEnumNames() next to the enum, in
// the enum's namespace (or, for an enum declared in a class, as a friend in that class), so argument-dependent lookup
// finds it. Its argument only selects it, and it returns anything a range-based for loop can go over more than once
// that gives TASWTomlEnumName<T>s: a reference to an array or a std::array, or a std::vector. A value may have several
// names: the first one listed is written, and the others are read too (e.g. an old name). A name is compared with the
// ASCII letters' case ignored; an empty one is skipped.
//
//     enum class EColor { Red, Green, Blue };
//
//     inline auto const& ASWTomlEnumNames(EColor)
//     {
//         static constexpr ASWToml::TASWTomlEnumName<EColor> names[] = {
//             { EColor::Red, "Red" }, { EColor::Green, "Green" }, { EColor::Blue, "Blue" },
//             { EColor::Green, "Lime" }, // Read as Green, never written
//         };
//
//         return names;
//     }
/////////////////////////////////////////////////////////////////////////////
template <typename T>
requires std::is_enum_v<T>
struct TASWTomlEnumName
{
    T Value;
    std::string_view Name;
};

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

// The type ASWTomlEnumNames() returns for T, found by argument-dependent lookup (see TASWTomlEnumName)
template <typename T>
using TEnumNamesResult = decltype(ASWTomlEnumNames(std::declval<T>()));

// The names of an enum's values, as a message lists them
using TNameVector = std::vector<std::string_view>;

// The longest part of a value's text a message shows (see ValueText())
constexpr std::size_t MaxValueTextSize = 40;

// Whether T is one of the integer types TASWTomlConverter converts as TOML integers: an integral type of at most 64
// bits, without const or volatile, other than bool, plain char and the wide and Unicode character types
template <typename T>
concept ConvertibleInteger = std::integral<T> && std::same_as<T, std::remove_cv_t<T> > &&
    sizeof(T) <= sizeof(std::int64_t) && !std::same_as<T, bool> && !std::same_as<T, char> &&
    !std::same_as<T, wchar_t> && !std::same_as<T, char8_t> && !std::same_as<T, char16_t> && !std::same_as<T, char32_t>;

// Whether T is an enum, without const or volatile, for which argument-dependent lookup finds ASWTomlEnumNames() (see
// TASWTomlEnumName), or one for which it finds none
template <typename T>
concept EnumWithNames = std::is_enum_v<T> && std::same_as<T, std::remove_cv_t<T> > &&
    requires { typename TEnumNamesResult<T>; };
template <typename T>
concept EnumWithoutNames = std::is_enum_v<T> && std::same_as<T, std::remove_cv_t<T> > && !EnumWithNames<T>;

// The ValueOutOfRange result of reading 'value', an offset date-time, a local date-time or a local time, as a type
// that counts in 'unit' and can't hold it: the message gives the first and last values the unit's count holds, as far
// as TOML's years 0 to 9999 (or a day, up to the leap second 23:59:60) go, e.g. "The value 9999-12-31T00:00:00Z is
// out of range: it must be from 1677-09-21T00:12:43.145224192Z to 2262-04-11T23:47:16.854775807Z." for nanoseconds
TASWTomlResult DateTimeOutOfRange(TASWTomlValue const& value, TDurationUnit const& unit);

// 'result' (a failure of a part of a value) as the failure of the array that holds it as its element 'index':
// "[index]" put in front of its Path, e.g. [2] or [2].port
TASWTomlResult ElementFailure(TASWTomlResult result, std::size_t index);

// Sets 'value' to 'name', the name an enum's value is written as, or leaves it as it was and fails with InvalidValue
// when the name isn't valid UTF-8, which TOML needs
TASWTomlResult EnumNameToValue(std::string_view name, TASWTomlValue& value);

// Reads 'value' as an integer type with the range 'min' to 'max' reads it (see TASWTomlConverter<integer types>)
// into 'out', which is left as it was on failure: TypeMismatch for a value that isn't an integer, ValueOutOfRange
// outside the range, whose message gives it ('max' as at most INT64_MAX, the largest TOML integer)
TASWTomlResult IntegerFromValue(TASWTomlValue const& value, std::int64_t min, std::uint64_t max, std::int64_t& out);

// The InvalidValue result of an enum with names reading 'value', a string that is none of its names or an integer
// that is no listed value's number (see TASWTomlConverter<enums with names>): the message lists 'names', the names the
// enum's values are written as, e.g. "The value "Purple" isn't a name of the enum: it must be Red, Green or Blue."
TASWTomlResult InvalidEnumName(TASWTomlValue const& value, TNameVector const& names);

// Whether 'text' is 'name' with the ASCII letters' case ignored; never when 'name' is empty
bool IsEnumName(std::string_view text, std::string_view name) noexcept;

// 'result' (a failure of a part of a value) as the failure of the table that holds it at 'key': the key, bare when it
// can be, else a basic string (each byte of it that isn't valid UTF-8 as '?'), put in front of its Path, e.g. port,
// "host name" or servers[2]
TASWTomlResult KeyFailure(TASWTomlResult result, std::string_view key);

// The InvalidValue result of reading 'value', an array, as a type of exactly 'length' elements (a std::array, a
// std::pair or a std::tuple): "The value [1, 2] has 2 elements, not 3."
TASWTomlResult LengthMismatch(TASWTomlValue const& value, std::size_t length);

// The InvalidValue result of a set reading 'element', an element of an array that reads as an element the set already
// has (e.g. 1 twice, or "red" after "Red" for an enum with names): "The value 1 reads as an element the set already
// has."
TASWTomlResult RepeatedElement(TASWTomlValue const& element);

// Sorts 'elements' by their TOML text (strings as basic strings), so an unordered set is written in the same order
// every time
void SortByText(TASWTomlValue::TValueVector& elements);

// Sets 'time' to the time of day 'count' units of 'unit' after midnight and returns true, or returns false (and leaves
// 'time' as it was) when that is before midnight or a day and a second or more after it. From a day on, the time is
// the leap second 23:59:60 (and the fraction past a day), which a local time of 23:59:60 reads back as.
bool TimeOfDayFromCount(std::int64_t count, TDurationUnit const& unit, TASWTomlLocalTime& time) noexcept;

// The ValueOutOfRange result of writing a time of day that is negative or a day and a second or more
TASWTomlResult TimeOfDayOutOfRange();

// The ValueOutOfRange result of writing a date or time point outside the years 0 to 9999, which TOML writes
TASWTomlResult TimePointOutOfRange();

// The TOML type of 'value' with an article, as messages name it: "a string", "an integer", "a float", "a boolean", "an
// offset date-time", "a local date-time", "a local date", "a local time", "an array" or "a table"
char const* TypeName(TASWTomlValue const& value) noexcept;

// The TypeMismatch result of reading 'value' as a type that reads 'wanted' (e.g. "a string"): "The value 5 is an
// integer, not a string."
TASWTomlResult TypeMismatch(TASWTomlValue const& value, std::string_view wanted);

// The InvalidValue result of an enum with names writing a value that has no name, whose number is 'number': "The
// value 7 has no name in the enum's ASWTomlEnumNames(), so it can't be written."
TASWTomlResult UnnamedEnumValue(std::string_view number);

// Sets 'value' to 'number' as an unsigned integer type writes it (see TASWTomlConverter<integer types>), or leaves it
// as it was and fails with ValueOutOfRange when 'number' is above INT64_MAX, which a TOML integer can't hold
TASWTomlResult UnsignedIntegerToValue(std::uint64_t number, TASWTomlValue& value);

// A failed result with 'code' and the message "The value <text> <problem>", the text as ValueText() gives it, e.g.
// ValueFailure(InvalidValue, value, "isn't one word.")
TASWTomlResult ValueFailure(ETomlCode code, TASWTomlValue const& value, std::string_view problem);

// 'value' as messages show it: as TOML text on one line (a string as a basic string, its control characters escaped),
// cut to at most MaxValueTextSize bytes (not inside a UTF-8 sequence) followed by "..." when it is longer; "of type
// <type>" (e.g. of type String) for a value that can't be written, such as a string that isn't valid UTF-8
std::string ValueText(TASWTomlValue const& value);

// Sets 'value' to an array of the elements of 'elements', a container of T, in its order, each converted by
// TASWTomlConverter<T>, or leaves it as it was and fails as the first element that fails, with the element's index in
// its Path (see ElementFailure())
template <typename T, typename TElements>
TASWTomlResult ArrayToValue(TElements const& elements, TASWTomlValue& value, TASWTomlValueOptions const& options)
{
    TASWTomlValue::TValueVector array;
    array.reserve(elements.size());
    // An element of a const std::vector<bool> is a bool, not a proxy
    for (T const& element : elements)
    {
        TASWTomlResult result = TASWTomlConverter<T>::ToValue(element, array.emplace_back(), options);
        if (!result.IsOk())
            return ElementFailure(std::move(result), array.size() - 1);
    }

    value = TASWTomlValue(std::move(array));

    return TASWTomlResult();
}

// Reads 'value', an array, into 'read', an empty set of T, each element into a new T{} by TASWTomlConverter<T>:
// TypeMismatch for a value that isn't an array, and the first element that fails, or that reads as an element 'read'
// already has (see RepeatedElement()), fails with its index in its Path (see ElementFailure()), leaving some elements
// in 'read'
template <typename TSet>
TASWTomlResult SetFromValue(TASWTomlValue const& value, TSet& read, TASWTomlValueOptions const& options)
{
    using T = typename TSet::value_type;

    TASWTomlValue::TValueVector const* const array = value.AsArray();
    if (array == nullptr)
        return TypeMismatch(value, "an array");

    for (std::size_t i = 0; i < array->size(); ++i)
    {
        T element{};
        TASWTomlResult result = TASWTomlConverter<T>::FromValue((*array)[i], element, options);
        if (!result.IsOk())
            return ElementFailure(std::move(result), i);

        if (!read.insert(std::move(element)).second)
            return ElementFailure(RepeatedElement((*array)[i]), i);
    }

    return TASWTomlResult();
}


/////////////////////////////////////////////////////////////////////////////
// TIntegerConverter
//
// Converts any integral type T as an integer, as TASWTomlConverter<integer types> describes, so bool and the character
// types too: the integer converters are this one, and the enum converters use it for an enum's underlying type
/////////////////////////////////////////////////////////////////////////////
template <std::integral T>
requires(sizeof(T) <= sizeof(std::int64_t))
struct TIntegerConverter
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, T& out, TASWTomlValueOptions const& /*options*/)
    {
        std::int64_t number = 0;
        TASWTomlResult result = IntegerFromValue(value, static_cast<std::int64_t>(std::numeric_limits<T>::min()),
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
            return UnsignedIntegerToValue(static_cast<std::uint64_t>(in), value);
        }
    }
};


/////////////////////////////////////////////////////////////////////////////
// TTupleConverter
//
// Converts TTuple, a std::pair or a std::tuple, as an array of exactly as many elements, each converted as its own
// type, as TASWTomlConverter<std::tuple<T...> > describes: the converters of both are this one
/////////////////////////////////////////////////////////////////////////////
template <typename TTuple>
struct TTupleConverter
{
private:
    static constexpr std::size_t Size = std::tuple_size_v<TTuple>;

    // Reads the elements of 'array' from the index I on into those of 'read', or fails as the first one that fails
    template <std::size_t I>
    static TASWTomlResult ElementsFromArray(TASWTomlValue::TValueVector const& array, TTuple& read,
        TASWTomlValueOptions const& options)
    {
        if constexpr (I == Size)
        {
            return TASWTomlResult();
        }
        else
        {
            TASWTomlResult result = TASWTomlConverter<std::tuple_element_t<I, TTuple> >::FromValue(array[I],
                std::get<I>(read), options);
            if (!result.IsOk())
                return ElementFailure(std::move(result), I);

            return ElementsFromArray<I + 1>(array, read, options);
        }
    }

    // Sets the elements of 'array' from the index I on to those of 'in', or fails as the first one that fails
    template <std::size_t I>
    static TASWTomlResult ElementsToArray(TTuple const& in, TASWTomlValue::TValueVector& array,
        TASWTomlValueOptions const& options)
    {
        if constexpr (I == Size)
        {
            return TASWTomlResult();
        }
        else
        {
            TASWTomlResult result = TASWTomlConverter<std::tuple_element_t<I, TTuple> >::ToValue(std::get<I>(in),
                array[I], options);
            if (!result.IsOk())
                return ElementFailure(std::move(result), I);

            return ElementsToArray<I + 1>(in, array, options);
        }
    }

public:
    static TASWTomlResult FromValue(TASWTomlValue const& value, TTuple& out, TASWTomlValueOptions const& options)
    {
        TASWTomlValue::TValueVector const* const array = value.AsArray();
        if (array == nullptr)
            return TypeMismatch(value, "an array");

        if (array->size() != Size)
            return LengthMismatch(value, Size);

        // Each element read into a new one
        TTuple read{};
        TASWTomlResult result = ElementsFromArray<0>(*array, read, options);
        if (result.IsOk())
            out = std::move(read);

        return result;
    }

    static TASWTomlResult ToValue(TTuple const& in, TASWTomlValue& value, TASWTomlValueOptions const& options)
    {
        TASWTomlValue::TValueVector array(Size);
        TASWTomlResult result = ElementsToArray<0>(in, array, options);
        if (result.IsOk())
            value = TASWTomlValue(std::move(array));

        return result;
    }
};

} // namespace Detail

// Whether T has a converter: TASWTomlConverter<T> is defined, with FromValue() and ToValue() as TASWTomlConverter
// describes them
template <typename T>
concept TomlConvertible = std::same_as<Detail::TFromValueResult<T>, TASWTomlResult> &&
    std::same_as<Detail::TToValueResult<T>, TASWTomlResult>;


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<TASWTomlLocalDateTime>
//
// A local date-time, as it is, both ways; any other type is TypeMismatch, an offset date-time too. Writing one that
// isn't valid (see TASWTomlLocalDateTime::IsValid()) is InvalidValue.
/////////////////////////////////////////////////////////////////////////////
template <>
struct TASWTomlConverter<TASWTomlLocalDateTime>
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, TASWTomlLocalDateTime& out,
        TASWTomlValueOptions const& options);
    static TASWTomlResult ToValue(TASWTomlLocalDateTime const& in, TASWTomlValue& value,
        TASWTomlValueOptions const& options);
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<TASWTomlLocalTime>
//
// A local time, as it is, both ways; any other type is TypeMismatch. Writing one that isn't valid (see
// TASWTomlLocalTime::IsValid()) is InvalidValue.
/////////////////////////////////////////////////////////////////////////////
template <>
struct TASWTomlConverter<TASWTomlLocalTime>
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, TASWTomlLocalTime& out,
        TASWTomlValueOptions const& options);
    static TASWTomlResult ToValue(TASWTomlLocalTime const& in, TASWTomlValue& value,
        TASWTomlValueOptions const& options);
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<TASWTomlOffsetDateTime>
//
// An offset date-time, as it is (with its offset), both ways; any other type is TypeMismatch, a local date-time too.
// Writing one that isn't valid (see TASWTomlOffsetDateTime::IsValid()) is InvalidValue.
/////////////////////////////////////////////////////////////////////////////
template <>
struct TASWTomlConverter<TASWTomlOffsetDateTime>
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, TASWTomlOffsetDateTime& out,
        TASWTomlValueOptions const& options);
    static TASWTomlResult ToValue(TASWTomlOffsetDateTime const& in, TASWTomlValue& value,
        TASWTomlValueOptions const& options);
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<TASWTomlValue>
//
// The value as it is, both ways: it never fails (but for std::bad_alloc), so a value of any type can be read and
// written, e.g. a table an application reads itself, or (as std::vector<TASWTomlValue>) an array of mixed types
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
// TASWTomlConverter<enums with names>
//
// An enum for which argument-dependent lookup finds ASWTomlEnumNames() (see TASWTomlEnumName). Reads a string that is
// one of its names with the ASCII letters' case ignored (no blanks are trimmed: a TOML string's quotes end it), or an
// integer that is the number of a value listed with a name, so a file that stored the number still reads. Another
// string or integer is InvalidValue, with a message that lists the names; any other type is TypeMismatch. Writes the
// first name listed for the value, as a string; a value with no name is InvalidValue, since it couldn't be read back.
/////////////////////////////////////////////////////////////////////////////
template <Detail::EnumWithNames T>
struct TASWTomlConverter<T>
{
private:
    using TNumber = std::underlying_type_t<T>;

    // The first name listed for 'value' in 'names' (empty when it has none), the one it is written as
    template <typename TNames>
    static std::string_view WrittenName(TNames const& names, T value) noexcept
    {
        for (auto const& name : names)
        {
            if (name.Value == value && !name.Name.empty())
                return name.Name;
        }

        return std::string_view();
    }

    // The names the values in 'names' are written as, each value's once, in the order listed
    template <typename TNames>
    static Detail::TNameVector WrittenNames(TNames const& names)
    {
        Detail::TNameVector written;
        for (auto const& name : names)
        {
            // Written unless an entry before it names the same value
            bool first = !name.Name.empty();
            for (auto const& earlier : names)
            {
                if (&earlier == &name)
                    break;

                if (earlier.Value == name.Value && !earlier.Name.empty())
                    first = false;
            }

            if (first)
                written.push_back(name.Name);
        }

        return written;
    }

public:
    static TASWTomlResult FromValue(TASWTomlValue const& value, T& out, TASWTomlValueOptions const& options)
    {
        auto const& names = ASWTomlEnumNames(T());
        std::string const* const text = value.AsString();
        if (text != nullptr)
        {
            for (auto const& name : names)
            {
                if (Detail::IsEnumName(*text, name.Name))
                {
                    out = name.Value;
                    return TASWTomlResult();
                }
            }
        }
        else if (value.IsInteger())
        {
            TNumber number = 0;
            if (Detail::TIntegerConverter<TNumber>::FromValue(value, number, options).IsOk())
            {
                if (!WrittenName(names, static_cast<T>(number)).empty())
                {
                    out = static_cast<T>(number);
                    return TASWTomlResult();
                }
            }
        }
        else
        {
            return Detail::TypeMismatch(value, "a string or an integer");
        }

        return Detail::InvalidEnumName(value, WrittenNames(names));
    }

    static TASWTomlResult ToValue(T const& in, TASWTomlValue& value, TASWTomlValueOptions const& /*options*/)
    {
        std::string_view const name = WrittenName(ASWTomlEnumNames(T()), in);
        if (!name.empty())
            return Detail::EnumNameToValue(name, value);

        // The value's number, for the message
        if constexpr (std::is_signed_v<TNumber>)
            return Detail::UnnamedEnumValue(std::to_string(static_cast<std::int64_t>(in)));
        else
            return Detail::UnnamedEnumValue(std::to_string(static_cast<std::uint64_t>(in)));
    }
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<enums without names>
//
// An enum for which argument-dependent lookup finds no ASWTomlEnumNames() converts as an integer of its underlying
// type, as TASWTomlConverter<integer types> describes (an underlying bool or character type is an integer too),
// whether the number is one of its enumerators or not (e.g. bit flags: 5 is the value 5), so std::byte is an integer
// from 0 to 255
/////////////////////////////////////////////////////////////////////////////
template <Detail::EnumWithoutNames T>
struct TASWTomlConverter<T>
{
private:
    using TNumber = std::underlying_type_t<T>;

public:
    static TASWTomlResult FromValue(TASWTomlValue const& value, T& out, TASWTomlValueOptions const& options)
    {
        TNumber number = 0;
        TASWTomlResult result = Detail::TIntegerConverter<TNumber>::FromValue(value, number, options);
        if (result.IsOk())
            out = static_cast<T>(number);

        return result;
    }

    static TASWTomlResult ToValue(T const& in, TASWTomlValue& value, TASWTomlValueOptions const& options)
    {
        return Detail::TIntegerConverter<TNumber>::ToValue(static_cast<TNumber>(in), value, options);
    }
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
struct TASWTomlConverter<T> : Detail::TIntegerConverter<T>
{
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<std::array<T, N> >
//
// An array of exactly N elements, as TASWTomlConverter<std::vector<T> > converts one; an array of another length is
// InvalidValue: "The value [1, 2] has 2 elements, not 3."
/////////////////////////////////////////////////////////////////////////////
template <TomlConvertible T, std::size_t N>
requires std::default_initializable<T>
struct TASWTomlConverter<std::array<T, N> >
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, std::array<T, N>& out,
        TASWTomlValueOptions const& options)
    {
        TASWTomlValue::TValueVector const* const array = value.AsArray();
        if (array == nullptr)
            return Detail::TypeMismatch(value, "an array");

        if (array->size() != N)
            return Detail::LengthMismatch(value, N);

        // Each element read into a new T{}
        std::array<T, N> read{};
        for (std::size_t i = 0; i < N; ++i)
        {
            TASWTomlResult result = TASWTomlConverter<T>::FromValue((*array)[i], read[i], options);
            if (!result.IsOk())
                return Detail::ElementFailure(std::move(result), i);
        }

        out = std::move(read);

        return TASWTomlResult();
    }

    static TASWTomlResult ToValue(std::array<T, N> const& in, TASWTomlValue& value,
        TASWTomlValueOptions const& options)
    {
        return Detail::ArrayToValue<T>(in, value, options);
    }
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<std::chrono::hh_mm_ss<D> >
//
// A local time, both ways, for a duration D the date and time types convert (see Detail::TomlDuration, e.g. seconds
// or nanoseconds); any other type is TypeMismatch. Reads the time rounded down to D (23:59:60, a leap second, is
// 24:00:00, as hh_mm_ss makes 24 hours); a time D's count can't hold is ValueOutOfRange. Writes the time of day, from
// 24:00:00 on as 23:59:60 (and the fraction), so what was read reads back the same; one that is negative or a day and a
// second or more is ValueOutOfRange.
/////////////////////////////////////////////////////////////////////////////
template <Detail::TomlDuration D>
struct TASWTomlConverter<std::chrono::hh_mm_ss<D> >
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, std::chrono::hh_mm_ss<D>& out,
        TASWTomlValueOptions const& /*options*/)
    {
        TASWTomlLocalTime const* const time = value.AsLocalTime();
        if (time == nullptr)
            return Detail::TypeMismatch(value, "a local time");

        if (!time->IsValid())
            return Detail::ValueFailure(ETomlCode::InvalidValue, value, "isn't a valid time.");

        std::optional<D> const sinceMidnight = time->ToDuration<D>();
        if (!sinceMidnight)
            return Detail::DateTimeOutOfRange(value, Detail::DurationUnit<D>());

        out = std::chrono::hh_mm_ss<D>(*sinceMidnight);

        return TASWTomlResult();
    }

    static TASWTomlResult ToValue(std::chrono::hh_mm_ss<D> const& in, TASWTomlValue& value,
        TASWTomlValueOptions const& /*options*/)
    {
        using TPrecision = typename std::chrono::hh_mm_ss<D>::precision;

        std::int64_t const count = static_cast<std::int64_t>(in.to_duration().count());
        TASWTomlLocalTime time;
        if (!Detail::TimeOfDayFromCount(count, Detail::DurationUnit<TPrecision>(), time))
            return Detail::TimeOfDayOutOfRange();

        value = TASWTomlValue(time);

        return TASWTomlResult();
    }
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<std::chrono::local_days>
//
// A local date, both ways (any other type is TypeMismatch); reading a date that isn't real is InvalidValue, writing
// one outside the years 0 to 9999 is ValueOutOfRange
/////////////////////////////////////////////////////////////////////////////
template <>
struct TASWTomlConverter<std::chrono::local_days>
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, std::chrono::local_days& out,
        TASWTomlValueOptions const& options);
    static TASWTomlResult ToValue(std::chrono::local_days const& in, TASWTomlValue& value,
        TASWTomlValueOptions const& options);
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<std::chrono::local_time<D> >
//
// A local date-time, both ways, for a duration D the date and time types convert (see Detail::TomlDuration, e.g.
// std::chrono::local_seconds) but days (a local date, see TASWTomlConverter<std::chrono::local_days>); any other type
// is TypeMismatch, an offset date-time too, since its offset would be lost. Reads the date and time rounded down to D;
// one D's count can't hold is ValueOutOfRange. Writing one outside the years 0 to 9999 is ValueOutOfRange.
/////////////////////////////////////////////////////////////////////////////
template <Detail::TomlDuration D>
struct TASWTomlConverter<std::chrono::local_time<D> >
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, std::chrono::local_time<D>& out,
        TASWTomlValueOptions const& /*options*/)
    {
        TASWTomlLocalDateTime const* const dateTime = value.AsLocalDateTime();
        if (dateTime == nullptr)
            return Detail::TypeMismatch(value, "a local date-time");

        if (!dateTime->IsValid())
            return Detail::ValueFailure(ETomlCode::InvalidValue, value, "isn't a valid date-time.");

        std::optional<std::chrono::local_time<D> > const localTime = dateTime->ToLocalTime<D>();
        if (!localTime)
            return Detail::DateTimeOutOfRange(value, Detail::DurationUnit<D>());

        out = *localTime;

        return TASWTomlResult();
    }

    static TASWTomlResult ToValue(std::chrono::local_time<D> const& in, TASWTomlValue& value,
        TASWTomlValueOptions const& /*options*/)
    {
        std::optional<TASWTomlLocalDateTime> const dateTime = TASWTomlLocalDateTime::FromLocalTime(in);
        if (!dateTime)
            return Detail::TimePointOutOfRange();

        value = TASWTomlValue(*dateTime);

        return TASWTomlResult();
    }
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<std::chrono::sys_days>
//
// A local date, both ways, as TASWTomlConverter<std::chrono::local_days> (a date has no offset, so it reads as the
// day it names in UTC)
/////////////////////////////////////////////////////////////////////////////
template <>
struct TASWTomlConverter<std::chrono::sys_days>
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, std::chrono::sys_days& out,
        TASWTomlValueOptions const& options);
    static TASWTomlResult ToValue(std::chrono::sys_days const& in, TASWTomlValue& value,
        TASWTomlValueOptions const& options);
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<std::chrono::sys_time<D> >
//
// An offset date-time, both ways, for a duration D the date and time types convert (see Detail::TomlDuration) but
// days (a local date, see TASWTomlConverter<std::chrono::sys_days>), so std::chrono::system_clock::time_point and
// sys_seconds too; any other type is TypeMismatch, a local date-time too, since its offset is unknown. Reads the
// instant in UTC, at any offset (a leap second, second 60, is the next minute's second 0), rounded down to D; one D's
// count can't hold is ValueOutOfRange: system_clock counts nanoseconds with libstdc++ (GCC, MinGW), so it holds only
// 1677-09-21T00:12:43.145224192Z to 2262-04-11T23:47:16.854775807Z there, while sys_seconds holds every instant TOML
// can write on every library. Writes the instant in UTC (with Z); one outside the years 0 to 9999 is ValueOutOfRange.
/////////////////////////////////////////////////////////////////////////////
template <Detail::TomlDuration D>
struct TASWTomlConverter<std::chrono::sys_time<D> >
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, std::chrono::sys_time<D>& out,
        TASWTomlValueOptions const& /*options*/)
    {
        TASWTomlOffsetDateTime const* const dateTime = value.AsOffsetDateTime();
        if (dateTime == nullptr)
            return Detail::TypeMismatch(value, "an offset date-time");

        if (!dateTime->IsValid())
            return Detail::ValueFailure(ETomlCode::InvalidValue, value, "isn't a valid date-time.");

        std::optional<std::chrono::sys_time<D> > const utc = dateTime->ToSysTime<D>();
        if (!utc)
            return Detail::DateTimeOutOfRange(value, Detail::DurationUnit<D>());

        out = *utc;

        return TASWTomlResult();
    }

    static TASWTomlResult ToValue(std::chrono::sys_time<D> const& in, TASWTomlValue& value,
        TASWTomlValueOptions const& /*options*/)
    {
        std::optional<TASWTomlOffsetDateTime> const dateTime = TASWTomlOffsetDateTime::FromSysTime(in);
        if (!dateTime)
            return Detail::TimePointOutOfRange();

        value = TASWTomlValue(*dateTime);

        return TASWTomlResult();
    }
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<std::chrono::year_month_day>
//
// A local date, as it is, both ways; any other type is TypeMismatch, an offset date-time too. Writing a date that
// isn't real (2023-02-30) is InvalidValue, one outside the years 0 to 9999 ValueOutOfRange, each with a message that
// shows the date.
/////////////////////////////////////////////////////////////////////////////
template <>
struct TASWTomlConverter<std::chrono::year_month_day>
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, std::chrono::year_month_day& out,
        TASWTomlValueOptions const& options);
    static TASWTomlResult ToValue(std::chrono::year_month_day const& in, TASWTomlValue& value,
        TASWTomlValueOptions const& options);
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
// TASWTomlConverter<std::pair<T1, T2> >
//
// An array of exactly two elements, the first converted as T1 and the second as T2 (e.g. ["example.com", 443]), as
// TASWTomlConverter<std::tuple<T...> > describes
/////////////////////////////////////////////////////////////////////////////
template <TomlConvertible T1, TomlConvertible T2>
requires std::default_initializable<T1> && std::default_initializable<T2>
struct TASWTomlConverter<std::pair<T1, T2> > : Detail::TTupleConverter<std::pair<T1, T2> >
{
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<std::set<T> >
//
// An array, as TASWTomlConverter<std::vector<T> > converts one, read with the set's comparator and allocator: an
// element that reads as one the set already has (1 twice, or "red" after "Red" for an enum with names) is InvalidValue,
// with its index in Path, as it is likely a mistake in the file. Writes the elements in the set's order.
/////////////////////////////////////////////////////////////////////////////
template <TomlConvertible T, typename TCompare, typename TAllocator>
requires std::default_initializable<T>
struct TASWTomlConverter<std::set<T, TCompare, TAllocator> >
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, std::set<T, TCompare, TAllocator>& out,
        TASWTomlValueOptions const& options)
    {
        std::set<T, TCompare, TAllocator> read(out.key_comp(), out.get_allocator());
        TASWTomlResult result = Detail::SetFromValue(value, read, options);
        if (result.IsOk())
            out = std::move(read);

        return result;
    }

    static TASWTomlResult ToValue(std::set<T, TCompare, TAllocator> const& in, TASWTomlValue& value,
        TASWTomlValueOptions const& options)
    {
        return Detail::ArrayToValue<T>(in, value, options);
    }
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
// TASWTomlConverter<std::tuple<T...> >
//
// An array of exactly as many elements as the tuple has, each converted as its own type, which must have a converter
// and a default constructor (e.g. ["example.com", 443, true] for std::tuple<std::string, int, bool>); any other type is
// TypeMismatch, an array of another length InvalidValue ("The value [1, 2] has 2 elements, not 3."). The first element
// that fails fails the tuple, with its index in front of its Path, and leaves 'out' as it was.
/////////////////////////////////////////////////////////////////////////////
template <TomlConvertible... T>
requires(std::default_initializable<T> && ...)
struct TASWTomlConverter<std::tuple<T...> > : Detail::TTupleConverter<std::tuple<T...> >
{
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


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<std::unordered_set<T> >
//
// As TASWTomlConverter<std::set<T> >, read with the set's hash, key equality and allocator, but written with the
// elements sorted by their TOML text (strings as basic strings), so a file doesn't change from run to run: an
// unordered_set<int> of 9, 10 and 80 is written as [10, 80, 9]. A failure to write an element has the index the
// element has in the set's own order in its Path.
/////////////////////////////////////////////////////////////////////////////
template <TomlConvertible T, typename THash, typename TEqual, typename TAllocator>
requires std::default_initializable<T>
struct TASWTomlConverter<std::unordered_set<T, THash, TEqual, TAllocator> >
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, std::unordered_set<T, THash, TEqual, TAllocator>& out,
        TASWTomlValueOptions const& options)
    {
        std::unordered_set<T, THash, TEqual, TAllocator> read(0, out.hash_function(), out.key_eq(),
            out.get_allocator());
        TASWTomlResult result = Detail::SetFromValue(value, read, options);
        if (result.IsOk())
            out = std::move(read);

        return result;
    }

    static TASWTomlResult ToValue(std::unordered_set<T, THash, TEqual, TAllocator> const& in, TASWTomlValue& value,
        TASWTomlValueOptions const& options)
    {
        TASWTomlValue array;
        TASWTomlResult result = Detail::ArrayToValue<T>(in, array, options);
        if (result.IsOk())
        {
            Detail::SortByText(*array.AsArray());
            value = std::move(array);
        }

        return result;
    }
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<std::vector<T> >
//
// An array of any type T that has a converter and a default constructor. Reads any array (an array value or an array
// of tables), each element into a new T{} by TASWTomlConverter<T>; any other type is TypeMismatch. The first element
// that fails fails the vector, with its index in front of its Path ([1], or [1].port for a key inside it), and leaves
// 'out' as it was. Writes an array of the elements in order (a std::vector<bool> too), so a document writes a vector
// of tables as an array of tables, and an empty vector as []. A vector of vectors needs nothing more, and
// std::vector<TASWTomlValue> reads and writes an array whose elements' types differ.
/////////////////////////////////////////////////////////////////////////////
template <TomlConvertible T, typename TAllocator>
requires std::default_initializable<T>
struct TASWTomlConverter<std::vector<T, TAllocator> >
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, std::vector<T, TAllocator>& out,
        TASWTomlValueOptions const& options)
    {
        TASWTomlValue::TValueVector const* const array = value.AsArray();
        if (array == nullptr)
            return Detail::TypeMismatch(value, "an array");

        std::vector<T, TAllocator> read(out.get_allocator());
        read.reserve(array->size());
        for (std::size_t i = 0; i < array->size(); ++i)
        {
            T element{};
            TASWTomlResult result = TASWTomlConverter<T>::FromValue((*array)[i], element, options);
            if (!result.IsOk())
                return Detail::ElementFailure(std::move(result), i);

            read.push_back(std::move(element));
        }

        out = std::move(read);

        return TASWTomlResult();
    }

    static TASWTomlResult ToValue(std::vector<T, TAllocator> const& in, TASWTomlValue& value,
        TASWTomlValueOptions const& options)
    {
        return Detail::ArrayToValue<T>(in, value, options);
    }
};

} // namespace ASWToml

//---------------------------------------------------------------------------
#endif // #ifndef ASWToml_ConvertersH
