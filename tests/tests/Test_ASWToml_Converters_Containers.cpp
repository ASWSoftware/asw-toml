/* **************************************************************************
Test_ASWToml_Converters_Containers.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "Test_ASWToml_Converters_Containers.h"
//---------------------------------------------------------------------------
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <limits>
#include <list>
#include <map>
#include <memory_resource>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWToml_Converters.h"
#include "ASWToml_Document.h"
#include "ASWToml_Options.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------
#include "UT_Helper_Converters.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

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

} // namespace

} // namespace ASWUnitTests

namespace ASWToml
{

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
std::string ArrayRoundTripProblem(T const& in, std::string const& firstLine);

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

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_ArrayConverters, "Converters")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_TableConverters, "Converters")
