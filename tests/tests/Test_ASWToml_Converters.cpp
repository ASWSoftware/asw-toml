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
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
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

std::string Escaped(std::string const& text);
TArray OneOfEachType();
template <TomlConvertible T>
std::string RefusalProblem(T const& initial, ETomlType accepted, std::string const& wanted);
template <TomlConvertible T>
TASWTomlResult ThroughDocument(T const& in, T& out, std::string& text);

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
    RefusalProblem

    What is wrong with how TASWTomlConverter<T>::FromValue() refuses each value of OneOfEachType() but the one of type
    'accepted': it must fail with TypeMismatch, a message that says the value's type isn't 'wanted' (e.g. "a string"),
    no Path, and 'out' (set to 'initial' first) unchanged; empty when nothing is wrong
*/
template <TomlConvertible T>
std::string RefusalProblem(T const& initial, ETomlType accepted, std::string const& wanted)
{
    for (TASWTomlValue const& value : OneOfEachType())
    {
        if (value.GetType() == accepted)
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
    CheckEmpty(RefusalProblem(true, ETomlType::Boolean, "a boolean"), "every other type should be refused");
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
    CheckEmpty(RefusalProblem(native, ETomlType::String, "a string"), "every other type should be refused");
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
    CheckEmpty(RefusalProblem("kept"s, ETomlType::String, "a string"), "every other type should be refused");
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
    CheckEmpty(RefusalProblem(accented, ETomlType::String, "a string"), "every other type should be refused");
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
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_TextConverters, "Converters")
