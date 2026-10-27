/* **************************************************************************
Test_ASWToml_Edits.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "Test_ASWToml_Edits.h"
//---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWToml_Converters.h"
#include "ASWToml_Edits.h"
#include "ASWToml_Options.h"
#include "ASWToml_Path.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

/////////////////////////////////////////////////////////////////////////////
// TTestPoint
//
// A struct converted as a table
/////////////////////////////////////////////////////////////////////////////
struct TTestPoint
{
    int X = 0;
    int Y = 0;
    std::string Label;
};

auto ASWTomlFields(TTestPoint const&)
{
    return std::tuple{ ASWToml::Field("x", &TTestPoint::X), ASWToml::Field("y", &TTestPoint::Y),
        ASWToml::Field("label", &TTestPoint::Label) };
}

// An enum converted by name, with a value that has none (so it can't be converted)
enum class ETestMode
{
    Fast,
    Slow,
    Unnamed,
};

auto const& ASWTomlEnumNames(ETestMode)
{
    static constexpr ASWToml::TASWTomlEnumName<ETestMode> names[] = {
        { ETestMode::Fast, "fast" }, { ETestMode::Slow, "slow" },
    };

    return names;
}

// A vector of the enum's values
using TModeVector = std::vector<ETestMode>;

// A type whose converter writes the integer base of the value options it is given as an integer, so a test sees which
// options reached it
struct TTestSeenOptions
{
};

} // namespace

} // namespace ASWUnitTests

//---------------------------------------------------------------------------

namespace ASWToml
{

template <>
struct TASWTomlConverter<ASWUnitTests::TTestSeenOptions>
{
    static TASWTomlResult FromValue(TASWTomlValue const& /*value*/, ASWUnitTests::TTestSeenOptions& /*out*/,
        TASWTomlValueOptions const& /*options*/)
    {
        return TASWTomlResult();
    }

    static TASWTomlResult ToValue(ASWUnitTests::TTestSeenOptions const& /*in*/, TASWTomlValue& value,
        TASWTomlValueOptions const& options)
    {
        value = TASWTomlValue(static_cast<std::int64_t>(options.IntegerBase));

        return TASWTomlResult();
    }
};

} // namespace ASWToml

//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

using ASWToml::EEditKind;
using ASWToml::EIntegerBase;
using ASWToml::ETomlCode;
using ASWToml::TASWTomlConverter;
using ASWToml::TASWTomlEdit;
using ASWToml::TASWTomlEdits;
using ASWToml::TASWTomlResult;
using ASWToml::TASWTomlValue;
using ASWToml::TASWTomlValueOptions;

//---------------------------------------------------------------------------

template <typename T>
TASWTomlResult ConversionFailure(T const& value);

template <typename T>
TASWTomlValue Converted(T const& value);

std::string EditProblem(TASWTomlEdit const& edit, EEditKind kind, std::string const& path,
    TASWTomlValue const& value, std::string const& text);

