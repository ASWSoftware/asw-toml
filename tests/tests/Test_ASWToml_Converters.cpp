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
#include <functional>
#include <initializer_list>
#include <limits>
#include <list>
#include <map>
#include <memory_resource>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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


/////////////////////////////////////////////////////////////////////////////
// TTestNoDefault
//
// An application's type with a converter (an integer, below) but no default constructor, so a container has no
// element to read into; with a hash, so an unordered set of it is a type that can be made
/////////////////////////////////////////////////////////////////////////////
struct TTestNoDefault
{
    struct THash
    {
        std::size_t operator()(TTestNoDefault const& value) const
        {
            return std::hash<int>()(value.Number);
        }
    };

    int Number;

    explicit TTestNoDefault(int number) : Number(number)
    {
    }

    bool operator==(TTestNoDefault const& other) const = default;
};


/////////////////////////////////////////////////////////////////////////////
// TTestTextLess
//
// A set's comparator of strings that ignores the case of ASCII letters when it is made so: state a set should read
// with only when the set is its own
/////////////////////////////////////////////////////////////////////////////
struct TTestTextLess
{
    bool IgnoreCase = false;

    // 'text' with its ASCII letters in lower case
    static std::string Folded(std::string text)
    {
        for (char& c : text)
        {
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        }

        return text;
    }

    bool operator()(std::string const& left, std::string const& right) const
    {
        if (IgnoreCase)
            return Folded(left) < Folded(right);

        return left < right;
    }
};


/////////////////////////////////////////////////////////////////////////////
// TTestTextEqual
//
// An unordered set's key equality of strings, which ignores the case of ASCII letters when it is made so, as
// TTestTextLess
/////////////////////////////////////////////////////////////////////////////
struct TTestTextEqual
{
    bool IgnoreCase = false;

    bool operator()(std::string const& left, std::string const& right) const
    {
        if (IgnoreCase)
            return TTestTextLess::Folded(left) == TTestTextLess::Folded(right);

        return left == right;
    }
};


/////////////////////////////////////////////////////////////////////////////
// TTestTextHash
//
// The hash of a string with its ASCII letters in lower case, which fits TTestTextEqual either way, mixed with a seed:
// state an unordered set should read with only when the set is its own
/////////////////////////////////////////////////////////////////////////////
struct TTestTextHash
{
    std::size_t Seed = 0;

    std::size_t operator()(std::string const& text) const
    {
        return std::hash<std::string>()(TTestTextLess::Folded(text)) ^ Seed;
    }
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

auto ASWTomlFields(TTestServer const&)
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
// TTestNamed
//
// A base class without fields of its own, whose member a derived struct lists
/////////////////////////////////////////////////////////////////////////////
struct TTestNamed
{
    std::string Name;

    bool operator==(TTestNamed const& other) const = default;
};


/////////////////////////////////////////////////////////////////////////////
// TTestLevel
//
// A struct that lists a member of its base class as a field
/////////////////////////////////////////////////////////////////////////////
struct TTestLevel : TTestNamed
{
    int Level = 1;

    bool operator==(TTestLevel const& other) const = default;
};

auto ASWTomlFields(TTestLevel const&)
{
    return std::tuple{ ASWToml::Field("name", &TTestLevel::Name), ASWToml::Field("level", &TTestLevel::Level) };
}


/////////////////////////////////////////////////////////////////////////////
// TTestExtended
//
// A struct derived from one with fields, without a list of its own, so it is converted by its base's list
/////////////////////////////////////////////////////////////////////////////
struct TTestExtended : TTestServer
{
    int Extra = 1;

    bool operator==(TTestExtended const& other) const = default;
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

auto ASWTomlFields(TTestCluster const&)
{
    return std::tuple{ ASWToml::Field("name", &TTestCluster::Name), ASWToml::Field("screen", &TTestCluster::Screen),
        ASWToml::Field("spare", &TTestCluster::Spare), ASWToml::OptionalField("limits", &TTestCluster::Limits),
        ASWToml::OptionalField("servers", &TTestCluster::Servers) };
}

// A struct without fields: an empty table
struct TTestEmpty
{
    bool operator==(TTestEmpty const& other) const = default;
};

std::tuple<> ASWTomlFields(TTestEmpty const&)
{
    return std::tuple<>();
}

// A struct whose list names a key twice, and one whose key isn't valid UTF-8, so neither can be written
struct TTestTwice
{
    int First = 1;
    int Second = 2;

    bool operator==(TTestTwice const& other) const = default;
};

auto ASWTomlFields(TTestTwice const&)
{
    return std::tuple{ ASWToml::Field("a", &TTestTwice::First), ASWToml::OptionalField("a", &TTestTwice::Second) };
}

struct TTestBadKey
{
    int Number = 1;

