/* **************************************************************************
Test_ASWToml_Converters.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "Test_ASWToml_Converters.h"
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWToml_Converters.h"
#include "ASWToml_Document.h"
#include "ASWToml_File.h"
#include "ASWToml_Options.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

/////////////////////////////////////////////////////////////////////////////
// TTestWord
//
// An application's type with a converter of its own (a full specialization, below): one word, written as a string
// without blanks
/////////////////////////////////////////////////////////////////////////////
struct TTestWord
{
    std::string Text;

    bool operator==(TTestWord const& other) const = default;
};


/////////////////////////////////////////////////////////////////////////////
// TTestBox
//
// An application's template with a converter for every TTestBox<T> whose T has one (a partial specialization with a
// requires clause, below): the item, converted as it is
/////////////////////////////////////////////////////////////////////////////
template <typename T>
struct TTestBox
{
    T Item{};

    bool operator==(TTestBox const& other) const = default;
};

// The enums of the enum converter tests. Those with names declare ASWTomlEnumNames() as an application does, next to
// the enum.

// Names in an array, as TASWTomlEnumName shows: Green has a second name, and Unlisted none
enum class EColor
{
    Red = -1,
    Green = 2,
    Blue = 40,
    Unlisted = 7,
};

auto const& ASWTomlEnumNames(EColor)
{
    static constexpr ASWToml::TASWTomlEnumName<EColor> names[] = {
        { EColor::Red, "Red" }, { EColor::Green, "Green" }, { EColor::Blue, "Blue" }, { EColor::Green, "Lime" },
    };

    return names;
}

// No names at all
enum class EEmpty
{
    Only,
};

std::vector<ASWToml::TASWTomlEnumName<EEmpty> > ASWTomlEnumNames(EEmpty)
{
    return std::vector<ASWToml::TASWTomlEnumName<EEmpty> >();
}

// A name that isn't valid UTF-8, so it can't be written
enum class EInvalidName
{
    Bad,
};

auto const& ASWTomlEnumNames(EInvalidName)
{
    static constexpr ASWToml::TASWTomlEnumName<EInvalidName> names[] = { { EInvalidName::Bad, "B\xFF" "d" } };

    return names;
}

// No ASWTomlEnumNames(), so converted as the underlying type: one for each kind of underlying type
enum class EFlags : unsigned int
{
    None = 0,
    Read = 1,
    Write = 2,
};

enum class ESmall : std::int8_t
{
    Low = -128,
    High = 127,
};

enum class ESwitch : bool
{
    Off = false,
    On = true,
};

enum class EWide : char16_t
{
    First = 0,
};

enum class EHuge : std::uint64_t
{
    Top = 18446744073709551615u,
};

// Names in a std::vector, returned by value
enum class EMode
{
    Off,
    Auto,
    Manual,
};

std::vector<ASWToml::TASWTomlEnumName<EMode> > ASWTomlEnumNames(EMode)
{
    return { { EMode::Off, "Off" }, { EMode::Auto, "Auto" }, { EMode::Manual, "Manual" } };
}

// An unscoped enum, with names in an array
enum EPlain
{
    PlainOne = 1,
    PlainTwo = 2,
};

auto const& ASWTomlEnumNames(EPlain)
{
    static constexpr ASWToml::TASWTomlEnumName<EPlain> names[] = { { PlainOne, "One" }, { PlainTwo, "Two" } };

    return names;
}

// Names in a std::array, with an unsigned char underlying type: Medium's first entry has an empty name, so its second
// one is written, and Huge's only entry has an empty one, so it isn't listed
enum class ESize : unsigned char
{
    Small = 0,
    Medium = 1,
    Huge = 200,
    Large = 255,
};

std::array<ASWToml::TASWTomlEnumName<ESize>, 5> const& ASWTomlEnumNames(ESize)
{
    static std::array<ASWToml::TASWTomlEnumName<ESize>, 5> const names = { {
        { ESize::Small, "Small" }, { ESize::Medium, "" }, { ESize::Medium, "Medium" }, { ESize::Huge, "" },
        { ESize::Large, "Large" },
    } };

    return names;
}

// A class that converts to an enum with names, but isn't an enum (never defined)
struct TColorHolder
{
    operator EColor() const;
};


/////////////////////////////////////////////////////////////////////////////
// TWindow
//
// A class with an enum whose names are a friend function, which only argument-dependent lookup finds; Maximized has a
// second name
/////////////////////////////////////////////////////////////////////////////
class TWindow
{
public:
    enum class EState
    {
        Normal,
        Maximized,
    };

    friend auto const& ASWTomlEnumNames(EState)
    {
        static constexpr ASWToml::TASWTomlEnumName<EState> names[] = {
            { EState::Normal, "Normal" }, { EState::Maximized, "Maximized" }, { EState::Maximized, "Zoomed" },
        };

        return names;
    }
};

} // namespace

} // namespace ASWUnitTests

namespace ASWToml
{

template <>
struct TASWTomlConverter<ASWUnitTests::TTestWord>
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, ASWUnitTests::TTestWord& out,
        TASWTomlValueOptions const& options)
    {
        std::string text;
        TASWTomlResult result = TASWTomlConverter<std::string>::FromValue(value, text, options);
        if (!result.IsOk())
            return result;

        if (text.empty() || text.find_first_of(" \t") != std::string::npos)
            return Detail::ValueFailure(ETomlCode::InvalidValue, value, "isn't one word.");

        out.Text = text;

        return result;
    }

    static TASWTomlResult ToValue(ASWUnitTests::TTestWord const& in, TASWTomlValue& value,
        TASWTomlValueOptions const& options)
    {
        return TASWTomlConverter<std::string>::ToValue(in.Text, value, options);
    }
};

template <TomlConvertible T>
struct TASWTomlConverter<ASWUnitTests::TTestBox<T> >
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, ASWUnitTests::TTestBox<T>& out,
        TASWTomlValueOptions const& options)
    {
        return TASWTomlConverter<T>::FromValue(value, out.Item, options);
    }

    static TASWTomlResult ToValue(ASWUnitTests::TTestBox<T> const& in, TASWTomlValue& value,
        TASWTomlValueOptions const& options)
    {
        return TASWTomlConverter<T>::ToValue(in.Item, value, options);
    }
};

} // namespace ASWToml

// The types with a converter: the library's, and an application's (a full specialization, and a partial one for the
// types it constrains itself to); none for a view, which can't hold a value read, or a pointer
static_assert(ASWToml::TomlConvertible<ASWToml::TASWTomlValue>, "a TASWTomlValue should have a converter");
static_assert(ASWToml::TomlConvertible<bool>, "bool should have a converter");
static_assert(ASWToml::TomlConvertible<std::filesystem::path>, "std::filesystem::path should have a converter");
static_assert(ASWToml::TomlConvertible<std::string>, "std::string should have a converter");
static_assert(ASWToml::TomlConvertible<std::u8string>, "std::u8string should have a converter");
static_assert(!ASWToml::TomlConvertible<std::string_view>, "std::string_view should have no converter");
static_assert(!ASWToml::TomlConvertible<char const*>, "char const* should have no converter");
static_assert(!ASWToml::TomlConvertible<void*>, "a pointer should have no converter");
static_assert(ASWToml::TomlConvertible<ASWUnitTests::TTestWord>, "an application's full specialization");
static_assert(ASWToml::TomlConvertible<ASWUnitTests::TTestBox<std::string> >, "an application's partial one");
static_assert(ASWToml::TomlConvertible<ASWUnitTests::TTestBox<ASWUnitTests::TTestWord> >, "of its own type too");
static_assert(!ASWToml::TomlConvertible<ASWUnitTests::TTestBox<char const*> >,
    "a partial specialization's constraint should leave out the types without a converter");

// The numbers: every integer type (the fixed-width ones are among them), float and double; not the character types,
// whose values could be a number or a character, nor long double, whose precision differs by compiler
static_assert(ASWToml::TomlConvertible<signed char>, "signed char should have a converter");
static_assert(ASWToml::TomlConvertible<short>, "short should have a converter");
static_assert(ASWToml::TomlConvertible<int>, "int should have a converter");
static_assert(ASWToml::TomlConvertible<long>, "long should have a converter");
static_assert(ASWToml::TomlConvertible<long long>, "long long should have a converter");
static_assert(ASWToml::TomlConvertible<unsigned char>, "unsigned char should have a converter");
static_assert(ASWToml::TomlConvertible<unsigned short>, "unsigned short should have a converter");
static_assert(ASWToml::TomlConvertible<unsigned int>, "unsigned int should have a converter");
static_assert(ASWToml::TomlConvertible<unsigned long>, "unsigned long should have a converter");
static_assert(ASWToml::TomlConvertible<unsigned long long>, "unsigned long long should have a converter");
static_assert(ASWToml::TomlConvertible<std::int8_t> && ASWToml::TomlConvertible<std::uint64_t>,
    "the fixed-width integer types should have converters");
static_assert(ASWToml::TomlConvertible<float> && ASWToml::TomlConvertible<double>, "float and double should have one");
static_assert(!ASWToml::TomlConvertible<char>, "plain char should have no converter");
static_assert(!ASWToml::TomlConvertible<wchar_t>, "wchar_t should have no converter");
static_assert(!ASWToml::TomlConvertible<char8_t>, "char8_t should have no converter");
static_assert(!ASWToml::TomlConvertible<char16_t>, "char16_t should have no converter");
static_assert(!ASWToml::TomlConvertible<char32_t>, "char32_t should have no converter");
static_assert(!ASWToml::TomlConvertible<long double>, "long double should have no converter");
static_assert(!ASWToml::TomlConvertible<int const> && !ASWToml::TomlConvertible<int volatile>,
    "an integer type with const or volatile should have no converter: it can't be read into");
static_assert(!ASWToml::Detail::ConvertibleInteger<bool>, "bool should have its own converter, not the integers'");

// Argument-dependent lookup finds ASWTomlEnumNames() wherever an application may declare it, and an enum without one
// has no names but converts too, std::byte included
static_assert(ASWToml::Detail::EnumWithNames<ASWUnitTests::EColor>, "names in an array should be found");
static_assert(ASWToml::Detail::EnumWithNames<ASWUnitTests::ESize>, "names in a std::array should be found");
static_assert(ASWToml::Detail::EnumWithNames<ASWUnitTests::EMode>, "names in a std::vector should be found");
static_assert(ASWToml::Detail::EnumWithNames<ASWUnitTests::EEmpty>, "an empty std::vector of names should be found");
static_assert(ASWToml::Detail::EnumWithNames<ASWUnitTests::EPlain>, "the names of an unscoped enum should be found");
static_assert(ASWToml::Detail::EnumWithNames<ASWUnitTests::TWindow::EState>, "names in a friend should be found");
static_assert(ASWToml::Detail::EnumWithoutNames<ASWUnitTests::EFlags>, "an enum without ASWTomlEnumNames()");
static_assert(ASWToml::Detail::EnumWithoutNames<std::byte>, "std::byte has no names");
static_assert(!ASWToml::Detail::EnumWithNames<ASWUnitTests::TColorHolder> &&
    !ASWToml::Detail::EnumWithoutNames<ASWUnitTests::TColorHolder>, "a class that converts to an enum isn't an enum");
static_assert(!ASWToml::Detail::EnumWithNames<int> && !ASWToml::Detail::EnumWithoutNames<int>, "int isn't an enum");
static_assert(ASWToml::TomlConvertible<ASWUnitTests::EColor> && ASWToml::TomlConvertible<ASWUnitTests::EFlags> &&
    ASWToml::TomlConvertible<std::byte>, "enums with and without names should have converters");
static_assert(!ASWToml::TomlConvertible<ASWUnitTests::EColor const> &&
    !ASWToml::TomlConvertible<ASWUnitTests::EFlags const>, "an enum with const should have no converter");
static_assert(!ASWToml::TomlConvertible<ASWUnitTests::TColorHolder>, "nor should a class that converts to an enum");

namespace ASWUnitTests
{

namespace
{

using namespace std::string_literals;

using ASWToml::ETomlCode;
using ASWToml::ETomlType;
using ASWToml::TASWTomlConverter;
using ASWToml::TASWTomlDocument;
using ASWToml::TASWTomlResult;
using ASWToml::TASWTomlValue;
using ASWToml::TASWTomlValueOptions;
using ASWToml::TomlConvertible;

using TArray = TASWTomlValue::TValueVector;
using TTable = TASWTomlValue::TKeyValueVector;

template <TomlConvertible T>
std::string EnumRoundTripProblem(std::initializer_list<T> values);
std::string Escaped(std::string const& text);
std::string FirstLine(std::string const& text);
template <ASWToml::Detail::ConvertibleInteger T>
std::string IntegerRangeProblem();
template <ASWToml::Detail::ConvertibleInteger T>
std::string IntegerRoundTripProblem();
template <ASWToml::Detail::ConvertibleInteger T>
std::int64_t LargestInteger();
TArray OneOfEachType();
template <typename T>
std::string ReadProblem(TASWTomlValue const& value, T expected);
template <TomlConvertible T>
std::string RefusalProblem(T const& initial, std::initializer_list<ETomlType> accepted, std::string const& wanted);
template <typename T>
std::string RejectProblem(TASWTomlValue const& value, ETomlCode code);
template <typename T>
bool SameBits(T a, T b);
template <TomlConvertible T>
TASWTomlResult ThroughDocument(T const& in, T& out, std::string& text);
template <TomlConvertible T>
std::string Written(T const& in);

//---------------------------------------------------------------------------
/*
    EnumRoundTripProblem

    What is wrong with how 'values', an enum's, go through a document (see ThroughDocument()): each must be written as
    Written() gives it and read back as itself; empty when nothing is wrong
*/
template <TomlConvertible T>
std::string EnumRoundTripProblem(std::initializer_list<T> values)
{
    for (T const in : values)
    {
        // The other value, so a read that does nothing fails
        T out = static_cast<T>(static_cast<long long>(in) == 0 ? 1 : 0);
        std::string text;
        std::string const written = Written(in);
        TASWTomlResult const through = ThroughDocument(in, out, text);
        if (!through.IsOk())
            return written + ": " + through.Message;

        if (out != in)
            return written + " should read back as itself";

        if (FirstLine(text) != "v = " + written)
            return written + ": the text is " + Escaped(text);
    }

    return std::string();
}
//---------------------------------------------------------------------------
/*
    Escaped

    'text' for a message: printable ASCII as it is, a backslash as \\, and every other byte as \xHH
*/
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
            escaped += "\\x"s + Digits[byte >> 4] + Digits[byte & 0xF];
    }

    return escaped;
}
//---------------------------------------------------------------------------
/*
    FirstLine

    'text' up to its first line break (LF or CRLF), or all of it when it has none
*/
std::string FirstLine(std::string const& text)
{
    return text.substr(0, text.find_first_of("\r\n"));
}
//---------------------------------------------------------------------------
/*
    IntegerRangeProblem

    What is wrong with how TASWTomlConverter<T>::FromValue() reads the ends of T's range (as far as a TOML integer
    holds it, see LargestInteger()) and the integers just beyond them: each end must read as itself, and a number
    beyond one must fail with ValueOutOfRange, a message that gives the range, no Path, and 'out' unchanged; empty
    when nothing is wrong
*/
template <ASWToml::Detail::ConvertibleInteger T>
std::string IntegerRangeProblem()
{
    std::int64_t const min = static_cast<std::int64_t>(std::numeric_limits<T>::min());
    std::int64_t const max = LargestInteger<T>();
    for (std::int64_t const end : { min, max })
    {
        T out = static_cast<T>(end == 0 ? 1 : 0);
        TASWTomlResult const result = TASWTomlConverter<T>::FromValue(end, out, TASWTomlValueOptions());
        if (!result.IsOk() || static_cast<std::int64_t>(out) != end)
            return std::to_string(end) + " should read as itself: " + result.Message;
    }

    // None below a signed 64-bit type's range, and none above a 64-bit type's: TOML integers end there
    std::vector<std::int64_t> beyond;
    if constexpr (std::is_unsigned_v<T> || sizeof(T) < sizeof(std::int64_t))
        beyond.push_back(min - 1);

    if constexpr (sizeof(T) < sizeof(std::int64_t))
        beyond.push_back(max + 1);

    for (std::int64_t const number : beyond)
    {
        T out = 7;
        TASWTomlResult const result = TASWTomlConverter<T>::FromValue(number, out, TASWTomlValueOptions());
        std::string const text = std::to_string(number);
        std::string const expected = "The value " + text + " is out of range: it must be from " +
            std::to_string(min) + " to " + std::to_string(max) + ".";
        if (result.Code != ETomlCode::ValueOutOfRange)
            return text + ": the code is " + ASWToml::ToString(result.Code);

        if (result.Message != expected)
            return text + ": the message is " + result.Message;

        if (!result.Path.empty())
            return text + ": the path is " + result.Path;

        if (out != 7)
            return text + ": the value read into changed";
    }

    return std::string();
}
//---------------------------------------------------------------------------
/*
    IntegerRoundTripProblem

    What is wrong with how T's smallest and largest values (see LargestInteger()), 0, 1, and -1 for a signed type go
    through a document (see ThroughDocument()): each must be written as its decimal digits and read back as itself;
    empty when nothing is wrong
*/
template <ASWToml::Detail::ConvertibleInteger T>
std::string IntegerRoundTripProblem()
{
    std::vector<T> values = { std::numeric_limits<T>::min(), 0, 1, static_cast<T>(LargestInteger<T>()) };
    if constexpr (std::is_signed_v<T>)
        values.push_back(-1);

    for (T const in : values)
    {
        T out = static_cast<T>(in == 0 ? 1 : 0);
        std::string text;
        TASWTomlResult const result = ThroughDocument(in, out, text);
        std::string const number = std::to_string(static_cast<std::int64_t>(in));
        if (!result.IsOk())
            return number + ": " + result.Message;

        if (out != in)
            return number + " should read back as itself";

        if (FirstLine(text) != "v = " + number)
            return number + ": the text is " + Escaped(text);
    }

    return std::string();
}
//---------------------------------------------------------------------------
/*
    LargestInteger

    The largest value of T that a TOML integer holds: T's largest, or INT64_MAX for an unsigned 64-bit type
*/
template <ASWToml::Detail::ConvertibleInteger T>
std::int64_t LargestInteger()
{
    if constexpr (std::is_unsigned_v<T> && sizeof(T) == sizeof(std::int64_t))
        return std::numeric_limits<std::int64_t>::max();
    else
        return static_cast<std::int64_t>(std::numeric_limits<T>::max());
}
//---------------------------------------------------------------------------
/*
    OneOfEachType

    A value of each TOML type, in the order of ETomlType
*/
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
/*
    ReadProblem

    What is wrong with how TASWTomlConverter<T>::FromValue() reads 'value', an enum's: it must read it as 'expected',
    into a T that is first the enum's value 0 and then its value 1 (so a read that does nothing fails one of them);
    empty when nothing is wrong
*/
template <typename T>
std::string ReadProblem(TASWTomlValue const& value, T expected)
{
    for (int const initial : { 0, 1 })
    {
        T out = static_cast<T>(initial);
        TASWTomlResult const result = TASWTomlConverter<T>::FromValue(value, out, TASWTomlValueOptions());
        if (!result.IsOk())
            return ASWToml::Detail::ValueText(value) + " should read: " + result.Message;

        if (out != expected)
        {
            return ASWToml::Detail::ValueText(value) + " should read as " +
                std::to_string(static_cast<long long>(expected)) + ", not " + std::to_string(static_cast<long long>(out));
        }
    }

    return std::string();
}
//---------------------------------------------------------------------------
/*
    RefusalProblem

    What is wrong with how TASWTomlConverter<T>::FromValue() refuses each value of OneOfEachType() but those of the
    types 'accepted': it must fail with TypeMismatch, a message that says the value's type isn't 'wanted' (e.g. "a
    string"), no Path, and 'out' (set to 'initial' first) unchanged; empty when nothing is wrong
*/
template <TomlConvertible T>
std::string RefusalProblem(T const& initial, std::initializer_list<ETomlType> accepted, std::string const& wanted)
{
    for (TASWTomlValue const& value : OneOfEachType())
    {
        bool isAccepted = false;
        for (ETomlType const type : accepted)
        {
            if (value.GetType() == type)
                isAccepted = true;
        }

        if (isAccepted)
            continue;

        T out = initial;
        TASWTomlResult const result = TASWTomlConverter<T>::FromValue(value, out, TASWTomlValueOptions());
        std::string const type = ASWToml::ToString(value.GetType());
        std::string const expected = "The value " + ASWToml::Detail::ValueText(value) + " is " +
            ASWToml::Detail::TypeName(value) + ", not " + wanted + ".";
        if (result.Code != ETomlCode::TypeMismatch)
            return type + ": the code is " + ASWToml::ToString(result.Code);

        if (result.Message != expected)
            return type + ": the message is " + Escaped(result.Message);

        if (!result.Path.empty())
            return type + ": the path is " + result.Path;

        if (!(out == initial))
            return type + ": the value read into changed";
    }

    return std::string();
}
//---------------------------------------------------------------------------
/*
    RejectProblem

    What is wrong with how TASWTomlConverter<T>::FromValue() refuses 'value', an enum's: it must fail with 'code', a
    message that shows the value, no Path, and the T read into (first the enum's value 0, then its value 1) unchanged;
    empty when nothing is wrong
*/
template <typename T>
std::string RejectProblem(TASWTomlValue const& value, ETomlCode code)
{
    std::string const text = ASWToml::Detail::ValueText(value);
    for (int const initial : { 0, 1 })
    {
        T out = static_cast<T>(initial);
        TASWTomlResult const result = TASWTomlConverter<T>::FromValue(value, out, TASWTomlValueOptions());
        if (result.Code != code)
            return text + ": the code is " + ASWToml::ToString(result.Code);

        if (result.Message.find("The value " + text + " ") != 0)
            return text + ": the message is " + Escaped(result.Message);

        if (!result.Path.empty())
            return text + ": the path is " + result.Path;

        if (out != static_cast<T>(initial))
            return text + ": the value read into changed";
    }

    return std::string();
}
//---------------------------------------------------------------------------
/*
    SameBits

    Whether 'a' and 'b' are the same bit for bit, so for a float that -0.0 isn't 0.0 and that a NaN is itself
*/
template <typename T>
bool SameBits(T a, T b)
{
    return std::memcmp(&a, &b, sizeof(T)) == 0;
}
//---------------------------------------------------------------------------
/*
    ThroughDocument

    Converts 'in' with TASWTomlConverter<T>::ToValue(), sets it as the key v of an empty document, sets 'text' to the
    document's text, parses that text again and reads v back into 'out' with FromValue(): the first step that fails, or
    Ok
*/
template <TomlConvertible T>
TASWTomlResult ThroughDocument(T const& in, T& out, std::string& text)
{
    TASWTomlValue value;
    TASWTomlResult result = TASWTomlConverter<T>::ToValue(in, value, TASWTomlValueOptions());
    if (!result.IsOk())
        return result;

    TASWTomlDocument document;
    result = document.SetValue("v", value);
    if (!result.IsOk())
        return result;

    text = document.ToText();
    TASWTomlDocument read;
    result = read.Parse(text);
    if (!result.IsOk())
        return result;

    TASWTomlValue const* const found = read.Find("v");
    if (found == nullptr)
    {
        result.Code = ETomlCode::NotFound;
        result.Message = "The document has no key v.";

        return result;
    }

    return TASWTomlConverter<T>::FromValue(*found, out, TASWTomlValueOptions());
}
//---------------------------------------------------------------------------
/*
    Written

    'in' as TASWTomlConverter<T>::ToValue() writes it, into a value that is a table first: the value as TOML text (see
    ValueText()), or "failed: " and the message, with "(changed)" after it when the table changed
*/
template <TomlConvertible T>
std::string Written(T const& in)
{
    TASWTomlValue const table = TTable{ { "x", 1 } };
    TASWTomlValue value = table;
    TASWTomlResult const result = TASWTomlConverter<T>::ToValue(in, value, TASWTomlValueOptions());
    if (result.IsOk())
        return ASWToml::Detail::ValueText(value);

    return "failed: " + result.Message + (value == table ? "" : " (changed)");
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_BoolConverter
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_BoolConverter::TTest_ASWToml_BoolConverter()
    : inherited("ASWToml_BoolConverter_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_BoolConverter::~TTest_ASWToml_BoolConverter()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_BoolConverter::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_BoolConverter::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_BoolConverter::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_BoolConverter::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BoolConverter, FromValue_ReadsBooleansOnly)
{
    // Arrange
    bool yes = false;
    bool no = true;
    bool kept = true;

    // Act
    TASWTomlResult const readYes = TASWTomlConverter<bool>::FromValue(true, yes, TASWTomlValueOptions());
    TASWTomlResult const readNo = TASWTomlConverter<bool>::FromValue(false, no, TASWTomlValueOptions());
    TASWTomlResult const one = TASWTomlConverter<bool>::FromValue(1, kept, TASWTomlValueOptions());
    TASWTomlResult const text = TASWTomlConverter<bool>::FromValue("true", kept, TASWTomlValueOptions());

    // Assert
    CheckTrue(readYes.IsOk() && yes, "true should read as true");
    CheckTrue(readNo.IsOk() && !no, "false should read as false");
    CheckEquals("The value 1 is an integer, not a boolean."s, one.Message, "1 isn't a boolean");
    CheckEquals("The value \"true\" is a string, not a boolean."s, text.Message, "nor is \"true\"");
    CheckTrue(kept, "a value refused should leave the bool as it was");
    CheckEmpty(RefusalProblem(true, { ETomlType::Boolean }, "a boolean"), "every other type should be refused");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BoolConverter, RoundTrip_ThroughADocument)
{
    for (bool const flag : { true, false })
    {
        // Arrange
        bool out = !flag;
        std::string text;

        // Act
        TASWTomlResult const result = ThroughDocument(flag, out, text);

        // Assert
        CheckTrue(result.IsOk(), "the value should go through a document: " + result.Message);
        CheckEquals(flag, out, "the value read back should be the one written");
        CheckContains(text, flag ? "v = true"s : "v = false"s, "the text should be the TOML boolean");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_BoolConverter, ToValue_ReplacesTheValue)
{
    // Arrange
    TASWTomlValue value = TTable{ { "x", 1 } };

    // Act
    TASWTomlResult const result = TASWTomlConverter<bool>::ToValue(false, value, TASWTomlValueOptions());

    // Assert
    CheckTrue(result.IsOk(), "writing a bool never fails");
    CheckTrue(value == TASWTomlValue(false), "the table should be replaced by the boolean");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_EnumConverters
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_EnumConverters::TTest_ASWToml_EnumConverters()
    : inherited("ASWToml_EnumConverters_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_EnumConverters::~TTest_ASWToml_EnumConverters()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_EnumConverters::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_EnumConverters::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_EnumConverters::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_EnumConverters::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_EnumConverters, FromValue_IgnoresTheCaseOfLetters)
{
    // Act & Assert
    // The first and last letters, A and Z, included
    CheckEmpty(ReadProblem("red", EColor::Red), "red");
    CheckEmpty(ReadProblem("GREEN", EColor::Green), "GREEN");
    CheckEmpty(ReadProblem("lIME", EColor::Green), "lIME, the second name");
    CheckEmpty(ReadProblem("bLuE", EColor::Blue), "bLuE");
    CheckEmpty(ReadProblem("auto", EMode::Auto), "auto");
    CheckEmpty(ReadProblem("MANUAL", EMode::Manual), "MANUAL");
    CheckEmpty(ReadProblem("maximized", TWindow::EState::Maximized), "maximized");
    CheckEmpty(ReadProblem("zOOMED", TWindow::EState::Maximized), "zOOMED");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_EnumConverters, FromValue_ReadsEveryName)
{
    // Act & Assert
    // Every kind of names: an array (with a second name), a std::array (with an empty name), a std::vector, an
    // unscoped enum's and a friend function
    CheckEmpty(ReadProblem("Red", EColor::Red), "Red");
    CheckEmpty(ReadProblem("Green", EColor::Green), "Green");
    CheckEmpty(ReadProblem("Blue", EColor::Blue), "Blue");
    CheckEmpty(ReadProblem("Lime", EColor::Green), "Lime");
    CheckEmpty(ReadProblem("Small", ESize::Small), "Small");
    CheckEmpty(ReadProblem("Medium", ESize::Medium), "Medium");
    CheckEmpty(ReadProblem("Large", ESize::Large), "Large");
    CheckEmpty(ReadProblem("Off", EMode::Off), "Off");
    CheckEmpty(ReadProblem("Auto", EMode::Auto), "Auto");
    CheckEmpty(ReadProblem("Manual", EMode::Manual), "Manual");
    CheckEmpty(ReadProblem("One", PlainOne), "One");
    CheckEmpty(ReadProblem("Two", PlainTwo), "Two");
    CheckEmpty(ReadProblem("Normal", TWindow::EState::Normal), "Normal");
    CheckEmpty(ReadProblem("Maximized", TWindow::EState::Maximized), "Maximized");
    CheckEmpty(ReadProblem("Zoomed", TWindow::EState::Maximized), "Zoomed");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_EnumConverters, FromValue_ReadsTheNumberOfAListedValue)
{
    // Act & Assert
    CheckEmpty(ReadProblem(-1, EColor::Red), "-1, a negative one");
    CheckEmpty(ReadProblem(2, EColor::Green), "2");
    CheckEmpty(ReadProblem(40, EColor::Blue), "40");
    CheckEmpty(ReadProblem(255, ESize::Large), "255, the largest of the underlying type");
    CheckEmpty(ReadProblem(2, PlainTwo), "2 of an unscoped enum");
    CheckEmpty(ReadProblem(1, TWindow::EState::Maximized), "1 of a friend function's");

    // Medium's first entry has an empty name, but its second one lists it
    CheckEmpty(ReadProblem(1, ESize::Medium), "1, Medium");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_EnumConverters, FromValue_RefusesOtherValues)
{
    // Arrange
    // Other names, names with more around them (blanks included: a TOML string's quotes end it), an enumerator that
    // isn't listed, the numbers of values that aren't listed or are outside the underlying type, and a name with a
    // letter that only matches outside ASCII
    std::string const texts[] = { "", " ", "Purple", "Unlisted", "Re", "Reds", " Red", "Red ", "Re d", "Red,",
        "Red Green", "\"Red\"", "7", "-1", std::string("Red\0", 4), "R\xC3\xA9" "d", "BL\xC3\x9A" "E" };
    std::int64_t const numbers[] = { 7, 3, 0, 41, 99999999999, std::numeric_limits<std::int64_t>::min() };

    // Act & Assert
    for (std::string const& text : texts)
        CheckEmpty(RejectProblem<EColor>(text, ETomlCode::InvalidValue), Escaped(text) + " should be refused");

    for (std::int64_t const number : numbers)
        CheckEmpty(RejectProblem<EColor>(number, ETomlCode::InvalidValue), std::to_string(number) + " should be too");

    // A number outside the underlying type is InvalidValue too, not ValueOutOfRange, and so is the number of a value
    // that is listed with an empty name only
    CheckEmpty(RejectProblem<ESize>(-1, ETomlCode::InvalidValue), "-1, below unsigned char");
    CheckEmpty(RejectProblem<ESize>(256, ETomlCode::InvalidValue), "256, above it");
    CheckEmpty(RejectProblem<ESize>(2, ETomlCode::InvalidValue), "2, no value of the enum");
    CheckEmpty(RejectProblem<ESize>(200, ETomlCode::InvalidValue), "200, a value with an empty name only");
    CheckEmpty(RejectProblem<ESize>("", ETomlCode::InvalidValue), "an empty string, which no empty name matches");

    // An enum without names reads nothing, its enumerator's number neither
    CheckEmpty(RejectProblem<EEmpty>("Only", ETomlCode::InvalidValue), "Only, of an enum without names");
    CheckEmpty(RejectProblem<EEmpty>(0, ETomlCode::InvalidValue), "0, of an enum without names");

    // Every other TOML type
    CheckEmpty(RefusalProblem(EColor::Blue, { ETomlType::String, ETomlType::Integer }, "a string or an integer"),
        "every other type should be refused");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_EnumConverters, Messages_NameTheProblem)
{
    // Arrange
    EColor color = EColor::Red;
    ESize size = ESize::Small;
    TWindow::EState state = TWindow::EState::Normal;
    EEmpty empty = EEmpty::Only;
    EMode mode = EMode::Off;
    TASWTomlValue value = "kept";

    // Act
    TASWTomlResult const colorRead = TASWTomlConverter<EColor>::FromValue("Purple", color, TASWTomlValueOptions());
    TASWTomlResult const numberRead = TASWTomlConverter<EColor>::FromValue(7, color, TASWTomlValueOptions());
    TASWTomlResult const sizeRead = TASWTomlConverter<ESize>::FromValue("Giant", size, TASWTomlValueOptions());
    TASWTomlResult const stateRead = TASWTomlConverter<TWindow::EState>::FromValue("Min", state,
        TASWTomlValueOptions());
    TASWTomlResult const emptyRead = TASWTomlConverter<EEmpty>::FromValue("Only", empty, TASWTomlValueOptions());
    TASWTomlResult const mismatch = TASWTomlConverter<EMode>::FromValue(true, mode, TASWTomlValueOptions());
    TASWTomlResult const written = TASWTomlConverter<EColor>::ToValue(EColor::Unlisted, value, TASWTomlValueOptions());

    // Assert
    // The names listed are those written: a second name of a value, or an empty one, isn't
    CheckEquals("The value \"Purple\" isn't a name of the enum: it must be Red, Green or Blue."s, colorRead.Message,
        "the message should list the names written, without Lime");
    CheckEquals("The value 7 isn't a name of the enum: it must be Red, Green or Blue."s, numberRead.Message,
        "a number that isn't a listed value's should say the same");
    CheckEquals("The value \"Giant\" isn't a name of the enum: it must be Small, Medium or Large."s, sizeRead.Message,
        "the message should list each value's name once");
    CheckEquals("The value \"Min\" isn't a name of the enum: it must be Normal or Maximized."s, stateRead.Message,
        "the message should list two names with \"or\", without Zoomed");
    CheckEquals("The value \"Only\" isn't a name of the enum: it has no names."s, emptyRead.Message,
        "the message should say that the enum has no names");
    CheckEquals("The value true is a boolean, not a string or an integer."s, mismatch.Message,
        "another type should be a TypeMismatch that names both types it reads");
    CheckEquals("The value 7 has no name in the enum's ASWTomlEnumNames(), so it can't be written."s, written.Message,
        "the message should give the value's number");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_EnumConverters, RoundTrip_EveryListedValue_ThroughADocument)
{
    // Act & Assert
    CheckEmpty(EnumRoundTripProblem({ EColor::Red, EColor::Green, EColor::Blue }), "EColor");
    CheckEmpty(EnumRoundTripProblem({ ESize::Small, ESize::Medium, ESize::Large }), "ESize");
    CheckEmpty(EnumRoundTripProblem({ EMode::Off, EMode::Auto, EMode::Manual }), "EMode");
    CheckEmpty(EnumRoundTripProblem({ PlainOne, PlainTwo }), "EPlain");
    CheckEmpty(EnumRoundTripProblem({ TWindow::EState::Normal, TWindow::EState::Maximized }), "TWindow::EState");
    CheckEmpty(EnumRoundTripProblem({ EFlags::None, EFlags::Write, static_cast<EFlags>(4294967295u) }), "EFlags");
    CheckEmpty(EnumRoundTripProblem({ ESmall::Low, ESmall::High }), "ESmall");
    CheckEmpty(EnumRoundTripProblem({ ESwitch::Off, ESwitch::On }), "ESwitch");
    CheckEmpty(EnumRoundTripProblem({ std::byte{ 0 }, std::byte{ 255 } }), "std::byte");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_EnumConverters, ToValue_RefusesWhatCantBeReadBack)
{
    // Act & Assert
    // Enumerators that aren't listed or are listed with an empty name only, numbers that are no enumerator, and the
    // enumerator of an enum without names; Written() starts from a table and says when it changed
    CheckEquals("failed: The value 7 has no name in the enum's ASWTomlEnumNames(), so it can't be written."s,
        Written(EColor::Unlisted), "an enumerator that isn't listed");
    CheckContains(Written(static_cast<EColor>(-1000)), "failed: The value -1000 has no name"s, "a number");
    CheckContains(Written(static_cast<ESize>(2)), "failed: The value 2 has no name"s, "a number of an unsigned type");
    CheckContains(Written(ESize::Huge), "failed: The value 200 has no name"s, "an enumerator with an empty name");
    CheckContains(Written(EEmpty::Only), "failed: The value 0 has no name"s, "an enum without names");

    // A name that isn't valid UTF-8 can't be written as a TOML string
    CheckEquals("failed: The string can't be written: it isn't valid UTF-8 (at byte 1), which TOML needs."s,
        Written(EInvalidName::Bad), "a name that isn't valid UTF-8");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_EnumConverters, ToValue_WritesTheFirstName)
{
    // Act & Assert
    // Written() starts from a table, so these also check that it is replaced
    CheckEquals("\"Red\""s, Written(EColor::Red), "Red");
    CheckEquals("\"Green\""s, Written(EColor::Green), "Green, not its second name Lime");
    CheckEquals("\"Blue\""s, Written(EColor::Blue), "Blue");
    CheckEquals("\"Medium\""s, Written(ESize::Medium), "Medium, whose first entry has an empty name");
    CheckEquals("\"Large\""s, Written(ESize::Large), "Large");
    CheckEquals("\"Manual\""s, Written(EMode::Manual), "Manual, from a std::vector");
    CheckEquals("\"Two\""s, Written(PlainTwo), "PlainTwo, of an unscoped enum");
    CheckEquals("\"Maximized\""s, Written(TWindow::EState::Maximized), "Maximized, from a friend, not Zoomed");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_EnumConverters, WithoutNames_ConvertsAsTheUnderlyingType)
{
    // Act & Assert
    // Any number of the underlying type, an enumerator or not, as an integer (not as a character or a boolean)
    CheckEmpty(ReadProblem(3, static_cast<EFlags>(3)), "3, flags that are no enumerator");
    CheckEmpty(ReadProblem(4294967295, static_cast<EFlags>(4294967295u)), "the largest unsigned int");
    CheckEmpty(ReadProblem(-128, ESmall::Low), "-128, the smallest std::int8_t");
    CheckEmpty(ReadProblem(127, ESmall::High), "127, the largest");
    CheckEmpty(ReadProblem(1, ESwitch::On), "1, a bool enum's true");
    CheckEmpty(ReadProblem(0, ESwitch::Off), "0, its false");
    CheckEmpty(ReadProblem(65535, static_cast<EWide>(65535)), "65535, the largest char16_t");
    CheckEmpty(ReadProblem(255, std::byte{ 255 }), "255, the largest std::byte");
    CheckEmpty(ReadProblem(9223372036854775807, static_cast<EHuge>(9223372036854775807u)),
        "INT64_MAX, the largest a std::uint64_t enum reads");

    CheckEquals("5"s, Written(static_cast<EFlags>(5)), "flags that are no enumerator");
    CheckEquals("-128"s, Written(ESmall::Low), "a std::int8_t enum, as an integer");
    CheckEquals("1"s, Written(ESwitch::On), "a bool enum, as an integer");
    CheckEquals("65"s, Written(static_cast<EWide>(65)), "a char16_t enum, as an integer");
    CheckEquals("171"s, Written(std::byte{ 0xAB }), "std::byte, as an integer");
    CheckEquals("failed: The value 18446744073709551615 is out of range: it must be from 0 to 9223372036854775807."s,
        Written(EHuge::Top), "a std::uint64_t enum above INT64_MAX can't be written");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_EnumConverters, WithoutNames_RefusesWhatTheUnderlyingTypeRefuses)
{
    // Arrange
    ESwitch value = ESwitch::Off;

    // Act
    TASWTomlResult const result = TASWTomlConverter<ESwitch>::FromValue(2, value, TASWTomlValueOptions());

    // Assert
    // The enumerators' names aren't known, and a boolean isn't an integer
    CheckEmpty(RejectProblem<EFlags>("Read", ETomlCode::TypeMismatch), "a name of an enum without names");
    CheckEmpty(RejectProblem<ESwitch>(true, ETomlCode::TypeMismatch), "a boolean for a bool enum");
    CheckEmpty(RefusalProblem(EFlags::Read, { ETomlType::Integer }, "an integer"), "every other type");

    // The underlying type's range
    CheckEmpty(RejectProblem<EFlags>(-1, ETomlCode::ValueOutOfRange), "-1 for an unsigned int");
    CheckEmpty(RejectProblem<EFlags>(4294967296, ETomlCode::ValueOutOfRange), "4294967296 for an unsigned int");
    CheckEmpty(RejectProblem<ESmall>(128, ETomlCode::ValueOutOfRange), "128 for a std::int8_t");
    CheckEmpty(RejectProblem<ESwitch>(2, ETomlCode::ValueOutOfRange), "2 for a bool");
    CheckEmpty(RejectProblem<EWide>(65536, ETomlCode::ValueOutOfRange), "65536 for a char16_t");
    CheckEmpty(RejectProblem<std::byte>(256, ETomlCode::ValueOutOfRange), "256 for std::byte");
    CheckEquals("The value 2 is out of range: it must be from 0 to 1."s, result.Message,
        "the message should give the underlying type's range");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_FloatConverters
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_FloatConverters::TTest_ASWToml_FloatConverters()
    : inherited("ASWToml_FloatConverters_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_FloatConverters::~TTest_ASWToml_FloatConverters()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_FloatConverters::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_FloatConverters::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_FloatConverters::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_FloatConverters::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_FloatConverters, Double_FromValue_ReadsFloatsAndExactIntegers)
{
    // Arrange
    // Integers with at most 53 significant bits (some with trailing zero bits, and the smallest integer, -2^63), then
    // integers with more
    std::int64_t const twoTo53 = std::int64_t(1) << 53;
    std::int64_t const exact[] = { 0, 1, -1, twoTo53 - 1, twoTo53, -twoTo53, (twoTo53 - 1) << 10, std::int64_t(1) << 62,
        std::numeric_limits<std::int64_t>::min() };
    std::int64_t const inexact[] = { twoTo53 + 1, -twoTo53 - 1, (twoTo53 + 1) << 9,
        std::numeric_limits<std::int64_t>::max(), std::numeric_limits<std::int64_t>::min() + 1 };
    double read = 0.0;
    double kept = 1.5;

    // Act
    TASWTomlResult const readFloat = TASWTomlConverter<double>::FromValue(0.1, read, TASWTomlValueOptions());
    TASWTomlResult const refused = TASWTomlConverter<double>::FromValue(twoTo53 + 1, kept, TASWTomlValueOptions());

    // Assert
    CheckTrue(readFloat.IsOk() && read == 0.1, "a float should read as it is");
    CheckEquals(ETomlCode::InvalidValue, refused.Code, "an integer a double can't hold exactly should be refused");
    CheckEquals("The value 9007199254740993 can't be held exactly: it has more than 53 significant bits."s,
        refused.Message, "the message");
    CheckEmpty(refused.Path, "no path: it is about the value itself");
    CheckTrue(kept == 1.5, "a value refused should leave the double as it was");
    for (std::int64_t const number : exact)
    {
        double out = 0.5;
        TASWTomlResult const result = TASWTomlConverter<double>::FromValue(number, out, TASWTomlValueOptions());
        CheckTrue(result.IsOk() && out == static_cast<double>(number), std::to_string(number) +
            " should read exactly: " + result.Message);
    }

    for (std::int64_t const number : inexact)
    {
        double out = 0.5;
        TASWTomlResult const result = TASWTomlConverter<double>::FromValue(number, out, TASWTomlValueOptions());
        CheckTrue(result.Code == ETomlCode::InvalidValue && out == 0.5, std::to_string(number) + " should be refused");
    }

    CheckEmpty(RefusalProblem(1.5, { ETomlType::Float, ETomlType::Integer }, "a float or an integer"),
        "every other type should be refused");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_FloatConverters, Double_RoundTrip_ThroughADocument)
{
    struct TCase
    {
        double Value;
        char const* Text;
    };

    TCase const cases[] = { { 0.1, "0.1" }, { -2.5, "-2.5" }, { 1.0 / 3.0, "0.3333333333333333" }, { 0.0, "0.0" },
        { -0.0, "-0.0" }, { 1e300, "1e+300" }, { std::numeric_limits<double>::max(), "1.7976931348623157e+308" },
        { std::numeric_limits<double>::min(), "2.2250738585072014e-308" },
        { std::numeric_limits<double>::denorm_min(), "5e-324" }, { std::numeric_limits<double>::infinity(), "inf" },
        { -std::numeric_limits<double>::infinity(), "-inf" } };

    for (TCase const& item : cases)
    {
        // Arrange
        double out = 7.0;
        std::string text;

        // Act
        TASWTomlResult const result = ThroughDocument(item.Value, out, text);

        // Assert
        CheckTrue(result.IsOk() && SameBits(item.Value, out), std::string(item.Text) +
            " should read back as the same double: " + result.Message);
        CheckEquals("v = "s + item.Text, FirstLine(text), "the double should be written as its shortest text");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_FloatConverters, Float_FromValue_ReadsTheNearestFloat)
{
    // Arrange
    // 2^24 + 1 needs 25 significant bits, 3 * 2^24 only two
    std::int64_t const twoTo24 = std::int64_t(1) << 24;
    float tenth = 0.0f;
    float exact = 0.0f;
    float negative = 0.0f;
    float spaced = 0.0f;
    float tiny = 1.0f;
    float negativeTiny = 1.0f;
    float smallest = 0.0f;
    float kept = 1.5f;

    // Act
    TASWTomlResult const readTenth = TASWTomlConverter<float>::FromValue(0.1, tenth, TASWTomlValueOptions());
    TASWTomlResult const readExact = TASWTomlConverter<float>::FromValue(twoTo24, exact, TASWTomlValueOptions());
    TASWTomlResult const readNegative = TASWTomlConverter<float>::FromValue(-twoTo24, negative, TASWTomlValueOptions());
    TASWTomlResult const readSpaced = TASWTomlConverter<float>::FromValue(3 * twoTo24, spaced, TASWTomlValueOptions());
    TASWTomlResult const refused = TASWTomlConverter<float>::FromValue(twoTo24 + 1, kept, TASWTomlValueOptions());
    TASWTomlResult const readTiny = TASWTomlConverter<float>::FromValue(1e-300, tiny, TASWTomlValueOptions());
    TASWTomlResult const readNegativeTiny = TASWTomlConverter<float>::FromValue(-1e-300, negativeTiny,
        TASWTomlValueOptions());
    TASWTomlResult const readSmallest = TASWTomlConverter<float>::FromValue(
        static_cast<double>(std::numeric_limits<float>::denorm_min()), smallest, TASWTomlValueOptions());

    // Assert
    CheckTrue(readTenth.IsOk() && tenth == 0.1f, "0.1 should read as the float nearest it");
    CheckTrue(readExact.IsOk() && exact == 16777216.0f, "2^24 should read exactly");
    CheckTrue(readNegative.IsOk() && negative == -16777216.0f, "and so should -2^24");
    CheckTrue(readSpaced.IsOk() && spaced == 50331648.0f, "and 3 * 2^24, whose low bits are 0");
    CheckEquals(ETomlCode::InvalidValue, refused.Code, "an integer a float can't hold exactly should be refused");
    CheckEquals("The value 16777217 can't be held exactly: it has more than 24 significant bits."s, refused.Message,
        "the message");
    CheckTrue(kept == 1.5f, "a value refused should leave the float as it was");
    CheckTrue(readTiny.IsOk() && tiny == 0.0f && !std::signbit(tiny), "a number too small for a float should be 0.0");
    CheckTrue(readNegativeTiny.IsOk() && negativeTiny == 0.0f && std::signbit(negativeTiny), "with its sign");
    CheckTrue(readSmallest.IsOk() && smallest == std::numeric_limits<float>::denorm_min(),
        "the smallest float should read as itself");
    CheckEmpty(RefusalProblem(1.5f, { ETomlType::Float, ETomlType::Integer }, "a float or an integer"),
        "every other type should be refused");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_FloatConverters, Float_FromValue_RefusesWhatIsBeyondItsRange)
{
    // Arrange
    // The largest float plus half the step from it to the next float up, 2^128 - 2^103, would round to infinity; the
    // double below it rounds to the largest float
    float const largest = std::numeric_limits<float>::max();
    double const limit = 0x1.FFFFFFp127;
    double const belowLimit = std::nextafter(limit, 0.0);
    float atLargest = 0.0f;
    float below = 0.0f;
    float negativeBelow = 0.0f;
    float kept = 1.5f;

    // Act
    TASWTomlResult const readLargest = TASWTomlConverter<float>::FromValue(static_cast<double>(largest), atLargest,
        TASWTomlValueOptions());
    TASWTomlResult const readBelow = TASWTomlConverter<float>::FromValue(belowLimit, below, TASWTomlValueOptions());
    TASWTomlResult const readNegativeBelow = TASWTomlConverter<float>::FromValue(-belowLimit, negativeBelow,
        TASWTomlValueOptions());
    TASWTomlResult const atLimit = TASWTomlConverter<float>::FromValue(limit, kept, TASWTomlValueOptions());
    TASWTomlResult const negativeLimit = TASWTomlConverter<float>::FromValue(-limit, kept, TASWTomlValueOptions());
    TASWTomlResult const huge = TASWTomlConverter<float>::FromValue(-1e300, kept, TASWTomlValueOptions());

    // Assert
    CheckTrue(readLargest.IsOk() && atLargest == largest, "the largest float should read as itself");
    CheckTrue(readBelow.IsOk() && below == largest, "a double that rounds to the largest float should read as it");
    CheckTrue(readNegativeBelow.IsOk() && negativeBelow == -largest, "and its negative as the smallest");
    CheckEquals(ETomlCode::ValueOutOfRange, atLimit.Code, "a double that would round to infinity should be refused");
    CheckEquals("The value 3.4028235677973366e+38 is out of range: it must be from -3.4028235e+38 to "
        "3.4028235e+38."s, atLimit.Message, "the message gives the range as the largest float is written");
    CheckEmpty(atLimit.Path, "no path: it is about the value itself");
    CheckEquals(ETomlCode::ValueOutOfRange, negativeLimit.Code, "and so should its negative");
    CheckEquals(ETomlCode::ValueOutOfRange, huge.Code, "and a much larger one");
    CheckTrue(kept == 1.5f, "a value refused should leave the float as it was");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_FloatConverters, Float_RandomValues_ReadBackTheSame)
{
    // Arrange
    // Random bit patterns from a fixed seed, so floats of every exponent, subnormal ones, inf and NaNs
    std::mt19937 random(20261024);
    int failures = 0;
    std::string firstFailure;

    // Act & Assert
    for (int i = 0; i < 200000; ++i)
    {
        std::uint32_t const bits = static_cast<std::uint32_t>(random());
        float const in = std::bit_cast<float>(bits);
        TASWTomlValue value;
        float out = 0.0f;
        TASWTomlResult const written = TASWTomlConverter<float>::ToValue(in, value, TASWTomlValueOptions());
        TASWTomlResult const read = TASWTomlConverter<float>::FromValue(value, out, TASWTomlValueOptions());
        bool same = SameBits(in, out);
        if (std::isnan(in))
            same = std::isnan(out) && std::signbit(out) == std::signbit(in);

        if (!written.IsOk() || !read.IsOk() || !same)
        {
            if (failures == 0)
                firstFailure = std::to_string(bits) + " as " + ASWToml::Detail::ValueText(value);

            ++failures;
        }
    }

    CheckEquals(0, failures, "every float should read back as itself, unlike " + firstFailure);
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_FloatConverters, Float_RoundTrip_ThroughADocument)
{
    struct TCase
    {
        float Value;
        char const* Text;
    };

    // The one positive float that is written as its exact value comes last (see Float_ToValue_WritesTheShortestText)
    TCase const cases[] = { { 0.1f, "0.1" }, { -2.5f, "-2.5" }, { 1.0f / 3.0f, "0.33333334" }, { 0.0f, "0.0" },
        { -0.0f, "-0.0" }, { 16777216.0f, "16777216.0" }, { 1e30f, "1e+30" },
        { std::numeric_limits<float>::max(), "3.4028235e+38" }, { -std::numeric_limits<float>::max(), "-3.4028235e+38" },
        { std::numeric_limits<float>::min(), "1.1754944e-38" }, { std::numeric_limits<float>::denorm_min(), "1e-45" },
        { std::numeric_limits<float>::infinity(), "inf" }, { -std::numeric_limits<float>::infinity(), "-inf" },
        { std::bit_cast<float>(std::uint32_t(0x15AE43FD)), "7.038530691851209e-26" } };

    for (TCase const& item : cases)
    {
        // Arrange
        float out = 7.0f;
        std::string text;

        // Act
        TASWTomlResult const result = ThroughDocument(item.Value, out, text);

        // Assert
        CheckTrue(result.IsOk() && SameBits(item.Value, out), std::string(item.Text) +
            " should read back as the same float: " + result.Message);
        CheckEquals("v = "s + item.Text, FirstLine(text), "the float should be written as its shortest text");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_FloatConverters, Float_ToValue_WritesTheShortestText)
{
    // Arrange
    // 0x15AE43FD is the one positive float whose shortest text, 7.038531e-26, read as a double, reads back as another
    // float, so it is written as its exact value, and so is its negative
    float const odd = std::bit_cast<float>(std::uint32_t(0x15AE43FD));
    TASWTomlValue tenth = TTable{ { "x", 1 } };
    TASWTomlValue oddValue;
    TASWTomlValue negativeOddValue;
    float oddBack = 0.0f;
    float negativeOddBack = 0.0f;

    // Act
    TASWTomlResult const writeTenth = TASWTomlConverter<float>::ToValue(0.1f, tenth, TASWTomlValueOptions());
    TASWTomlResult const writeOdd = TASWTomlConverter<float>::ToValue(odd, oddValue, TASWTomlValueOptions());
    TASWTomlResult const writeNegativeOdd = TASWTomlConverter<float>::ToValue(-odd, negativeOddValue,
        TASWTomlValueOptions());
    TASWTomlResult const readOdd = TASWTomlConverter<float>::FromValue(oddValue, oddBack, TASWTomlValueOptions());
    TASWTomlResult const readNegativeOdd = TASWTomlConverter<float>::FromValue(negativeOddValue, negativeOddBack,
        TASWTomlValueOptions());

    // Assert
    CheckTrue(writeTenth.IsOk() && tenth == TASWTomlValue(0.1), "0.1f should replace the table with the double 0.1");
    CheckTrue(static_cast<float>(7.038531e-26) != odd, "the odd float's shortest text should read as another float");
    CheckTrue(writeOdd.IsOk() && oddValue == TASWTomlValue(static_cast<double>(odd)),
        "so the odd float should be written as its exact value");
    CheckTrue(writeNegativeOdd.IsOk() && negativeOddValue == TASWTomlValue(-static_cast<double>(odd)),
        "and so should its negative");
    CheckTrue(readOdd.IsOk() && SameBits(odd, oddBack), "the odd float should read back as itself");
    CheckTrue(readNegativeOdd.IsOk() && SameBits(-odd, negativeOddBack), "and so should its negative");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_FloatConverters, InfinityAndNan_KeepTheirSign)
{
    for (double const sign : { 1.0, -1.0 })
    {
        // Arrange
        double const infinity = std::copysign(std::numeric_limits<double>::infinity(), sign);
        double const nan = std::copysign(std::numeric_limits<double>::quiet_NaN(), sign);
        float const floatNan = std::copysign(std::numeric_limits<float>::quiet_NaN(), static_cast<float>(sign));
        bool const negative = sign < 0;
        std::string const prefix = negative ? "-" : "";
        double doubleInfinity = 0.0;
        double doubleNan = 0.0;
        double doubleBack = 0.0;
        float floatInfinity = 0.0f;
        float floatNanRead = 0.0f;
        float floatBack = 0.0f;
        std::string doubleText;
        std::string floatText;

        // Act
        TASWTomlResult const readDoubleInfinity = TASWTomlConverter<double>::FromValue(infinity, doubleInfinity,
            TASWTomlValueOptions());
        TASWTomlResult const readDoubleNan = TASWTomlConverter<double>::FromValue(nan, doubleNan,
            TASWTomlValueOptions());
        TASWTomlResult const readFloatInfinity = TASWTomlConverter<float>::FromValue(infinity, floatInfinity,
            TASWTomlValueOptions());
        TASWTomlResult const readFloatNan = TASWTomlConverter<float>::FromValue(nan, floatNanRead,
            TASWTomlValueOptions());
        TASWTomlResult const doubleThrough = ThroughDocument(nan, doubleBack, doubleText);
        TASWTomlResult const floatThrough = ThroughDocument(floatNan, floatBack, floatText);

        // Assert
        CheckTrue(readDoubleInfinity.IsOk() && doubleInfinity == infinity, prefix + "inf should read as a double");
        CheckTrue(readFloatInfinity.IsOk() && std::isinf(floatInfinity) && std::signbit(floatInfinity) == negative,
            prefix + "inf should read as a float");
        CheckTrue(readDoubleNan.IsOk() && std::isnan(doubleNan) && std::signbit(doubleNan) == negative,
            prefix + "nan should read as a double NaN with its sign");
        CheckTrue(readFloatNan.IsOk() && std::isnan(floatNanRead) && std::signbit(floatNanRead) == negative,
            prefix + "nan should read as a float NaN with its sign");
        CheckTrue(doubleThrough.IsOk() && std::isnan(doubleBack) && std::signbit(doubleBack) == negative,
            prefix + "nan should go through a document as a double");
        CheckEquals("v = " + prefix + "nan", FirstLine(doubleText), "written as nan with its sign");
        CheckTrue(floatThrough.IsOk() && std::isnan(floatBack) && std::signbit(floatBack) == negative,
            prefix + "nan should go through a document as a float");
        CheckEquals("v = " + prefix + "nan", FirstLine(floatText), "written the same");
    }
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_IntegerConverters
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_IntegerConverters::TTest_ASWToml_IntegerConverters()
    : inherited("ASWToml_IntegerConverters_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_IntegerConverters::~TTest_ASWToml_IntegerConverters()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_IntegerConverters::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_IntegerConverters::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_IntegerConverters::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_IntegerConverters::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_IntegerConverters, FromValue_ReadsEachTypesRange)
{
    // Act & Assert
    CheckEmpty(IntegerRangeProblem<signed char>(), "signed char should read its range and no more");
    CheckEmpty(IntegerRangeProblem<short>(), "short should read its range and no more");
    CheckEmpty(IntegerRangeProblem<int>(), "int should read its range and no more");
    CheckEmpty(IntegerRangeProblem<long>(), "long should read its range and no more");
    CheckEmpty(IntegerRangeProblem<long long>(), "long long should read its range and no more");
    CheckEmpty(IntegerRangeProblem<unsigned char>(), "unsigned char should read its range and no more");
    CheckEmpty(IntegerRangeProblem<unsigned short>(), "unsigned short should read its range and no more");
    CheckEmpty(IntegerRangeProblem<unsigned int>(), "unsigned int should read its range and no more");
    CheckEmpty(IntegerRangeProblem<unsigned long>(), "unsigned long should read its range and no more");
    CheckEmpty(IntegerRangeProblem<unsigned long long>(), "unsigned long long should read its range and no more");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_IntegerConverters, FromValue_ReadsIntegersOnly)
{
    // Arrange
    int kept = 7;

    // Act
    TASWTomlResult const whole = TASWTomlConverter<int>::FromValue(3.0, kept, TASWTomlValueOptions());

    // Assert
    CheckEquals(ETomlCode::TypeMismatch, whole.Code, "a float should be refused even when it is whole");
    CheckEquals("The value 3.0 is a float, not an integer."s, whole.Message, "the message");
    CheckEquals(7, kept, "a value refused should leave the integer as it was");
    std::string const wanted = "an integer";
    CheckEmpty(RefusalProblem<signed char>(7, { ETomlType::Integer }, wanted), "signed char should refuse the others");
    CheckEmpty(RefusalProblem<short>(7, { ETomlType::Integer }, wanted), "short should refuse the others");
    CheckEmpty(RefusalProblem<int>(7, { ETomlType::Integer }, wanted), "int should refuse the others");
    CheckEmpty(RefusalProblem<long>(7, { ETomlType::Integer }, wanted), "long should refuse the others");
    CheckEmpty(RefusalProblem<long long>(7, { ETomlType::Integer }, wanted), "long long should refuse the others");
    CheckEmpty(RefusalProblem<unsigned char>(7, { ETomlType::Integer }, wanted),
        "unsigned char should refuse the others");
    CheckEmpty(RefusalProblem<unsigned short>(7, { ETomlType::Integer }, wanted),
        "unsigned short should refuse the others");
    CheckEmpty(RefusalProblem<unsigned int>(7, { ETomlType::Integer }, wanted), "unsigned int should refuse the others");
    CheckEmpty(RefusalProblem<unsigned long>(7, { ETomlType::Integer }, wanted),
        "unsigned long should refuse the others");
    CheckEmpty(RefusalProblem<unsigned long long>(7, { ETomlType::Integer }, wanted),
        "unsigned long long should refuse the others");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_IntegerConverters, Messages_GiveTheRange)
{
    // Arrange
    std::uint8_t byte = 1;
    std::int8_t small = 1;
    std::uint64_t large = 1;

    // Act
    TASWTomlResult const above = TASWTomlConverter<std::uint8_t>::FromValue(300, byte, TASWTomlValueOptions());
    TASWTomlResult const below = TASWTomlConverter<std::int8_t>::FromValue(-129, small, TASWTomlValueOptions());
    TASWTomlResult const negative = TASWTomlConverter<std::uint64_t>::FromValue(-1, large, TASWTomlValueOptions());

    // Assert
    CheckEquals("The value 300 is out of range: it must be from 0 to 255."s, above.Message, "above the range");
    CheckEquals("The value -129 is out of range: it must be from -128 to 127."s, below.Message, "below it");
    CheckEquals("The value -1 is out of range: it must be from 0 to 9223372036854775807."s, negative.Message,
        "an unsigned 64-bit type's range should end where a TOML integer's does");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_IntegerConverters, RoundTrip_EveryType_ThroughADocument)
{
    // Act & Assert
    CheckEmpty(IntegerRoundTripProblem<signed char>(), "signed char should go through a document");
    CheckEmpty(IntegerRoundTripProblem<short>(), "short should go through a document");
    CheckEmpty(IntegerRoundTripProblem<int>(), "int should go through a document");
    CheckEmpty(IntegerRoundTripProblem<long>(), "long should go through a document");
    CheckEmpty(IntegerRoundTripProblem<long long>(), "long long should go through a document");
    CheckEmpty(IntegerRoundTripProblem<unsigned char>(), "unsigned char should go through a document");
    CheckEmpty(IntegerRoundTripProblem<unsigned short>(), "unsigned short should go through a document");
    CheckEmpty(IntegerRoundTripProblem<unsigned int>(), "unsigned int should go through a document");
    CheckEmpty(IntegerRoundTripProblem<unsigned long>(), "unsigned long should go through a document");
    CheckEmpty(IntegerRoundTripProblem<unsigned long long>(), "unsigned long long should go through a document");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_IntegerConverters, ToValue_RefusesWhatATomlIntegerCantHold)
{
    // Arrange
    std::uint64_t const largest = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    TASWTomlValue written = TTable{ { "x", 1 } };
    TASWTomlValue negative;
    TASWTomlValue kept = "kept";

    // Act
    TASWTomlResult const writeLargest = TASWTomlConverter<std::uint64_t>::ToValue(largest, written,
        TASWTomlValueOptions());
    TASWTomlResult const writeNegative = TASWTomlConverter<short>::ToValue(-5, negative, TASWTomlValueOptions());
    TASWTomlResult const above = TASWTomlConverter<std::uint64_t>::ToValue(largest + 1, kept, TASWTomlValueOptions());
    TASWTomlResult const max = TASWTomlConverter<std::uint64_t>::ToValue(std::numeric_limits<std::uint64_t>::max(),
        kept, TASWTomlValueOptions());

    // Assert
    CheckTrue(writeLargest.IsOk() && written == TASWTomlValue(std::numeric_limits<std::int64_t>::max()),
        "the largest TOML integer should replace the table");
    CheckTrue(writeNegative.IsOk() && negative == TASWTomlValue(-5), "a negative number should be written as it is");
    CheckEquals(ETomlCode::ValueOutOfRange, above.Code, "a number above INT64_MAX can't be written");
    CheckEquals("The value 9223372036854775808 is out of range: it must be from 0 to 9223372036854775807."s,
        above.Message, "the message gives the range");
    CheckEmpty(above.Path, "no path: it is about the value itself");
    CheckEquals("The value 18446744073709551615 is out of range: it must be from 0 to 9223372036854775807."s,
        max.Message, "nor can the largest unsigned 64-bit number");
    CheckTrue(kept == TASWTomlValue("kept"), "a number refused should leave the value as it was");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_TextConverters
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_TextConverters::TTest_ASWToml_TextConverters()
    : inherited("ASWToml_TextConverters_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_TextConverters::~TTest_ASWToml_TextConverters()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_TextConverters::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_TextConverters::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_TextConverters::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_TextConverters::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TextConverters, ApplicationConverter_IsUsedAsTheLibrarys)
{
    // Arrange
    TTestWord word;
    TTestWord kept{ "kept" };
    TTestBox<TTestWord> box;
    TTestBox<TTestWord> const written{ TTestWord{ "boxed" } };
    std::string text;

    // Act
    TASWTomlResult const read = TASWTomlConverter<TTestWord>::FromValue("hello", word, TASWTomlValueOptions());
    TASWTomlResult const refused = TASWTomlConverter<TTestWord>::FromValue("two words", kept, TASWTomlValueOptions());
    TASWTomlResult const mismatch = TASWTomlConverter<TTestWord>::FromValue(5, kept, TASWTomlValueOptions());
    TASWTomlResult const roundTrip = ThroughDocument(written, box, text);

    // Assert
    CheckTrue(read.IsOk() && word.Text == "hello", "the application's converter should read its type");
    CheckEquals(ETomlCode::InvalidValue, refused.Code, "and refuse what it doesn't accept");
    CheckEquals("The value \"two words\" isn't one word."s, refused.Message, "with the library's message helper");
    CheckEquals(ETomlCode::TypeMismatch, mismatch.Code, "the library's converter it uses refuses other types");
    CheckEquals("kept"s, kept.Text, "a value refused should leave it as it was");
    CheckTrue(roundTrip.IsOk() && box == written, "a partial specialization should convert through a document: " +
        roundTrip.Message);
    CheckContains(text, "v = \"boxed\""s, "as the string its item is");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TextConverters, Helpers_PathOfAPart)
{
    // Arrange
    TASWTomlResult failure = ASWToml::Detail::ValueFailure(ETomlCode::ValueOutOfRange, 70000, "is too large.");

    // Act
    TASWTomlResult const port = ASWToml::Detail::KeyFailure(failure, "port");
    TASWTomlResult const element = ASWToml::Detail::ElementFailure(port, 1);
    TASWTomlResult const servers = ASWToml::Detail::KeyFailure(element, "servers");
    TASWTomlResult const nested = ASWToml::Detail::ElementFailure(ASWToml::Detail::ElementFailure(failure, 2), 0);
    TASWTomlResult const quoted = ASWToml::Detail::KeyFailure(ASWToml::Detail::KeyFailure(failure, "a.b"), "host name");
    TASWTomlResult const invalid = ASWToml::Detail::KeyFailure(failure, "\xFF");
    TASWTomlResult const partly = ASWToml::Detail::KeyFailure(failure, "x\xFF\xFE" "caf\xC3\xA9");
    TASWTomlResult const empty = ASWToml::Detail::KeyFailure(failure, "");

    // Assert
    CheckEquals("port"s, port.Path, "a key of the value");
    CheckEquals("[1].port"s, element.Path, "an element in front of a key");
    CheckEquals("servers[1].port"s, servers.Path, "a key in front of an element");
    CheckEquals("[0][2]"s, nested.Path, "an element of an element");
    CheckEquals("\"host name\".\"a.b\""s, quoted.Path, "keys that can't be bare, as basic strings");
    CheckEquals("\"?\""s, invalid.Path, "a key that isn't valid UTF-8, its bytes that aren't as '?'");
    CheckEquals("\"x??caf\xC3\xA9\""s, partly.Path, "each byte that isn't, the valid ones as they are");
    CheckEquals("\"\""s, empty.Path, "the empty key");
    CheckEquals(ETomlCode::ValueOutOfRange, servers.Code, "the code stays");
    CheckEquals(failure.Message, servers.Message, "and so does the message");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TextConverters, Helpers_TypeMismatch)
{
    // Arrange
    std::vector<std::string> const names = { "a string", "an integer", "a float", "a boolean", "an offset date-time",
        "a local date-time", "a local date", "a local time", "an array", "a table" };
    TArray const values = OneOfEachType();

    // Act
    TASWTomlResult const result = ASWToml::Detail::TypeMismatch(5, "a string");

    // Assert
    AssertEquals(names.size(), values.size(), "a name for each type");
    for (std::size_t i = 0; i < values.size(); ++i)
        CheckEquals(names[i], std::string(ASWToml::Detail::TypeName(values[i])), "the name of each type");

    CheckEquals(ETomlCode::TypeMismatch, result.Code, "the code");
    CheckEquals("The value 5 is an integer, not a string."s, result.Message, "the message");
    CheckEmpty(result.Path, "no path: it is about the value itself");
    CheckEquals(std::size_t(0), result.Line, "not about a place in a text");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TextConverters, Helpers_ValueText)
{
    // Arrange
    // A string of 50 letters is 52 bytes with its quotes, cut to 40; one with a two-byte character at bytes 39 and 40
    // is cut before it
    std::string const letters(50, 'a');
    std::string const accented = std::string(38, 'a') + "\xC3\xA9" "bc";
    std::chrono::year_month_day const date{ std::chrono::year{ 1979 }, std::chrono::May, std::chrono::day{ 27 } };
    std::chrono::year_month_day const badDate{ std::chrono::year{ 2023 }, std::chrono::February,
        std::chrono::day{ 30 } };

    // Act & Assert
    CheckEquals("\"a\\tb\""s, ASWToml::Detail::ValueText("a\tb"), "a string as a basic string, escaped");
    CheckEquals("\"C:\\\\x\""s, ASWToml::Detail::ValueText("C:\\x"), "a backslash too, never a literal string");
    CheckEquals("42"s, ASWToml::Detail::ValueText(42), "an integer");
    CheckEquals("2.5"s, ASWToml::Detail::ValueText(2.5), "a float");
    CheckEquals("1979-05-27"s, ASWToml::Detail::ValueText(date), "a date");
    CheckEquals("[1, 2]"s, ASWToml::Detail::ValueText(TArray{ 1, 2 }), "an array on one line");
    CheckEquals("{ x = 1 }"s, ASWToml::Detail::ValueText(TTable{ { "x", 1 } }), "a table as an inline table");
    CheckEquals("\"" + std::string(39, 'a') + "...", ASWToml::Detail::ValueText(letters), "cut to 40 bytes");
    CheckEquals("\"" + std::string(38, 'a') + "\"", ASWToml::Detail::ValueText(std::string(38, 'a')),
        "40 bytes, as they are");
    CheckEquals("\"" + std::string(38, 'a') + "...", ASWToml::Detail::ValueText(accented),
        "cut before a UTF-8 sequence, not inside it");
    CheckEquals("of type String"s, ASWToml::Detail::ValueText("\xFF"), "a string that isn't valid UTF-8");
    CheckEquals("of type LocalDate"s, ASWToml::Detail::ValueText(badDate), "a date that isn't valid");
    CheckEquals("The value 7 is odd."s, ASWToml::Detail::ValueFailure(ETomlCode::InvalidValue, 7, "is odd.").Message,
        "a message about a value");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TextConverters, Path_ReadsAndWritesUtf8)
{
    // Arrange
    std::filesystem::path const accented = ASWToml::Utf8ToPath("caf\xC3\xA9/app.toml");
#if defined(_WIN32)
    std::filesystem::path const native = L"C:\\Users\\me\\app.toml";
    std::string const nativeLine = "v = 'C:\\Users\\me\\app.toml'";
#else
    std::filesystem::path const native = "/home/me/app.toml";
    std::string const nativeLine = "v = \"/home/me/app.toml\"";
#endif
    TASWTomlValue value;
    std::filesystem::path read;
    std::filesystem::path roundTrip;
    std::string text;

    // Act
    TASWTomlResult const written = TASWTomlConverter<std::filesystem::path>::ToValue(accented, value,
        TASWTomlValueOptions());
    TASWTomlResult const readBack = TASWTomlConverter<std::filesystem::path>::FromValue(value, read,
        TASWTomlValueOptions());
    TASWTomlResult const throughDocument = ThroughDocument(native, roundTrip, text);

    // Assert
    CheckTrue(written.IsOk() && value == TASWTomlValue("caf\xC3\xA9/app.toml"), "a path should be its UTF-8 text");
    CheckTrue(readBack.IsOk() && read == accented, "and read back as the same path");
    CheckTrue(throughDocument.IsOk() && roundTrip == native, "a native path should go through a document: " +
        throughDocument.Message);
    CheckContains(text, nativeLine, "with its separators as it holds them (a Windows path as a literal string)");
    CheckEmpty(RefusalProblem(native, { ETomlType::String }, "a string"), "every other type should be refused");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TextConverters, Path_RefusesWhatDoesntReadBack)
{
    // Arrange
    std::filesystem::path out = "kept";
    TASWTomlValue value = 5;
#if defined(_WIN32)
    // An unpaired surrogate, which UTF-8 can't hold
    std::filesystem::path const unwritable = std::wstring{ L'a', static_cast<wchar_t>(0xD800), L'b' };
#else
    // A byte that isn't UTF-8
    std::filesystem::path const unwritable = "a\xFF" "b";
#endif

    // Act
    TASWTomlResult const read = TASWTomlConverter<std::filesystem::path>::FromValue("a\xFF", out,
        TASWTomlValueOptions());
    TASWTomlResult const written = TASWTomlConverter<std::filesystem::path>::ToValue(unwritable, value,
        TASWTomlValueOptions());

    // Assert
    CheckEquals(ETomlCode::InvalidValue, read.Code, "a string that isn't valid UTF-8 isn't a path");
    CheckEquals("The value of type String isn't a path: it isn't valid UTF-8."s, read.Message, "the message");
    CheckTrue(out == "kept", "the path should stay as it was");
    CheckEquals(ETomlCode::InvalidValue, written.Code, "a path that wouldn't read back can't be written");
    CheckContains(written.Message, "wouldn't read back as the same path"s, "the message");
    CheckTrue(value == TASWTomlValue(5), "the value should stay as it was");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TextConverters, String_ReadsStringsOnly)
{
    // Arrange
    std::string out;
    std::string kept = "kept";

    // Act
    TASWTomlResult const read = TASWTomlConverter<std::string>::FromValue("caf\xC3\xA9", out, TASWTomlValueOptions());
    TASWTomlResult const number = TASWTomlConverter<std::string>::FromValue(80, kept, TASWTomlValueOptions());

    // Assert
    CheckTrue(read.IsOk() && out == "caf\xC3\xA9", "a string should read as it is");
    CheckEquals("The value 80 is an integer, not a string."s, number.Message, "a number isn't a string");
    CheckEquals("kept"s, kept, "a value refused should leave the string as it was");
    CheckEmpty(RefusalProblem("kept"s, { ETomlType::String }, "a string"), "every other type should be refused");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TextConverters, String_RoundTrip_ThroughADocument)
{
    // Arrange
    std::vector<std::string> const texts = { "", "plain", "with \"quotes\"", "line\nbreak", "tab\t and \\ backslash",
        "C:\\Users\\me", "it's", "caf\xC3\xA9", "\x7F" "delete", "\x01" "control", "  blanks  " };

    for (std::string const& in : texts)
    {
        std::string out = "not read";
        std::string text;

        // Act
        TASWTomlResult const result = ThroughDocument(in, out, text);

        // Assert
        CheckTrue(result.IsOk(), Escaped(in) + ": should go through a document: " + result.Message);
        CheckEquals(Escaped(in), Escaped(out), Escaped(in) + ": should read back the same");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TextConverters, String_WritesValidUtf8Only)
{
    // Arrange
    TASWTomlValue value = 5;

    // Act
    TASWTomlResult const invalid = TASWTomlConverter<std::string>::ToValue("ab\xFF", value, TASWTomlValueOptions());
    TASWTomlResult const surrogate = TASWTomlConverter<std::string>::ToValue("\xED\xA0\x80", value,
        TASWTomlValueOptions());
    TASWTomlResult const valid = TASWTomlConverter<std::string>::ToValue("caf\xC3\xA9", value,
        TASWTomlValueOptions());

    // Assert
    CheckEquals(ETomlCode::InvalidValue, invalid.Code, "a byte that isn't UTF-8 can't be written");
    CheckEquals("The string can't be written: it isn't valid UTF-8 (at byte 2), which TOML needs."s, invalid.Message,
        "the message gives the byte");
    CheckEquals(ETomlCode::InvalidValue, surrogate.Code, "nor a surrogate");
    CheckTrue(valid.IsOk() && value == TASWTomlValue("caf\xC3\xA9"), "valid UTF-8 is written as it is");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TextConverters, U8String_AsTheStringsBytes)
{
    // Arrange
    std::u8string const accented = { u8'c', u8'a', u8'f', static_cast<char8_t>(0xC3), static_cast<char8_t>(0xA9) };
    std::u8string const invalid = { u8'a', static_cast<char8_t>(0xFF) };
    std::u8string out;
    std::u8string roundTrip;
    TASWTomlValue value = 5;
    std::string text;

    // Act
    TASWTomlResult const read = TASWTomlConverter<std::u8string>::FromValue("caf\xC3\xA9", out,
        TASWTomlValueOptions());
    TASWTomlResult const refused = TASWTomlConverter<std::u8string>::ToValue(invalid, value, TASWTomlValueOptions());
    TASWTomlResult const throughDocument = ThroughDocument(accented, roundTrip, text);

    // Assert
    CheckTrue(read.IsOk() && out == accented, "a string should read as its bytes");
    CheckEquals(ETomlCode::InvalidValue, refused.Code, "bytes that aren't UTF-8 can't be written");
    CheckTrue(value == TASWTomlValue(5), "the value should stay as it was");
    CheckTrue(throughDocument.IsOk() && roundTrip == accented, "it should go through a document: " +
        throughDocument.Message);
    CheckEmpty(RefusalProblem(accented, { ETomlType::String }, "a string"), "every other type should be refused");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TextConverters, Value_AsItIs)
{
    for (TASWTomlValue const& in : OneOfEachType())
    {
        // Arrange
        TASWTomlValue read = "not read";
        TASWTomlValue written = "not written";
        std::string const type = ASWToml::ToString(in.GetType());

        // Act
        TASWTomlResult const fromValue = TASWTomlConverter<TASWTomlValue>::FromValue(in, read, TASWTomlValueOptions());
        TASWTomlResult const toValue = TASWTomlConverter<TASWTomlValue>::ToValue(in, written, TASWTomlValueOptions());

        // Assert
        CheckTrue(fromValue.IsOk() && read == in, type + ": a value should read as it is");
        CheckTrue(toValue.IsOk() && written == in, type + ": and be written as it is");
    }
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_BoolConverter, "Converters")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_EnumConverters, "Converters")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_FloatConverters, "Converters")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_IntegerConverters, "Converters")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_TextConverters, "Converters")