//---------------------------------------------------------------------------
/*
    ConversionFailure

    The result of converting 'value' with its converter and the default value options
*/
template <typename T>
TASWTomlResult ConversionFailure(T const& value)
{
    TASWTomlValue converted;

    return TASWTomlConverter<T>::ToValue(value, converted, TASWTomlValueOptions());
}
//---------------------------------------------------------------------------
/*
    Converted

    'value' converted with its converter and the default value options
*/
template <typename T>
TASWTomlValue Converted(T const& value)
{
    TASWTomlValue converted;
    static_cast<void>(TASWTomlConverter<T>::ToValue(value, converted, TASWTomlValueOptions()));

    return converted;
}
//---------------------------------------------------------------------------
/*
    EditProblem

    What is wrong with 'edit', which should be of 'kind' at 'path' (as TASWTomlPath::ToString() writes it), with
    'value' and 'text'; empty when nothing is
*/
std::string EditProblem(TASWTomlEdit const& edit, EEditKind kind, std::string const& path,
    TASWTomlValue const& value, std::string const& text)
{
    if (edit.Kind != kind)
        return "of another kind";

    if (edit.Path.ToString() != path)
        return "at " + edit.Path.ToString() + ", not " + path;

    if (!ASWToml::Detail::IdenticalValues(edit.Value, value))
        return "with another value";

    if (edit.Text != text)
        return "with the text " + edit.Text + ", not " + text;

    return std::string();
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Edits
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_Edits::TTest_ASWToml_Edits()
    : inherited("ASWToml_Edits_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_Edits::~TTest_ASWToml_Edits()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Edits::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Edits::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Edits::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Edits::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Edits, Add_ConverterGetsTheValueOptions)
{
    // Arrange
    TASWTomlValueOptions octal;
    octal.IntegerBase = EIntegerBase::Octal;
    TASWTomlEdits edits;

    // Act
    edits.SetValue("given", TTestSeenOptions(), octal).SetValue("none", TTestSeenOptions());
    edits.Append("appended", TTestSeenOptions(), octal);

    // Assert
    TASWTomlEdits::TEditVector const& added = edits.GetEdits();
    CheckEquals(3U, added.size(), "each value should be added");
    if (added.size() != 3)
        return;

    CheckEmpty(EditProblem(added[0], EEditKind::SetValue, "given", static_cast<std::int64_t>(EIntegerBase::Octal), ""),
        "the converter should get the value options given");
    CheckEmpty(EditProblem(added[1], EEditKind::SetValue, "none", static_cast<std::int64_t>(EIntegerBase::Auto), ""),
        "or the default ones");
    CheckEmpty(EditProblem(added[2], EEditKind::Append, "appended", static_cast<std::int64_t>(EIntegerBase::Octal),
        ""), "an appended value's too");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Edits, Add_ConvertsTypedValues)
{
    // Arrange
    TASWTomlValueOptions hexadecimal;
    hexadecimal.IntegerBase = EIntegerBase::Hexadecimal;
    TTestPoint const point{ 1, 2, "p" };
    std::string const text = "string";
    TASWTomlValue const value = Converted(std::vector<int>{ 7, 8 });
    TASWTomlEdits edits;

    // Act
    edits.SetValue("flag", true);
    edits.SetValue("ratio", 1.5);
    edits.SetValue("point", point);
    edits.SetValue("mode", ETestMode::Slow);
    edits.SetValue("value", value);
    edits.SetValue("literal", "literal");
    edits.SetValue("string", text);
    edits.SetValue("view", std::string_view("view"));
    edits.SetValue("null", static_cast<char const*>(nullptr));
    edits.Append("ports", 255, hexadecimal);
    edits.Append("modes", TModeVector{ ETestMode::Fast });
    edits.Append("names", "name", hexadecimal);

    // Assert
    TASWTomlEdits::TEditVector const& added = edits.GetEdits();
    CheckTrue(edits.GetResult().IsOk(), "every value should convert");
    CheckEquals(12U, added.size(), "each should add an edit");
    if (added.size() != 12)
        return;

    CheckEmpty(EditProblem(added[0], EEditKind::SetValue, "flag", true, ""), "a bool should be a boolean");
    CheckEmpty(EditProblem(added[1], EEditKind::SetValue, "ratio", 1.5, ""), "a double a float");
    CheckEmpty(EditProblem(added[2], EEditKind::SetValue, "point", Converted(point), ""),
        "a struct a table, as its converter writes it");
    CheckEmpty(EditProblem(added[3], EEditKind::SetValue, "mode", "slow", ""), "an enum its name");
    CheckEmpty(EditProblem(added[4], EEditKind::SetValue, "value", value, ""), "a TASWTomlValue itself");
    CheckEmpty(EditProblem(added[5], EEditKind::SetValue, "literal", "literal", ""), "a string literal a string");
    CheckEmpty(EditProblem(added[6], EEditKind::SetValue, "string", "string", ""), "a std::string a string");
    CheckEmpty(EditProblem(added[7], EEditKind::SetValue, "view", "view", ""), "a std::string_view a string");
    CheckEmpty(EditProblem(added[8], EEditKind::SetValue, "null", "", ""), "a nullptr text an empty string");
    CheckEmpty(EditProblem(added[9], EEditKind::Append, "ports", 255, ""), "an appended integer an integer");
    CheckEmpty(EditProblem(added[10], EEditKind::Append, "modes", Converted(TModeVector{ ETestMode::Fast }), ""),
        "an appended vector an array");
    CheckEmpty(EditProblem(added[11], EEditKind::Append, "names", "name", ""), "an appended text a string");
    CheckFalse(added[0].Values.has_value(), "an edit given no value options should have none, for the document's");
    CheckTrue(added[9].Values.has_value() && added[9].Values->IntegerBase == EIntegerBase::Hexadecimal,
        "an edit given value options should keep them");
    CheckTrue(added[11].Values.has_value() && added[11].Values->IntegerBase == EIntegerBase::Hexadecimal,
        "a text given value options too");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Edits, Add_FailedConversionAddsNothing)
{
    // Arrange
    TASWTomlResult const element = ConversionFailure(TModeVector{ ETestMode::Fast, ETestMode::Unnamed });
    TASWTomlResult const large = ConversionFailure(std::numeric_limits<std::uint64_t>::max());
    TASWTomlEdits edits;
    TASWTomlEdits later;

    // Act
    edits.SetValue("first", 1);
    edits.SetValue("modes", TModeVector{ ETestMode::Fast, ETestMode::Unnamed });
    edits.Append("mode", ETestMode::Unnamed);
    edits.SetValue("text", "\xFF");
    edits.Remove("old");
    TASWTomlResult const kept = edits.GetResult();
    std::size_t const keptCount = edits.GetEdits().size();
    edits.Clear();
    later.SetValue("count", std::numeric_limits<std::uint64_t>::max());

    // Assert
    CheckEquals(ETomlCode::InvalidValue, kept.Code, "the first failure should be kept, with the converter's code");
    CheckEquals(element.Message, kept.Message, "and its message");
    CheckEquals("modes[1]", kept.Path, "and the edit's path in front of the converter's");
    CheckEquals(2U, keptCount, "the values that failed should add no edit, the others theirs");
    CheckTrue(edits.IsEmpty() && edits.GetResult().IsOk(), "Clear() should remove the edits and the failure");
    CheckEquals(ETomlCode::ValueOutOfRange, later.GetResult().Code, "an integer out of range should fail");
    CheckEquals(large.Message, later.GetResult().Message, "with the converter's message");
    CheckEquals("count", later.GetResult().Path, "at the edit's path");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Edits, Add_KeepsEachEditInOrder)
{
    // Arrange
    TASWTomlValueOptions hexadecimal;
    hexadecimal.IntegerBase = EIntegerBase::Hexadecimal;
    TASWTomlEdits edits;
    bool const emptyAtFirst = edits.IsEmpty();

    // Act
    TASWTomlEdits& chained = edits.SetValue("server.port", 8080).Append("ports", 1, hexadecimal).Remove("old");
    edits.Rename("server.name", "host").SetComment("server", "The server").SetTrailingComment("server.port", "TCP");

    // Assert
    TASWTomlEdits::TEditVector const& added = edits.GetEdits();
    CheckTrue(emptyAtFirst, "new edits should be empty");
    CheckTrue(&chained == &edits, "each method should return the edits, so calls can be chained");
    CheckFalse(edits.IsEmpty(), "edits that were added should not be");
    CheckTrue(edits.GetResult().IsOk(), "nothing should have failed");
    CheckEquals(6U, added.size(), "each call should add one edit");
    if (added.size() != 6)
        return;

    CheckEmpty(EditProblem(added[0], EEditKind::SetValue, "server.port", 8080, ""), "first the value set");
    CheckEmpty(EditProblem(added[1], EEditKind::Append, "ports", 1, ""), "then the value appended");
    CheckEmpty(EditProblem(added[2], EEditKind::Remove, "old", TASWTomlValue(), ""), "the removal");
    CheckEmpty(EditProblem(added[3], EEditKind::Rename, "server.name", TASWTomlValue(), "host"),
        "the renaming, with the new key");
    CheckEmpty(EditProblem(added[4], EEditKind::SetComment, "server", TASWTomlValue(), "The server"),
        "the comment");
    CheckEmpty(EditProblem(added[5], EEditKind::SetTrailingComment, "server.port", TASWTomlValue(), "TCP"),
        "and the trailing comment");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Edits, Add_TextsAreStrings)
{
    // Arrange
    TASWTomlValueOptions octal;
    octal.IntegerBase = EIntegerBase::Octal;
    char const* const pointer = "pointer";
    std::string_view const view = "view";
    TASWTomlEdits edits;

    // Act
    edits.Append("a", pointer).Append("b", pointer, octal).Append("c", view).Append("d", view, octal);
    edits.SetValue("e", pointer).SetValue("f", pointer, octal).SetValue("g", view).SetValue("h", view, octal);

    // Assert
    TASWTomlEdits::TEditVector const& added = edits.GetEdits();
    CheckEquals(8U, added.size(), "each text should be added");
    if (added.size() != 8)
        return;

    char const* const paths[] = { "a", "b", "c", "d", "e", "f", "g", "h" };
    for (std::size_t i = 0; i < 8; ++i)
    {
        std::string const msg = std::string(paths[i]) + ": ";
        EEditKind const kind = i < 4 ? EEditKind::Append : EEditKind::SetValue;
        TASWTomlValue const value = i % 4 < 2 ? "pointer" : "view";
        CheckEmpty(EditProblem(added[i], kind, paths[i], value, ""), msg + "the text should be a string");
        CheckEquals(i % 2 == 1, added[i].Values.has_value(), msg + "with the value options when they were given");
        if (added[i].Values.has_value())
            CheckEquals(EIntegerBase::Octal, added[i].Values->IntegerBase, msg + "which should be kept");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Edits, Copies_KeepEditsAndResult)
{
    // Arrange
    TASWTomlEdits edits;
    edits.SetValue("a", 1).Remove("b").Append("mode", ETestMode::Unnamed);

    // Act
    TASWTomlEdits const copy = edits;
    TASWTomlEdits moved = std::move(edits);

    // Assert
    CheckEquals(2U, copy.GetEdits().size(), "a copy should have the edits");
    CheckEquals("mode", copy.GetResult().Path, "and the failure");
    CheckEquals(2U, moved.GetEdits().size(), "and so should edits moved from them");
    CheckEquals("mode", moved.GetResult().Path, "with the failure");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWToml_Edits)
