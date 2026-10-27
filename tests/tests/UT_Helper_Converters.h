/* **************************************************************************
UT_Helper_Converters.h
Author: Anthony S. West - ASW Software

Test helpers of the converter tests (Test_ASWToml_Converters and its parts):
the test types and enums they share, with the converters and the field and
name lists an application would declare, functions that check a
converter's results, and the random values of the fuzz tests, which the
JSON tests use too.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#ifndef UT_Helper_ConvertersH
#define UT_Helper_ConvertersH
//---------------------------------------------------------------------------
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <tuple>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWToml_Converters.h"
#include "ASWToml_Document.h"
#include "ASWToml_Options.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
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

// The structs of the struct converter tests, each with ASWTomlFields() as an application declares it, next to the
// struct


/////////////////////////////////////////////////////////////////////////////
// TTestServer
//
// The struct of decision 12's example: a field that a table must have, an optional one, and a std::optional member
/////////////////////////////////////////////////////////////////////////////
struct TTestServer
{
    std::string Host;
    std::uint16_t Port = 80;
    bool Enabled = true;
    std::optional<std::string> Comment;

    bool operator==(TTestServer const& other) const = default;
};

inline auto ASWTomlFields(TTestServer const&)
{
    return std::tuple{ ASWToml::Field("host", &TTestServer::Host), ASWToml::Field("port", &TTestServer::Port),
        ASWToml::OptionalField("enabled", &TTestServer::Enabled), ASWToml::Field("comment", &TTestServer::Comment) };
}


/////////////////////////////////////////////////////////////////////////////
// TTestScreen
//
// A class whose fields are private members, listed by a friend in a static constexpr tuple
/////////////////////////////////////////////////////////////////////////////
class TTestScreen
{
private:
    int m_Width = 640;
    int m_Height = 480;

public:
    TTestScreen() = default;

    TTestScreen(int width, int height)
        : m_Width(width),
          m_Height(height)
    {
    }

    bool operator==(TTestScreen const& other) const = default;

    friend auto const& ASWTomlFields(TTestScreen const&)
    {
        static constexpr std::tuple fields{ ASWToml::Field("width", &TTestScreen::m_Width),
            ASWToml::OptionalField("height", &TTestScreen::m_Height) };

        return fields;
    }
};


/////////////////////////////////////////////////////////////////////////////
// TTestCluster
//
// A struct whose fields hold a struct, an optional struct, a map and a vector of structs
/////////////////////////////////////////////////////////////////////////////
struct TTestCluster
{
    std::string Name;
    TTestScreen Screen;
    std::optional<TTestScreen> Spare;
    std::map<std::string, int> Limits;
    std::vector<TTestServer> Servers;

    bool operator==(TTestCluster const& other) const = default;
};

inline auto ASWTomlFields(TTestCluster const&)
{
    return std::tuple{ ASWToml::Field("name", &TTestCluster::Name), ASWToml::Field("screen", &TTestCluster::Screen),
        ASWToml::Field("spare", &TTestCluster::Spare), ASWToml::OptionalField("limits", &TTestCluster::Limits),
        ASWToml::OptionalField("servers", &TTestCluster::Servers) };
}

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

inline auto const& ASWTomlEnumNames(EColor)
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

inline std::vector<ASWToml::TASWTomlEnumName<EEmpty> > ASWTomlEnumNames(EEmpty)
{
    return std::vector<ASWToml::TASWTomlEnumName<EEmpty> >();
}

// A name that isn't valid UTF-8, so it can't be written
enum class EInvalidName
{
    Bad,
};

inline auto const& ASWTomlEnumNames(EInvalidName)
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

} // namespace ASWToml

namespace ASWUnitTests
{

// The keys of the random tables (see RandomTomlValue()): the test structs' keys, map keys of each kind, and a few
// others
inline constexpr char const* FuzzKeys[] = {
    "host", "port", "enabled", "comment", "name", "screen", "spare", "limits", "servers", "width", "height", "1", "-1",
    "80", "01", "Red", "red", "Lime", "Blue", "a", "a b", "",
};

//---------------------------------------------------------------------------

// The date 'year'-'month'-'day', real or not
std::chrono::year_month_day Date(int year, unsigned month, unsigned day);

// 'text' for a message: printable ASCII as it is, a backslash as \\, and every other byte as \xHH
std::string Escaped(std::string const& text);

// 'text' up to its first line break (LF or CRLF), or all of it when it has none
std::string FirstLine(std::string const& text);

// 'text' with each CRLF line break as LF, so a new document's text (whose line breaks are the platform's) compares the
// same everywhere
std::string LfText(std::string const& text);

// A value of each TOML type, in the order of ETomlType
ASWToml::TASWTomlValue::TValueVector OneOfEachType();

// A random real date of the years 0 to 9999, which TOML writes
std::chrono::year_month_day RandomDate(std::mt19937& generator);

// A random double: one time in four any bits (NaNs with payloads among them), else one of a few that matter to the
// converters (beyond float's range, the float pair whose shortest text reads back as another float, integers a float
// or double can't hold exactly, -0.0, the infinities, NaN), or a small number with a fraction
double RandomDouble(std::mt19937& generator);

// A random integer: one time in four any 64 bits, one time in four one at the end of a type's range (or just past it),
// or a number a float or double can't hold exactly, else a small one
std::int64_t RandomInteger(std::mt19937& generator);

// A random text of up to six pieces: letters, names the test enums have, blanks, a line break, a control character,
// quotes, a backslash, two- and four-byte UTF-8 sequences, and, unless 'validUtf8', at times a byte that isn't UTF-8
std::string RandomText(std::mt19937& generator, bool validUtf8);

// A random valid time of day: any hour, minute and second (60 too), and one time in two a fraction
ASWToml::TASWTomlLocalTime RandomTime(std::mt19937& generator);

// A random value a document can hold: a string, an integer, a float, a boolean, each of the date and time types, or
// below a 'depth' of 3, an array of up to three values or a table of up to three keys of FuzzKeys
ASWToml::TASWTomlValue RandomTomlValue(std::mt19937& generator, std::size_t depth);

// What is wrong with how TASWTomlConverter<T>::FromValue() fails to read 'value': it must fail with 'code', the message
// 'message' and the Path 'path', and leave 'out' (set to 'initial' first) unchanged; empty when nothing is wrong
template <ASWToml::TomlConvertible T>
std::string ReadFailureProblem(ASWToml::TASWTomlValue const& value, T const& initial, ASWToml::ETomlCode code,
    std::string const& message, std::string const& path);

// What is wrong with how TASWTomlConverter<T>::FromValue() refuses each value of OneOfEachType() but those of the types
// 'accepted': it must fail with TypeMismatch, a message that says the value's type isn't 'wanted' (e.g. "a string"), no
// Path, and 'out' (set to 'initial' first) unchanged; empty when nothing is wrong
template <ASWToml::TomlConvertible T>
std::string RefusalProblem(T const& initial, std::initializer_list<ASWToml::ETomlType> accepted,
    std::string const& wanted);

// What is wrong with how 'in', a map, goes through a document (see ThroughDocument()): it must read back as itself, and
// the document's text, with LF line breaks, must be 'expected'; empty when nothing is wrong
template <ASWToml::TomlConvertible T>
std::string TableRoundTripProblem(T const& in, std::string const& expected);

// Converts 'in' with TASWTomlConverter<T>::ToValue(), sets it as the key v of an empty document, sets 'text' to the
// document's text, parses that text again and reads v back into 'out' with FromValue(): the first step that fails, or
// Ok
template <ASWToml::TomlConvertible T>
ASWToml::TASWTomlResult ThroughDocument(T const& in, T& out, std::string& text);

// The time of day 'hour':'minute':'second' and 'nanoseconds', valid or not
ASWToml::TASWTomlLocalTime TimeOfDay(int hour, int minute, int second, std::int64_t nanoseconds = 0);

// What is wrong with how TASWTomlConverter<T>::ToValue() fails to write 'in': it must fail with 'code', the message
// 'message' and the Path 'path', and leave the value (a table first) unchanged; empty when nothing is wrong
template <ASWToml::TomlConvertible T>
std::string WriteFailureProblem(T const& in, ASWToml::ETomlCode code, std::string const& message,
    std::string const& path);

// 'in' as TASWTomlConverter<T>::ToValue() writes it, into a value that is a table first: the value as TOML text (see
// ValueText()), or "failed: " and the message, with "(changed)" after it when the table changed
template <ASWToml::TomlConvertible T>
std::string Written(T const& in);

//---------------------------------------------------------------------------
// The templates' definitions
//---------------------------------------------------------------------------
template <ASWToml::TomlConvertible T>
std::string ReadFailureProblem(ASWToml::TASWTomlValue const& value, T const& initial, ASWToml::ETomlCode code,
    std::string const& message, std::string const& path)
{
    T out = initial;
    ASWToml::TASWTomlResult const result = ASWToml::TASWTomlConverter<T>::FromValue(value, out,
        ASWToml::TASWTomlValueOptions());
    std::string const text = ASWToml::Detail::ValueText(value);
    if (result.Code != code)
        return text + ": the code is " + ASWToml::ToString(result.Code);

    if (result.Message != message)
        return text + ": the message is " + Escaped(result.Message);

    if (result.Path != path)
        return text + ": the path is " + result.Path;

    if (!(out == initial))
        return text + ": the value read into changed";

    return std::string();
}
//---------------------------------------------------------------------------
template <ASWToml::TomlConvertible T>
std::string RefusalProblem(T const& initial, std::initializer_list<ASWToml::ETomlType> accepted,
    std::string const& wanted)
{
    for (ASWToml::TASWTomlValue const& value : OneOfEachType())
    {
        bool isAccepted = false;
        for (ASWToml::ETomlType const type : accepted)
        {
            if (value.GetType() == type)
                isAccepted = true;
        }

        if (isAccepted)
            continue;

        T out = initial;
        ASWToml::TASWTomlResult const result = ASWToml::TASWTomlConverter<T>::FromValue(value, out,
            ASWToml::TASWTomlValueOptions());
        std::string const type = ASWToml::ToString(value.GetType());
        std::string const expected = "The value " + ASWToml::Detail::ValueText(value) + " is " +
            ASWToml::Detail::TypeName(value) + ", not " + wanted + ".";
        if (result.Code != ASWToml::ETomlCode::TypeMismatch)
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
template <ASWToml::TomlConvertible T>
std::string TableRoundTripProblem(T const& in, std::string const& expected)
{
    T out{};
    std::string text;
    std::string const name = Escaped(FirstLine(expected));
    ASWToml::TASWTomlResult const result = ThroughDocument(in, out, text);
    if (!result.IsOk())
        return name + ": " + result.Message;

    if (!(out == in))
        return name + ": it should read back as itself";

    if (LfText(text) != expected)
        return name + ": the text is " + Escaped(text);

    return std::string();
}
//---------------------------------------------------------------------------
template <ASWToml::TomlConvertible T>
ASWToml::TASWTomlResult ThroughDocument(T const& in, T& out, std::string& text)
{
    ASWToml::TASWTomlValue value;
    ASWToml::TASWTomlResult result = ASWToml::TASWTomlConverter<T>::ToValue(in, value, ASWToml::TASWTomlValueOptions());
    if (!result.IsOk())
        return result;

    ASWToml::TASWTomlDocument document;
    result = document.SetValue("v", value);
    if (!result.IsOk())
        return result;

    text = document.ToText();
    ASWToml::TASWTomlDocument read;
    result = read.Parse(text);
    if (!result.IsOk())
        return result;

    ASWToml::TASWTomlValue const* const found = read.Find("v");
    if (found == nullptr)
    {
        result.Code = ASWToml::ETomlCode::NotFound;
        result.Message = "The document has no key v.";

        return result;
    }

    return ASWToml::TASWTomlConverter<T>::FromValue(*found, out, ASWToml::TASWTomlValueOptions());
}
//---------------------------------------------------------------------------
template <ASWToml::TomlConvertible T>
std::string WriteFailureProblem(T const& in, ASWToml::ETomlCode code, std::string const& message,
    std::string const& path)
{
    ASWToml::TASWTomlValue const table = ASWToml::TASWTomlValue::TKeyValueVector{ { "x", 1 } };
    ASWToml::TASWTomlValue value = table;
    ASWToml::TASWTomlResult const result = ASWToml::TASWTomlConverter<T>::ToValue(in, value,
        ASWToml::TASWTomlValueOptions());
    if (result.Code != code)
        return path + ": the code is " + ASWToml::ToString(result.Code);

    if (result.Message != message)
        return path + ": the message is " + Escaped(result.Message);

    if (result.Path != path)
        return path + ": the path is " + result.Path;

    if (!(value == table))
        return path + ": the value written into changed";

    return std::string();
}
//---------------------------------------------------------------------------
template <ASWToml::TomlConvertible T>
std::string Written(T const& in)
{
    ASWToml::TASWTomlValue const table = ASWToml::TASWTomlValue::TKeyValueVector{ { "x", 1 } };
    ASWToml::TASWTomlValue value = table;
    ASWToml::TASWTomlResult const result = ASWToml::TASWTomlConverter<T>::ToValue(in, value,
        ASWToml::TASWTomlValueOptions());
    if (result.IsOk())
        return ASWToml::Detail::ValueText(value);

    return "failed: " + result.Message + (value == table ? "" : " (changed)");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef UT_Helper_ConvertersH
