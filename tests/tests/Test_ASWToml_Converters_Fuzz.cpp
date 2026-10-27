/* **************************************************************************
Test_ASWToml_Converters_Fuzz.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "Test_ASWToml_Converters_Fuzz.h"
//---------------------------------------------------------------------------
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWToml_Converters.h"
#include "ASWToml_Document.h"
#include "ASWToml_File.h"
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

using ASWToml::ETomlCode;
using ASWToml::TASWTomlConverter;
using ASWToml::TASWTomlDocument;
using ASWToml::TASWTomlLocalTime;
using ASWToml::TASWTomlOffsetDateTime;
using ASWToml::TASWTomlResult;
using ASWToml::TASWTomlValue;
using ASWToml::TASWTomlValueOptions;
using ASWToml::TomlConvertible;

using TArray = TASWTomlValue::TValueVector;
using TTable = TASWTomlValue::TKeyValueVector;

// The fuzz tests: the seed of the first random value of each type, and how many each type gets
constexpr std::uint32_t FuzzFirstSeed = 20261025;
constexpr std::size_t FuzzCount = 300;

//---------------------------------------------------------------------------
std::string FailurePathProblem(TASWTomlValue const& value, TASWTomlResult const& failure);
void RandomChange(std::mt19937& generator, TASWTomlValue& value);
void RandomFill(std::mt19937& generator, bool& out);
void RandomFill(std::mt19937& generator, double& out);
void RandomFill(std::mt19937& generator, float& out);
void RandomFill(std::mt19937& generator, std::chrono::hh_mm_ss<std::chrono::milliseconds>& out);
void RandomFill(std::mt19937& generator, std::chrono::local_seconds& out);
void RandomFill(std::mt19937& generator, std::chrono::sys_days& out);
void RandomFill(std::mt19937& generator, std::chrono::sys_seconds& out);
void RandomFill(std::mt19937& generator, std::chrono::year_month_day& out);
void RandomFill(std::mt19937& generator, std::filesystem::path& out);
void RandomFill(std::mt19937& generator, std::string& out);
void RandomFill(std::mt19937& generator, std::tuple<int, bool, std::string>& out);
void RandomFill(std::mt19937& generator, std::u8string& out);
void RandomFill(std::mt19937& generator, ASWToml::TASWTomlLocalTime& out);
void RandomFill(std::mt19937& generator, ASWToml::TASWTomlOffsetDateTime& out);
void RandomFill(std::mt19937& generator, TASWTomlValue& out);
void RandomFill(std::mt19937& generator, TTestCluster& out);
void RandomFill(std::mt19937& generator, TTestScreen& out);
void RandomFill(std::mt19937& generator, TTestServer& out);
template <ASWToml::Detail::ConvertibleInteger T>
void RandomFill(std::mt19937& generator, T& out);
template <typename T>
requires std::is_enum_v<T>
void RandomFill(std::mt19937& generator, T& out);
template <typename T, std::size_t Size>
void RandomFill(std::mt19937& generator, std::array<T, Size>& out);
template <typename K, typename T, typename TCompare, typename TAllocator>
void RandomFill(std::mt19937& generator, std::map<K, T, TCompare, TAllocator>& out);
template <typename T1, typename T2>
void RandomFill(std::mt19937& generator, std::pair<T1, T2>& out);
template <typename T, typename TCompare, typename TAllocator>
void RandomFill(std::mt19937& generator, std::set<T, TCompare, TAllocator>& out);
template <typename K, typename T, typename THash, typename TEqual, typename TAllocator>
void RandomFill(std::mt19937& generator, std::unordered_map<K, T, THash, TEqual, TAllocator>& out);
template <typename T, typename THash, typename TEqual, typename TAllocator>
void RandomFill(std::mt19937& generator, std::unordered_set<T, THash, TEqual, TAllocator>& out);
template <typename T, typename TAllocator>
void RandomFill(std::mt19937& generator, std::vector<T, TAllocator>& out);
template <TomlConvertible T>
std::string RandomReadProblem();
template <TomlConvertible T>
std::string RandomValueProblem();
template <TomlConvertible T>
std::string WrittenAgainProblem(std::mt19937& generator, TASWTomlValue const& value);

//---------------------------------------------------------------------------
/*
    FailurePathProblem

    What is wrong with the Path of 'failure', the failure of a converter to read 'value': empty, or a place in it (the
    value at the key v of a document, looked up there): a value it holds, or for NotFound, a key it lacks; empty when
    nothing is wrong
*/
std::string FailurePathProblem(TASWTomlValue const& value, TASWTomlResult const& failure)
{
    if (failure.Path.empty())
        return std::string();

    TASWTomlDocument document;
    TASWTomlResult const set = document.SetValue("v", value);
    if (!set.IsOk())
        return "the value should be set in a document: " + set.Message;

    std::string path = "v";
    if (failure.Path.front() != '[')
        path += '.';

    path += failure.Path;
    bool const found = document.Find(path) != nullptr;
    if (failure.Code == ETomlCode::NotFound && found)
        return "the path " + failure.Path + " of a NotFound failure names a value the value has";

    if (failure.Code != ETomlCode::NotFound && !found)
        return "the path " + failure.Path + " names no value of the value";

    return std::string();
}
//---------------------------------------------------------------------------
/*
    RandomChange

    Changes 'value' at random: one time in three a part of it instead (an element of an array or the value of a key
    of a table, and so on down), else a table loses or gains a key at times, and any value may be replaced by a random
    one (see RandomTomlValue())
*/
void RandomChange(std::mt19937& generator, TASWTomlValue& value)
{
    TArray* const array = value.AsArray();
    TTable* const table = value.AsTable();
    std::uint32_t const choice = generator() % 6;
    if (choice < 2 && array != nullptr && !array->empty())
    {
        std::size_t const index = generator() % array->size();
        RandomChange(generator, (*array)[index]);
    }
    else if (choice < 2 && table != nullptr && !table->empty())
    {
        std::size_t const index = generator() % table->size();
        RandomChange(generator, (*table)[index].Value);
    }
    else if (choice == 2 && table != nullptr && !table->empty())
    {
        std::size_t const index = generator() % table->size();
        table->erase(table->begin() + static_cast<std::ptrdiff_t>(index));
    }
    else if (choice == 3 && table != nullptr)
    {
        std::string const key = FuzzKeys[generator() % std::size(FuzzKeys)];
        value.Set(key, RandomTomlValue(generator, 2));
    }
    else
    {
        value = RandomTomlValue(generator, 1);
    }
}
//---------------------------------------------------------------------------
/*
    RandomFill

    Sets 'out' to a random value of its type: most of them can be written, and some can't (a string that isn't
    valid UTF-8, a number a TOML integer can't hold, an enum value without a name, a date that isn't real or outside
    the years 0 to 9999, a time of day beyond a day and a second), so both ways are checked; a container gets up to
    three random elements
*/
void RandomFill(std::mt19937& generator, bool& out)
{
    out = generator() % 2 == 0;
}
//---------------------------------------------------------------------------
void RandomFill(std::mt19937& generator, double& out)
{
    out = RandomDouble(generator);
}
//---------------------------------------------------------------------------
void RandomFill(std::mt19937& generator, float& out)
{
    float const special[] = {
        0.1f, -0.0f, 7.038531e-26f, 16777216.0f, std::numeric_limits<float>::max(),
        std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(),
    };

    if (generator() % 2 == 0)
    {
        std::uint32_t const bits = generator();
        out = std::bit_cast<float>(bits);
    }
    else
    {
        out = special[generator() % std::size(special)];
    }
}
//---------------------------------------------------------------------------
void RandomFill(std::mt19937& generator, std::chrono::hh_mm_ss<std::chrono::milliseconds>& out)
{
    // Any time from a second before midnight to two seconds after the end of the day, or one time in four within a
    // second of midnight, of 24:00:00 (written as 23:59:60) or of a day and a second (which can't be written)
    std::int64_t const ends[] = { 0, 86400000, 86401000 };
    std::int64_t count = static_cast<std::int64_t>(generator() % 86403000) - 1000;
    if (generator() % 4 == 0)
    {
        std::int64_t const end = ends[generator() % std::size(ends)];
        count = end + static_cast<std::int64_t>(generator() % 2001) - 1000;
    }

    out = std::chrono::hh_mm_ss<std::chrono::milliseconds>(std::chrono::milliseconds(count));
}
//---------------------------------------------------------------------------
void RandomFill(std::mt19937& generator, std::chrono::local_seconds& out)
{
    std::chrono::sys_seconds instant;
    RandomFill(generator, instant);
    out = std::chrono::local_seconds(instant.time_since_epoch());
}
//---------------------------------------------------------------------------
void RandomFill(std::mt19937& generator, std::chrono::sys_days& out)
{
    // From about 50 years before the year 0 to as long after 9999
    std::int64_t const count = static_cast<std::int64_t>(generator() % 3690000) - 737000;
    out = std::chrono::sys_days(std::chrono::days(count));
}
//---------------------------------------------------------------------------
void RandomFill(std::mt19937& generator, std::chrono::sys_seconds& out)
{
    // A day of the years -50 to 10049, and any second of it
    std::chrono::sys_days day;
    RandomFill(generator, day);
    std::int64_t const second = generator() % 86400;
    out = std::chrono::sys_seconds(std::chrono::seconds(day.time_since_epoch().count() * std::int64_t{ 86400 } +
        second));
}
//---------------------------------------------------------------------------
void RandomFill(std::mt19937& generator, std::chrono::year_month_day& out)
{
    // Real or not, mostly of the years 0 to 9999
    int const year = static_cast<int>(generator() % 10021) - 10;
    unsigned const month = generator() % 14;
    unsigned const day = generator() % 33;
    out = Date(year, month, day);
}
//---------------------------------------------------------------------------
void RandomFill(std::mt19937& generator, std::filesystem::path& out)
{
    out = ASWToml::Utf8ToPath(RandomText(generator, true));
}
//---------------------------------------------------------------------------
void RandomFill(std::mt19937& generator, std::string& out)
{
    out = RandomText(generator, false);
}
//---------------------------------------------------------------------------
void RandomFill(std::mt19937& generator, std::tuple<int, bool, std::string>& out)
{
    RandomFill(generator, std::get<0>(out));
    RandomFill(generator, std::get<1>(out));
    RandomFill(generator, std::get<2>(out));
}
//---------------------------------------------------------------------------
void RandomFill(std::mt19937& generator, std::u8string& out)
{
    std::string const text = RandomText(generator, false);
    out.assign(text.begin(), text.end());
}
//---------------------------------------------------------------------------
void RandomFill(std::mt19937& generator, ASWToml::TASWTomlLocalTime& out)
{
    // Valid but one time in eight
    out = RandomTime(generator);
    if (generator() % 8 == 0)
        out.Hour = std::chrono::hours(24);
}
//---------------------------------------------------------------------------
void RandomFill(std::mt19937& generator, ASWToml::TASWTomlOffsetDateTime& out)
{
    out.Date = RandomDate(generator);
    RandomFill(generator, out.Time);
    std::int64_t const offset = static_cast<std::int64_t>(generator() % 2879) - 1439;
    out.Offset = std::chrono::minutes(offset);
}
//---------------------------------------------------------------------------
void RandomFill(std::mt19937& generator, TASWTomlValue& out)
{
    out = RandomTomlValue(generator, 1);
}
//---------------------------------------------------------------------------
void RandomFill(std::mt19937& generator, TTestCluster& out)
{
    RandomFill(generator, out.Name);
    RandomFill(generator, out.Screen);
    out.Spare.reset();
    if (generator() % 2 == 0)
        RandomFill(generator, out.Spare.emplace());

    RandomFill(generator, out.Limits);
    RandomFill(generator, out.Servers);
}
//---------------------------------------------------------------------------
void RandomFill(std::mt19937& generator, TTestScreen& out)
{
    int width = 0;
    RandomFill(generator, width);
    int height = 0;
    RandomFill(generator, height);
    out = TTestScreen(width, height);
}
//---------------------------------------------------------------------------
void RandomFill(std::mt19937& generator, TTestServer& out)
{
    RandomFill(generator, out.Host);
    RandomFill(generator, out.Port);
    RandomFill(generator, out.Enabled);
    out.Comment.reset();
    if (generator() % 2 == 0)
        RandomFill(generator, out.Comment.emplace());
}
//---------------------------------------------------------------------------
template <ASWToml::Detail::ConvertibleInteger T>
void RandomFill(std::mt19937& generator, T& out)
{
    // Converted modulo 2^N, so a type gets numbers beyond its range when they convert back as others
    out = static_cast<T>(RandomInteger(generator));
}
//---------------------------------------------------------------------------
template <typename T>
requires std::is_enum_v<T>
void RandomFill(std::mt19937& generator, T& out)
{
    using TNumber = std::underlying_type_t<T>;

    // One of the values with a name one time in two, else any number of the underlying type
    if constexpr (ASWToml::Detail::EnumWithNames<T>)
    {
        std::vector<T> named;
        for (auto const& name : ASWTomlEnumNames(T()))
            named.push_back(name.Value);

        if (!named.empty() && generator() % 2 == 0)
        {
            out = named[generator() % named.size()];
            return;
        }
    }

    out = static_cast<T>(static_cast<TNumber>(RandomInteger(generator)));
}
//---------------------------------------------------------------------------
template <typename T, std::size_t Size>
void RandomFill(std::mt19937& generator, std::array<T, Size>& out)
{
    for (T& element : out)
        RandomFill(generator, element);
}
//---------------------------------------------------------------------------
template <typename K, typename T, typename TCompare, typename TAllocator>
void RandomFill(std::mt19937& generator, std::map<K, T, TCompare, TAllocator>& out)
{
    out.clear();
    std::size_t const count = generator() % 4;
    for (std::size_t i = 0; i < count; ++i)
    {
        K key{};
        RandomFill(generator, key);
        T item{};
        RandomFill(generator, item);
        out.emplace(std::move(key), std::move(item));
    }
}
//---------------------------------------------------------------------------
template <typename T1, typename T2>
void RandomFill(std::mt19937& generator, std::pair<T1, T2>& out)
{
    RandomFill(generator, out.first);
    RandomFill(generator, out.second);
}
//---------------------------------------------------------------------------
template <typename T, typename TCompare, typename TAllocator>
void RandomFill(std::mt19937& generator, std::set<T, TCompare, TAllocator>& out)
{
    out.clear();
    std::size_t const count = generator() % 4;
    for (std::size_t i = 0; i < count; ++i)
    {
        T element{};
        RandomFill(generator, element);
        out.insert(std::move(element));
    }
}
//---------------------------------------------------------------------------
template <typename K, typename T, typename THash, typename TEqual, typename TAllocator>
void RandomFill(std::mt19937& generator, std::unordered_map<K, T, THash, TEqual, TAllocator>& out)
{
    out.clear();
    std::size_t const count = generator() % 4;
    for (std::size_t i = 0; i < count; ++i)
    {
        K key{};
        RandomFill(generator, key);
        T item{};
        RandomFill(generator, item);
        out.emplace(std::move(key), std::move(item));
    }
}
//---------------------------------------------------------------------------
template <typename T, typename THash, typename TEqual, typename TAllocator>
void RandomFill(std::mt19937& generator, std::unordered_set<T, THash, TEqual, TAllocator>& out)
{
    out.clear();
    std::size_t const count = generator() % 4;
    for (std::size_t i = 0; i < count; ++i)
    {
        T element{};
        RandomFill(generator, element);
        out.insert(std::move(element));
    }
}
//---------------------------------------------------------------------------
template <typename T, typename TAllocator>
void RandomFill(std::mt19937& generator, std::vector<T, TAllocator>& out)
{
    out.clear();
    std::size_t const count = generator() % 4;
    for (std::size_t i = 0; i < count; ++i)
    {
        T element{};
        RandomFill(generator, element);
        out.push_back(std::move(element));
    }
}
//---------------------------------------------------------------------------
/*
    RandomReadProblem

    What is wrong with how TASWTomlConverter<T>::FromValue() reads FuzzCount random values, each from a generator
    seeded with FuzzFirstSeed and its number: a value T writes, changed at random up to two times (see
    RandomChange()), or a random value of any type (see RandomTomlValue()). Each must read, and then, when the T read
    into (a random one) can be written, be written as a value that reads and is written again the same (see
    WrittenAgainProblem()), or fail with TypeMismatch,
    InvalidValue, ValueOutOfRange or NotFound, a message and a Path that names a place in the value (see
    FailurePathProblem()), leaving the T read into as it was; and some must read. Empty when nothing is wrong.
*/
template <TomlConvertible T>
std::string RandomReadProblem()
{
    std::size_t reads = 0;
    for (std::size_t i = 0; i < FuzzCount; ++i)
    {
        std::uint32_t const seed = FuzzFirstSeed + static_cast<std::uint32_t>(i);
        std::string const at = "seed " + std::to_string(seed) + ": ";
        std::mt19937 generator(seed);

        // The value read into, and a value to read
        T initial{};
        RandomFill(generator, initial);
        TASWTomlValue before;
        bool const writable = TASWTomlConverter<T>::ToValue(initial, before, TASWTomlValueOptions()).IsOk();
        T source{};
        RandomFill(generator, source);
        TASWTomlValue value;
        if (TASWTomlConverter<T>::ToValue(source, value, TASWTomlValueOptions()).IsOk())
        {
            std::uint32_t const changes = generator() % 3;
            for (std::uint32_t change = 0; change < changes; ++change)
                RandomChange(generator, value);
        }
        else
        {
            value = RandomTomlValue(generator, 0);
        }

        T out = initial;
        TASWTomlResult const read = TASWTomlConverter<T>::FromValue(value, out, TASWTomlValueOptions());
        std::string const text = ASWToml::Detail::ValueText(value);
        if (read.IsOk())
        {
            // A struct keeps what the value lacks as the T read into had it, so only one read into a T that can be
            // written can be written
            ++reads;
            if (!writable)
                continue;

            TASWTomlValue written;
            TASWTomlResult const result = TASWTomlConverter<T>::ToValue(out, written, TASWTomlValueOptions());
            if (!result.IsOk())
                return at + text + " read, but can't be written again: " + result.Message;

            std::string const problem = WrittenAgainProblem<T>(generator, written);
            if (!problem.empty())
                return at + text + ": " + problem;

            continue;
        }

        bool const expectedCode = read.Code == ETomlCode::TypeMismatch || read.Code == ETomlCode::InvalidValue ||
            read.Code == ETomlCode::ValueOutOfRange || read.Code == ETomlCode::NotFound;
        if (!expectedCode)
            return at + text + ": the code is " + ASWToml::ToString(read.Code);

        if (read.Message.find("The ") != 0)
            return at + text + ": the message is " + Escaped(read.Message);

        std::string const pathProblem = FailurePathProblem(value, read);
        if (!pathProblem.empty())
            return at + text + ": " + pathProblem;

        TASWTomlValue after;
        bool const unchanged = TASWTomlConverter<T>::ToValue(out, after, TASWTomlValueOptions()).IsOk() &&
            ASWToml::Detail::IdenticalValues(after, before);
        if (writable && !unchanged)
            return at + text + ": the value read into changed";
    }

    if (reads == 0)
        return "no random value read";

    return std::string();
}
//---------------------------------------------------------------------------
/*
    RandomValueProblem

    What is wrong with how FuzzCount random values of T (see RandomFill()), each from a generator seeded with
    FuzzFirstSeed and its number, are written by TASWTomlConverter<T>::ToValue(): each must be written as a value that
    reads and is written again the same (see WrittenAgainProblem()), and that goes through a document's text (see
    ThroughDocument()) and is written the same then, or fail with InvalidValue or ValueOutOfRange and a message,
    leaving the value written into as it was; and some must be written. Empty when nothing is wrong.
*/
template <TomlConvertible T>
std::string RandomValueProblem()
{
    std::size_t writes = 0;
    for (std::size_t i = 0; i < FuzzCount; ++i)
    {
        std::uint32_t const seed = FuzzFirstSeed + static_cast<std::uint32_t>(i);
        std::string const at = "seed " + std::to_string(seed) + ": ";
        std::mt19937 generator(seed);
        T in{};
        RandomFill(generator, in);

        TASWTomlValue const table = TTable{ { "x", 1 } };
        TASWTomlValue value = table;
        TASWTomlResult const result = TASWTomlConverter<T>::ToValue(in, value, TASWTomlValueOptions());
        if (!result.IsOk())
        {
            if (result.Code != ETomlCode::InvalidValue && result.Code != ETomlCode::ValueOutOfRange)
                return at + "the code of a failed write is " + ASWToml::ToString(result.Code);

            if (result.Message.find("The ") != 0)
                return at + "the message of a failed write is " + Escaped(result.Message);

            if (!(value == table))
                return at + "a failed write changed the value";

            continue;
        }

        ++writes;
        std::string const text = ASWToml::Detail::ValueText(value);
        std::string const problem = WrittenAgainProblem<T>(generator, value);
        if (!problem.empty())
            return at + text + ": " + problem;

        T through{};
        std::string document;
        TASWTomlResult const read = ThroughDocument(in, through, document);
        if (!read.IsOk())
            return at + text + " should go through a document: " + read.Message + " (" + Escaped(document) + ")";

        TASWTomlValue again;
        bool const same = TASWTomlConverter<T>::ToValue(through, again, TASWTomlValueOptions()).IsOk() &&
            ASWToml::Detail::IdenticalValues(again, value);
        if (!same)
            return at + text + " should read back from a document as itself, not " + Written(through);
    }

    if (writes == 0)
        return "no random value written";

    return std::string();
}
//---------------------------------------------------------------------------
/*
    WrittenAgainProblem

    What is wrong with how 'value', which TASWTomlConverter<T>::ToValue() wrote, is read and written again: it must
    read as a T (a random one first, see RandomFill(), since what a T writes doesn't depend on what the T read into
    held), and that T must be written as the same value (see IdenticalValues(): a NaN is a NaN, -0.0 isn't 0.0); empty
    when nothing is wrong
*/
template <TomlConvertible T>
std::string WrittenAgainProblem(std::mt19937& generator, TASWTomlValue const& value)
{
    T read{};
    RandomFill(generator, read);
    TASWTomlResult const result = TASWTomlConverter<T>::FromValue(value, read, TASWTomlValueOptions());
    if (!result.IsOk())
        return "it should read back: " + result.Message;

    TASWTomlValue again;
    TASWTomlResult const written = TASWTomlConverter<T>::ToValue(read, again, TASWTomlValueOptions());
    if (!written.IsOk())
        return "it should be written again: " + written.Message;

    if (!ASWToml::Detail::IdenticalValues(again, value))
        return "it should be written again the same, not as " + ASWToml::Detail::ValueText(again);

    return std::string();
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_ConverterFuzz
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_ConverterFuzz::TTest_ASWToml_ConverterFuzz()
    : inherited("ASWToml_ConverterFuzz_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_ConverterFuzz::~TTest_ASWToml_ConverterFuzz()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ConverterFuzz::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ConverterFuzz::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ConverterFuzz::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ConverterFuzz::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ConverterFuzz, RandomTomlValues_ReadAsArraysAndTables)
{
    // Arrange & Act & Assert
    CheckEmpty(RandomReadProblem<std::vector<int> >(), "std::vector<int>");
    CheckEmpty(RandomReadProblem<std::vector<std::string> >(), "std::vector<std::string>");
    CheckEmpty(RandomReadProblem<std::vector<std::vector<double> > >(), "std::vector<std::vector<double> >");
    CheckEmpty(RandomReadProblem<std::vector<bool> >(), "std::vector<bool>");
    CheckEmpty(RandomReadProblem<std::vector<TASWTomlValue> >(), "std::vector<TASWTomlValue>");
    CheckEmpty(RandomReadProblem<std::array<std::uint8_t, 2> >(), "std::array<std::uint8_t, 2>");
    CheckEmpty(RandomReadProblem<std::set<std::string> >(), "std::set<std::string>");
    CheckEmpty(RandomReadProblem<std::unordered_set<std::int64_t> >(), "std::unordered_set<std::int64_t>");
    CheckEmpty(RandomReadProblem<std::pair<std::string, std::uint16_t> >(), "std::pair<std::string, std::uint16_t>");
    CheckEmpty(RandomReadProblem<std::tuple<int, bool, std::string> >(), "std::tuple<int, bool, std::string>");
    CheckEmpty(RandomReadProblem<std::map<std::string, int> >(), "std::map<std::string, int>");
    CheckEmpty(RandomReadProblem<std::map<int, std::vector<bool> > >(), "std::map<int, std::vector<bool> >");
    CheckEmpty(RandomReadProblem<std::map<std::uint64_t, std::string> >(), "std::map<std::uint64_t, std::string>");
    CheckEmpty(RandomReadProblem<std::unordered_map<EColor, double> >(), "std::unordered_map<EColor, double>");
    CheckEmpty(RandomReadProblem<TTestServer>(), "TTestServer");
    CheckEmpty(RandomReadProblem<std::vector<TTestServer> >(), "std::vector<TTestServer>");
    CheckEmpty(RandomReadProblem<TTestCluster>(), "TTestCluster");
    CheckEmpty(RandomReadProblem<std::map<std::string, TTestServer> >(), "std::map<std::string, TTestServer>");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ConverterFuzz, RandomTomlValues_ReadAsScalars)
{
    // Arrange & Act & Assert
    CheckEmpty(RandomReadProblem<bool>(), "bool");
    CheckEmpty(RandomReadProblem<std::int8_t>(), "std::int8_t");
    CheckEmpty(RandomReadProblem<std::uint8_t>(), "std::uint8_t");
    CheckEmpty(RandomReadProblem<std::int16_t>(), "std::int16_t");
    CheckEmpty(RandomReadProblem<std::uint16_t>(), "std::uint16_t");
    CheckEmpty(RandomReadProblem<int>(), "int");
    CheckEmpty(RandomReadProblem<unsigned int>(), "unsigned int");
    CheckEmpty(RandomReadProblem<std::int64_t>(), "std::int64_t");
    CheckEmpty(RandomReadProblem<std::uint64_t>(), "std::uint64_t");
    CheckEmpty(RandomReadProblem<float>(), "float");
    CheckEmpty(RandomReadProblem<double>(), "double");
    CheckEmpty(RandomReadProblem<std::string>(), "std::string");
    CheckEmpty(RandomReadProblem<std::u8string>(), "std::u8string");
    CheckEmpty(RandomReadProblem<std::filesystem::path>(), "std::filesystem::path");
    CheckEmpty(RandomReadProblem<EColor>(), "EColor");
    CheckEmpty(RandomReadProblem<EFlags>(), "EFlags");
    CheckEmpty(RandomReadProblem<ESmall>(), "ESmall");
    CheckEmpty(RandomReadProblem<TWindow::EState>(), "TWindow::EState");
    CheckEmpty(RandomReadProblem<std::chrono::year_month_day>(), "std::chrono::year_month_day");
    CheckEmpty(RandomReadProblem<std::chrono::sys_days>(), "std::chrono::sys_days");
    CheckEmpty(RandomReadProblem<std::chrono::sys_seconds>(), "std::chrono::sys_seconds");
    CheckEmpty(RandomReadProblem<std::chrono::local_seconds>(), "std::chrono::local_seconds");
    CheckEmpty(RandomReadProblem<std::chrono::hh_mm_ss<std::chrono::milliseconds> >(),
        "std::chrono::hh_mm_ss<std::chrono::milliseconds>");
    CheckEmpty(RandomReadProblem<ASWToml::TASWTomlLocalTime>(), "TASWTomlLocalTime");
    CheckEmpty(RandomReadProblem<ASWToml::TASWTomlOffsetDateTime>(), "TASWTomlOffsetDateTime");
    CheckEmpty(RandomReadProblem<TASWTomlValue>(), "TASWTomlValue");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ConverterFuzz, RandomValues_ArraysAndTables_RoundTrip)
{
    // Arrange & Act & Assert
    CheckEmpty(RandomValueProblem<std::vector<int> >(), "std::vector<int>");
    CheckEmpty(RandomValueProblem<std::vector<std::string> >(), "std::vector<std::string>");
    CheckEmpty(RandomValueProblem<std::vector<std::vector<double> > >(), "std::vector<std::vector<double> >");
    CheckEmpty(RandomValueProblem<std::vector<bool> >(), "std::vector<bool>");
    CheckEmpty(RandomValueProblem<std::vector<TASWTomlValue> >(), "std::vector<TASWTomlValue>");
    CheckEmpty(RandomValueProblem<std::array<std::uint8_t, 2> >(), "std::array<std::uint8_t, 2>");
    CheckEmpty(RandomValueProblem<std::set<std::string> >(), "std::set<std::string>");
    CheckEmpty(RandomValueProblem<std::unordered_set<std::int64_t> >(), "std::unordered_set<std::int64_t>");
    CheckEmpty(RandomValueProblem<std::pair<std::string, std::uint16_t> >(), "std::pair<std::string, std::uint16_t>");
    CheckEmpty(RandomValueProblem<std::tuple<int, bool, std::string> >(), "std::tuple<int, bool, std::string>");
    CheckEmpty(RandomValueProblem<std::map<std::string, int> >(), "std::map<std::string, int>");
    CheckEmpty(RandomValueProblem<std::map<int, std::vector<bool> > >(), "std::map<int, std::vector<bool> >");
    CheckEmpty(RandomValueProblem<std::map<std::uint64_t, std::string> >(), "std::map<std::uint64_t, std::string>");
    CheckEmpty(RandomValueProblem<std::unordered_map<EColor, double> >(), "std::unordered_map<EColor, double>");
    CheckEmpty(RandomValueProblem<TTestServer>(), "TTestServer");
    CheckEmpty(RandomValueProblem<std::vector<TTestServer> >(), "std::vector<TTestServer>");
    CheckEmpty(RandomValueProblem<TTestCluster>(), "TTestCluster");
    CheckEmpty(RandomValueProblem<std::map<std::string, TTestServer> >(), "std::map<std::string, TTestServer>");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ConverterFuzz, RandomValues_Scalars_RoundTrip)
{
    // Arrange & Act & Assert
    CheckEmpty(RandomValueProblem<bool>(), "bool");
    CheckEmpty(RandomValueProblem<std::int8_t>(), "std::int8_t");
    CheckEmpty(RandomValueProblem<std::uint8_t>(), "std::uint8_t");
    CheckEmpty(RandomValueProblem<std::int16_t>(), "std::int16_t");
    CheckEmpty(RandomValueProblem<std::uint16_t>(), "std::uint16_t");
    CheckEmpty(RandomValueProblem<int>(), "int");
    CheckEmpty(RandomValueProblem<unsigned int>(), "unsigned int");
    CheckEmpty(RandomValueProblem<std::int64_t>(), "std::int64_t");
    CheckEmpty(RandomValueProblem<std::uint64_t>(), "std::uint64_t");
    CheckEmpty(RandomValueProblem<float>(), "float");
    CheckEmpty(RandomValueProblem<double>(), "double");
    CheckEmpty(RandomValueProblem<std::string>(), "std::string");
    CheckEmpty(RandomValueProblem<std::u8string>(), "std::u8string");
    CheckEmpty(RandomValueProblem<std::filesystem::path>(), "std::filesystem::path");
    CheckEmpty(RandomValueProblem<EColor>(), "EColor");
    CheckEmpty(RandomValueProblem<EFlags>(), "EFlags");
    CheckEmpty(RandomValueProblem<ESmall>(), "ESmall");
    CheckEmpty(RandomValueProblem<TWindow::EState>(), "TWindow::EState");
    CheckEmpty(RandomValueProblem<std::chrono::year_month_day>(), "std::chrono::year_month_day");
    CheckEmpty(RandomValueProblem<std::chrono::sys_days>(), "std::chrono::sys_days");
    CheckEmpty(RandomValueProblem<std::chrono::sys_seconds>(), "std::chrono::sys_seconds");
    CheckEmpty(RandomValueProblem<std::chrono::local_seconds>(), "std::chrono::local_seconds");
    CheckEmpty(RandomValueProblem<std::chrono::hh_mm_ss<std::chrono::milliseconds> >(),
        "std::chrono::hh_mm_ss<std::chrono::milliseconds>");
    CheckEmpty(RandomValueProblem<ASWToml::TASWTomlLocalTime>(), "TASWTomlLocalTime");
    CheckEmpty(RandomValueProblem<ASWToml::TASWTomlOffsetDateTime>(), "TASWTomlOffsetDateTime");
    CheckEmpty(RandomValueProblem<TASWTomlValue>(), "TASWTomlValue");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_ConverterFuzz, "Converters")
