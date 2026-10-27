/* **************************************************************************
Test_ASWToml_Json.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "Test_ASWToml_Json.h"
//---------------------------------------------------------------------------
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <system_error>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWToml_Document.h"
#include "ASWToml_Json.h"
#include "ASWToml_Options.h"
#include "ASWToml_Scalars.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
#include "TomlTest_Corpus.h"
//---------------------------------------------------------------------------
#include "UT_Helper_Converters.h"
#include "UT_Helper_Json.h"
#include "UT_Helper_TomlTest.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

using namespace std::string_literals;

using ASWToml::EJsonNonFinite;
using ASWToml::EJsonStyle;
using ASWToml::ETomlCode;
using ASWToml::TASWTomlDocument;
using ASWToml::TASWTomlJsonOptions;
using ASWToml::TASWTomlResult;
using ASWToml::TASWTomlValue;

using TArray = TASWTomlValue::TValueVector;
using TTable = TASWTomlValue::TKeyValueVector;

// The random values: the seed of the first, and how many there are
constexpr std::uint32_t FirstSeed = 20261027;
constexpr std::size_t RandomCount = 1000;

// The largest nesting depth a document's values have
constexpr std::size_t DepthLimit = ASWToml::TASWTomlReadingOptions::MaxNestingDepthLimit;

//---------------------------------------------------------------------------

TASWTomlValue EachType();
std::string Json(TASWTomlValue const& value, TASWTomlJsonOptions const& options);
TASWTomlValue NestedArrays(std::size_t depth);
std::string PlainProblem(TJsonValue const& json, TASWTomlValue const& value, std::string const& path);
std::string StructureProblem(TASWTomlValue const& value, unsigned indent);
std::string TaggedProblem(TASWTomlValue const& value, unsigned indent);

//---------------------------------------------------------------------------
/*
    EachType

    A table with a value of each TOML type: s, i, f, b, odt, ldt, ld, lt, a (an array) and t (a table)
*/
TASWTomlValue EachType()
{
    TASWTomlValue value;
    value.Set("s", "text");
    value.Set("i", 42);
    value.Set("f", 1.5);
    value.Set("b", true);
    value.Set("odt", ASWToml::TASWTomlOffsetDateTime{ Date(1979, 5, 27), TimeOfDay(7, 32, 0),
        std::chrono::minutes(-480) });
    value.Set("ldt", ASWToml::TASWTomlLocalDateTime{ Date(1979, 5, 27), TimeOfDay(7, 32, 0, 500000000) });
    value.Set("ld", Date(1979, 5, 27));
    value.Set("lt", TimeOfDay(7, 32, 0));
    value.Set("a", TArray{ 1, "two" });
    value.Set("t", TTable{ { "x", 1 } });

    return value;
}
//---------------------------------------------------------------------------
/*
    Json

    'value' as ToJson() writes it with 'options', or "failed: " and the message
*/
std::string Json(TASWTomlValue const& value, TASWTomlJsonOptions const& options)
{
    std::string json;
    TASWTomlResult const result = ASWToml::ToJson(value, json, options);
    if (!result.IsOk())
        return "failed: " + result.Message;

    return json;
}
//---------------------------------------------------------------------------
/*
    NestedArrays

    The integer 1 inside 'depth' arrays, e.g. [[1]] for 2
*/
TASWTomlValue NestedArrays(std::size_t depth)
{
    TASWTomlValue value = 1;
    for (std::size_t i = 0; i < depth; ++i)
        value = TASWTomlValue(TArray{ value });

    return value;
}
//---------------------------------------------------------------------------
/*
    PlainProblem

    What is wrong with 'json', read from the Plain JSON of 'value' (with NonFinite String), at 'path': a table must be
    an object of the same keys in the same order, an array an array of as many elements, each of them as its value
    is; a string a string of the same text, an integer a number of its decimal digits, a finite float a number that
    reads as the same double (a zero's sign too), inf and nan a string of the float as TOML writes it, a boolean a
    boolean, and a date or time a string the library's reader reads as it. Empty when nothing is wrong.
*/
std::string PlainProblem(TJsonValue const& json, TASWTomlValue const& value, std::string const& path)
{
    std::string const at = "At \"" + Escaped(path) + "\": ";

    if (TTable const* const table = value.AsTable())
    {
        if (json.Kind != EJsonKind::Object || json.Members.size() != table->size())
            return at + "a table of " + std::to_string(table->size()) + " keys isn't an object of as many members.";

        for (std::size_t i = 0; i < table->size(); ++i)
        {
            if (json.Members[i].Key != (*table)[i].Key)
                return at + "the key \"" + Escaped(json.Members[i].Key) + "\" should be \"" + (*table)[i].Key + "\".";

            std::string problem = PlainProblem(json.Members[i].Value, (*table)[i].Value, path + "." + (*table)[i].Key);
            if (!problem.empty())
                return problem;
        }

        return std::string();
    }

    if (TArray const* const array = value.AsArray())
    {
        if (json.Kind != EJsonKind::Array || json.Elements.size() != array->size())
            return at + "an array of " + std::to_string(array->size()) + " values isn't an array of as many.";

        for (std::size_t i = 0; i < array->size(); ++i)
        {
            std::string problem = PlainProblem(json.Elements[i], (*array)[i], path + "[" + std::to_string(i) + "]");
            if (!problem.empty())
                return problem;
        }

        return std::string();
    }

    if (std::string const* const text = value.AsString())
        return json.Kind == EJsonKind::String && json.Text == *text ? std::string() : at + "the string differs.";

    if (std::int64_t const* const integer = value.AsInteger())
    {
        bool const same = json.Kind == EJsonKind::Number && json.Text == std::to_string(*integer);

        return same ? std::string() : at + "the integer is written as " + json.Text + ".";
    }

    if (bool const* const flag = value.AsBoolean())
    {
        bool const same = json.Kind == EJsonKind::Boolean && json.Text == (*flag ? "true" : "false");

        return same ? std::string() : at + "the boolean is written as " + json.Text + ".";
    }

    if (double const* const number = value.AsFloat())
    {
        std::string toml;
        ASWToml::Detail::WriteFloat(*number, toml);
        if (!std::isfinite(*number))
            return json.Kind == EJsonKind::String && json.Text == toml ? std::string() : at + toml + " isn't a string.";

        double read = 0.0;
        char const* const last = json.Text.data() + json.Text.size();
        std::from_chars_result const result = std::from_chars(json.Text.data(), last, read);
        bool const same = json.Kind == EJsonKind::Number && result.ec == std::errc() && result.ptr == last &&
            std::bit_cast<std::uint64_t>(read) == std::bit_cast<std::uint64_t>(*number);

        return same ? std::string() : at + "the float " + toml + " is written as " + json.Text + ".";
    }

    std::size_t position = 0;
    TASWTomlValue read;
    bool const same = json.Kind == EJsonKind::String &&
        ASWToml::Detail::ReadDateTime(json.Text, position, read).IsOk() && position == json.Text.size() &&
        read == value;

    return same ? std::string() : at + "the date or time is written as " + json.Text + ".";
}
//---------------------------------------------------------------------------
/*
    StructureProblem

    What is wrong with the Plain JSON (NonFinite String) of 'value' with 'indent': it must be valid JSON, holding what
    'value' does (see PlainProblem()); empty when nothing is wrong
*/
std::string StructureProblem(TASWTomlValue const& value, unsigned indent)
{
    std::string json;
    TASWTomlResult const result = ASWToml::ToJson(value, json,
        { .Style = EJsonStyle::Plain, .NonFinite = EJsonNonFinite::String, .Indent = indent });
    if (!result.IsOk())
        return "It isn't written: " + result.Message;

    TJsonValue read;
    std::string const problem = ReadJson(json, read);
    if (!problem.empty())
        return "It isn't valid JSON: " + problem + " " + Escaped(json);

    return PlainProblem(read, value, std::string());
}
//---------------------------------------------------------------------------
/*
    TaggedProblem

    What is wrong with the Tagged JSON of 'value' with 'indent': it must be valid JSON (with NonFinite Fail, which the
    Tagged style doesn't use) that reads back as a value identical to 'value' (see Detail::IdenticalValues()); empty
    when nothing is wrong
*/
std::string TaggedProblem(TASWTomlValue const& value, unsigned indent)
{
    std::string json;
    TASWTomlResult const result = ASWToml::ToJson(value, json, { .Style = EJsonStyle::Tagged, .Indent = indent });
    if (!result.IsOk())
        return "It isn't written: " + result.Message;

    TJsonValue read;
    std::string problem = ReadJson(json, read);
    if (!problem.empty())
        return "It isn't valid JSON: " + problem + " " + Escaped(json);

    TASWTomlValue rebuilt;
    problem = TaggedToValue(read, rebuilt);
    if (!problem.empty())
        return "It isn't tagged JSON: " + problem + " " + Escaped(json);

    if (!ASWToml::Detail::IdenticalValues(rebuilt, value))
        return "It reads back as another value: " + Escaped(json);

    return std::string();
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Json
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_Json::TTest_ASWToml_Json()
    : inherited("ASWToml_Json_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_Json::~TTest_ASWToml_Json()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Json::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Json::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Json::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Json::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Json, Depth_ADocumentAtTheLimitIsWritten)
{
    // Arrange
    // A document with values as deep as it can hold: an array, an inline table and tables, each at the limit
    std::string const arrays = "a = " + std::string(DepthLimit, '[') + "1" + std::string(DepthLimit, ']') + "\n";
    std::string inlineTables = "b = ";
    for (std::size_t i = 0; i < DepthLimit; ++i)
        inlineTables += "{ c = ";

    inlineTables += "1" + std::string(DepthLimit, '}') + "\n";
    std::string header = "[d";
    for (std::size_t i = 1; i < DepthLimit; ++i)
        header += ".d";

    header += "]\ne = 1\n";
    ASWToml::TASWTomlOptions options;
    options.Reading.MaxNestingDepth = DepthLimit;
    TASWTomlDocument document;
    TASWTomlResult const parsed = document.Parse(arrays + inlineTables + header, options);

    // Act
    std::string const problem = StructureProblem(document.GetRoot(), 0);

    // Assert
    CheckTrue(parsed.IsOk(), "the document should parse: " + parsed.Message);
    CheckEmpty(problem, "every value of the document should be written");
    CheckEmpty(TaggedProblem(document.GetRoot(), 2), "and written Tagged");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Json, Depth_DeeperThanTheLimitFails)
{
    // Arrange
    // 1 inside the limit's arrays of the root table, and inside one more; and the same with an array as the value
    // written, which isn't counted, as the root table isn't
    TASWTomlValue atLimit;
    atLimit.Set("a", NestedArrays(DepthLimit));
    TASWTomlValue tooDeep;
    tooDeep.Set("a", NestedArrays(DepthLimit + 1));
    TASWTomlValue const arrayAtLimit = NestedArrays(DepthLimit + 1);
    TASWTomlValue const arrayTooDeep = NestedArrays(DepthLimit + 2);
    std::string json = "before";

    // Act
    TASWTomlResult const failed = ASWToml::ToJson(tooDeep, json, { .Indent = 0 });
    TASWTomlResult const arrayFailed = ASWToml::ToJson(arrayTooDeep, json, { .Indent = 0 });

    // Assert
    std::string const nested = std::string(DepthLimit, '[') + "1" + std::string(DepthLimit, ']');
    CheckEquals("{\"a\":" + nested + "}", Json(atLimit, { .Indent = 0 }), "1 inside the limit's arrays is written");
    CheckEquals("[" + nested + "]", Json(arrayAtLimit, { .Indent = 0 }), "and so is the array that holds them");

    std::string path = "a";
    for (std::size_t i = 0; i <= DepthLimit; ++i)
        path += "[0]";

    CheckEquals(ETomlCode::NestingTooDeep, failed.Code, "1 inside one more array should fail");
    CheckEquals(path, failed.Path, "at the 1");
    CheckEquals("The value can't be written as JSON: it is inside more than 128 tables and arrays, deeper than "
        "Reading.MaxNestingDepth lets a document's values be."s, failed.Message, "saying why");
    CheckEquals(ETomlCode::NestingTooDeep, arrayFailed.Code, "an array as the value written should fail one deeper");
    CheckEquals(path.substr(1) + "[0]", arrayFailed.Path, "at its 1");
    CheckEquals("before"s, json, "the text should be left as it was");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Json, Failures_HaveThePathAndChangeNothing)
{
    // Arrange
    TASWTomlValue servers;
    servers.Set("name", "\xFF");
    TASWTomlValue badString;
    badString.Set("servers", TArray{ TTable{ { "name", "a" } }, servers });
    TASWTomlValue badKey;
    badKey.Set("t", TTable{ { "k\xFF", 1 } });
    TASWTomlValue badDate;
    badDate.Set("d", Date(2023, 2, 29));
    TASWTomlValue const badTime = TArray{ TimeOfDay(24, 0, 0) };
    TASWTomlValue badOffset;
    badOffset.Set("o", ASWToml::TASWTomlOffsetDateTime{ Date(1979, 5, 27), TimeOfDay(7, 32, 0),
        std::chrono::hours(24) });
    TASWTomlValue badFloat;
    badFloat.Set("servers", TArray{ TTable{ { "ratio", 0.5 } }, TTable{ { "ratio", std::nan("") } } });
    std::string json = "before";

    // Act
    TASWTomlResult const stringFailure = ASWToml::ToJson(badString, json);
    TASWTomlResult const taggedFailure = ASWToml::ToJson(badString, json, { .Style = EJsonStyle::Tagged });
    TASWTomlResult const keyFailure = ASWToml::ToJson(badKey, json);
    TASWTomlResult const dateFailure = ASWToml::ToJson(badDate, json, { .Style = EJsonStyle::Tagged });
    TASWTomlResult const timeFailure = ASWToml::ToJson(badTime, json);
    TASWTomlResult const offsetFailure = ASWToml::ToJson(badOffset, json);
    TASWTomlResult const floatFailure = ASWToml::ToJson(badFloat, json);

    // Assert
    CheckEquals(ETomlCode::InvalidValue, stringFailure.Code, "a string that isn't valid UTF-8 should fail");
    CheckEquals("servers[1].name"s, stringFailure.Path, "at its path");
    CheckEquals("The string can't be written: it isn't valid UTF-8."s, stringFailure.Message, "saying why");
    CheckEquals("servers[1].name"s, taggedFailure.Path, "in the Tagged style too");
    CheckEquals(ETomlCode::InvalidValue, keyFailure.Code, "a key that isn't valid UTF-8 should fail");
    CheckEquals("t.\"k?\""s, keyFailure.Path, "at its path, the byte as '?'");
    CheckEquals("The key can't be written: it isn't valid UTF-8."s, keyFailure.Message, "saying why");
    CheckEquals(ETomlCode::InvalidValue, dateFailure.Code, "a date that isn't real should fail");
    CheckEquals("d"s, dateFailure.Path, "at its path");
    CheckEquals("The date can't be written: it must be a real date with a year from 0 to 9999."s, dateFailure.Message,
        "with the writer's message");
    CheckEquals(ETomlCode::InvalidValue, timeFailure.Code, "a time that isn't valid should fail");
    CheckEquals("[0]"s, timeFailure.Path, "at its path");
    CheckEquals(ETomlCode::InvalidValue, offsetFailure.Code, "an offset that isn't valid should fail");
    CheckEquals("o"s, offsetFailure.Path, "at its path");
    CheckEquals(ETomlCode::InvalidValue, floatFailure.Code, "nan should fail by default");
    CheckEquals("servers[1].ratio"s, floatFailure.Path, "at its path");
    CheckEquals("The float nan can't be written: JSON has no inf or nan (see TASWTomlJsonOptions::NonFinite)."s,
        floatFailure.Message, "saying why");
    CheckEquals("before"s, json, "each failure should leave the text as it was");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Json, Indent_LaysOutLikeJsonStringify)
{
    // Arrange
    TASWTomlValue value;
    value.Set("a", 1);
    value.Set("b", TArray{ 1, TArray(), TTable() });
    value.Set("c", TTable{ { "d", "x" } });
    value.Set("e", TTable());
    TASWTomlValue tagged;
    tagged.Set("a", 1);
    tagged.Set("b", TArray{ true });

    // Act
    std::string const two = Json(value, TASWTomlJsonOptions());
    std::string const four = Json(value, { .Indent = 4 });
    std::string const taggedTwo = Json(tagged, { .Style = EJsonStyle::Tagged });

    // Assert
    CheckEquals("{\n  \"a\": 1,\n  \"b\": [\n    1,\n    [],\n    {}\n  ],\n  \"c\": {\n    \"d\": \"x\"\n  },\n"
        "  \"e\": {}\n}"s, two, "the default should indent each level by 2 spaces");
    CheckEquals("{\n    \"a\": 1,\n    \"b\": [\n        1,\n        [],\n        {}\n    ],\n    \"c\": {\n"
        "        \"d\": \"x\"\n    },\n    \"e\": {}\n}"s, four, "Indent 4 by 4");
    CheckEquals("{\n  \"a\": {\"type\": \"integer\", \"value\": \"1\"},\n  \"b\": [\n"
        "    {\"type\": \"bool\", \"value\": \"true\"}\n  ]\n}"s, taggedTwo, "a tagged value should be on one line");
    CheckEquals("{}"s, Json(TASWTomlValue(), TASWTomlJsonOptions()), "an empty table should be {}");
    CheckEquals("[]"s, Json(TArray(), { .Indent = 4 }), "an empty array []");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Json, Indent_ZeroWritesOneLine)
{
    // Arrange
    TASWTomlValue value;
    value.Set("a", 1);
    value.Set("b", TArray{ 1, 2, TArray() });
    value.Set("c", TTable{ { "d", TTable() } });

    // Act
    std::string const plain = Json(value, { .Indent = 0 });
    std::string const tagged = Json(value, { .Style = EJsonStyle::Tagged, .Indent = 0 });

    // Assert
    CheckEquals("{\"a\":1,\"b\":[1,2,[]],\"c\":{\"d\":{}}}"s, plain, "Indent 0 should write one line without blanks");
    CheckEquals("{\"a\":{\"type\":\"integer\",\"value\":\"1\"},\"b\":[{\"type\":\"integer\",\"value\":\"1\"},"
        "{\"type\":\"integer\",\"value\":\"2\"},[]],\"c\":{\"d\":{}}}"s, tagged, "and so should the Tagged style");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Json, Options_InvalidOnesFail)
{
    // Arrange
    std::string json = "before";

    // Act
    TASWTomlResult const result = ASWToml::ToJson(TASWTomlValue(), json, { .Indent = 17 });

    // Assert
    CheckEquals(ETomlCode::InvalidOptions, result.Code, "options Validate() refuses should fail");
    CheckEquals("Indent is 17; it can be at most 16."s, result.Message, "with Validate()'s message");
    CheckEquals("before"s, json, "and leave the text as it was");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Json, Options_ValidateRefusesWhatCantBeUsed)
{
    // Arrange
    TASWTomlJsonOptions const defaults;
    TASWTomlJsonOptions const widest{ .Style = EJsonStyle::Tagged, .NonFinite = EJsonNonFinite::String,
        .Indent = TASWTomlJsonOptions::MaxIndent };
    TASWTomlJsonOptions const style{ .Style = static_cast<EJsonStyle>(2) };
    TASWTomlJsonOptions const nonFinite{ .NonFinite = static_cast<EJsonNonFinite>(-1) };
    TASWTomlJsonOptions const indent{ .Indent = TASWTomlJsonOptions::MaxIndent + 1 };

    // Act & Assert
    CheckTrue(defaults.Validate().IsOk(), "the defaults should be valid");
    CheckEquals(EJsonStyle::Plain, defaults.Style, "the default style is Plain");
    CheckEquals(EJsonNonFinite::Fail, defaults.NonFinite, "inf and nan fail by default");
    CheckEquals(2U, defaults.Indent, "the default indent is 2");
    CheckEquals(16U, TASWTomlJsonOptions::MaxIndent, "the largest indent is 16");
    CheckTrue(widest.Validate().IsOk(), "the largest indent should be valid");
    CheckEquals("Style isn't one of the EJsonStyle values."s, style.Validate().Message, "a style cast from 2 isn't");
    CheckEquals(ETomlCode::InvalidOptions, style.Validate().Code, "with InvalidOptions");
    CheckEquals("NonFinite isn't one of the EJsonNonFinite values."s, nonFinite.Validate().Message,
        "nor is a NonFinite cast from -1");
    CheckEquals(ETomlCode::InvalidOptions, nonFinite.Validate().Code, "with InvalidOptions");
    CheckEquals("Indent is 17; it can be at most 16."s, indent.Validate().Message, "nor an indent above the largest");
    CheckEquals(ETomlCode::InvalidOptions, indent.Validate().Code, "with InvalidOptions");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Json, Plain_CorpusFilesHaveTheirStructure)
{
    for (TTomlTestFile const& file : TomlTestValidFiles())
    {
        // Arrange
        std::string const name(file.Name);
        TASWTomlDocument document;
        TASWTomlResult const parsed = document.Parse(file.Text);

        // Act & Assert
        CheckTrue(parsed.IsOk(), name + " should parse: " + parsed.Message);
        CheckEmpty(StructureProblem(document.GetRoot(), 0), name + " should be written on one line as it holds");
        CheckEmpty(StructureProblem(document.GetRoot(), 2), name + " and indented");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Json, Plain_EachType)
{
    // Arrange
    TASWTomlValue const value = EachType();

    // Act
    std::string const json = Json(value, { .Indent = 0 });

    // Assert
    CheckEquals("{\"s\":\"text\",\"i\":42,\"f\":1.5,\"b\":true,\"odt\":\"1979-05-27T07:32:00-08:00\","
        "\"ldt\":\"1979-05-27T07:32:00.5\",\"ld\":\"1979-05-27\",\"lt\":\"07:32:00\",\"a\":[1,\"two\"],"
        "\"t\":{\"x\":1}}"s, json, "each type should be written as JSON's value, a date or time as a string");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Json, Plain_Escapes)
{
    // Arrange
    // Every control character, '"', '\', U+007F, '/' and UTF-8 of two, three and four bytes
    std::string text;
    for (int c = 0; c < 0x20; ++c)
        text += static_cast<char>(c);

    text += "\"\\\x7F/\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";
    TASWTomlValue value;
    value.Set(text, text);

    // Act
    std::string const json = Json(value, { .Indent = 0 });

    // Assert
    std::string const escaped = "\"\\u0000\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007\\b\\t\\n\\u000B\\f\\r"
        "\\u000E\\u000F\\u0010\\u0011\\u0012\\u0013\\u0014\\u0015\\u0016\\u0017\\u0018\\u0019\\u001A\\u001B\\u001C"
        "\\u001D\\u001E\\u001F\\\"\\\\\x7F/\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\"";
    CheckEquals("{" + escaped + ":" + escaped + "}", json, "keys and strings should be escaped as JSON needs");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Json, Plain_IntegersAndFloatsAreNumbers)
{
    // Arrange
    TASWTomlValue const numbers = TArray{ std::numeric_limits<std::int64_t>::max(),
        std::numeric_limits<std::int64_t>::min(), 9007199254740993, -0.0, 5e-324, 1e16, 0.1, 1.0,
        std::numeric_limits<double>::max() };
    TASWTomlDocument document;
    TASWTomlResult const parsed = document.Parse("hex = 0xFF\noctal = 0o17\nbinary = 0b101\nfloat = 1_000.5\n");

    // Act
    std::string const json = Json(numbers, { .Indent = 0 });
    std::string const bases = Json(document.GetRoot(), { .Indent = 0 });

    // Assert
    CheckEquals("[9223372036854775807,-9223372036854775808,9007199254740993,-0.0,5e-324,1e+16,0.1,1.0,"
        "1.7976931348623157e+308]"s, json, "integers in decimal (beyond 2^53 too), floats as TOML writes them");
    CheckEmpty(StructureProblem(numbers, 0), "each of them a JSON number that reads as the same value");
    CheckTrue(parsed.IsOk(), "the document should parse");
    CheckEquals("{\"hex\":255,\"octal\":15,\"binary\":5,\"float\":1000.5}"s, bases,
        "an integer in decimal, whatever base the file wrote");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Json, Plain_KeysInTheTablesOrder)
{
    // Arrange
    TASWTomlDocument document;
    TASWTomlResult const parsed = document.Parse("b = 1\na = 2\n[z]\ny = 3\nx = 4\n[c]\n");

    // Act
    std::string const json = Json(document.GetRoot(), { .Indent = 0 });

    // Assert
    CheckTrue(parsed.IsOk(), "the document should parse");
    CheckEquals("{\"b\":1,\"a\":2,\"z\":{\"y\":3,\"x\":4},\"c\":{}}"s, json, "the keys should be in the tables' order");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Json, Plain_NonFiniteFloats)
{
    // Arrange
    double const infinity = std::numeric_limits<double>::infinity();
    double const notANumber = std::numeric_limits<double>::quiet_NaN();
    TASWTomlValue const floats = TArray{ infinity, -infinity, notANumber, -notANumber, 1.5 };
    TASWTomlValue const minusInf = TArray{ 1.5, -infinity };
    std::string json = "before";

    // Act
    TASWTomlResult const failed = ASWToml::ToJson(minusInf, json, { .NonFinite = EJsonNonFinite::Fail });
    std::string const null = Json(floats, { .NonFinite = EJsonNonFinite::Null, .Indent = 0 });
    std::string const strings = Json(floats, { .NonFinite = EJsonNonFinite::String, .Indent = 0 });

    // Assert
    CheckEquals(ETomlCode::InvalidValue, failed.Code, "Fail should fail");
    CheckEquals("[1]"s, failed.Path, "at the float's path");
    CheckEquals("The float -inf can't be written: JSON has no inf or nan (see TASWTomlJsonOptions::NonFinite)."s,
        failed.Message, "saying which float");
    CheckEquals("before"s, json, "leaving the text as it was");
    CheckEquals("[null,null,null,null,1.5]"s, null, "Null should write null");
    CheckEquals("[\"inf\",\"-inf\",\"nan\",\"-nan\",1.5]"s, strings, "String should write TOML's text as a string");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Json, Plain_RandomValuesHaveTheirStructure)
{
    for (std::size_t i = 0; i < RandomCount; ++i)
    {
        // Arrange
        std::mt19937 generator(FirstSeed + static_cast<std::uint32_t>(i));
        TASWTomlValue const value = RandomTomlValue(generator, 0);
        unsigned const indent = static_cast<unsigned>(i % 3);

        // Act
        std::string const problem = StructureProblem(value, indent);

        // Assert
        CheckEmpty(problem, "random value " + std::to_string(i) + " should be written as it is");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Json, Scalar_RootIsAJsonScalar)
{
    // Arrange
    TASWTomlValue const number = 42;
    TASWTomlValue const string = "a \"b\"";
    TASWTomlValue const time = TimeOfDay(7, 32, 0, 1000);

    // Act & Assert
    CheckEquals("42"s, Json(number, TASWTomlJsonOptions()), "an integer should be a JSON number");
    CheckEquals("\"a \\\"b\\\"\""s, Json(string, TASWTomlJsonOptions()), "a string a JSON string");
    CheckEquals("\"07:32:00.000001\""s, Json(time, TASWTomlJsonOptions()), "a time a string");
    CheckEquals("{\"type\": \"integer\", \"value\": \"42\"}"s, Json(number, { .Style = EJsonStyle::Tagged }),
        "and a tagged integer a tagged value");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Json, Tagged_CorpusFilesHaveTheExpectedValues)
{
    for (TTomlTestFile const& file : TomlTestValidFiles())
    {
        // Arrange
        std::string const name(file.Name);
        TASWTomlDocument document;
        TASWTomlResult const parsed = document.Parse(file.Text);
        std::string json;
        TASWTomlResult const written = ASWToml::ToJson(document.GetRoot(), json, { .Style = EJsonStyle::Tagged });

        // Act
        TJsonValue read;
        std::string const problem = ReadJson(json, read);
        TASWTomlValue rebuilt;
        std::string const taggedProblem = TaggedToValue(read, rebuilt);

        // Assert
        CheckTrue(parsed.IsOk(), name + " should parse: " + parsed.Message);
        CheckTrue(written.IsOk(), name + " should be written: " + written.Message);
        CheckEmpty(problem, name + " should be written as valid JSON");
        CheckEmpty(taggedProblem, name + " as tagged JSON");
        CheckEmpty(ValuesDiffer(file, rebuilt), name + " that has toml-test's expected values");
        CheckEmpty(TaggedProblem(document.GetRoot(), 0), name + " that reads back as the document's values");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Json, Tagged_EachType)
{
    // Arrange
    TASWTomlValue const value = EachType();

    // Act
    std::string const json = Json(value, { .Style = EJsonStyle::Tagged, .Indent = 0 });

    // Assert
    CheckEquals("{\"s\":{\"type\":\"string\",\"value\":\"text\"},\"i\":{\"type\":\"integer\",\"value\":\"42\"},"
        "\"f\":{\"type\":\"float\",\"value\":\"1.5\"},\"b\":{\"type\":\"bool\",\"value\":\"true\"},"
        "\"odt\":{\"type\":\"datetime\",\"value\":\"1979-05-27T07:32:00-08:00\"},"
        "\"ldt\":{\"type\":\"datetime-local\",\"value\":\"1979-05-27T07:32:00.5\"},"
        "\"ld\":{\"type\":\"date-local\",\"value\":\"1979-05-27\"},"
        "\"lt\":{\"type\":\"time-local\",\"value\":\"07:32:00\"},"
        "\"a\":[{\"type\":\"integer\",\"value\":\"1\"},{\"type\":\"string\",\"value\":\"two\"}],"
        "\"t\":{\"x\":{\"type\":\"integer\",\"value\":\"1\"}}}"s, json,
        "each value but a table or an array should be its toml-test type and its value as a string");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Json, Tagged_NonFiniteFloatsAreWritten)
{
    // Arrange
    double const infinity = std::numeric_limits<double>::infinity();
    double const notANumber = std::numeric_limits<double>::quiet_NaN();
    TASWTomlValue const floats = TArray{ infinity, -infinity, notANumber, -notANumber, -0.0 };

    // Act
    std::string const json = Json(floats, { .Style = EJsonStyle::Tagged, .NonFinite = EJsonNonFinite::Fail,
        .Indent = 0 });

    // Assert
    CheckEquals("[{\"type\":\"float\",\"value\":\"inf\"},{\"type\":\"float\",\"value\":\"-inf\"},"
        "{\"type\":\"float\",\"value\":\"nan\"},{\"type\":\"float\",\"value\":\"-nan\"},"
        "{\"type\":\"float\",\"value\":\"-0.0\"}]"s, json, "the Tagged style should write every float, NonFinite Fail "
        "or not");
    CheckEmpty(TaggedProblem(floats, 2), "and they should read back with their signs");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Json, Tagged_RandomValuesReadBackIdentical)
{
    for (std::size_t i = 0; i < RandomCount; ++i)
    {
        // Arrange
        std::mt19937 generator(FirstSeed + static_cast<std::uint32_t>(i));
        TASWTomlValue const value = RandomTomlValue(generator, 0);
        unsigned const indent = static_cast<unsigned>(i % 3);

        // Act
        std::string const problem = TaggedProblem(value, indent);

        // Assert
        CheckEmpty(problem, "random value " + std::to_string(i) + " should read back as itself");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Json, ToString_NamesEachValue)
{
    // Act & Assert
    CheckEquals("Plain"s, std::string(ASWToml::ToString(EJsonStyle::Plain)), "Plain");
    CheckEquals("Tagged"s, std::string(ASWToml::ToString(EJsonStyle::Tagged)), "Tagged");
    CheckEquals("Unknown"s, std::string(ASWToml::ToString(static_cast<EJsonStyle>(2))), "a style cast from 2");
    CheckEquals("Fail"s, std::string(ASWToml::ToString(EJsonNonFinite::Fail)), "Fail");
    CheckEquals("Null"s, std::string(ASWToml::ToString(EJsonNonFinite::Null)), "Null");
    CheckEquals("String"s, std::string(ASWToml::ToString(EJsonNonFinite::String)), "String");
    CheckEquals("Unknown"s, std::string(ASWToml::ToString(static_cast<EJsonNonFinite>(3))), "a NonFinite cast from 3");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWToml_Json)