    bool operator==(TTestBadKey const& other) const = default;
};

auto ASWTomlFields(TTestBadKey const&)
{
    return std::tuple{ ASWToml::Field("\xFF", &TTestBadKey::Number) };
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

// Two values whose names differ only in the case of their letters, so both read as the first, and a value between
// them
enum class ETwin
{
    Upper,
    Other,
    Lower,
};

auto const& ASWTomlEnumNames(ETwin)
{
    static constexpr ASWToml::TASWTomlEnumName<ETwin> names[] = {
        { ETwin::Upper, "Twin" }, { ETwin::Other, "Other" }, { ETwin::Lower, "twin" },
    };

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

// A struct whose ASWTomlFields() doesn't return a std::tuple of fields, and one whose fields are another struct's, so
// neither has a converter. They are outside the anonymous namespace, since a function of it used only in a
// static_assert would be a warning (Clang's -Wunneeded-internal-declaration).
struct TTestNotFields
{
    int Number = 1;
};

std::vector<int> ASWTomlFields(TTestNotFields const&);

struct TTestOtherFields
{
    int Number = 1;
};

std::tuple<ASWToml::TASWTomlField<TTestNotFields, int> > ASWTomlFields(TTestOtherFields const&);

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

template <>
struct TASWTomlConverter<ASWUnitTests::TTestNoDefault>
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, ASWUnitTests::TTestNoDefault& out,
        TASWTomlValueOptions const& options)
    {
        return TASWTomlConverter<int>::FromValue(value, out.Number, options);
    }

    static TASWTomlResult ToValue(ASWUnitTests::TTestNoDefault const& in, TASWTomlValue& value,
        TASWTomlValueOptions const& options)
    {
        return TASWTomlConverter<int>::ToValue(in.Number, value, options);
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

// The dates, time points and times of day, of any duration the date and time types convert, and the TOML date and
// time types; not a duration (TOML has none), nor a time point or time of day that counts in a floating-point type
static_assert(ASWToml::TomlConvertible<std::chrono::year_month_day>, "year_month_day should have a converter");
static_assert(ASWToml::TomlConvertible<std::chrono::sys_days> && ASWToml::TomlConvertible<std::chrono::local_days>,
    "sys_days and local_days should have converters");
static_assert(ASWToml::TomlConvertible<std::chrono::sys_seconds> &&
    ASWToml::TomlConvertible<std::chrono::system_clock::time_point>, "sys_seconds and system_clock should have one");
static_assert(ASWToml::TomlConvertible<std::chrono::local_seconds> &&
    ASWToml::TomlConvertible<std::chrono::local_time<std::chrono::nanoseconds> >, "local times should have one");
static_assert(ASWToml::TomlConvertible<std::chrono::hh_mm_ss<std::chrono::seconds> > &&
    ASWToml::TomlConvertible<std::chrono::hh_mm_ss<std::chrono::nanoseconds> >, "hh_mm_ss should have one");
static_assert(ASWToml::TomlConvertible<ASWToml::TASWTomlLocalDateTime> &&
    ASWToml::TomlConvertible<ASWToml::TASWTomlLocalTime> && ASWToml::TomlConvertible<ASWToml::TASWTomlOffsetDateTime>,
    "the TOML date and time types should have converters");
static_assert(!ASWToml::TomlConvertible<std::chrono::seconds>, "a duration should have no converter");
static_assert(!ASWToml::TomlConvertible<std::chrono::sys_time<std::chrono::duration<double> > >,
    "a time point that counts in a double should have no converter");
static_assert(!ASWToml::TomlConvertible<std::chrono::hh_mm_ss<std::chrono::duration<double> > >,
    "nor a time of day that counts in one");
static_assert(!ASWToml::TomlConvertible<std::chrono::year>, "a year alone should have no converter");

// The arrays: vectors (with any allocator), std::array, the sets, pairs and tuples of the types with converters and
// default constructors, nested too; not those of other types, nor other containers
static_assert(ASWToml::TomlConvertible<std::vector<int> > && ASWToml::TomlConvertible<std::vector<bool> >,
    "vectors should have converters, std::vector<bool> too");
static_assert(ASWToml::TomlConvertible<std::pmr::vector<std::string> >, "with any allocator");
static_assert(ASWToml::TomlConvertible<std::vector<std::vector<ASWUnitTests::EColor> > >, "a vector of vectors too");
static_assert(ASWToml::TomlConvertible<std::vector<ASWToml::TASWTomlValue> >, "a vector of values: a mixed array");
static_assert(ASWToml::TomlConvertible<std::vector<ASWUnitTests::TTestWord> >, "a vector of an application's type");
static_assert(ASWToml::TomlConvertible<std::array<double, 3> > && ASWToml::TomlConvertible<std::array<int, 0> >,
    "std::array should have a converter, of no elements too");
static_assert(ASWToml::TomlConvertible<std::set<std::string> > &&
    ASWToml::TomlConvertible<std::set<int, std::greater<> > > &&
    ASWToml::TomlConvertible<std::unordered_set<std::int64_t> >, "the sets should have converters");
static_assert(ASWToml::TomlConvertible<std::pair<std::string, std::uint16_t> > &&
    ASWToml::TomlConvertible<std::tuple<std::string, int, bool> > && ASWToml::TomlConvertible<std::tuple<> >,
    "pairs and tuples should have converters, the empty tuple too");
static_assert(!ASWToml::TomlConvertible<std::vector<char const*> > && !ASWToml::TomlConvertible<std::vector<char> >,
    "a vector of a type without a converter should have none");
static_assert(!ASWToml::TomlConvertible<std::array<char const*, 2> > &&
    !ASWToml::TomlConvertible<std::set<std::string_view> > && !ASWToml::TomlConvertible<std::unordered_set<char> > &&
    !ASWToml::TomlConvertible<std::pair<int, char const*> > && !ASWToml::TomlConvertible<std::tuple<int, char> >,
    "nor should another array of one");
static_assert(ASWToml::TomlConvertible<ASWUnitTests::TTestNoDefault> &&
    !ASWToml::TomlConvertible<std::vector<ASWUnitTests::TTestNoDefault> > &&
    !ASWToml::TomlConvertible<std::array<ASWUnitTests::TTestNoDefault, 1> > &&
    !ASWToml::TomlConvertible<std::set<ASWUnitTests::TTestNoDefault> > &&
    !ASWToml::TomlConvertible<std::unordered_set<ASWUnitTests::TTestNoDefault, ASWUnitTests::TTestNoDefault::THash> > &&
    !ASWToml::TomlConvertible<std::pair<ASWUnitTests::TTestNoDefault, int> > &&
    !ASWToml::TomlConvertible<std::pair<int, ASWUnitTests::TTestNoDefault> > &&
    !ASWToml::TomlConvertible<std::tuple<ASWUnitTests::TTestNoDefault> >,
    "an array of a type without a default constructor should have no converter: it has no element to read into");
static_assert(!ASWToml::TomlConvertible<std::list<int> >, "a std::list should have no converter");

// The tables: maps (with any comparator, hash or allocator) whose keys are strings, integers or enums, of the types
// with converters and default constructors, nested too; not maps with other keys, nor of other types
static_assert(ASWToml::TomlConvertible<std::map<std::string, int> > &&
    ASWToml::TomlConvertible<std::unordered_map<std::string, int> >, "maps of strings should have converters");
static_assert(ASWToml::TomlConvertible<std::map<int, std::string> > &&
    ASWToml::TomlConvertible<std::map<std::uint64_t, int> > &&
    ASWToml::TomlConvertible<std::unordered_map<std::int8_t, int> >, "and of integers");
static_assert(ASWToml::TomlConvertible<std::map<ASWUnitTests::EColor, int> > &&
    ASWToml::TomlConvertible<std::map<ASWUnitTests::EFlags, int> > &&
    ASWToml::TomlConvertible<std::unordered_map<ASWUnitTests::ESwitch, int> > &&
    ASWToml::TomlConvertible<std::map<std::byte, int> >, "and of enums with and without names");
static_assert(ASWToml::TomlConvertible<std::map<std::string, int, std::greater<> > > &&
    ASWToml::TomlConvertible<std::pmr::map<std::string, int> > &&
    ASWToml::TomlConvertible<std::pmr::unordered_map<std::string, int> >, "with any comparator and allocator");
static_assert(ASWToml::TomlConvertible<std::map<std::string, std::map<std::string, std::vector<int> > > > &&
    ASWToml::TomlConvertible<std::map<std::string, ASWToml::TASWTomlValue> >, "of tables, arrays and values too");
static_assert(!ASWToml::TomlConvertible<std::map<double, int> > && !ASWToml::TomlConvertible<std::map<bool, int> > &&
    !ASWToml::TomlConvertible<std::map<char, int> > && !ASWToml::TomlConvertible<std::map<std::string_view, int> > &&
    !ASWToml::TomlConvertible<std::map<std::u8string, int> > &&
    !ASWToml::TomlConvertible<std::unordered_map<std::filesystem::path, int> >,
    "a map whose keys aren't strings, integers or enums should have no converter");
static_assert(!ASWToml::TomlConvertible<std::map<std::string, char const*> > &&
    !ASWToml::TomlConvertible<std::unordered_map<int, char> > &&
    !ASWToml::TomlConvertible<std::map<std::string, ASWUnitTests::TTestNoDefault> > &&
    !ASWToml::TomlConvertible<std::unordered_map<std::string, ASWUnitTests::TTestNoDefault> >,
    "nor should a map of a type without a converter or a default constructor");
static_assert(!ASWToml::TomlConvertible<std::multimap<std::string, int> >, "a std::multimap should have no converter");

// The structs: those for which argument-dependent lookup finds ASWTomlFields() returning a std::tuple of their fields
// (or of a base class's), in a vector or a map too; not a struct without it, or whose function returns anything else
static_assert(ASWToml::Detail::StructWithFields<ASWUnitTests::TTestServer> &&
    ASWToml::Detail::StructWithFields<ASWUnitTests::TTestScreen> &&
    ASWToml::Detail::StructWithFields<ASWUnitTests::TTestLevel> &&
    ASWToml::Detail::StructWithFields<ASWUnitTests::TTestEmpty>, "a struct with a field list should be found");
static_assert(ASWToml::Detail::StructWithFields<ASWUnitTests::TTestExtended>,
    "a struct derived from one with a field list should be found, with its base's list");
static_assert(!ASWToml::Detail::StructWithFields<ASWUnitTests::TTestNamed> &&
    !ASWToml::Detail::StructWithFields<ASWUnitTests::TTestNotFields> &&
    !ASWToml::Detail::StructWithFields<ASWUnitTests::TTestOtherFields> &&
    !ASWToml::Detail::StructWithFields<ASWUnitTests::TTestServer const>,
    "a struct without a field list, or whose list isn't a std::tuple of its own fields, should be none");
static_assert(ASWToml::TomlConvertible<ASWUnitTests::TTestServer> &&
    ASWToml::TomlConvertible<std::vector<ASWUnitTests::TTestServer> > &&
    ASWToml::TomlConvertible<std::map<std::string, ASWUnitTests::TTestCluster> >,
    "a struct should have a converter, in a vector or a map too");
static_assert(!ASWToml::TomlConvertible<ASWUnitTests::TTestNamed> &&
    !ASWToml::TomlConvertible<ASWUnitTests::TTestNotFields>, "a struct without a field list should have none");

// The field lists can be constant expressions
static_assert(ASWToml::Field("a", &ASWUnitTests::TTestServer::Host).Key == "a" &&
    !ASWToml::Field("a", &ASWUnitTests::TTestServer::Host).Optional &&
    ASWToml::OptionalField("b", &ASWUnitTests::TTestServer::Port).Optional &&
    ASWToml::OptionalField("b", &ASWUnitTests::TTestServer::Port).Member == &ASWUnitTests::TTestServer::Port,
    "Field() and OptionalField() should make a field in a constant expression");

namespace ASWUnitTests
{

namespace
{

using namespace std::string_literals;

using ASWToml::ETomlCode;
using ASWToml::ETomlType;
using ASWToml::TASWTomlConverter;
using ASWToml::TASWTomlDocument;
using ASWToml::TASWTomlLocalDateTime;
using ASWToml::TASWTomlLocalTime;
using ASWToml::TASWTomlOffsetDateTime;
using ASWToml::TASWTomlResult;
using ASWToml::TASWTomlValue;
using ASWToml::TASWTomlValueOptions;
using ASWToml::TomlConvertible;

using TArray = TASWTomlValue::TValueVector;
using TTable = TASWTomlValue::TKeyValueVector;

template <TomlConvertible T>
std::string ArrayRoundTripProblem(T const& in, std::string const& firstLine);
std::chrono::year_month_day Date(int year, unsigned month, unsigned day);
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
std::string LfText(std::string const& text);
TArray OneOfEachType();
template <TomlConvertible T>
std::string ReadFailureProblem(TASWTomlValue const& value, T const& initial, ETomlCode code,
    std::string const& message, std::string const& path);
template <typename T>
std::string ReadProblem(TASWTomlValue const& value, T expected);
template <TomlConvertible T>
std::string RefusalProblem(T const& initial, std::initializer_list<ETomlType> accepted, std::string const& wanted);
template <typename T>
std::string RejectProblem(TASWTomlValue const& value, ETomlCode code);
template <typename T>
bool SameBits(T a, T b);
template <TomlConvertible T>
std::string TableRoundTripProblem(T const& in, std::string const& expected);
template <TomlConvertible T>
TASWTomlResult ThroughDocument(T const& in, T& out, std::string& text);
ASWToml::TASWTomlLocalTime TimeOfDay(int hour, int minute, int second, std::int64_t nanoseconds = 0);
template <TomlConvertible T>
std::string WriteFailureProblem(T const& in, ETomlCode code, std::string const& message, std::string const& path);
template <TomlConvertible T>
std::string Written(T const& in);

//---------------------------------------------------------------------------
/*
    ArrayRoundTripProblem

    What is wrong with how 'in', a container that isn't empty, goes through a document (see ThroughDocument()): it
    must read back as itself, and the document's first line must be 'firstLine'; empty when nothing is wrong
*/
template <TomlConvertible T>
std::string ArrayRoundTripProblem(T const& in, std::string const& firstLine)
{
    T out{};
    std::string text;
    TASWTomlResult const result = ThroughDocument(in, out, text);
    if (!result.IsOk())
        return firstLine + ": " + result.Message;

    if (!(out == in))
        return firstLine + ": it should read back as itself";

    if (FirstLine(text) != firstLine)
        return firstLine + ": the text is " + Escaped(text);

    return std::string();
}
//---------------------------------------------------------------------------
/*
    Date

    The date 'year'-'month'-'day', real or not
*/
std::chrono::year_month_day Date(int year, unsigned month, unsigned day)
{
    return std::chrono::year_month_day{ std::chrono::year{ year }, std::chrono::month{ month },
        std::chrono::day{ day } };
}
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
    LfText

    'text' with each CRLF line break as LF, so a new document's text (whose line breaks are the platform's) compares
    the same everywhere
*/
std::string LfText(std::string const& text)
{
    std::string lf;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != '\r' || i + 1 == text.size() || text[i + 1] != '\n')
            lf += text[i];
    }

    return lf;
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
    ReadFailureProblem

    What is wrong with how TASWTomlConverter<T>::FromValue() fails to read 'value': it must fail with 'code', the
    message 'message' and the Path 'path', and leave 'out' (set to 'initial' first) unchanged; empty when nothing is
    wrong
*/
template <TomlConvertible T>
std::string ReadFailureProblem(TASWTomlValue const& value, T const& initial, ETomlCode code,
    std::string const& message, std::string const& path)
{
    T out = initial;
    TASWTomlResult const result = TASWTomlConverter<T>::FromValue(value, out, TASWTomlValueOptions());
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
    TableRoundTripProblem

    What is wrong with how 'in', a map, goes through a document (see ThroughDocument()): it must read back as itself,
    and the document's text, with LF line breaks, must be 'expected'; empty when nothing is wrong
*/
template <TomlConvertible T>
std::string TableRoundTripProblem(T const& in, std::string const& expected)
{
    T out{};
    std::string text;
    std::string const name = Escaped(FirstLine(expected));
    TASWTomlResult const result = ThroughDocument(in, out, text);
    if (!result.IsOk())
        return name + ": " + result.Message;

    if (!(out == in))
        return name + ": it should read back as itself";

    if (LfText(text) != expected)
        return name + ": the text is " + Escaped(text);

    return std::string();
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
    TimeOfDay

    The time of day 'hour':'minute':'second' and 'nanoseconds', valid or not
*/
ASWToml::TASWTomlLocalTime TimeOfDay(int hour, int minute, int second, std::int64_t nanoseconds)
{
    ASWToml::TASWTomlLocalTime time;
    time.Hour = std::chrono::hours(hour);
    time.Minute = std::chrono::minutes(minute);
    time.Second = std::chrono::seconds(second);
    time.Fraction = std::chrono::nanoseconds(nanoseconds);

    return time;
}
//---------------------------------------------------------------------------
/*
    WriteFailureProblem

    What is wrong with how TASWTomlConverter<T>::ToValue() fails to write 'in': it must fail with 'code', the message
    'message' and the Path 'path', and leave the value (a table first) unchanged; empty when nothing is wrong
*/
template <TomlConvertible T>
std::string WriteFailureProblem(T const& in, ETomlCode code, std::string const& message, std::string const& path)
{
    TASWTomlValue const table = TTable{ { "x", 1 } };
    TASWTomlValue value = table;
    TASWTomlResult const result = TASWTomlConverter<T>::ToValue(in, value, TASWTomlValueOptions());
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
// TTest_ASWToml_ArrayConverters
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_ArrayConverters::TTest_ASWToml_ArrayConverters()
    : inherited("ASWToml_ArrayConverters_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_ArrayConverters::~TTest_ASWToml_ArrayConverters()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ArrayConverters::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ArrayConverters::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ArrayConverters::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ArrayConverters::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ArrayConverters, Array_ReadsExactlyItsLength)
{
    // Arrange
    using TThree = std::array<int, 3>;
    TThree const initial = { 7, 7, 7 };
    TThree three = initial;
    std::array<int, 0> none;

    // Act
    TASWTomlResult const read = TASWTomlConverter<TThree>::FromValue(TArray{ 1, 2, 3 }, three, TASWTomlValueOptions());
    TASWTomlResult const readNone = TASWTomlConverter<std::array<int, 0> >::FromValue(TArray{}, none,
        TASWTomlValueOptions());

    // Assert
    CheckTrue(read.IsOk() && three == TThree{ 1, 2, 3 }, "an array of three elements should read: " + read.Message);
    CheckTrue(readNone.IsOk(), "an empty array should read as a std::array of none");
    CheckEquals("[1, 2, 3]"s, Written(TThree{ 1, 2, 3 }), "the elements should be written in order");
    CheckEmpty(ReadFailureProblem(TArray{ 1, 2 }, initial, ETomlCode::InvalidValue,
        "The value [1, 2] has 2 elements, not 3.", ""), "an array of two elements should be refused");
    CheckEmpty(ReadFailureProblem(TArray{ 1 }, initial, ETomlCode::InvalidValue, "The value [1] has 1 element, not 3.",
        ""), "and one of one");
    CheckEmpty(ReadFailureProblem(TArray{ 1, 2, 3, 4 }, initial, ETomlCode::InvalidValue,
        "The value [1, 2, 3, 4] has 4 elements, not 3.", ""), "and one of four");
    CheckEmpty(ReadFailureProblem(TArray{ 1 }, std::array<int, 0>(), ETomlCode::InvalidValue,
        "The value [1] has 1 element, not 0.", ""), "a std::array of none should read only an empty array");
    CheckEmpty(ReadFailureProblem(TArray{ 1, "x", 3 }, initial, ETomlCode::TypeMismatch,
        "The value \"x\" is a string, not an integer.", "[1]"), "an element that fails should fail it, at its index");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ArrayConverters, Elements_UseTheirTypesConverter)
{
    // Arrange
    std::vector<EColor> colors;
    std::vector<TTestWord> const initialWords = { TTestWord{ "kept" } };

    // Act
    TASWTomlResult const read = TASWTomlConverter<std::vector<EColor> >::FromValue(TArray{ "red", "Lime", 40 }, colors,
        TASWTomlValueOptions());

    // Assert
    CheckTrue(read.IsOk() && colors == std::vector<EColor>{ EColor::Red, EColor::Green, EColor::Blue },
        "each element should read as the enum reads it: " + read.Message);
    CheckEquals("[\"Red\", \"Green\"]"s, Written(std::vector<EColor>{ EColor::Red, EColor::Green }),
        "and be written as it writes it");
    CheckEmpty(WriteFailureProblem(std::vector<EColor>{ EColor::Red, EColor::Unlisted }, ETomlCode::InvalidValue,
        "The value 7 has no name in the enum's ASWTomlEnumNames(), so it can't be written.", "[1]"),
        "so a value without a name can't be written");
    CheckEmpty(ReadFailureProblem(TArray{ "one", "two words" }, initialWords, ETomlCode::InvalidValue,
        "The value \"two words\" isn't one word.", "[1]"), "an application's converter should read each element");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ArrayConverters, EveryType_RefusesWhatIsntAnArray)
{
    // Arrange & Act & Assert
    std::initializer_list<ETomlType> const accepted = { ETomlType::Array };
    CheckEmpty(RefusalProblem(std::vector<int>{ 7 }, accepted, "an array"), "a vector should read arrays only");
    CheckEmpty(RefusalProblem(std::array<int, 1>{ 7 }, accepted, "an array"), "and a std::array");
    CheckEmpty(RefusalProblem(std::set<int>{ 7 }, accepted, "an array"), "and a std::set");
    CheckEmpty(RefusalProblem(std::unordered_set<int>{ 7 }, accepted, "an array"), "and a std::unordered_set");
    CheckEmpty(RefusalProblem(std::pair<int, int>{ 7, 7 }, accepted, "an array"), "and a std::pair");
    CheckEmpty(RefusalProblem(std::tuple<int>{ 7 }, accepted, "an array"), "and a std::tuple");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ArrayConverters, Helpers_SortByText)
{
    // Arrange
    TASWTomlValue const one = TArray{ 1 };
    TASWTomlValue const table = TTable{ { "x", 1 } };
    TArray elements = { true, table, "D", 2, one, "C:\\d" };
    TArray none;

    // Act
    ASWToml::Detail::SortByText(elements);
    ASWToml::Detail::SortByText(none);

    // Assert
    CheckTrue(elements == TArray{ "C:\\d", "D", 2, one, true, table },
        "the elements should be in the order of their texts, strings as basic strings (\"C:\\\\d\" before \"D\"): " +
        ASWToml::Detail::ValueText(elements));
    CheckTrue(none.empty(), "no elements should stay none");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ArrayConverters, Pair_ReadsTwoElementsOfTheirOwnTypes)
{
    // Arrange
    using TEndpoint = std::pair<std::string, std::uint16_t>;
    // The numbers as std::uint16_t, so std::pair's constructor converts none
    TEndpoint const initial = { "kept", std::uint16_t{ 1 } };
    TEndpoint const expected = { "example.com", std::uint16_t{ 443 } };
    TEndpoint endpoint = initial;

    // Act
    TASWTomlResult const read = TASWTomlConverter<TEndpoint>::FromValue(TArray{ "example.com", 443 }, endpoint,
        TASWTomlValueOptions());

    // Assert
    CheckTrue(read.IsOk() && endpoint == expected, "a pair should read: " + read.Message);
    CheckEquals("[\"example.com\", 443]"s, Written(expected), "the first element should be written first");
    CheckEmpty(ReadFailureProblem(TArray{ 443, "example.com" }, initial, ETomlCode::TypeMismatch,
        "The value 443 is an integer, not a string.", "[0]"), "each element should be read as its own type");
    CheckEmpty(ReadFailureProblem(TArray{ "example.com", 70000 }, initial, ETomlCode::ValueOutOfRange,
        "The value 70000 is out of range: it must be from 0 to 65535.", "[1]"), "the second one too");
    CheckEmpty(ReadFailureProblem(TArray{ "example.com" }, initial, ETomlCode::InvalidValue,
        "The value [\"example.com\"] has 1 element, not 2.", ""), "an array of another length should be refused");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ArrayConverters, RoundTrip_EveryType_ThroughADocument)
{
    // Arrange
    TASWTomlValue const firstTable = TTable{ { "a", 1 } };
    TASWTomlValue const secondTable = TTable{ { "a", 2 } };
    std::vector<std::string> const letters = { "a", "b" };
    std::vector<std::string> const letter = { "c" };
    std::vector<std::vector<std::string> > const nested = { letters, std::vector<std::string>(), letter };
    std::vector<int> emptied = { 7 };
    std::string text;

    // Act
    TASWTomlResult const empty = ThroughDocument(std::vector<int>(), emptied, text);

    // Assert
    CheckTrue(empty.IsOk() && emptied.empty(), "an empty vector should go through a document: " + empty.Message);
    CheckEquals("v = []"s, FirstLine(text), "as an empty array");
    CheckEmpty(ArrayRoundTripProblem(std::vector<int>{ 1, 2, 3 }, "v = [1, 2, 3]"), "a vector of integers");
    CheckEmpty(ArrayRoundTripProblem(nested, "v = [[\"a\", \"b\"], [], [\"c\"]]"), "a vector of vectors");
    CheckEmpty(ArrayRoundTripProblem(std::vector<bool>{ true, false }, "v = [true, false]"), "a std::vector<bool>");
    CheckEmpty(ArrayRoundTripProblem(std::array<double, 2>{ 0.5, -2.0 }, "v = [0.5, -2.0]"), "a std::array");
    CheckEmpty(ArrayRoundTripProblem(std::set<std::string>{ "b", "a" }, "v = [\"a\", \"b\"]"), "a std::set");
    CheckEmpty(ArrayRoundTripProblem(std::unordered_set<int>{ 9, 10 }, "v = [10, 9]"), "a std::unordered_set");
    CheckEmpty(ArrayRoundTripProblem(std::pair<std::string, int>{ "example.com", 443 }, "v = [\"example.com\", 443]"),
        "a std::pair");
    CheckEmpty(ArrayRoundTripProblem(std::tuple<int, bool, std::string>{ 1, true, "x" }, "v = [1, true, \"x\"]"),
        "a std::tuple");
    CheckEmpty(ArrayRoundTripProblem(std::vector<TASWTomlValue>{ 1, "x", 2.5 }, "v = [1, \"x\", 2.5]"),
        "an array of mixed types");
    CheckEmpty(ArrayRoundTripProblem(std::vector<TASWTomlValue>{ firstTable, secondTable }, "[[v]]"),
        "an array of tables");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ArrayConverters, Set_ReadsEachElementOnce)
{
    // Arrange
    std::set<int> const initial = { 7 };
    std::unordered_set<int> const initialUnordered = { 7 };
    std::set<EColor> const initialColors = { EColor::Blue };
    std::set<int> numbers = initial;
    std::unordered_set<int> unordered = initialUnordered;

    // Act
    TASWTomlResult const read = TASWTomlConverter<std::set<int> >::FromValue(TArray{ 3, 1 }, numbers,
        TASWTomlValueOptions());
    TASWTomlResult const readUnordered = TASWTomlConverter<std::unordered_set<int> >::FromValue(TArray{ 3, 1 },
        unordered, TASWTomlValueOptions());

    // Assert
    CheckTrue(read.IsOk() && numbers == std::set<int>{ 1, 3 }, "the elements should replace a set's: " + read.Message);
    CheckTrue(readUnordered.IsOk() && unordered == std::unordered_set<int>{ 1, 3 },
        "and an unordered set's: " + readUnordered.Message);
    CheckEmpty(ReadFailureProblem(TArray{ 1, 2, 1 }, initial, ETomlCode::InvalidValue,
        "The value 1 reads as an element the set already has.", "[2]"),
        "an element read twice should fail the set, at its second index");
    CheckEmpty(ReadFailureProblem(TArray{ 2, 2 }, initialUnordered, ETomlCode::InvalidValue,
        "The value 2 reads as an element the set already has.", "[1]"), "and an unordered set");
    CheckEmpty(ReadFailureProblem(TArray{ "Red", "Lime", "green" }, initialColors, ETomlCode::InvalidValue,
        "The value \"green\" reads as an element the set already has.", "[2]"), "two names of a value are one element");
    CheckEmpty(ReadFailureProblem(TArray{ 1, "x", 1 }, initial, ETomlCode::TypeMismatch,
        "The value \"x\" is a string, not an integer.", "[1]"), "the first element that fails should fail the set");
    CheckEmpty(ReadFailureProblem(TArray{ 1, "x" }, initialUnordered, ETomlCode::TypeMismatch,
        "The value \"x\" is a string, not an integer.", "[1]"), "and an unordered set");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ArrayConverters, Set_ReadsWithTheSetsOwnComparator)
{
    // Arrange
    using TNoCaseSet = std::set<std::string, TTestTextLess>;
    using TNoCaseHashSet = std::unordered_set<std::string, TTestTextHash, TTestTextEqual>;
    TNoCaseSet const initial(TTestTextLess{ true });
    TNoCaseHashSet const initialHashed(0, TTestTextHash{ 7 }, TTestTextEqual{ true });
    TNoCaseSet texts = initial;
    TNoCaseHashSet hashed = initialHashed;

    // Act
    TASWTomlResult const read = TASWTomlConverter<TNoCaseSet>::FromValue(TArray{ "B", "a" }, texts,
        TASWTomlValueOptions());
    TASWTomlResult const readHashed = TASWTomlConverter<TNoCaseHashSet>::FromValue(TArray{ "B", "a" }, hashed,
        TASWTomlValueOptions());

    // Assert
    CheckTrue(read.IsOk() && texts.key_comp().IgnoreCase, "the set should keep its comparator: " + read.Message);
    CheckEquals("[\"a\", \"B\"]"s, Written(texts), "and its order, which ignores the case");
    CheckTrue(readHashed.IsOk() && hashed.key_eq().IgnoreCase && hashed.hash_function().Seed == 7,
        "an unordered set should keep its key equality and hash");
    CheckEmpty(ReadFailureProblem(TArray{ "a", "A" }, initial, ETomlCode::InvalidValue,
        "The value \"A\" reads as an element the set already has.", "[1]"), "A should be a, as its comparator says");
    CheckEmpty(ReadFailureProblem(TArray{ "a", "A" }, initialHashed, ETomlCode::InvalidValue,
        "The value \"A\" reads as an element the set already has.", "[1]"), "or an unordered set's key equality");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ArrayConverters, Set_WritesInItsOrder)
{
    // Arrange & Act & Assert
    CheckEquals("[1, 2, 3]"s, Written(std::set<int>{ 3, 1, 2 }), "a set should be written in its order");
    CheckEquals("[3, 2, 1]"s, Written(std::set<int, std::greater<> >{ 1, 3, 2 }), "whichever its comparator gives");
    CheckEquals("[]"s, Written(std::set<int>()), "an empty set as an empty array");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ArrayConverters, ToValue_FailsAtTheFirstElementThatFails)
{
    // Arrange
    std::string const invalid = "\xFF";
    std::string const message = "The string can't be written: it isn't valid UTF-8 (at byte 0), which TOML needs.";
    std::vector<std::string> const letter = { "a" };
    std::vector<std::string> const invalids = { invalid, invalid };
    std::vector<std::vector<std::string> > const nested = { letter, invalids };

    // Act & Assert
    CheckEmpty(WriteFailureProblem(std::vector<std::string>{ "a", invalid, invalid }, ETomlCode::InvalidValue, message,
        "[1]"), "a vector should fail at the first element that can't be written");
    CheckEmpty(WriteFailureProblem(nested, ETomlCode::InvalidValue, message, "[1][0]"), "inside a nested one too");
    CheckEmpty(WriteFailureProblem(std::vector<unsigned long long>{ 1, 18446744073709551615u },
        ETomlCode::ValueOutOfRange, "The value 18446744073709551615 is out of range: it must be from 0 to "
        "9223372036854775807.", "[1]"), "with the element's code and message");
    CheckEmpty(WriteFailureProblem(std::array<std::string, 2>{ invalid, "a" }, ETomlCode::InvalidValue, message, "[0]"),
        "a std::array too");
    CheckEmpty(WriteFailureProblem(std::set<std::string>{ "a", invalid }, ETomlCode::InvalidValue, message, "[1]"),
        "a set at the element's index in its order");
    CheckEmpty(WriteFailureProblem(std::unordered_set<std::string>{ invalid }, ETomlCode::InvalidValue, message, "[0]"),
        "an unordered set too");
    CheckEmpty(WriteFailureProblem(std::pair<std::string, std::string>{ "a", invalid }, ETomlCode::InvalidValue,
        message, "[1]"), "a pair");
    CheckEmpty(WriteFailureProblem(std::tuple<std::string, int, std::string>{ invalid, 1, invalid },
        ETomlCode::InvalidValue, message, "[0]"), "and a tuple");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ArrayConverters, Tuple_ReadsEachElementAsItsOwnType)
{
    // Arrange
    using TRecord = std::tuple<std::string, int, bool>;
    TRecord const initial = { "kept", 7, false };
    TRecord record = initial;
    std::tuple<> none;

    // Act
    TASWTomlResult const read = TASWTomlConverter<TRecord>::FromValue(TArray{ "example.com", 443, true }, record,
        TASWTomlValueOptions());
    TASWTomlResult const readNone = TASWTomlConverter<std::tuple<> >::FromValue(TArray{}, none, TASWTomlValueOptions());

    // Assert
    CheckTrue(read.IsOk() && record == TRecord{ "example.com", 443, true }, "a tuple should read: " + read.Message);
    CheckTrue(readNone.IsOk(), "an empty array should read as an empty tuple");
    CheckEquals("[\"example.com\", 443, true]"s, Written(TRecord{ "example.com", 443, true }),
        "the elements should be written in order");
    CheckEquals("[]"s, Written(std::tuple<>()), "an empty tuple as an empty array");
    CheckEmpty(ReadFailureProblem(TArray{ "example.com", 443, "yes" }, initial, ETomlCode::TypeMismatch,
        "The value \"yes\" is a string, not a boolean.", "[2]"), "each element should be read as its own type");
    CheckEmpty(ReadFailureProblem(TArray{ "example.com", true, 1 }, initial, ETomlCode::TypeMismatch,
        "The value true is a boolean, not an integer.", "[1]"), "the first element that fails should fail the tuple");
    CheckEmpty(ReadFailureProblem(TArray{ "example.com", 443 }, initial, ETomlCode::InvalidValue,
        "The value [\"example.com\", 443] has 2 elements, not 3.", ""), "an array of another length should be refused");
    CheckEmpty(ReadFailureProblem(TArray{ 1 }, std::tuple<>(), ETomlCode::InvalidValue,
        "The value [1] has 1 element, not 0.", ""), "by an empty tuple too");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ArrayConverters, UnorderedSet_WritesSortedByText)
{
    // Arrange & Act & Assert
    CheckEquals("[1, 10, 100, 2, 20]"s, Written(std::unordered_set<int>{ 20, 1, 100, 2, 10 }),
        "an unordered set should be written in the order of its elements' texts");
    CheckEquals("[\"B\", \"a\", \"b\"]"s, Written(std::unordered_set<std::string>{ "b", "a", "B" }),
        "strings as basic strings");
    CheckEquals("[]"s, Written(std::unordered_set<int>()), "an empty one as an empty array");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ArrayConverters, Vector_FromValue_FailsAtTheFirstElementThatFails)
{
    // Arrange
    std::vector<int> const initial = { 7 };
    std::vector<std::vector<int> > const initialNested = { initial, initial };
    TASWTomlValue const first = TArray{ 1 };
    TASWTomlValue const second = TArray{ 1, "x" };

    // Act & Assert
    CheckEmpty(ReadFailureProblem(TArray{ 1, "x", 2.5 }, initial, ETomlCode::TypeMismatch,
        "The value \"x\" is a string, not an integer.", "[1]"), "the first element that fails should fail the vector");
    CheckEmpty(ReadFailureProblem(TArray{ first, second }, initialNested, ETomlCode::TypeMismatch,
        "The value \"x\" is a string, not an integer.", "[1][1]"), "with the place inside a nested array");
    CheckEmpty(ReadFailureProblem(TArray{ 1, 300 }, std::vector<std::uint8_t>{ 7 }, ETomlCode::ValueOutOfRange,
        "The value 300 is out of range: it must be from 0 to 255.", "[1]"), "with the element's code and message");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ArrayConverters, Vector_FromValue_ReadsAnyArray)
{
    // Arrange
    TASWTomlDocument document;
    TASWTomlResult const parsed = document.Parse("[[s]]\na = 1\n\n[[s]]\na = 2\n");
    TASWTomlValue const* const found = document.Find("s");
    TASWTomlValue const tables = found != nullptr ? *found : TASWTomlValue();
    TASWTomlValue const firstTable = TTable{ { "a", 1 } };
    TASWTomlValue const secondTable = TTable{ { "a", 2 } };
    std::vector<int> numbers = { 7 };
    std::vector<int> emptied = { 7 };
    std::vector<bool> flags;
    std::vector<TASWTomlValue> mixed;
    std::vector<TASWTomlValue> elements;

    // Act
    TASWTomlResult const readNumbers = TASWTomlConverter<std::vector<int> >::FromValue(TArray{ 1, 2, 3 }, numbers,
        TASWTomlValueOptions());
    TASWTomlResult const readEmpty = TASWTomlConverter<std::vector<int> >::FromValue(TArray{}, emptied,
        TASWTomlValueOptions());
    TASWTomlResult const readFlags = TASWTomlConverter<std::vector<bool> >::FromValue(TArray{ true, false, true },
        flags, TASWTomlValueOptions());
    TASWTomlResult const readMixed = TASWTomlConverter<std::vector<TASWTomlValue> >::FromValue(TArray{ 1, "x", 2.5 },
        mixed, TASWTomlValueOptions());
    TASWTomlResult const readTables = TASWTomlConverter<std::vector<TASWTomlValue> >::FromValue(tables, elements,
        TASWTomlValueOptions());

    // Assert
    CheckTrue(readNumbers.IsOk() && numbers == std::vector<int>{ 1, 2, 3 },
        "an array should replace the vector's elements: " + readNumbers.Message);
    CheckTrue(readEmpty.IsOk() && emptied.empty(), "an empty array should read as an empty vector");
    CheckTrue(readFlags.IsOk() && flags == std::vector<bool>{ true, false, true }, "and booleans as a vector<bool>");
    CheckTrue(readMixed.IsOk() && mixed == std::vector<TASWTomlValue>{ 1, "x", 2.5 },
        "an array of mixed types should read as values");
    CheckTrue(parsed.IsOk() && found != nullptr, "the document should parse: " + parsed.Message);
    CheckTrue(readTables.IsOk() && elements == std::vector<TASWTomlValue>{ firstTable, secondTable },
        "an array of tables should read too: " + readTables.Message);
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ArrayConverters, Vector_ToValue_WritesEachElement)
{
    // Arrange
    std::vector<int> const pair = { 1, 2 };
    std::vector<int> const single = { 3 };
    std::vector<std::vector<int> > const nested = { pair, single };
    TASWTomlValue value = TTable{ { "x", 1 } };

    // Act
    TASWTomlResult const result = TASWTomlConverter<std::vector<int> >::ToValue({ 1, 2, 3 }, value,
        TASWTomlValueOptions());

    // Assert
    CheckTrue(result.IsOk() && value == TASWTomlValue(TArray{ 1, 2, 3 }),
        "the table should be replaced by an array of the elements: " + result.Message);
    CheckEquals("[]"s, Written(std::vector<int>()), "an empty vector should be an empty array");
    CheckEquals("[true, false]"s, Written(std::vector<bool>{ true, false }), "a vector<bool> an array of booleans");
    CheckEquals("[[1, 2], [3]]"s, Written(nested), "and a vector of vectors an array of arrays");
}
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
// TTest_ASWToml_DateTimeConverters
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_DateTimeConverters::TTest_ASWToml_DateTimeConverters()
    : inherited("ASWToml_DateTimeConverters_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_DateTimeConverters::~TTest_ASWToml_DateTimeConverters()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DateTimeConverters::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DateTimeConverters::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DateTimeConverters::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_DateTimeConverters::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeConverters, Date_ReadsLocalDatesOnly)
{
    // Arrange
    std::chrono::year_month_day const date = Date(1979, 5, 27);
    std::chrono::year_month_day const unreal = Date(2023, 2, 30);
    std::chrono::year_month_day readDate{};
    std::chrono::year_month_day readUnreal{};
    std::chrono::sys_days sysDays;
    std::chrono::local_days localDays;
    std::chrono::sys_days const kept = std::chrono::sys_days(Date(2000, 1, 1));
    std::chrono::sys_days refused = kept;

    // Act
    TASWTomlResult const asDate = TASWTomlConverter<std::chrono::year_month_day>::FromValue(date, readDate,
        TASWTomlValueOptions());
    TASWTomlResult const asUnreal = TASWTomlConverter<std::chrono::year_month_day>::FromValue(unreal, readUnreal,
        TASWTomlValueOptions());
    TASWTomlResult const asSysDays = TASWTomlConverter<std::chrono::sys_days>::FromValue(date, sysDays,
        TASWTomlValueOptions());
    TASWTomlResult const asLocalDays = TASWTomlConverter<std::chrono::local_days>::FromValue(date, localDays,
        TASWTomlValueOptions());
    TASWTomlResult const unrealDays = TASWTomlConverter<std::chrono::sys_days>::FromValue(unreal, refused,
        TASWTomlValueOptions());

    // Assert
    CheckTrue(asDate.IsOk() && readDate == date, "a local date should read as a year_month_day");
    CheckTrue(asUnreal.IsOk() && readUnreal == unreal, "as it is, even when it isn't a real date");
    CheckTrue(asSysDays.IsOk() && sysDays == std::chrono::sys_days(date), "as sys_days");
    CheckTrue(asLocalDays.IsOk() && localDays == std::chrono::local_days(date), "and as local_days");
    CheckEquals(ETomlCode::InvalidValue, unrealDays.Code, "a date that isn't real has no days to count");
    CheckEquals("The value of type LocalDate isn't a real date."s, unrealDays.Message, "the message");
    CheckTrue(refused == kept, "a value refused should leave the days as they were");
    CheckEmpty(RefusalProblem(date, { ETomlType::LocalDate }, "a local date"), "year_month_day refuses other types");
    CheckEmpty(RefusalProblem(kept, { ETomlType::LocalDate }, "a local date"), "and so do sys_days");
    CheckEmpty(RefusalProblem(std::chrono::local_days(Date(2000, 1, 1)), { ETomlType::LocalDate }, "a local date"),
        "and local_days");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeConverters, Date_WritesTomlsYearsOnly)
{
    // Arrange
    // The days of 0000-01-01 and 9999-12-31, and those just beyond them
    std::chrono::sys_days const first = std::chrono::sys_days(Date(0, 1, 1));
    std::chrono::sys_days const last = std::chrono::sys_days(Date(9999, 12, 31));
    std::string const outOfRange = "failed: The time point can't be written: TOML's years are from 0 to 9999.";
    TASWTomlValue kept = "kept";

    // Act
    TASWTomlResult const unreal = TASWTomlConverter<std::chrono::year_month_day>::ToValue(Date(2023, 2, 30), kept,
        TASWTomlValueOptions());
    TASWTomlResult const tooLate = TASWTomlConverter<std::chrono::year_month_day>::ToValue(Date(10000, 1, 1), kept,
        TASWTomlValueOptions());
    TASWTomlResult const daysTooLate = TASWTomlConverter<std::chrono::sys_days>::ToValue(last + std::chrono::days(1),
        kept, TASWTomlValueOptions());

    // Assert
    CheckEquals("0000-01-01"s, Written(Date(0, 1, 1)), "the first date TOML writes");
    CheckEquals("9999-12-31"s, Written(Date(9999, 12, 31)), "the last");
    CheckEquals(ETomlCode::InvalidValue, unreal.Code, "a date that isn't real can't be written");
    CheckEquals("The date 2023-02-30 can't be written: it isn't a real date."s, unreal.Message, "the message");
    CheckEquals(ETomlCode::ValueOutOfRange, tooLate.Code, "nor a date after 9999");
    CheckEquals("The date 10000-01-01 can't be written: TOML's years are from 0 to 9999."s, tooLate.Message,
        "the message");
    CheckEquals("failed: The date -0001-12-31 can't be written: TOML's years are from 0 to 9999."s,
        Written(Date(-1, 12, 31)), "nor one before 0, with its sign");
    CheckEquals("0000-01-01"s, Written(first), "sys_days of the first date");
    CheckEquals("9999-12-31"s, Written(last), "and of the last");
    CheckEquals(outOfRange, Written(first - std::chrono::days(1)), "the day before the first");
    CheckEquals(outOfRange, Written(last + std::chrono::days(1)), "the day after the last");
    CheckEquals(ETomlCode::ValueOutOfRange, daysTooLate.Code, "out of range");
    CheckEquals("0000-01-01"s, Written(std::chrono::local_days(Date(0, 1, 1))), "local_days of the first date");
    CheckEquals("9999-12-31"s, Written(std::chrono::local_days(Date(9999, 12, 31))), "and of the last");
    CheckEquals(outOfRange, Written(std::chrono::local_days(Date(10000, 1, 1))), "a local day after the last");
    CheckEquals(outOfRange, Written(std::chrono::local_days(Date(-1, 12, 31))), "and before the first");
    CheckTrue(kept == TASWTomlValue("kept"), "a date refused should leave the value as it was");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeConverters, Helpers_DateTimeOutOfRange)
{
    // Arrange
    // Units with ranges no standard duration has: counts of seconds and of 100 ns from -10 that TOML ends at 9999,
    // a count of 7 seconds to 10 that TOML starts at 0000 (its first count there is 2 seconds into the year), and
    // counts of seconds and nanoseconds that hold more than a day
    using TUnit = ASWToml::Detail::TDurationUnit;
    std::int64_t const max = std::numeric_limits<std::int64_t>::max();
    std::int64_t const min = std::numeric_limits<std::int64_t>::min();
    TUnit const fromBefore1970{ 1, 1, -10, max };
    TUnit const ticksFromBefore1970{ 1, 10000000, -10, max };
    TUnit const sevenSeconds{ 7, 1, min, 10 };
    TUnit const nanoseconds{ 1, 1000000000, min, max };
    TUnit const hundredSeconds{ 1, 1, min, 100 };
    TASWTomlOffsetDateTime const instant{ Date(1979, 5, 27), TimeOfDay(7, 32, 0), std::chrono::minutes(0) };
    TASWTomlLocalDateTime const local{ Date(1979, 5, 27), TimeOfDay(7, 32, 0) };
    TASWTomlLocalTime const time = TimeOfDay(7, 32, 0);

    // Act & Assert
    CheckEquals("The value 1979-05-27T07:32:00Z is out of range: it must be from 1969-12-31T23:59:50Z to "
        "9999-12-31T23:59:59Z."s, ASWToml::Detail::DateTimeOutOfRange(instant, fromBefore1970).Message,
        "an instant's range that TOML ends at its last second");
    CheckEquals("The value 1979-05-27T07:32:00 is out of range: it must be from 1969-12-31T23:59:50 to "
        "9999-12-31T23:59:59."s, ASWToml::Detail::DateTimeOutOfRange(local, fromBefore1970).Message,
        "a local date-time's too");
    CheckEquals("The value 1979-05-27T07:32:00Z is out of range: it must be from 1969-12-31T23:59:59.999999Z to "
        "9999-12-31T23:59:59.9999999Z."s, ASWToml::Detail::DateTimeOutOfRange(instant, ticksFromBefore1970).Message,
        "at its last 100 ns");
    CheckEquals("The value 1979-05-27T07:32:00 is out of range: it must be from 1969-12-31T23:59:59.999999 to "
        "9999-12-31T23:59:59.9999999."s, ASWToml::Detail::DateTimeOutOfRange(local, ticksFromBefore1970).Message,
        "a local date-time's too");
    CheckEquals("The value 1979-05-27T07:32:00Z is out of range: it must be from 0000-01-01T00:00:02Z to "
        "1970-01-01T00:01:10Z."s, ASWToml::Detail::DateTimeOutOfRange(instant, sevenSeconds).Message,
        "a range that TOML starts at the unit's first count in the year 0");
    CheckEquals("The value 1979-05-27T07:32:00 is out of range: it must be from 0000-01-01T00:00:02 to "
        "1970-01-01T00:01:10."s, ASWToml::Detail::DateTimeOutOfRange(local, sevenSeconds).Message,
        "a local date-time's too");
    CheckEquals("The value 07:32:00 is out of range: it must be from 00:00:00 to 00:01:40."s,
        ASWToml::Detail::DateTimeOutOfRange(time, hundredSeconds).Message, "a time of day's range");
    CheckEquals("The value 07:32:00 is out of range: it must be from 00:00:00 to 23:59:60."s,
        ASWToml::Detail::DateTimeOutOfRange(time, fromBefore1970).Message, "one that a day ends at the leap second");
    CheckEquals("The value 07:32:00 is out of range: it must be from 00:00:00 to 23:59:60.999999999."s,
        ASWToml::Detail::DateTimeOutOfRange(time, nanoseconds).Message, "at its last nanosecond");
    CheckEquals(ETomlCode::ValueOutOfRange, ASWToml::Detail::DateTimeOutOfRange(time, nanoseconds).Code, "the code");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeConverters, Helpers_TimeOfDayFromCount)
{
    // Arrange
    // Seconds, milliseconds, units of 7 seconds (a day isn't a whole number of them) and the largest unit there is
    using TUnit = ASWToml::Detail::TDurationUnit;
    std::int64_t const max = std::numeric_limits<std::int64_t>::max();
    std::int64_t const min = std::numeric_limits<std::int64_t>::min();
    TUnit const seconds{ 1, 1, min, max };
    TUnit const milliseconds{ 1, 1000, min, max };
    TUnit const sevenSeconds{ 7, 1, min, max };
    TUnit const largest{ max, 1, min, max };
    TASWTomlLocalTime time = TimeOfDay(1, 2, 3);
    TASWTomlLocalTime const kept = time;

    // Act & Assert
    CheckTrue(ASWToml::Detail::TimeOfDayFromCount(0, seconds, time) && time == TimeOfDay(0, 0, 0), "midnight");
    CheckTrue(ASWToml::Detail::TimeOfDayFromCount(86399, seconds, time) && time == TimeOfDay(23, 59, 59),
        "the last second of the day");
    CheckTrue(ASWToml::Detail::TimeOfDayFromCount(86400, seconds, time) && time == TimeOfDay(23, 59, 60),
        "a day as the leap second");
    CheckTrue(ASWToml::Detail::TimeOfDayFromCount(86400500, milliseconds, time) &&
        time == TimeOfDay(23, 59, 60, 500000000), "with its fraction");
    CheckTrue(ASWToml::Detail::TimeOfDayFromCount(12342, sevenSeconds, time) && time == TimeOfDay(23, 59, 54),
        "the last 7 seconds that start in the day");
    CheckTrue(ASWToml::Detail::TimeOfDayFromCount(0, largest, time) && time == TimeOfDay(0, 0, 0),
        "the only count of the largest unit that is a time of day");

    time = kept;
    CheckFalse(ASWToml::Detail::TimeOfDayFromCount(86401, seconds, time), "a day and a second is too late");
    CheckFalse(ASWToml::Detail::TimeOfDayFromCount(-1, seconds, time), "a second before midnight too early");
    CheckFalse(ASWToml::Detail::TimeOfDayFromCount(12343, sevenSeconds, time), "86401 seconds in units of 7");
    CheckFalse(ASWToml::Detail::TimeOfDayFromCount(1, largest, time), "one count of the largest unit");
    CheckTrue(time == kept, "a count refused should leave the time as it was");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeConverters, LocalTime_ReadsLocalDateTimesOnly)
{
    // Arrange
    // Rounded down, before 1970 too; second 60 is the next minute's second 0
    TASWTomlLocalDateTime const dateTime{ Date(1979, 5, 27), TimeOfDay(7, 32, 0, 500000000) };
    TASWTomlLocalDateTime const before1970{ Date(1969, 12, 31), TimeOfDay(23, 59, 59, 500000000) };
    TASWTomlLocalDateTime const leap{ Date(1998, 12, 31), TimeOfDay(23, 59, 60) };
    TASWTomlOffsetDateTime const instant{ Date(1979, 5, 27), TimeOfDay(7, 32, 0), std::chrono::minutes(0) };
    TASWTomlLocalDateTime const unreal{ Date(2023, 2, 30), TimeOfDay(0, 0, 0) };
    std::chrono::local_seconds const expected = std::chrono::local_days(Date(1979, 5, 27)) + std::chrono::seconds(27120);
    std::chrono::local_seconds const kept{ std::chrono::seconds(1) };
    std::chrono::local_seconds seconds;
    std::chrono::local_time<std::chrono::milliseconds> milliseconds;
    std::chrono::local_seconds early;
    std::chrono::local_seconds leapRead;
    std::chrono::local_seconds refused = kept;

    // Act
    TASWTomlResult const readSeconds = TASWTomlConverter<std::chrono::local_seconds>::FromValue(dateTime, seconds,
        TASWTomlValueOptions());
    TASWTomlResult const readMilliseconds = TASWTomlConverter<std::chrono::local_time<std::chrono::milliseconds> >::
        FromValue(dateTime, milliseconds, TASWTomlValueOptions());
    TASWTomlResult const readEarly = TASWTomlConverter<std::chrono::local_seconds>::FromValue(before1970, early,
        TASWTomlValueOptions());
    TASWTomlResult const readLeap = TASWTomlConverter<std::chrono::local_seconds>::FromValue(leap, leapRead,
        TASWTomlValueOptions());
    TASWTomlResult const offset = TASWTomlConverter<std::chrono::local_seconds>::FromValue(instant, refused,
        TASWTomlValueOptions());
    TASWTomlResult const invalid = TASWTomlConverter<std::chrono::local_seconds>::FromValue(unreal, refused,
        TASWTomlValueOptions());

    // Assert
    CheckTrue(readSeconds.IsOk() && seconds == expected, "a local date-time should read as local_seconds rounded down");
    CheckTrue(readMilliseconds.IsOk() && milliseconds == expected + std::chrono::milliseconds(500),
        "and as milliseconds with its fraction");
    CheckEquals(std::int64_t(-1), static_cast<std::int64_t>(early.time_since_epoch().count()),
        "half a second before 1970 should round down to the second before");
    CheckTrue(readEarly.IsOk(), "that reads");
    CheckTrue(readLeap.IsOk() && leapRead == std::chrono::local_days(Date(1999, 1, 1)),
        "a leap second should read as the next minute's second 0");
    CheckEquals("The value 1979-05-27T07:32:00Z is an offset date-time, not a local date-time."s, offset.Message,
        "an offset date-time isn't a local one: its offset would be lost");
    CheckEquals(ETomlCode::InvalidValue, invalid.Code, "a local date-time that isn't valid has no time point");
    CheckEquals("The value of type LocalDateTime isn't a valid date-time."s, invalid.Message, "the message");
    CheckTrue(refused == kept, "a value refused should leave the time point as it was");
    CheckEmpty(RefusalProblem(kept, { ETomlType::LocalDateTime }, "a local date-time"), "every other type");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeConverters, LocalTime_ReadsWhatItsDurationCounts)
{
    // Arrange
    // Nanoseconds count only from 1677 to 2262; seconds count every date-time TOML writes; 32-bit minutes count back
    // past 0000 but forward only to 6053
    using TMinutes32 = std::chrono::duration<std::int32_t, std::ratio<60> >;
    TASWTomlLocalDateTime const late{ Date(9999, 12, 31), TimeOfDay(23, 59, 59) };
    std::chrono::local_time<std::chrono::nanoseconds> nanoseconds;
    std::chrono::local_seconds seconds;
    std::chrono::local_time<TMinutes32> minutes;

    // Act
    TASWTomlResult const asNanoseconds = TASWTomlConverter<std::chrono::local_time<std::chrono::nanoseconds> >::
        FromValue(late, nanoseconds, TASWTomlValueOptions());
    TASWTomlResult const asSeconds = TASWTomlConverter<std::chrono::local_seconds>::FromValue(late, seconds,
        TASWTomlValueOptions());
    TASWTomlResult const asMinutes = TASWTomlConverter<std::chrono::local_time<TMinutes32> >::FromValue(late, minutes,
        TASWTomlValueOptions());

    // Assert
    CheckEquals(ETomlCode::ValueOutOfRange, asNanoseconds.Code, "9999 should be beyond nanoseconds");
    CheckEquals("The value 9999-12-31T23:59:59 is out of range: it must be from 1677-09-21T00:12:43.145224192 to "
        "2262-04-11T23:47:16.854775807."s, asNanoseconds.Message, "the message gives the range nanoseconds hold");
    CheckTrue(asSeconds.IsOk() && seconds.time_since_epoch() == std::chrono::seconds(253402300799),
        "seconds should hold it");
    CheckEquals("9999-12-31T23:59:59"s, Written(seconds), "and write it back");
    CheckEquals("2262-04-11T23:47:16.854775807"s, Written(std::chrono::local_time<std::chrono::nanoseconds>::max()),
        "the last nanosecond");
    CheckEquals("failed: The time point can't be written: TOML's years are from 0 to 9999."s,
        Written(seconds + std::chrono::seconds(1)), "a second after 9999 can't be written");
    CheckEquals("The value 9999-12-31T23:59:59 is out of range: it must be from 0000-01-01T00:00:00 to "
        "6053-01-23T02:07:00."s, asMinutes.Message, "a range that TOML ends on one side");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeConverters, RoundTrip_EveryType_ThroughADocument)
{
    // Arrange
    // A value of each type and the line it is written as; an instant in UTC
    std::chrono::sys_seconds const instant = std::chrono::sys_days(Date(1979, 5, 27)) + std::chrono::seconds(27120);
    std::chrono::local_seconds const local = std::chrono::local_days(Date(1979, 5, 27)) + std::chrono::seconds(27120);
    std::chrono::sys_time<std::chrono::milliseconds> const withFraction = instant + std::chrono::milliseconds(250);
    std::chrono::system_clock::time_point const clock = std::chrono::time_point_cast<
        std::chrono::system_clock::duration>(instant) + std::chrono::microseconds(5);
    std::chrono::local_time<std::chrono::microseconds> const localFraction = local + std::chrono::microseconds(5);
    std::chrono::hh_mm_ss<std::chrono::seconds> const time{ std::chrono::seconds(27120) };
    std::chrono::hh_mm_ss<std::chrono::nanoseconds> const timeFraction{ std::chrono::nanoseconds(27120000000001) };
    std::chrono::minutes const offset{ -420 };
    TASWTomlOffsetDateTime const offsetDateTime{ Date(1979, 5, 27), TimeOfDay(0, 32, 0, 999999), offset };
    TASWTomlLocalDateTime const localDateTime{ Date(1979, 5, 27), TimeOfDay(7, 32, 0) };
    TASWTomlLocalTime const localTime = TimeOfDay(23, 59, 60, 5);
    std::string text;

    // Act & Assert
    std::chrono::year_month_day date{};
    CheckTrue(ThroughDocument(Date(1979, 5, 27), date, text).IsOk() && date == Date(1979, 5, 27), "a year_month_day");
    CheckEquals("v = 1979-05-27"s, FirstLine(text), "as a local date");
    std::chrono::sys_days sysDays;
    CheckTrue(ThroughDocument(std::chrono::sys_days(Date(1979, 5, 27)), sysDays, text).IsOk() &&
        sysDays == std::chrono::sys_days(Date(1979, 5, 27)), "sys_days");
    CheckEquals("v = 1979-05-27"s, FirstLine(text), "as a local date");
    std::chrono::local_days localDays;
    CheckTrue(ThroughDocument(std::chrono::local_days(Date(1979, 5, 27)), localDays, text).IsOk() &&
        localDays == std::chrono::local_days(Date(1979, 5, 27)), "local_days");
    CheckEquals("v = 1979-05-27"s, FirstLine(text), "as a local date");
    std::chrono::sys_seconds instantRead;
    CheckTrue(ThroughDocument(instant, instantRead, text).IsOk() && instantRead == instant, "sys_seconds");
    CheckEquals("v = 1979-05-27T07:32:00Z"s, FirstLine(text), "as an offset date-time in UTC");
    std::chrono::sys_time<std::chrono::milliseconds> withFractionRead;
    CheckTrue(ThroughDocument(withFraction, withFractionRead, text).IsOk() && withFractionRead == withFraction,
        "milliseconds");
    CheckEquals("v = 1979-05-27T07:32:00.25Z"s, FirstLine(text), "with the fraction");
    std::chrono::system_clock::time_point clockRead;
    CheckTrue(ThroughDocument(clock, clockRead, text).IsOk() && clockRead == clock, "system_clock::time_point");
    CheckEquals("v = 1979-05-27T07:32:00.000005Z"s, FirstLine(text), "with the fraction");
    std::chrono::local_seconds localRead;
    CheckTrue(ThroughDocument(local, localRead, text).IsOk() && localRead == local, "local_seconds");
    CheckEquals("v = 1979-05-27T07:32:00"s, FirstLine(text), "as a local date-time");
    std::chrono::local_time<std::chrono::microseconds> localFractionRead;
    CheckTrue(ThroughDocument(localFraction, localFractionRead, text).IsOk() && localFractionRead == localFraction,
        "microseconds");
    CheckEquals("v = 1979-05-27T07:32:00.000005"s, FirstLine(text), "with the fraction");
    std::chrono::hh_mm_ss<std::chrono::seconds> timeRead;
    CheckTrue(ThroughDocument(time, timeRead, text).IsOk() && timeRead.to_duration() == time.to_duration(),
        "hh_mm_ss of seconds");
    CheckEquals("v = 07:32:00"s, FirstLine(text), "as a local time");
    std::chrono::hh_mm_ss<std::chrono::nanoseconds> timeFractionRead;
    CheckTrue(ThroughDocument(timeFraction, timeFractionRead, text).IsOk() &&
        timeFractionRead.to_duration() == timeFraction.to_duration(), "hh_mm_ss of nanoseconds");
    CheckEquals("v = 07:32:00.000000001"s, FirstLine(text), "with the fraction");
    TASWTomlOffsetDateTime offsetDateTimeRead;
    CheckTrue(ThroughDocument(offsetDateTime, offsetDateTimeRead, text).IsOk() && offsetDateTimeRead == offsetDateTime,
        "an offset date-time");
    CheckEquals("v = 1979-05-27T00:32:00.000999999-07:00"s, FirstLine(text), "with its offset");
    TASWTomlLocalDateTime localDateTimeRead;
    CheckTrue(ThroughDocument(localDateTime, localDateTimeRead, text).IsOk() && localDateTimeRead == localDateTime,
        "a local date-time");
    CheckEquals("v = 1979-05-27T07:32:00"s, FirstLine(text), "as it is");
    TASWTomlLocalTime localTimeRead;
    CheckTrue(ThroughDocument(localTime, localTimeRead, text).IsOk() && localTimeRead == localTime, "a local time");
    CheckEquals("v = 23:59:60.000000005"s, FirstLine(text), "as it is, a leap second too");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeConverters, SysTime_ReadsTheInstantAtAnyOffset)
{
    // Arrange
    // 00:32 at -07:00 is 07:32 in UTC; rounded down, before 1970 too; second 60 is the next minute's second 0
    std::chrono::minutes const offset{ -420 };
    std::chrono::minutes const utc{ 0 };
    TASWTomlOffsetDateTime const dateTime{ Date(1979, 5, 27), TimeOfDay(0, 32, 0, 999999999), offset };
    TASWTomlOffsetDateTime const before1970{ Date(1969, 12, 31), TimeOfDay(23, 59, 59, 500000000), utc };
    TASWTomlOffsetDateTime const leap{ Date(1998, 12, 31), TimeOfDay(23, 59, 60), utc };
    std::chrono::sys_seconds const expected = std::chrono::sys_days(Date(1979, 5, 27)) + std::chrono::seconds(27120);
    TASWTomlLocalDateTime const local{ Date(1979, 5, 27), TimeOfDay(7, 32, 0) };
    TASWTomlOffsetDateTime const dayOffset{ Date(1979, 5, 27), TimeOfDay(0, 0, 0), std::chrono::minutes(1440) };
    std::chrono::sys_seconds const kept{ std::chrono::seconds(1) };
    std::chrono::sys_seconds seconds;
    std::chrono::sys_time<std::chrono::milliseconds> milliseconds;
    std::chrono::sys_seconds early;
    std::chrono::sys_seconds leapRead;
    std::chrono::sys_seconds refused = kept;

    // Act
    TASWTomlResult const readSeconds = TASWTomlConverter<std::chrono::sys_seconds>::FromValue(dateTime, seconds,
        TASWTomlValueOptions());
    TASWTomlResult const readMilliseconds = TASWTomlConverter<std::chrono::sys_time<std::chrono::milliseconds> >::
        FromValue(dateTime, milliseconds, TASWTomlValueOptions());
    TASWTomlResult const readEarly = TASWTomlConverter<std::chrono::sys_seconds>::FromValue(before1970, early,
        TASWTomlValueOptions());
    TASWTomlResult const readLeap = TASWTomlConverter<std::chrono::sys_seconds>::FromValue(leap, leapRead,
        TASWTomlValueOptions());
    TASWTomlResult const localRead = TASWTomlConverter<std::chrono::sys_seconds>::FromValue(local, refused,
        TASWTomlValueOptions());
    TASWTomlResult const invalid = TASWTomlConverter<std::chrono::sys_seconds>::FromValue(dayOffset, refused,
        TASWTomlValueOptions());

    // Assert
    CheckTrue(readSeconds.IsOk() && seconds == expected, "the instant in UTC, rounded down to seconds");
    CheckTrue(readMilliseconds.IsOk() && milliseconds == expected + std::chrono::milliseconds(999),
        "and to milliseconds");
    CheckTrue(readEarly.IsOk() && early.time_since_epoch() == std::chrono::seconds(-1),
        "half a second before 1970 should round down to the second before");
    CheckTrue(readLeap.IsOk() && leapRead == std::chrono::sys_days(Date(1999, 1, 1)),
        "a leap second should read as the next minute's second 0");
    CheckEquals("The value 1979-05-27T07:32:00 is a local date-time, not an offset date-time."s, localRead.Message,
        "a local date-time isn't an instant: its offset is unknown");
    CheckEquals(ETomlCode::InvalidValue, invalid.Code, "an offset date-time that isn't valid has no instant");
    CheckEquals("The value of type OffsetDateTime isn't a valid date-time."s, invalid.Message, "the message");
    CheckTrue(refused == kept, "a value refused should leave the time point as it was");
    CheckEmpty(RefusalProblem(kept, { ETomlType::OffsetDateTime }, "an offset date-time"), "every other type");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeConverters, SysTime_ReadsWhatItsDurationCounts)
{
    // Arrange
    // Nanoseconds count only from 1677 to 2262, and so does system_clock with libstdc++; seconds count every instant
    // TOML writes; 32-bit minutes count back past 0000 but forward only to 6053
    using TMinutes32 = std::chrono::duration<std::int32_t, std::ratio<60> >;
    TASWTomlOffsetDateTime const late{ Date(9999, 12, 31), TimeOfDay(23, 59, 59), std::chrono::minutes(0) };
    TASWTomlOffsetDateTime const first{ Date(0, 1, 1), TimeOfDay(0, 0, 0), std::chrono::minutes(0) };
    std::chrono::sys_time<std::chrono::nanoseconds> nanoseconds;
    std::chrono::sys_seconds seconds;
    std::chrono::sys_seconds firstSeconds;
    std::chrono::system_clock::time_point clock;
    std::chrono::sys_time<TMinutes32> minutes;

    // Act
    TASWTomlResult const asNanoseconds = TASWTomlConverter<std::chrono::sys_time<std::chrono::nanoseconds> >::
        FromValue(late, nanoseconds, TASWTomlValueOptions());
    TASWTomlResult const asSeconds = TASWTomlConverter<std::chrono::sys_seconds>::FromValue(late, seconds,
        TASWTomlValueOptions());
    TASWTomlResult const firstAsSeconds = TASWTomlConverter<std::chrono::sys_seconds>::FromValue(first, firstSeconds,
        TASWTomlValueOptions());
    TASWTomlResult const asClock = TASWTomlConverter<std::chrono::system_clock::time_point>::FromValue(late, clock,
        TASWTomlValueOptions());
    TASWTomlResult const asMinutes = TASWTomlConverter<std::chrono::sys_time<TMinutes32> >::FromValue(late, minutes,
        TASWTomlValueOptions());

    // Assert
    CheckEquals(ETomlCode::ValueOutOfRange, asNanoseconds.Code, "9999 should be beyond nanoseconds");
    CheckEquals("The value 9999-12-31T23:59:59Z is out of range: it must be from 1677-09-21T00:12:43.145224192Z to "
        "2262-04-11T23:47:16.854775807Z."s, asNanoseconds.Message, "the message gives the range nanoseconds hold");
    CheckTrue(asSeconds.IsOk() && seconds.time_since_epoch() == std::chrono::seconds(253402300799),
        "seconds should hold the last second TOML writes");
    CheckTrue(firstAsSeconds.IsOk() && firstSeconds.time_since_epoch() == std::chrono::seconds(-62167219200),
        "and the first");
    if (std::is_same_v<std::chrono::system_clock::duration, std::chrono::nanoseconds>)
        CheckEquals(ETomlCode::ValueOutOfRange, asClock.Code, "system_clock counts nanoseconds here (libstdc++)");
    else
        CheckTrue(asClock.IsOk() && clock == seconds, "system_clock holds 9999 here (libc++, MSVC)");

    CheckEquals("The value 9999-12-31T23:59:59Z is out of range: it must be from 0000-01-01T00:00:00Z to "
        "6053-01-23T02:07:00Z."s, asMinutes.Message, "a range that TOML ends on one side");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeConverters, SysTime_WritesInUtc)
{
    // Arrange
    // The first and last seconds TOML writes, and the seconds beyond them
    std::chrono::sys_seconds const first{ std::chrono::seconds(-62167219200) };
    std::chrono::sys_seconds const last{ std::chrono::seconds(253402300799) };
    std::string const outOfRange = "failed: The time point can't be written: TOML's years are from 0 to 9999.";
    TASWTomlValue kept = "kept";

    // Act
    TASWTomlResult const tooLate = TASWTomlConverter<std::chrono::sys_seconds>::ToValue(last + std::chrono::seconds(1),
        kept, TASWTomlValueOptions());

    // Assert
    CheckEquals("0000-01-01T00:00:00Z"s, Written(first), "the first second, in UTC");
    CheckEquals("9999-12-31T23:59:59Z"s, Written(last), "the last");
    CheckEquals(outOfRange, Written(first - std::chrono::seconds(1)), "the second before the first");
    CheckEquals(outOfRange, Written(last + std::chrono::seconds(1)), "the second after the last");
    CheckEquals(ETomlCode::ValueOutOfRange, tooLate.Code, "out of range");
    CheckTrue(kept == TASWTomlValue("kept"), "a time point refused should leave the value as it was");
    CheckEquals("1677-09-21T00:12:43.145224192Z"s, Written(std::chrono::sys_time<std::chrono::nanoseconds>::min()),
        "the first nanosecond");
    CheckEquals("1969-12-31T23:59:59.999Z"s, Written(std::chrono::sys_time<std::chrono::milliseconds>(
        std::chrono::milliseconds(-1))), "a millisecond before 1970");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeConverters, TimeOfDay_ReadsLocalTimesOnly)
{
    // Arrange
    // Rounded down to the duration; 23:59:60 is 24:00:00, as hh_mm_ss makes it; a count of 8 bits holds only 127
    // seconds
    using TSeconds8 = std::chrono::duration<std::int8_t>;
    std::chrono::hh_mm_ss<std::chrono::milliseconds> milliseconds;
    std::chrono::hh_mm_ss<std::chrono::seconds> leap;
    std::chrono::hh_mm_ss<TSeconds8> small;
    std::chrono::hh_mm_ss<std::chrono::seconds> refused{ std::chrono::seconds(5) };

    // Act
    TASWTomlResult const readMilliseconds = TASWTomlConverter<std::chrono::hh_mm_ss<std::chrono::milliseconds> >::
        FromValue(TimeOfDay(7, 32, 0, 123456789), milliseconds, TASWTomlValueOptions());
    TASWTomlResult const readLeap = TASWTomlConverter<std::chrono::hh_mm_ss<std::chrono::seconds> >::FromValue(
        TimeOfDay(23, 59, 60), leap, TASWTomlValueOptions());
    TASWTomlResult const tooFar = TASWTomlConverter<std::chrono::hh_mm_ss<TSeconds8> >::FromValue(TimeOfDay(0, 3, 0),
        small, TASWTomlValueOptions());
    TASWTomlResult const dateTime = TASWTomlConverter<std::chrono::hh_mm_ss<std::chrono::seconds> >::FromValue(
        TASWTomlLocalDateTime(), refused, TASWTomlValueOptions());
    TASWTomlResult const text = TASWTomlConverter<std::chrono::hh_mm_ss<std::chrono::seconds> >::FromValue("07:32:00",
        refused, TASWTomlValueOptions());
    TASWTomlResult const invalid = TASWTomlConverter<std::chrono::hh_mm_ss<std::chrono::seconds> >::FromValue(
        TimeOfDay(24, 0, 0), refused, TASWTomlValueOptions());

    // Assert
    CheckTrue(readMilliseconds.IsOk() && milliseconds.to_duration() == std::chrono::milliseconds(27120123),
        "a local time should read rounded down to milliseconds");
    CheckTrue(readLeap.IsOk() && leap.hours() == std::chrono::hours(24) && leap.minutes() == std::chrono::minutes(0),
        "a leap second should read as 24:00:00");
    CheckEquals(ETomlCode::ValueOutOfRange, tooFar.Code, "a time an 8-bit count can't hold should be out of range");
    CheckEquals("The value 00:03:00 is out of range: it must be from 00:00:00 to 00:02:07."s, tooFar.Message,
        "the message gives the range the count holds");
    CheckEquals("The value 1970-01-01T00:00:00 is a local date-time, not a local time."s, dateTime.Message,
        "a local date-time isn't a time of day");
    CheckEquals(ETomlCode::TypeMismatch, text.Code, "nor is a string");
    CheckEquals(ETomlCode::InvalidValue, invalid.Code, "a time that isn't valid has no duration");
    CheckEquals("The value of type LocalTime isn't a valid time."s, invalid.Message, "the message");
    CheckTrue(refused.to_duration() == std::chrono::seconds(5), "a value refused should leave the time as it was");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeConverters, TimeOfDay_WritesUpToTheLeapSecond)
{
    // Arrange
    // 24:00:00 up to a second more is what 23:59:60 reads as, so it is written as that
    std::chrono::hh_mm_ss<std::chrono::milliseconds> const leapHalf{ std::chrono::milliseconds(86400500) };
    std::chrono::hh_mm_ss<std::chrono::milliseconds> leapHalfBack;
    std::string text;
    std::string const outOfRange = "failed: The time of day can't be written: it must be at least 0 and less than a "
        "day and a second (24:00:00 is written as the leap second 23:59:60).";
    TASWTomlValue kept = "kept";

    // Act
    TASWTomlResult const leapThrough = ThroughDocument(leapHalf, leapHalfBack, text);
    TASWTomlResult const tooLate = TASWTomlConverter<std::chrono::hh_mm_ss<std::chrono::seconds> >::ToValue(
        std::chrono::hh_mm_ss<std::chrono::seconds>(std::chrono::seconds(86401)), kept, TASWTomlValueOptions());

    // Assert
    CheckEquals("00:00:00"s, Written(std::chrono::hh_mm_ss<std::chrono::seconds>(std::chrono::seconds(0))), "midnight");
    CheckEquals("23:59:59.999"s, Written(std::chrono::hh_mm_ss<std::chrono::milliseconds>(std::chrono::milliseconds(
        86399999))), "the last millisecond of the day");
    CheckEquals("01:30:00"s, Written(std::chrono::hh_mm_ss<std::chrono::minutes>(std::chrono::minutes(90))),
        "a time of day in minutes");
    CheckEquals("23:59:60"s, Written(std::chrono::hh_mm_ss<std::chrono::seconds>(std::chrono::hours(24))),
        "24:00:00 as the leap second");
    CheckTrue(leapThrough.IsOk() && leapHalfBack.to_duration() == leapHalf.to_duration(),
        "24:00:00.5 should go through a document");
    CheckEquals("v = 23:59:60.5"s, FirstLine(text), "as 23:59:60.5");
    CheckEquals(outOfRange, Written(std::chrono::hh_mm_ss<std::chrono::seconds>(std::chrono::seconds(-1))),
        "a negative time can't be written");
    CheckEquals(outOfRange, Written(std::chrono::hh_mm_ss<std::chrono::seconds>(std::chrono::seconds(86401))),
        "nor a day and a second");
    CheckEquals(ETomlCode::ValueOutOfRange, tooLate.Code, "out of range");
    CheckTrue(kept == TASWTomlValue("kept"), "a time refused should leave the value as it was");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_DateTimeConverters, TomlTypes_AsTheyAre)
{
    // Arrange
    TASWTomlOffsetDateTime const offsetDateTime{ Date(1979, 5, 27), TimeOfDay(0, 32, 0), std::chrono::minutes(-420) };
    TASWTomlLocalDateTime const localDateTime{ Date(1979, 5, 27), TimeOfDay(7, 32, 0) };
    TASWTomlLocalTime const localTime = TimeOfDay(23, 59, 60);
    TASWTomlOffsetDateTime const dayOffset{ Date(1979, 5, 27), TimeOfDay(0, 0, 0), std::chrono::minutes(1440) };
    TASWTomlLocalDateTime const unrealDate{ Date(2023, 2, 30), TimeOfDay(0, 0, 0) };
    TASWTomlOffsetDateTime offsetDateTimeRead;
    TASWTomlLocalDateTime localDateTimeRead;
    TASWTomlLocalTime localTimeRead;
    TASWTomlValue kept = "kept";

    // Act
    TASWTomlResult const readOffset = TASWTomlConverter<TASWTomlOffsetDateTime>::FromValue(offsetDateTime,
        offsetDateTimeRead, TASWTomlValueOptions());
    TASWTomlResult const readLocal = TASWTomlConverter<TASWTomlLocalDateTime>::FromValue(localDateTime,
        localDateTimeRead, TASWTomlValueOptions());
    TASWTomlResult const readTime = TASWTomlConverter<TASWTomlLocalTime>::FromValue(localTime, localTimeRead,
        TASWTomlValueOptions());
    TASWTomlResult const badOffset = TASWTomlConverter<TASWTomlOffsetDateTime>::ToValue(dayOffset, kept,
        TASWTomlValueOptions());
    TASWTomlResult const badDate = TASWTomlConverter<TASWTomlLocalDateTime>::ToValue(unrealDate, kept,
        TASWTomlValueOptions());
    TASWTomlResult const badTime = TASWTomlConverter<TASWTomlLocalTime>::ToValue(TimeOfDay(24, 0, 0), kept,
        TASWTomlValueOptions());

    // Assert
    CheckTrue(readOffset.IsOk() && offsetDateTimeRead == offsetDateTime, "an offset date-time should read as it is");
    CheckTrue(readLocal.IsOk() && localDateTimeRead == localDateTime, "a local date-time too");
    CheckTrue(readTime.IsOk() && localTimeRead == localTime, "and a local time, a leap second too");
    CheckEquals("1979-05-27T00:32:00-07:00"s, Written(offsetDateTime), "written with its offset");
    CheckEquals("1979-05-27T07:32:00"s, Written(localDateTime), "a local date-time written as it is");
    CheckEquals("23:59:60"s, Written(localTime), "a local time written as it is");
    CheckEquals(ETomlCode::InvalidValue, badOffset.Code, "an offset of a day can't be written");
    CheckEquals("The offset can't be written: it must be from -23:59 to +23:59."s, badOffset.Message,
        "the writer's message");
    CheckEquals("The date can't be written: it must be a real date with a year from 0 to 9999."s, badDate.Message,
        "nor a date that isn't real");
    CheckEquals(ETomlCode::InvalidValue, badTime.Code, "nor a time that isn't valid");
    CheckTrue(kept == TASWTomlValue("kept"), "a value refused should leave the value as it was");
    CheckEmpty(RefusalProblem(offsetDateTime, { ETomlType::OffsetDateTime }, "an offset date-time"),
        "an offset date-time refuses the other types");
    CheckEmpty(RefusalProblem(localDateTime, { ETomlType::LocalDateTime }, "a local date-time"),
        "a local date-time too");
    CheckEmpty(RefusalProblem(localTime, { ETomlType::LocalTime }, "a local time"), "and a local time");
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
// TTest_ASWToml_StructConverters
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_StructConverters::TTest_ASWToml_StructConverters()
    : inherited("ASWToml_StructConverters_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_StructConverters::~TTest_ASWToml_StructConverters()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_StructConverters::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_StructConverters::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_StructConverters::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_StructConverters::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_StructConverters, EveryType_RefusesWhatIsntATable)
{
    // Arrange & Act & Assert
    std::initializer_list<ETomlType> const accepted = { ETomlType::Table };
    CheckEmpty(RefusalProblem(TTestServer(), accepted, "a table"), "a struct should read tables only");
    CheckEmpty(RefusalProblem(TTestEmpty(), accepted, "a table"), "even one without fields");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_StructConverters, Fields_OfABaseClassAndPrivateMembers)
{
    // Arrange
    TTestLevel level;
    TTestScreen screen;
    TTestExtended extended;
    extended.Extra = 7;

    // Act
    TASWTomlResult const readLevel = TASWTomlConverter<TTestLevel>::FromValue(TTable{ { "name", "n" }, { "level", 3 } },
        level, TASWTomlValueOptions());
    TASWTomlResult const readScreen = TASWTomlConverter<TTestScreen>::FromValue(TTable{ { "width", 800 } }, screen,
        TASWTomlValueOptions());
    TASWTomlResult const readExtended = TASWTomlConverter<TTestExtended>::FromValue(TTable{ { "host", "h" },
        { "port", 1 }, { "extra", 2 } }, extended, TASWTomlValueOptions());

    // Assert
    CheckTrue(readLevel.IsOk() && level.Name == "n" && level.Level == 3,
        "a member of a base class should be a field: " + readLevel.Message);
    CheckEquals("{ name = \"n\", level = 3 }"s, Written(level), "and be written");
    CheckTrue(readScreen.IsOk() && screen == TTestScreen(800, 480),
        "private members listed by a friend should be fields: " + readScreen.Message);
    CheckEquals("{ width = 800, height = 480 }"s, Written(screen), "and be written");
    CheckTrue(readExtended.IsOk() && extended.Host == "h" && extended.Port == 1 && extended.Extra == 7,
        "a struct derived from one with fields should be read by its base's list: " + readExtended.Message);
    CheckEquals("{ host = \"h\", port = 1, enabled = true }"s, Written(extended), "and written by it");
    CheckEquals("{}"s, Written(TTestEmpty()), "a struct without fields should be an empty table");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_StructConverters, FromValue_FailsAtTheFirstFieldThatFails)
{
    // Arrange
    TTestServer initial;
    initial.Host = "kept";
    std::vector<TTestServer> const initialServers = { initial };
    TTestCluster initialCluster;
    initialCluster.Name = "kept";
    TASWTomlValue const complete = TTable{ { "host", "a" }, { "port", 1 } };
    TASWTomlValue const incomplete = TTable{ { "host", "b" } };
    TASWTomlValue const servers = TArray{ complete, incomplete };
    TASWTomlValue const wrongWidth = TTable{ { "width", "wide" } };

    // Act & Assert
    CheckEmpty(ReadFailureProblem(TTable{ { "host", "a" } }, initial, ETomlCode::NotFound,
        "The table has no key port.", "port"), "a key the table lacks should fail the struct, at the key");
    CheckEmpty(ReadFailureProblem(TTable{ { "port", 1 } }, initial, ETomlCode::NotFound, "The table has no key host.",
        "host"), "the first in the list's order");
    CheckEmpty(ReadFailureProblem(TTable{ { "port", "x" }, { "host", 5 } }, initial, ETomlCode::TypeMismatch,
        "The value 5 is an integer, not a string.", "host"), "a value that fails should fail it, at its key");
    CheckEmpty(ReadFailureProblem(TTable{ { "host", "a" }, { "port", 70000 } }, initial, ETomlCode::ValueOutOfRange,
        "The value 70000 is out of range: it must be from 0 to 65535.", "port"), "with the value's code and message");
    CheckEmpty(ReadFailureProblem(TTable{ { "host", "a" }, { "port", 1 }, { "comment", 2 } }, initial,
        ETomlCode::TypeMismatch, "The value 2 is an integer, not a string.", "comment"),
        "a std::optional member should be read as its item's type");
    CheckEmpty(ReadFailureProblem(servers, initialServers, ETomlCode::NotFound, "The table has no key port.",
        "[1].port"), "an element of a vector of structs should fail at its index and key");
    CheckEmpty(ReadFailureProblem(TTable{ { "name", "c" }, { "screen", wrongWidth } }, initialCluster,
        ETomlCode::TypeMismatch, "The value \"wide\" is a string, not an integer.", "screen.width"),
        "a struct in a struct should fail at the path of its key");
    CheckEmpty(ReadFailureProblem(TTable{ { "name", "c" }, { "screen", complete }, { "servers", servers } },
        initialCluster, ETomlCode::NotFound, "The table has no key width.", "screen.width"),
        "the first field in the list's order, not the table's");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_StructConverters, FromValue_ReadsEachFieldIntoACopy)
{
    // Arrange
    TTestServer server;
    server.Host = "kept";
    server.Port = 1;
    server.Enabled = false;
    server.Comment = "kept";
    TTestServer partial = server;

    // Act
    TASWTomlResult const read = TASWTomlConverter<TTestServer>::FromValue(TTable{ { "extra", true },
        { "port", 8080 }, { "host", "example.com" } }, server, TASWTomlValueOptions());
    TASWTomlResult const readPartial = TASWTomlConverter<TTestServer>::FromValue(TTable{ { "host", "a" },
        { "port", 2 }, { "enabled", true }, { "comment", "new" } }, partial, TASWTomlValueOptions());

    // Assert
    CheckTrue(read.IsOk() && server.Host == "example.com" && server.Port == 8080,
        "each field should read its key's value, whatever the table's order: " + read.Message);
    CheckFalse(server.Enabled, "an optional field the table lacks should keep the value it had");
    CheckFalse(server.Comment.has_value(), "a std::optional member the table lacks should be std::nullopt");
    CheckTrue(readPartial.IsOk() && partial.Enabled && partial.Comment == "new",
        "and both should read a key the table has: " + readPartial.Message);
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_StructConverters, OptionalStruct_ReadsIntoTheValueItHolds)
{
    // Arrange
    TTestCluster cluster;
    cluster.Name = "c";
    cluster.Spare = TTestScreen(1, 2);
    TTestCluster empty = cluster;
    empty.Spare.reset();
    TASWTomlValue const spare = TTable{ { "width", 3 } };

    // Act
    TASWTomlResult const read = TASWTomlConverter<TTestCluster>::FromValue(TTable{ { "name", "c" },
        { "screen", spare }, { "spare", spare } }, cluster, TASWTomlValueOptions());
    TASWTomlResult const readEmpty = TASWTomlConverter<TTestCluster>::FromValue(TTable{ { "name", "c" },
        { "screen", spare }, { "spare", spare } }, empty, TASWTomlValueOptions());

    // Assert
    CheckTrue(read.IsOk() && cluster.Spare == TTestScreen(3, 2),
        "a std::optional struct should be read into the struct it holds, which keeps what the table lacks: " +
        read.Message);
    CheckTrue(readEmpty.IsOk() && empty.Spare == TTestScreen(3, 480),
        "and into a new one when it holds none: " + readEmpty.Message);
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_StructConverters, RoundTrip_EveryType_ThroughADocument)
{
    // Arrange
    TTestServer first;
    first.Host = "a";
    TTestServer second;
    second.Host = "b";
    second.Port = 443;
    second.Enabled = false;
    second.Comment = "backup";
    TTestCluster cluster;
    cluster.Name = "c";
    cluster.Limits = { { "cpu", 2 } };
    cluster.Servers = { first, second };
    std::map<std::string, TTestServer> const named = { { "main", first } };

    // Act & Assert
    CheckEmpty(TableRoundTripProblem(first, "[v]\nhost = \"a\"\nport = 80\nenabled = true\n"),
        "a struct, without its std::optional member that is std::nullopt");
    CheckEmpty(TableRoundTripProblem(second, "[v]\nhost = \"b\"\nport = 443\nenabled = false\ncomment = \"backup\"\n"),
        "with it when it holds a value");
    CheckEmpty(TableRoundTripProblem(std::vector<TTestServer>{ first, second }, "[[v]]\nhost = \"a\"\nport = 80\n"
        "enabled = true\n\n[[v]]\nhost = \"b\"\nport = 443\nenabled = false\ncomment = \"backup\"\n"),
        "a vector of structs as an array of tables");
    CheckEmpty(TableRoundTripProblem(named, "[v.main]\nhost = \"a\"\nport = 80\nenabled = true\n"),
        "a map of structs as a table of tables");
    CheckEmpty(TableRoundTripProblem(cluster, "[v]\nname = \"c\"\n\n[v.screen]\nwidth = 640\nheight = 480\n\n"
        "[v.limits]\ncpu = 2\n\n[[v.servers]]\nhost = \"a\"\nport = 80\nenabled = true\n\n[[v.servers]]\nhost = \"b\"\n"
        "port = 443\nenabled = false\ncomment = \"backup\"\n"), "a struct of structs, maps and vectors");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_StructConverters, ToValue_FailsForAKeyOrValueThatCantBeWritten)
{
    // Arrange
    TTestServer server;
    server.Host = "\xFF";
    TTestCluster cluster;
    cluster.Servers = { TTestServer(), server };

    // Act & Assert
    CheckEmpty(WriteFailureProblem(server, ETomlCode::InvalidValue,
        "The string can't be written: it isn't valid UTF-8 (at byte 0), which TOML needs.", "host"),
        "a value that can't be written should fail the struct, at its key");
    CheckEmpty(WriteFailureProblem(cluster, ETomlCode::InvalidValue,
        "The string can't be written: it isn't valid UTF-8 (at byte 0), which TOML needs.", "servers[1].host"),
        "inside a field's value too");
    CheckEmpty(WriteFailureProblem(TTestTwice(), ETomlCode::InvalidValue, "The key a is named by two fields of the "
        "struct's ASWTomlFields(): each field needs a key of its own.", ""),
        "a key named twice in the list should fail it, without a path");
    CheckEmpty(WriteFailureProblem(TTestBadKey(), ETomlCode::InvalidValue,
        "The key can't be written: it isn't valid UTF-8 (at byte 0), which TOML needs.", ""),
        "and so should a key that isn't valid UTF-8");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_StructConverters, ToValue_WritesEveryFieldInTheListsOrder)
{
    // Arrange
    TTestServer server;
    server.Host = "h";
    server.Comment = "c";
    TASWTomlValue value = TArray{ 1 };

    // Act
    TASWTomlResult const result = TASWTomlConverter<TTestServer>::ToValue(server, value, TASWTomlValueOptions());

    // Assert
    TTable const* const table = value.AsTable();
    CheckTrue(result.IsOk() && table != nullptr && table->size() == 4,
        "the array should be replaced by a table of the four fields: " + result.Message);
    if (table != nullptr && table->size() == 4)
    {
        CheckTrue((*table)[0].Key == "host" && (*table)[1].Key == "port" && (*table)[2].Key == "enabled" &&
            (*table)[3].Key == "comment", "in the order of the list");
        CheckTrue((*table)[0].Value == TASWTomlValue("h") && (*table)[1].Value == TASWTomlValue(80) &&
            (*table)[2].Value == TASWTomlValue(true) && (*table)[3].Value == TASWTomlValue("c"),
            "each with its member's value");
    }
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_TableConverters
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_TableConverters::TTest_ASWToml_TableConverters()
    : inherited("ASWToml_TableConverters_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_TableConverters::~TTest_ASWToml_TableConverters()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_TableConverters::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_TableConverters::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_TableConverters::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_TableConverters::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TableConverters, EnumKeys_ReadByName)
{
    // Arrange
    using TColors = std::map<EColor, int>;
    using TStates = std::map<TWindow::EState, int>;
    TColors const initial = { { EColor::Blue, 7 } };
    TColors colors = initial;
    TStates states;

    // Act
    TASWTomlResult const read = TASWTomlConverter<TColors>::FromValue(TTable{ { "red", 1 }, { "LIME", 2 } }, colors,
        TASWTomlValueOptions());
    TASWTomlResult const readStates = TASWTomlConverter<TStates>::FromValue(TTable{ { "Zoomed", 1 } }, states,
        TASWTomlValueOptions());

    // Assert
    CheckTrue(read.IsOk() && colors == TColors{ { EColor::Red, 1 }, { EColor::Green, 2 } },
        "each key should read as one of the enum's names, the case of its letters ignored: " + read.Message);
    CheckTrue(readStates.IsOk() && states == TStates{ { TWindow::EState::Maximized, 1 } },
        "a value's second name too: " + readStates.Message);
    CheckEquals("{ Red = 1, Green = 2 }"s, Written(TColors{ { EColor::Green, 2 }, { EColor::Red, 1 } }),
        "a key should be written as its value's first name, in the order of the values");
    CheckEmpty(ReadFailureProblem(TTable{ { "Red", 1 }, { "Purple", 2 } }, initial, ETomlCode::InvalidValue,
        "The key Purple isn't a name of the enum: it must be Red, Green or Blue.", "Purple"),
        "a key that isn't a name should fail the map, at the key");
    CheckEmpty(ReadFailureProblem(TTable{ { "40", 1 } }, initial, ETomlCode::InvalidValue,
        "The key 40 isn't a name of the enum: it must be Red, Green or Blue.", "40"),
        "and so should a listed value's number: a key is a string");
    CheckEmpty(ReadFailureProblem(TTable{ { "a b", 1 } }, std::map<EEmpty, int>(), ETomlCode::InvalidValue,
        "The key \"a b\" isn't a name of the enum: it has no names.", "\"a b\""),
        "an enum with no names should read no key");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TableConverters, EveryType_RefusesWhatIsntATable)
{
    // Arrange & Act & Assert
    std::initializer_list<ETomlType> const accepted = { ETomlType::Table };
    CheckEmpty(RefusalProblem(std::map<std::string, int>{ { "k", 7 } }, accepted, "a table"),
        "a map should read tables only");
    CheckEmpty(RefusalProblem(std::unordered_map<int, int>{ { 1, 7 } }, accepted, "a table"), "and an unordered map");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TableConverters, FromValue_FailsAtTheFirstKeyThatFails)
{
    // Arrange
    std::map<std::string, int> const initial = { { "kept", 7 } };
    std::map<std::string, std::vector<int> > const initialLists = { { "kept", { 7 } } };
    std::map<std::string, std::map<std::string, int> > const initialNested = { { "kept", initial } };
    // The byte as a std::uint8_t, so std::pair's constructor converts none
    std::map<std::string, std::uint8_t> const initialBytes = { { "kept", std::uint8_t{ 7 } } };
    TASWTomlValue const list = TArray{ 1, "x" };
    TASWTomlValue const inner = TTable{ { "x", 1 }, { "y", "z" } };

    // Act & Assert
    CheckEmpty(ReadFailureProblem(TTable{ { "a", 1 }, { "port", "x" }, { "b", true } }, initial,
        ETomlCode::TypeMismatch, "The value \"x\" is a string, not an integer.", "port"),
        "the first value that fails should fail the map, at its key");
    CheckEmpty(ReadFailureProblem(TTable{ { "b", "x" }, { "a", "y" } }, initial, ETomlCode::TypeMismatch,
        "The value \"x\" is a string, not an integer.", "b"), "the first in the table's order, not the map's");
    CheckEmpty(ReadFailureProblem(TTable{ { "host name", list } }, initialLists, ETomlCode::TypeMismatch,
        "The value \"x\" is a string, not an integer.", "\"host name\"[1]"), "with the place inside its value");
    CheckEmpty(ReadFailureProblem(TTable{ { "a", inner } }, initialNested, ETomlCode::TypeMismatch,
        "The value \"z\" is a string, not an integer.", "a.y"), "inside a nested map too");
    CheckEmpty(ReadFailureProblem(TTable{ { "a", 300 } }, initialBytes, ETomlCode::ValueOutOfRange,
        "The value 300 is out of range: it must be from 0 to 255.", "a"), "with the value's code and message");
    CheckEmpty(ReadFailureProblem(TTable{ { "port", "x" } }, std::unordered_map<std::string, int>{ { "kept", 7 } },
        ETomlCode::TypeMismatch, "The value \"x\" is a string, not an integer.", "port"), "and an unordered map");
    CheckEmpty(ReadFailureProblem(TTable{ { "x", "y" } }, std::map<int, int>{ { 1, 7 } }, ETomlCode::InvalidValue,
        "The key x isn't a decimal integer (such as 80 or -1, without a + or leading zeros).", "x"),
        "a key should be read before its value");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TableConverters, FromValue_ReadsAnyTable)
{
    // Arrange
    using TNumbers = std::map<std::string, int>;
    TNumbers const expected = { { "a", 1 }, { "b", 2 } };
    TNumbers emptied = { { "kept", 7 } };
    TASWTomlDocument document;
    TASWTomlResult const parsed = document.Parse("inline = { a = 1, b = 2 }\ndotted.a = 1\ndotted.b = 2\n\n"
        "[header]\na = 1\nb = 2\n");

    // Act
    TASWTomlResult const readEmpty = TASWTomlConverter<TNumbers>::FromValue(TTable{}, emptied, TASWTomlValueOptions());

    // Assert
    CheckTrue(parsed.IsOk(), "the document should parse: " + parsed.Message);
    for (char const* const table : { "inline", "dotted", "header" })
    {
        TNumbers numbers = { { "kept", 7 } };
        TASWTomlResult read;
        TASWTomlValue const* const found = document.Find(table);
        if (found != nullptr)
            read = TASWTomlConverter<TNumbers>::FromValue(*found, numbers, TASWTomlValueOptions());

        CheckTrue(found != nullptr && read.IsOk() && numbers == expected,
            "the keys of the " + std::string(table) + " table should replace the map's: " + read.Message);
    }

    CheckTrue(readEmpty.IsOk() && emptied.empty(), "an empty table should read as an empty map");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TableConverters, Helpers_RepeatedEnumKey)
{
    // Arrange
    std::string const problem = " names two values of the enum, which would read back as one: each value needs a "
        "name of its own in ASWTomlEnumNames().";
    TTable const upperFirst = { { "Twin", 1 }, { "Other", 2 }, { "twin", 3 } };
    TTable const lowerFirst = { { "twin", 3 }, { "Other", 2 }, { "Twin", 1 } };
    TTable const three = { { "TWIN", 1 }, { "twin", 2 }, { "Twin", 3 } };
    TTable const same = { { "Red", 1 }, { "Red", 2 } };
    TTable const different = { { "Red", 1 }, { "Green", 2 }, { "Re", 3 } };

    // Act
    TASWTomlResult const fromUpperFirst = ASWToml::Detail::RepeatedEnumKey(upperFirst);
    TASWTomlResult const fromLowerFirst = ASWToml::Detail::RepeatedEnumKey(lowerFirst);
    TASWTomlResult const fromThree = ASWToml::Detail::RepeatedEnumKey(three);
    TASWTomlResult const fromSame = ASWToml::Detail::RepeatedEnumKey(same);
    TASWTomlResult const fromDifferent = ASWToml::Detail::RepeatedEnumKey(different);

    // Assert
    CheckTrue(fromUpperFirst.Code == ETomlCode::InvalidValue && fromUpperFirst.Path.empty(),
        "two keys that are one name with the case ignored should be refused, without a path");
    CheckEquals("The key twin" + problem, fromUpperFirst.Message, "naming the second of them in byte order");
    CheckEquals("The key twin" + problem, fromLowerFirst.Message, "whatever the table's order");
    CheckEquals("The key Twin" + problem, fromThree.Message, "of three, the second in byte order");
    CheckEquals("The key Red" + problem, fromSame.Message, "two keys that are the same should be refused too");
    CheckTrue(fromDifferent.IsOk(), "keys that differ with the case ignored should be accepted");
    CheckTrue(ASWToml::Detail::RepeatedEnumKey(TTable()).IsOk(), "and no keys");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TableConverters, IntegerKeys_ReadAsDecimalDigits)
{
    // Arrange
    using TPorts = std::map<int, std::string>;
    using TLarge = std::map<std::uint64_t, int>;
    using TSmall = std::map<std::int64_t, int>;
    using TSwitches = std::map<ESwitch, int>;
    std::int64_t const smallest = std::numeric_limits<std::int64_t>::min();
    std::int64_t const largest = std::numeric_limits<std::int64_t>::max();
    TTable const portKeys = { { "80", "http" }, { "0", "zero" }, { "-1", "none" } };
    TTable const largeKeys = { { "18446744073709551615", 1 }, { "0", 2 } };
    TTable const smallKeys = { { "-9223372036854775808", 1 }, { "9223372036854775807", 2 } };
    TPorts ports = { { 7, "kept" } };
    TLarge large;
    TSmall small;
    TSwitches switches;

    // Act
    TASWTomlResult const read = TASWTomlConverter<TPorts>::FromValue(portKeys, ports, TASWTomlValueOptions());
    TASWTomlResult const readLarge = TASWTomlConverter<TLarge>::FromValue(largeKeys, large, TASWTomlValueOptions());
    TASWTomlResult const readSmall = TASWTomlConverter<TSmall>::FromValue(smallKeys, small, TASWTomlValueOptions());
    TASWTomlResult const readSwitches = TASWTomlConverter<TSwitches>::FromValue(TTable{ { "1", 1 }, { "0", 2 } },
        switches, TASWTomlValueOptions());

    // Assert
    CheckTrue(read.IsOk() && ports == TPorts{ { -1, "none" }, { 0, "zero" }, { 80, "http" } },
        "each key should read as the number its digits give: " + read.Message);
    CheckTrue(readLarge.IsOk() && large == TLarge{ { 18446744073709551615u, 1 }, { 0, 2 } },
        "an unsigned 64-bit type's whole range, which a key can hold as text: " + readLarge.Message);
    CheckTrue(readSmall.IsOk() && small == TSmall{ { smallest, 1 }, { largest, 2 } },
        "and a signed one's: " + readSmall.Message);
    CheckTrue(readSwitches.IsOk() && switches == TSwitches{ { ESwitch::On, 1 }, { ESwitch::Off, 2 } },
        "an enum without names as its underlying type's numbers: " + readSwitches.Message);
    CheckEquals("{ -1 = 1, 0 = 2, 80 = 3 }"s, Written(std::map<int, int>{ { 80, 3 }, { -1, 1 }, { 0, 2 } }),
        "a key should be written as its decimal digits, in the order of the numbers");
    CheckEquals("{ 18446744073709551615 = 1 }"s, Written(TLarge{ { 18446744073709551615u, 1 } }),
        "any number of an unsigned 64-bit type");
    CheckEquals("{ -9223372036854775808 = 1 }"s, Written(TSmall{ { smallest, 1 } }), "and of a signed one");
    CheckEquals("{ 0 = 1, 1 = 2 }"s, Written(TSwitches{ { ESwitch::On, 2 }, { ESwitch::Off, 1 } }),
        "an enum without names as its underlying type's numbers");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TableConverters, IntegerKeys_RefuseWhatIsntANumberInRange)
{
    // Arrange
    using TPorts = std::map<int, std::string>;
    TPorts const initial = { { 7, "kept" } };
    std::string const notDigits = " isn't a decimal integer (such as 80 or -1, without a + or leading zeros).";
    std::string const tooLong = "99999999999999999999999999999999999999999999";
    // Each key that isn't the form a number is written in, and how a path writes it
    std::pair<char const*, char const*> const others[] = {
        { "01", "01" }, { "-0", "-0" }, { "+1", "\"+1\"" }, { "1_000", "1_000" }, { "0x10", "0x10" },
        { "1e3", "1e3" }, { " 1", "\" 1\"" }, { "1 ", "\"1 \"" }, { "-", "-" }, { "--1", "--1" }, { "", "\"\"" },
    };

    // Act & Assert
    for (std::pair<char const*, char const*> const& other : others)
    {
        CheckEmpty(ReadFailureProblem(TTable{ { other.first, "x" } }, initial, ETomlCode::InvalidValue,
            "The key "s + other.second + notDigits, other.second), "a key that isn't 0, or digits that don't start "
            "with 0 after an optional -, should be refused: " + std::string(other.first));
    }

    CheckEmpty(ReadFailureProblem(TTable{ { "256", 1 } }, std::map<std::uint8_t, int>(), ETomlCode::ValueOutOfRange,
        "The key 256 is out of range: it must be from 0 to 255.", "256"), "a key above the type's range should fail");
    CheckEmpty(ReadFailureProblem(TTable{ { "-1", 1 } }, std::map<std::uint8_t, int>(), ETomlCode::ValueOutOfRange,
        "The key -1 is out of range: it must be from 0 to 255.", "-1"), "and a negative one for an unsigned type");
    CheckEmpty(ReadFailureProblem(TTable{ { "-129", 1 } }, std::map<std::int8_t, int>(), ETomlCode::ValueOutOfRange,
        "The key -129 is out of range: it must be from -128 to 127.", "-129"), "and one below a signed type's range");
    CheckEmpty(ReadFailureProblem(TTable{ { "18446744073709551616", 1 } }, std::map<std::uint64_t, int>(),
        ETomlCode::ValueOutOfRange, "The key 18446744073709551616 is out of range: it must be from 0 to "
        "18446744073709551615.", "18446744073709551616"), "and one of more than 64 bits");
    CheckEmpty(ReadFailureProblem(TTable{ { "-9223372036854775809", 1 } }, std::map<std::int64_t, int>(),
        ETomlCode::ValueOutOfRange, "The key -9223372036854775809 is out of range: it must be from "
        "-9223372036854775808 to 9223372036854775807.", "-9223372036854775809"), "or below the 64-bit range");
    CheckEmpty(ReadFailureProblem(TTable{ { "2", 1 } }, std::map<ESwitch, int>(), ETomlCode::ValueOutOfRange,
        "The key 2 is out of range: it must be from 0 to 1.", "2"), "an enum without names has its underlying type's");
    CheckEmpty(ReadFailureProblem(TTable{ { tooLong, 1 } }, std::map<int, int>(), ETomlCode::ValueOutOfRange,
        "The key " + tooLong.substr(0, 40) + "... is out of range: it must be from -2147483648 to 2147483647.", tooLong),
        "a long key should be cut in the message, but not in the path");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TableConverters, ReadsWithTheMapsOwnComparator)
{
    // Arrange
    using TNoCaseMap = std::map<std::string, int, TTestTextLess>;
    using TNoCaseHashMap = std::unordered_map<std::string, int, TTestTextHash, TTestTextEqual>;
    TNoCaseMap const initial(TTestTextLess{ true });
    TNoCaseHashMap const initialHashed(0, TTestTextHash{ 7 }, TTestTextEqual{ true });
    TNoCaseMap texts = initial;
    TNoCaseHashMap hashed = initialHashed;

    // Act
    TASWTomlResult const read = TASWTomlConverter<TNoCaseMap>::FromValue(TTable{ { "B", 1 }, { "a", 2 } }, texts,
        TASWTomlValueOptions());
    TASWTomlResult const readHashed = TASWTomlConverter<TNoCaseHashMap>::FromValue(TTable{ { "B", 1 }, { "a", 2 } },
        hashed, TASWTomlValueOptions());

    // Assert
    CheckTrue(read.IsOk() && texts.key_comp().IgnoreCase, "the map should keep its comparator: " + read.Message);
    CheckEquals("{ a = 2, B = 1 }"s, Written(texts), "and its order, which ignores the case");
    CheckTrue(readHashed.IsOk() && hashed.key_eq().IgnoreCase && hashed.hash_function().Seed == 7,
        "an unordered map should keep its key equality and hash: " + readHashed.Message);
    CheckEmpty(ReadFailureProblem(TTable{ { "a", 1 }, { "A", 2 } }, initial, ETomlCode::InvalidValue,
        "The key A reads as a key the map already has.", "A"), "A should be a, as its comparator says");
    CheckEmpty(ReadFailureProblem(TTable{ { "a", 1 }, { "A", 2 } }, initialHashed, ETomlCode::InvalidValue,
        "The key A reads as a key the map already has.", "A"), "or an unordered map's key equality");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TableConverters, RepeatedKey_FailsTheMap)
{
    // Arrange
    std::map<EColor, int> const initial = { { EColor::Blue, 7 } };
    std::unordered_map<EColor, int> const initialUnordered = { { EColor::Blue, 7 } };

    // Act & Assert
    CheckEmpty(ReadFailureProblem(TTable{ { "Red", 1 }, { "red", 2 } }, initial, ETomlCode::InvalidValue,
        "The key red reads as a key the map already has.", "red"),
        "two keys that read as one value should fail the map, at the second");
    CheckEmpty(ReadFailureProblem(TTable{ { "Lime", 1 }, { "Blue", 2 }, { "GREEN", 3 } }, initial,
        ETomlCode::InvalidValue, "The key GREEN reads as a key the map already has.", "GREEN"),
        "two names of a value should be one key");
    CheckEmpty(ReadFailureProblem(TTable{ { "Red", 1 }, { "red", 2 } }, initialUnordered, ETomlCode::InvalidValue,
        "The key red reads as a key the map already has.", "red"), "in an unordered map too");
    CheckEmpty(ReadFailureProblem(TTable{ { "Red", 1 }, { "red", "x" } }, initial, ETomlCode::InvalidValue,
        "The key red reads as a key the map already has.", "red"), "before its value is read");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TableConverters, RoundTrip_EveryType_ThroughADocument)
{
    // Arrange
    std::map<std::string, int> const first = { { "x", 1 } };
    std::map<std::string, int> const second = { { "y", 2 } };
    std::map<std::string, std::map<std::string, int> > const nested = { { "b", second }, { "a", first } };
    std::map<std::string, std::vector<int> > const lists = { { "ports", { 80, 443 } } };

    // Act & Assert
    CheckEmpty(TableRoundTripProblem(std::map<std::string, int>{ { "b", 1 }, { "host name", 2 } },
        "[v]\nb = 1\n\"host name\" = 2\n"), "a map of strings");
    CheckEmpty(TableRoundTripProblem(std::map<int, std::string>{ { 80, "b" }, { -1, "a" } },
        "[v]\n-1 = \"a\"\n80 = \"b\"\n"), "a map of integers");
    CheckEmpty(TableRoundTripProblem(std::map<std::uint64_t, int>{ { 18446744073709551615u, 1 } },
        "[v]\n18446744073709551615 = 1\n"), "an unsigned 64-bit key");
    CheckEmpty(TableRoundTripProblem(std::map<EColor, bool>{ { EColor::Blue, true }, { EColor::Red, false } },
        "[v]\nRed = false\nBlue = true\n"), "a map of an enum with names");
    CheckEmpty(TableRoundTripProblem(std::map<EFlags, int>{ { EFlags::Write, 1 } }, "[v]\n2 = 1\n"),
        "a map of an enum without names");
    CheckEmpty(TableRoundTripProblem(std::unordered_map<std::string, int>{ { "b", 1 }, { "a", 2 } },
        "[v]\na = 2\nb = 1\n"), "an unordered map");
    CheckEmpty(TableRoundTripProblem(nested, "[v.a]\nx = 1\n\n[v.b]\ny = 2\n"), "a map of maps");
    CheckEmpty(TableRoundTripProblem(lists, "[v]\nports = [80, 443]\n"), "a map of vectors");
    CheckEmpty(TableRoundTripProblem(std::map<std::string, int>(), "[v]\n"), "an empty map");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TableConverters, ToValue_FailsAtTheFirstKeyOrValueThatFails)
{
    // Arrange
    std::string const invalid = "\xFF";
    std::string const message = "The string can't be written: it isn't valid UTF-8 (at byte 0), which TOML needs.";
    std::map<std::string, std::string> const inner = { { "x", invalid } };
    std::map<std::string, std::map<std::string, std::string> > const nested = { { "host name", inner } };
    std::map<std::string, std::string> const texts = { { "c", invalid }, { "a", "ok" }, { "b", invalid } };
    std::map<ETwin, int> const twinKeys = { { ETwin::Upper, 1 }, { ETwin::Other, 3 }, { ETwin::Lower, 2 } };
    std::string const twins = "The key twin names two values of the enum, which would read back as one: each value "
        "needs a name of its own in ASWTomlEnumNames().";

    // Act & Assert
    CheckEmpty(WriteFailureProblem(texts, ETomlCode::InvalidValue, message, "b"),
        "a map should fail at the first value that can't be written, in its order");
    CheckEmpty(WriteFailureProblem(nested, ETomlCode::InvalidValue, message, "\"host name\".x"),
        "inside a nested map too");
    CheckEmpty(WriteFailureProblem(std::unordered_map<std::string, std::uint64_t>{ { "a", 18446744073709551615u } },
        ETomlCode::ValueOutOfRange, "The value 18446744073709551615 is out of range: it must be from 0 to "
        "9223372036854775807.", "a"), "with the value's code and message, in an unordered map too");
    CheckEmpty(WriteFailureProblem(std::map<std::string, int>{ { "a" + invalid, 1 } }, ETomlCode::InvalidValue,
        "The key can't be written: it isn't valid UTF-8 (at byte 1), which TOML needs.", ""),
        "a key that isn't valid UTF-8 should fail the map, without a path");
    CheckEmpty(WriteFailureProblem(std::map<EColor, int>{ { EColor::Red, 1 }, { EColor::Unlisted, 2 } },
        ETomlCode::InvalidValue, "The value 7 has no name in the enum's ASWTomlEnumNames(), so it can't be written.",
        ""), "and so should an enum value without a name");
    CheckEmpty(WriteFailureProblem(std::map<EInvalidName, int>{ { EInvalidName::Bad, 1 } }, ETomlCode::InvalidValue,
        "The string can't be written: it isn't valid UTF-8 (at byte 1), which TOML needs.", ""),
        "or one whose name isn't valid UTF-8");
    CheckEmpty(WriteFailureProblem(twinKeys, ETomlCode::InvalidValue, twins, ""),
        "or two values written as names that would read back as one");
    CheckEmpty(WriteFailureProblem(std::unordered_map<ETwin, int>{ { ETwin::Lower, 2 }, { ETwin::Upper, 1 } },
        ETomlCode::InvalidValue, twins, ""), "in an unordered map too");
    CheckEquals("{ twin = 2 }"s, Written(std::map<ETwin, int>{ { ETwin::Lower, 2 } }), "but either alone is written");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TableConverters, ToValue_WritesInTheMapsOrder)
{
    // Arrange
    TASWTomlValue const expected = TTable{ { "a", 1 }, { "b", 2 } };
    TASWTomlValue value = TArray{ 1 };

    // Act
    TASWTomlResult const result = TASWTomlConverter<std::map<std::string, int> >::ToValue({ { "b", 2 }, { "a", 1 } },
        value, TASWTomlValueOptions());

    // Assert
    CheckTrue(result.IsOk() && value == expected,
        "the array should be replaced by a table of the map's keys: " + result.Message);
    CheckEquals("{ a = 1, b = 2 }"s, ASWToml::Detail::ValueText(value), "in the map's order");
    CheckEquals("{ b = 2, a = 1 }"s, Written(std::map<std::string, int, std::greater<> >{ { "a", 1 }, { "b", 2 } }),
        "whichever its comparator gives");
    CheckEquals("{}"s, Written(std::map<std::string, int>()), "an empty map should be an empty table");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_TableConverters, UnorderedMap_WritesKeysSortedByText)
{
    // Arrange
    std::unordered_map<int, int> const numbers = { { 20, 4 }, { 1, 1 }, { 2, 2 }, { 10, 3 } };
    std::unordered_map<std::string, int> const texts = { { "b", 1 }, { "a", 2 }, { "B", 3 } };

    // Act & Assert
    CheckEquals("{ 1 = 1, 10 = 3, 2 = 2, 20 = 4 }"s, Written(numbers),
        "an unordered map should be written with its keys in the order of their texts");
    CheckEquals("{ B = 3, a = 2, b = 1 }"s, Written(texts), "byte by byte");
    CheckEquals("{}"s, Written(std::unordered_map<int, int>()), "an empty one as an empty table");
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
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_ArrayConverters, "Converters")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_BoolConverter, "Converters")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_DateTimeConverters, "Converters")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_EnumConverters, "Converters")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_FloatConverters, "Converters")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_IntegerConverters, "Converters")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_StructConverters, "Converters")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_TableConverters, "Converters")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_TextConverters, "Converters")
