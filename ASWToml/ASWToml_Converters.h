/* **************************************************************************
ASWToml_Converters.h
Author: Anthony S. West - ASW Software

A TOML file reader/writer library.

TASWTomlConverter<T>, which converts a value of an application's type T to
and from a TASWTomlValue, and the concept TomlConvertible, which says that a
type has one. The library has converters for TASWTomlValue itself,
std::string, std::u8string, std::filesystem::path and bool; an application
adds its own by specializing TASWTomlConverter for its type.

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
#include <filesystem>
#include <string>
#include <string_view>
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

// 'result' (a failure of a part of a value) as the failure of the array that holds it as its element 'index':
// "[index]" put in front of its Path, e.g. [2] or [2].port
TASWTomlResult ElementFailure(TASWTomlResult result, std::size_t index);

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
