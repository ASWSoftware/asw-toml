/* **************************************************************************
Test_ASWToml_Interface.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "Test_ASWToml_Interface.h"
//---------------------------------------------------------------------------
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWToml_Converters.h"
#include "ASWToml_Document.h"
#include "ASWToml_Edits.h"
#include "ASWToml_Interface.h"
#include "ASWToml_Options.h"
#include "ASWToml_Path.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
#include "ASWToml_Version.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

/////////////////////////////////////////////////////////////////////////////
// TTestPoint
//
// A struct converted as a table: x must be there, y and label may be missing
/////////////////////////////////////////////////////////////////////////////
struct TTestPoint
{
    int X = 0;
    int Y = 0;
    std::string Label = "none";

    bool operator==(TTestPoint const& other) const = default;
};

auto ASWTomlFields(TTestPoint const&)
{
    return std::tuple{ ASWToml::Field("x", &TTestPoint::X), ASWToml::OptionalField("y", &TTestPoint::Y),
        ASWToml::OptionalField("label", &TTestPoint::Label) };
}

// An enum converted by name, with a value that has none (so it can't be written)
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

// A type whose converter reads the integer base of the value options it is given (whatever the value), and writes it
// as an integer, so a test sees which options reached it
struct TTestSeenOptions
{
    ASWToml::EIntegerBase Base = ASWToml::EIntegerBase::Auto;
};

// A type whose converter throws: a std::runtime_error ("converter failed") when Standard is set, else the int 7
struct TTestThrows
{
    bool Standard = true;
};

} // namespace

} // namespace ASWUnitTests

namespace ASWToml
{

template <>
struct TASWTomlConverter<ASWUnitTests::TTestSeenOptions>
{
    static TASWTomlResult FromValue(TASWTomlValue const& /*value*/, ASWUnitTests::TTestSeenOptions& out,
        TASWTomlValueOptions const& options)
    {
        out.Base = options.IntegerBase;

        return TASWTomlResult();
    }

    static TASWTomlResult ToValue(ASWUnitTests::TTestSeenOptions const& /*in*/, TASWTomlValue& value,
        TASWTomlValueOptions const& options)
    {
        value = TASWTomlValue(static_cast<std::int64_t>(options.IntegerBase));

        return TASWTomlResult();
    }
};

template <>
struct TASWTomlConverter<ASWUnitTests::TTestThrows>
{
    static TASWTomlResult FromValue(TASWTomlValue const& /*value*/, ASWUnitTests::TTestThrows& out,
        TASWTomlValueOptions const& /*options*/)
    {
        if (out.Standard)
            throw std::runtime_error("converter failed");

        throw 7;
    }

    static TASWTomlResult ToValue(ASWUnitTests::TTestThrows const& in, TASWTomlValue& /*value*/,
        TASWTomlValueOptions const& /*options*/)
    {
        if (in.Standard)
            throw std::runtime_error("converter failed");

        throw 7;
    }
};

} // namespace ASWToml

//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

using ASWToml::EIntegerBase;
using ASWToml::ESeverity;
using ASWToml::EStringStyle;
using ASWToml::ETomlCode;
using ASWToml::TASWTomlConverter;
using ASWToml::TASWTomlDocument;
using ASWToml::TASWTomlEdits;
using ASWToml::TASWTomlLogCallback;
using ASWToml::TASWTomlMessage;
using ASWToml::TASWTomlOptions;
using ASWToml::TASWTomlPath;
using ASWToml::TASWTomlResult;
using ASWToml::TASWTomlValue;
using ASWToml::TASWTomlValueOptions;
using ASWToml::TMessageVector;
using ASWToml::TStringVector;

using TTable = TASWTomlValue::TKeyValueVector;

// The text most tests read: a value of each kind, a table, an inline table and an array of tables, with values that
// fail to convert (server.port, points[1].y, limits.max as an integer)
constexpr char const* SettingsText =
    "name = \"app\"\n"
    "port = 80\n"
    "mode = \"slow\"\n"
    "ports = [80]\n"
    "limits = { max = \"high\", min = 1 }\n"
    "\n"
    "[server]\n"
    "host = \"example.com\"\n"
    "port = \"abc\"\n"
    "\n"
    "[[points]]\n"
    "x = 1\n"
    "y = 2\n"
    "\n"
    "[[points]]\n"
    "x = 3\n"
    "y = \"four\"\n";


/////////////////////////////////////////////////////////////////////////////
// TTestToml
//
// A fake IASWToml: a TASWTomlDocument without locks or files, which records what it reports (a failed parse in its
// constructor too) and how often its value options are asked for
/////////////////////////////////////////////////////////////////////////////
class TTestToml : public ASWToml::IASWToml
{
private:
    TASWTomlDocument m_Document;
    mutable TMessageVector m_Reported;
    mutable int m_ValueOptionsAsked = 0;

    // The NotFound result of the operations a fake has no use for
    static TASWTomlResult Unsupported();

protected:
    TASWTomlResult DocumentAppend(TASWTomlPath const& path, TASWTomlValue const& value,
        TASWTomlValueOptions const& values) override;
    bool DocumentFind(TASWTomlPath const& path, TASWTomlValue& value) const override;
    TASWTomlResult DocumentSetValue(TASWTomlPath const& path, TASWTomlValue const& value,
        TASWTomlValueOptions const& values) override;
    void Report(TASWTomlMessage const& message) const noexcept override;

public:
    // A document parsed from 'text' with 'options'
    explicit TTestToml(std::string_view text, TASWTomlOptions const& options = TASWTomlOptions());

    bool Contains(TASWTomlPath const& path) const override;
    std::string GetComment(TASWTomlPath const& path) const override;
    TASWTomlDocument GetDocument() const override;
    std::string GetFilePath() const override;
    TStringVector GetKeyNames(TASWTomlPath const& path) const override;
    TASWTomlOptions GetOptions() const override;

    // What it reported, in order
    TMessageVector const& GetReported() const noexcept;

    std::string GetTrailingComment(TASWTomlPath const& path) const override;
    ASWToml::TASWTomlValueOptions GetValueOptions() const override;

    // How many times GetValueOptions() was called
    int GetValueOptionsAsked() const noexcept;

    std::string_view GetVersionStr() const noexcept override;
    bool IsModified() const override;
    TASWTomlResult Load(std::string_view filePath) override;
    TASWTomlResult LoadText(std::string_view text) override;
    bool Remove(TASWTomlPath const& path) override;
    TASWTomlResult Rename(TASWTomlPath const& path, std::string_view newKey) override;

    // Reports 'result' as IASWToml reports a failed operation
    void ReportResult(TASWTomlResult const& result) const noexcept;

    TASWTomlResult Save() override;
    TASWTomlResult SaveAs(std::string_view filePath) override;
    TASWTomlResult SetComment(TASWTomlPath const& path, std::string_view text) override;
    void SetLogCallback(TASWTomlLogCallback callback) override;
    TASWTomlResult SetOptions(TASWTomlOptions const& options) override;
    TASWTomlResult SetTrailingComment(TASWTomlPath const& path, std::string_view text) override;
    std::string ToText() const override;
};


/////////////////////////////////////////////////////////////////////////////
// TThrowingRemoveToml
//
// A TTestToml whose Remove() throws a std::runtime_error ("remove failed")
/////////////////////////////////////////////////////////////////////////////
class TThrowingRemoveToml : public TTestToml
{
public:
    // A document parsed from 'text'
    explicit TThrowingRemoveToml(std::string_view text);

    bool Remove(TASWTomlPath const& path) override;
};

//---------------------------------------------------------------------------

std::string MessageProblem(TASWTomlMessage const& message, ESeverity severity, ETomlCode code, std::string const& text,
    std::string const& path);
std::string ResultProblem(TASWTomlResult const& result, ETomlCode code, std::string const& message,
    std::string const& path);

//---------------------------------------------------------------------------
/*
    MessageProblem

    What is wrong with 'message', a reported message, compared with the one expected (its severity, code, text and
    path, no file path, line, column or system error); empty when nothing is.
*/
std::string MessageProblem(TASWTomlMessage const& message, ESeverity severity, ETomlCode code, std::string const& text,
    std::string const& path)
{
    std::string problem;
    if (message.Severity != severity)
        problem += std::string(" severity ") + ASWToml::ToString(message.Severity);

    if (message.Code != code)
        problem += std::string(" code ") + ASWToml::ToString(message.Code);

    if (message.Text != text)
        problem += " text '" + message.Text + "'";

    if (message.Path != path)
        problem += " path '" + message.Path + "'";

    if (!message.FilePath.empty() || message.Line != 0 || message.Column != 0 || message.SystemError != 0)
        problem += " a file path, line, column or system error";

    return problem.empty() ? problem : "the message has" + problem + " (" + ASWToml::ToString(message) + ")";
}
//---------------------------------------------------------------------------
/*
    ResultProblem

    What is wrong with 'result' compared with the result expected (its code, message and path, no file path, line,
    column or system error); empty when nothing is.
*/
std::string ResultProblem(TASWTomlResult const& result, ETomlCode code, std::string const& message,
    std::string const& path)
{
    std::string problem;
    if (result.Code != code)
        problem += std::string(" code ") + ASWToml::ToString(result.Code);

    if (result.Message != message)
        problem += " message '" + result.Message + "'";

    if (result.Path != path)
        problem += " path '" + result.Path + "'";

    if (!result.FilePath.empty() || result.Line != 0 || result.Column != 0 || result.SystemError != 0)
        problem += " a file path, line, column or system error";

    return problem.empty() ? problem : "the result has" + problem;
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTestToml
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTestToml::TTestToml(std::string_view text, TASWTomlOptions const& options)
{
    TASWTomlResult const result = m_Document.Parse(text, options);
    if (!result.IsOk())
        ReportFailure(result);
}
//---------------------------------------------------------------------------
bool TTestToml::Contains(TASWTomlPath const& path) const
{
    return m_Document.Find(path) != nullptr;
}
//---------------------------------------------------------------------------
TASWTomlResult TTestToml::DocumentAppend(TASWTomlPath const& path, TASWTomlValue const& value,
    TASWTomlValueOptions const& values)
{
    TASWTomlResult const result = m_Document.Append(path, value, values);
    if (!result.IsOk())
        ReportFailure(result);

    return result;
}
//---------------------------------------------------------------------------
bool TTestToml::DocumentFind(TASWTomlPath const& path, TASWTomlValue& value) const
{
    TASWTomlValue const* const found = m_Document.Find(path);
    if (found == nullptr)
        return false;

    value = *found;

    return true;
}
//---------------------------------------------------------------------------
TASWTomlResult TTestToml::DocumentSetValue(TASWTomlPath const& path, TASWTomlValue const& value,
    TASWTomlValueOptions const& values)
{
    TASWTomlResult const result = m_Document.SetValue(path, value, values);
    if (!result.IsOk())
        ReportFailure(result);

    return result;
}
//---------------------------------------------------------------------------
std::string TTestToml::GetComment(TASWTomlPath const& path) const
{
    return m_Document.GetComment(path);
}
//---------------------------------------------------------------------------
TASWTomlDocument TTestToml::GetDocument() const
{
    return m_Document;
}
//---------------------------------------------------------------------------
std::string TTestToml::GetFilePath() const
{
    return std::string();
}
//---------------------------------------------------------------------------
TStringVector TTestToml::GetKeyNames(TASWTomlPath const& path) const
{
    TStringVector names;
    TASWTomlValue const* const found = m_Document.Find(path);
    if (found != nullptr && found->IsTable())
    {
        for (ASWToml::TASWTomlKeyValue const& keyValue : *found->AsTable())
            names.push_back(keyValue.Key);
    }

    return names;
}
//---------------------------------------------------------------------------
TASWTomlOptions TTestToml::GetOptions() const
{
    return m_Document.GetOptions();
}
//---------------------------------------------------------------------------
TMessageVector const& TTestToml::GetReported() const noexcept
{
    return m_Reported;
}
//---------------------------------------------------------------------------
std::string TTestToml::GetTrailingComment(TASWTomlPath const& path) const
{
    return m_Document.GetTrailingComment(path);
}
//---------------------------------------------------------------------------
TASWTomlValueOptions TTestToml::GetValueOptions() const
{
    ++m_ValueOptionsAsked;

    return m_Document.GetOptions().Values;
}
//---------------------------------------------------------------------------
int TTestToml::GetValueOptionsAsked() const noexcept
{
    return m_ValueOptionsAsked;
}
//---------------------------------------------------------------------------
std::string_view TTestToml::GetVersionStr() const noexcept
{
    return ASWToml::Version;
}
//---------------------------------------------------------------------------
bool TTestToml::IsModified() const
{
    return m_Document.IsModified();
}
//---------------------------------------------------------------------------
TASWTomlResult TTestToml::Load(std::string_view /*filePath*/)
{
    return Unsupported();
}
//---------------------------------------------------------------------------
TASWTomlResult TTestToml::LoadText(std::string_view text)
{
    return m_Document.Parse(text, m_Document.GetOptions());
}
//---------------------------------------------------------------------------
bool TTestToml::Remove(TASWTomlPath const& path)
{
    return m_Document.Remove(path);
}
//---------------------------------------------------------------------------
TASWTomlResult TTestToml::Rename(TASWTomlPath const& path, std::string_view newKey)
{
    return m_Document.Rename(path, newKey);
}
//---------------------------------------------------------------------------
void TTestToml::Report(TASWTomlMessage const& message) const noexcept
{
    try
    {
        m_Reported.push_back(message);
    }
    catch (...)
    {
        // A fake drops what it can't record
    }
}
//---------------------------------------------------------------------------
void TTestToml::ReportResult(TASWTomlResult const& result) const noexcept
{
    ReportFailure(result);
}
//---------------------------------------------------------------------------
TASWTomlResult TTestToml::Save()
{
    return Unsupported();
}
//---------------------------------------------------------------------------
TASWTomlResult TTestToml::SaveAs(std::string_view /*filePath*/)
{
    return Unsupported();
}
//---------------------------------------------------------------------------
TASWTomlResult TTestToml::SetComment(TASWTomlPath const& path, std::string_view text)
{
    return m_Document.SetComment(path, text);
}
//---------------------------------------------------------------------------
void TTestToml::SetLogCallback(TASWTomlLogCallback /*callback*/)
{
}
//---------------------------------------------------------------------------
TASWTomlResult TTestToml::SetOptions(TASWTomlOptions const& options)
{
    return m_Document.SetOptions(options);
}
//---------------------------------------------------------------------------
TASWTomlResult TTestToml::SetTrailingComment(TASWTomlPath const& path, std::string_view text)
{
    return m_Document.SetTrailingComment(path, text);
}
//---------------------------------------------------------------------------
std::string TTestToml::ToText() const
{
    return m_Document.ToText();
}
//---------------------------------------------------------------------------
TASWTomlResult TTestToml::Unsupported()
{
    TASWTomlResult result;
    result.Code = ETomlCode::NotFound;
    result.Message = "A fake has no files.";

    return result;
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TThrowingRemoveToml
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TThrowingRemoveToml::TThrowingRemoveToml(std::string_view text)
    : TTestToml(text)
{
}
//---------------------------------------------------------------------------
bool TThrowingRemoveToml::Remove(TASWTomlPath const& /*path*/)
{
    throw std::runtime_error("remove failed");
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Interface
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_Interface::TTest_ASWToml_Interface()
    : inherited("ASWToml_Interface_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_Interface::~TTest_ASWToml_Interface()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Interface::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Interface::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Interface::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Interface::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, Append_ConversionFailureIsReported)
{
    // Arrange
    TTestToml toml(SettingsText);
    std::string const before = toml.ToText();
    std::string const unnamedText = "The value 2 has no name in the enum's ASWTomlEnumNames(), so it can't be written.";
    TASWTomlValue written;
    std::string const invalidUtf8Text = TASWTomlConverter<std::string>::ToValue("\xFF", written,
        TASWTomlValueOptions()).Message;
    std::string const invalidPathText = TASWTomlPath("a..b").GetError();

    // Act
    TASWTomlResult const unnamed = toml.Append("modes", ETestMode::Unnamed);
    TASWTomlResult const invalidUtf8 = toml.Append("ports", "\xFF");
    TASWTomlResult const invalidPath = toml.Append("a..b", ETestMode::Unnamed);

    // Assert
    CheckEmpty(ResultProblem(unnamed, ETomlCode::InvalidValue, unnamedText, "modes"),
        "a value the converter can't write should fail with its code and message, at the path");
    CheckEmpty(ResultProblem(invalidUtf8, ETomlCode::InvalidValue, invalidUtf8Text, "ports"),
        "a text should be checked as any string is");
    CheckEmpty(ResultProblem(invalidPath, ETomlCode::InvalidArgument, invalidPathText, ""),
        "a path that isn't valid should fail with its error, before the value is converted");
    CheckEquals(before, toml.ToText(), "nothing should change");
    CheckEquals(3U, toml.GetReported().size(), "each failure should be reported once");
    if (toml.GetReported().size() == 3)
    {
        CheckEmpty(MessageProblem(toml.GetReported()[0], ESeverity::Error, ETomlCode::InvalidValue, unnamedText,
            "modes"), "as an Error with the path");
        CheckEmpty(MessageProblem(toml.GetReported()[1], ESeverity::Error, ETomlCode::InvalidValue, invalidUtf8Text,
            "ports"), "the text's too");
        CheckEmpty(MessageProblem(toml.GetReported()[2], ESeverity::Error, ETomlCode::InvalidArgument,
            invalidPathText, ""), "and the path's");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, Append_ConverterExceptionIsReported)
{
    // Arrange
    TTestToml toml(SettingsText);
    std::string const before = toml.ToText();
    TTestThrows other;
    other.Standard = false;

    // Act
    TASWTomlResult const standard = toml.Append("ports", TTestThrows());
    TASWTomlResult const unknown = toml.Append("ports", other);

    // Assert
    CheckEmpty(ResultProblem(standard, ETomlCode::UnexpectedException,
        "An unexpected exception was thrown: converter failed", "ports"),
        "a std::exception should be UnexpectedException with its what(), at the path");
    CheckEmpty(ResultProblem(unknown, ETomlCode::UnexpectedException,
        "An exception that isn't a std::exception was thrown.", "ports"), "any other exception too");
    CheckEquals(before, toml.ToText(), "nothing should change");
    CheckEquals(2U, toml.GetReported().size(), "each should be reported");
    if (toml.GetReported().size() == 2)
    {
        CheckEmpty(MessageProblem(toml.GetReported()[1], ESeverity::Error, ETomlCode::UnexpectedException,
            "An exception that isn't a std::exception was thrown.", "ports"), "as an Error with the path");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, Append_ConvertsTheValue)
{
    // Arrange
    TTestToml toml("ports = [80]\n");
    TTestPoint point;
    point.X = 5;
    point.Y = 6;

    // Act
    TASWTomlResult const port = toml.Append("ports", 443);
    TASWTomlResult const element = toml.Append("points", point);
    TASWTomlResult const literal = toml.Append("names", "a");
    TASWTomlResult const view = toml.Append("names", std::string_view("b"));
    TASWTomlResult const null = toml.Append("names", static_cast<char const*>(nullptr));

    // Assert
    CheckTrue(port.IsOk() && element.IsOk() && literal.IsOk() && view.IsOk() && null.IsOk(), "each should succeed");
    CheckEquals(std::string("ports = [80, 443]\n"
        "names = [\"a\", \"b\", \"\"]\n"
        "\n"
        "[[points]]\n"
        "x = 5\n"
        "y = 6\n"
        "label = \"none\"\n"), toml.ToText(),
        "a value should be appended to an array, a struct as an element of an array of tables, and a text as a string");
    CheckEquals(0U, toml.GetReported().size(), "nothing should be reported");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, Append_UsesTheValueOptions)
{
    // Arrange
    TASWTomlOptions hexadecimal;
    hexadecimal.Values.IntegerBase = EIntegerBase::Hexadecimal;
    TTestToml toml("masks = [1]\n", hexadecimal);
    TASWTomlValueOptions octal;
    octal.IntegerBase = EIntegerBase::Octal;
    TASWTomlValueOptions basic;
    basic.StringStyle = EStringStyle::Basic;

    // Act
    TASWTomlResult const ownOptions = toml.Append("masks", 255);
    TASWTomlResult const givenOptions = toml.Append("masks", 8, octal);
    TASWTomlResult const seenOwn = toml.Append("seen", TTestSeenOptions());
    TASWTomlResult const seenGiven = toml.Append("seen", TTestSeenOptions(), octal);
    TASWTomlResult const textOwn = toml.Append("paths", "C:\\x");
    TASWTomlResult const textGiven = toml.Append("paths", "C:\\y", basic);
    TASWTomlResult const viewGiven = toml.Append("paths", std::string_view("C:\\z"), basic);

    // Assert
    CheckTrue(ownOptions.IsOk() && givenOptions.IsOk() && seenOwn.IsOk() && seenGiven.IsOk() && textOwn.IsOk() &&
        textGiven.IsOk() && viewGiven.IsOk(), "each should succeed");
    CheckEquals(std::string("masks = [1, 0xFF, 0o10]\n"
        "seen = [0x2, 0o3]\n"
        "paths = ['C:\\x', \"C:\\\\y\", \"C:\\\\z\"]\n"), toml.ToText(),
        "the converter and the document should get the object's value options, or the ones given");
    CheckEquals(3, toml.GetValueOptionsAsked(), "the object's should be asked for once for each Append() without");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, Apply_ConversionFailureChangesNothing)
{
    // Arrange
    TTestToml toml(SettingsText);
    std::string const before = toml.ToText();
    TASWTomlEdits edits;
    edits.SetValue("port", 8080).SetValue("mode", ETestMode::Unnamed).SetValue("name", "other");

    // Act
    TASWTomlResult const result = toml.Apply(edits);

    // Assert
    CheckEmpty(ResultProblem(result, edits.GetResult().Code, edits.GetResult().Message, "mode"),
        "the edits' conversion failure should be returned");
    CheckEquals(before, toml.ToText(), "no edit should be made, not even the one before it");
    CheckEquals(1U, toml.GetReported().size(), "it should be reported once");
    if (toml.GetReported().size() == 1)
    {
        CheckEmpty(MessageProblem(toml.GetReported()[0], ESeverity::Error, result.Code, result.Message, "mode"),
            "as an Error with the path");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, Apply_ExceptionIsReported)
{
    // Arrange
    TThrowingRemoveToml toml(SettingsText);
    TASWTomlEdits edits;
    edits.SetValue("port", 8080).Remove("name").SetValue("mode", "fast");

    // Act
    TASWTomlResult const result = toml.Apply(edits);

    // Assert
    CheckEmpty(ResultProblem(result, ETomlCode::UnexpectedException,
        "An unexpected exception was thrown: remove failed", "name"),
        "the exception should be UnexpectedException with the path of the edit it came from");
    CheckEquals(8080, toml.GetValue("port", 0), "the edit before it should be kept");
    CheckEquals(std::string("app"), toml.GetValue("name", ""), "the removal should change nothing");
    CheckEquals(std::string("slow"), toml.GetValue("mode", ""), "and the edit after it shouldn't be made");
    CheckEquals(1U, toml.GetReported().size(), "it should be reported once");
    if (toml.GetReported().size() == 1)
    {
        CheckEmpty(MessageProblem(toml.GetReported()[0], ESeverity::Error, result.Code, result.Message, "name"),
            "as an Error with the path");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, Apply_MakesTheEditsByTheOperations)
{
    // Arrange
    // A fake written before Apply() existed (it doesn't override it) gets the default
    TTestToml toml(SettingsText);
    TASWTomlDocument document;
    TASWTomlResult const parsed = document.Parse(SettingsText);
    TASWTomlEdits edits;
    edits.SetValue("port", 8080).Append("ports", 81).Rename("server.host", "address");
    edits.SetComment("server", "The server").SetTrailingComment("name", "The app").Remove("points[0]");
    edits.Remove("missing").SetValue("limits.min", 2);

    // Act
    TASWTomlResult const result = toml.Apply(edits);
    TASWTomlResult const expected = document.Apply(edits);

    // Assert
    CheckTrue(parsed.IsOk() && expected.IsOk(), "the document should make the edits");
    CheckTrue(result.IsOk(), "the edits should succeed: " + result.Message);
    CheckEquals(document.ToText(), toml.ToText(), "the text should be what the document's Apply() makes");
    CheckTrue(toml.IsModified(), "the document should be modified");
    CheckEquals(0U, toml.GetReported().size(), "nothing should be reported");
    CheckEquals(3, toml.GetValueOptionsAsked(), "the value options should be asked for once for each value edit");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, Apply_RemovesAsABatchDoes)
{
    // Arrange
    TTestToml toml(SettingsText);
    TASWTomlDocument document;
    TASWTomlResult const parsed = document.Parse(SettingsText);
    std::string const before = toml.ToText();
    TASWTomlEdits invalid;
    invalid.Remove("a..b");
    TASWTomlEdits root;
    root.Remove("");
    TASWTomlEdits missing;
    missing.Remove("missing.key").Remove("ports[3]");

    // Act
    TASWTomlResult const invalidResult = toml.Apply(invalid);
    TASWTomlResult const rootResult = toml.Apply(root);
    TASWTomlResult const missingResult = toml.Apply(missing);
    TASWTomlResult const invalidExpected = document.Apply(invalid);
    TASWTomlResult const rootExpected = document.Apply(root);

    // Assert
    CheckTrue(parsed.IsOk(), "the document should be parsed");
    CheckEmpty(ResultProblem(invalidResult, ETomlCode::InvalidArgument, TASWTomlPath("a..b").GetError(),
        invalidExpected.Path), "a path that isn't valid should fail with its error");
    CheckEmpty(ResultProblem(invalidResult, invalidExpected.Code, invalidExpected.Message, invalidExpected.Path),
        "as the document's Apply() fails");
    CheckEmpty(ResultProblem(rootResult, ETomlCode::InvalidArgument, "Can't remove the root table.", ""),
        "the root should fail");
    CheckEmpty(ResultProblem(rootResult, rootExpected.Code, rootExpected.Message, rootExpected.Path),
        "as the document's Apply() fails");
    CheckTrue(missingResult.IsOk(), "a removal of nothing should succeed");
    CheckEquals(before, toml.ToText(), "nothing should change");
    CheckFalse(toml.IsModified(), "the document shouldn't be modified");
    CheckEquals(2U, toml.GetReported().size(), "each failure should be reported once");
    if (toml.GetReported().size() == 2)
    {
        CheckEmpty(MessageProblem(toml.GetReported()[1], ESeverity::Error, rootResult.Code, rootResult.Message, ""),
            "as an Error");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, Apply_StopsAtTheFirstFailure)
{
    // Arrange
    TTestToml toml(SettingsText);
    TASWTomlDocument document;
    TASWTomlResult const parsed = document.Parse(SettingsText);
    TASWTomlResult const first = document.SetValue("port", 8080);
    TASWTomlResult const expected = document.SetValue("port.x", 1);
    TASWTomlEdits edits;
    edits.SetValue("port", 8080).SetValue("port.x", 1).SetValue("name", "other");

    // Act
    TASWTomlResult const result = toml.Apply(edits);

    // Assert
    CheckTrue(parsed.IsOk() && first.IsOk() && !expected.IsOk(), "the document should fail at the second edit");
    CheckEmpty(ResultProblem(result, expected.Code, expected.Message, "port.x"),
        "the failing edit's result should be returned");
    CheckEquals(document.ToText(), toml.ToText(), "the edit before it should be kept, the one after it not made");
    CheckEquals(1U, toml.GetReported().size(), "the failure should be reported once, by the operation that failed");
    if (toml.GetReported().size() == 1)
    {
        CheckEmpty(MessageProblem(toml.GetReported()[0], ESeverity::Error, result.Code, result.Message, "port.x"),
            "as the result says");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, Apply_UsesTheValueOptions)
{
    // Arrange
    TASWTomlOptions hexadecimal;
    hexadecimal.Values.IntegerBase = EIntegerBase::Hexadecimal;
    TTestToml toml("port = 1\nmasks = [1]\n", hexadecimal);
    TASWTomlValueOptions octal;
    octal.IntegerBase = EIntegerBase::Octal;
    TASWTomlEdits edits;
    edits.SetValue("port", 255).Append("masks", 8, octal).SetValue("mask", 9, octal).Append("masks", 16);

    // Act
    TASWTomlResult const result = toml.Apply(edits);

    // Assert
    CheckTrue(result.IsOk(), "the edits should succeed: " + result.Message);
    CheckEquals(std::string("port = 0xFF\n"
        "masks = [1, 0o10, 0x10]\n"
        "mask = 0o11\n"), toml.ToText(), "each value should be written with its edit's value options, or the object's");
    CheckEquals(2, toml.GetValueOptionsAsked(), "the object's should be asked for once for each edit without");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, GetValue_ConverterExceptionIsThrown)
{
    // Arrange
    TTestToml toml(SettingsText);
    TTestThrows other;
    other.Standard = false;
    std::string standard;
    int unknown = 0;

    // Act
    try
    {
        static_cast<void>(toml.GetValue("port", TTestThrows()));
    }
    catch (std::runtime_error const& e)
    {
        standard = e.what();
    }

    try
    {
        static_cast<void>(toml.GetValue("port", other));
    }
    catch (int number)
    {
        unknown = number;
    }

    // Assert
    CheckEquals(std::string("converter failed"), standard, "a converter's exception should go through GetValue()");
    CheckEquals(7, unknown, "any exception");
    CheckEquals(0U, toml.GetReported().size(), "and not be reported");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, GetValue_ConvertsTheValue)
{
    // Arrange
    TTestToml toml(SettingsText);
    TASWTomlValue const limits = TTable{ { "max", "high" }, { "min", 1 } };
    TTestPoint first;
    first.X = 1;
    first.Y = 2;

    // Act & Assert
    CheckEquals(80, toml.GetValue("port", 0), "an int");
    CheckEquals(80, static_cast<int>(toml.GetValue("port", std::uint16_t{ 0 })), "an integer of another type");
    CheckEquals(std::string("example.com"), toml.GetValue("server.host", std::string()), "a string in a table");
    CheckTrue(toml.GetValue("mode", ETestMode::Fast) == ETestMode::Slow, "an enum by name");
    CheckTrue(toml.GetValue("ports", std::vector<int>()) == std::vector<int>{ 80 }, "an array");
    CheckEquals(2, toml.GetValue({ "points", 0, "y" }, 0), "at a path made of parts");
    CheckTrue(toml.GetValue("points[0]", TTestPoint()) == first, "a struct, an element of an array of tables");
    CheckTrue(toml.GetValue("limits", TASWTomlValue()) == limits, "a TASWTomlValue as it is");
    CheckEquals(0U, toml.GetReported().size(), "nothing should be reported");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, GetValue_InvalidPathGivesTheDefaultAndAnError)
{
    // Arrange
    TTestToml toml(SettingsText);

    // Act
    int const number = toml.GetValue("a..b", 5);
    std::string const text = toml.GetValue("port[", "none");

    // Assert
    CheckEquals(5, number, "a path that isn't valid should give the default");
    CheckEquals(std::string("none"), text, "with a text default too");
    CheckEquals(2U, toml.GetReported().size(), "each should be reported");
    if (toml.GetReported().size() == 2)
    {
        CheckEmpty(MessageProblem(toml.GetReported()[0], ESeverity::Error, ETomlCode::InvalidArgument,
            TASWTomlPath("a..b").GetError(), ""), "as an Error with the path's error");
        CheckEmpty(MessageProblem(toml.GetReported()[1], ESeverity::Error, ETomlCode::InvalidArgument,
            TASWTomlPath("port[").GetError(), ""), "each");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, GetValue_MissingValueGivesTheDefault)
{
    // Arrange
    TTestToml toml(SettingsText);

    // Act & Assert
    CheckEquals(5, toml.GetValue("missing", 5), "a missing key should give the default");
    CheckEquals(std::string("none"), toml.GetValue("server.missing", std::string("none")), "in a table too");
    CheckEquals(6, toml.GetValue("ports[1]", 6), "an index past the end of an array");
    CheckEquals(7, toml.GetValue({ "points", 2, "x" }, 7), "of an array of tables");
    CheckEquals(8, toml.GetValue("port.x", 8), "a key under a value that isn't a table");
    CheckEquals(0U, toml.GetReported().size(), "silently");
    CheckEquals(0, toml.GetValueOptionsAsked(), "without asking for the value options");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, GetValue_RefusedValueGivesTheDefaultAndAWarning)
{
    // Arrange
    TTestToml toml(SettingsText);
    TTestToml root("x = \"one\"\nbig = 300\n");
    TTestPoint defaultPoint;
    defaultPoint.X = 9;
    std::vector<TTestPoint> const defaultPoints = { defaultPoint };

    // Act
    int const port = toml.GetValue("server.port", 7);
    std::vector<TTestPoint> const points = toml.GetValue("points", defaultPoints);
    TTestPoint const point = root.GetValue("", defaultPoint);
    std::uint8_t const big = root.GetValue("big", std::uint8_t{ 1 });

    // Assert
    CheckEquals(7, port, "a value of another type should give the default");
    CheckTrue(points == defaultPoints, "an element that doesn't convert too");
    CheckTrue(point == defaultPoint, "the root table read as a struct");
    CheckEquals(1, static_cast<int>(big), "a number out of range");
    CheckEquals(2U, toml.GetReported().size(), "each should be reported");
    CheckEquals(2U, root.GetReported().size(), "each should be reported");
    if (toml.GetReported().size() == 2 && root.GetReported().size() == 2)
    {
        CheckEmpty(MessageProblem(toml.GetReported()[0], ESeverity::Warning, ETomlCode::TypeMismatch,
            "The value \"abc\" is a string, not an integer.", "server.port"),
            "as a Warning with the converter's code and text, at the path");
        CheckEmpty(MessageProblem(toml.GetReported()[1], ESeverity::Warning, ETomlCode::TypeMismatch,
            "The value \"four\" is a string, not an integer.", "points[1].y"),
            "at the place in the document that failed");
        CheckEmpty(MessageProblem(root.GetReported()[0], ESeverity::Warning, ETomlCode::TypeMismatch,
            "The value \"one\" is a string, not an integer.", "x"), "a key of the root table without a path before it");
        CheckEmpty(MessageProblem(root.GetReported()[1], ESeverity::Warning, ETomlCode::ValueOutOfRange,
            "The value 300 is out of range: it must be from 0 to 255.", "big"), "with the converter's code");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, GetValue_StructKeepsTheDefaultsMembers)
{
    // Arrange
    TTestToml toml("[point]\nx = 1\n");
    TTestPoint defaultPoint;
    defaultPoint.X = 9;
    defaultPoint.Y = 5;
    defaultPoint.Label = "default";

    // Act
    TTestPoint const point = toml.GetValue("point", defaultPoint);

    // Assert
    CheckEquals(1, point.X, "a key the table has should be read");
    CheckEquals(5, point.Y, "a member whose key the table lacks should be the default's");
    CheckEquals(std::string("default"), point.Label, "each of them");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, GetValue_TextDefaultReadsAString)
{
    // Arrange
    TTestToml toml(SettingsText);
    char buffer[] = "buffer";
    TASWTomlValueOptions const options;

    // Act & Assert
    static_assert(std::is_same_v<decltype(toml.GetValue("name", "none")), std::string>,
        "a text default should give a std::string");
    CheckEquals(std::string("app"), toml.GetValue("name", "none"), "a string literal default should read a string");
    CheckEquals(std::string("none"), toml.GetValue("missing", "none"), "and be given when there is none");
    CheckEquals(std::string("buffer"), toml.GetValue("missing", buffer), "a char array");
    CheckEquals(std::string("view"), toml.GetValue("missing", std::string_view("view")), "a std::string_view");
    CheckEquals(std::string(), toml.GetValue("missing", static_cast<char const*>(nullptr)), "a nullptr as \"\"");
    CheckEquals(std::string("app"), toml.GetValue("name", "none", options), "with value options too");
    CheckEquals(std::string("app"), toml.GetValue("name", std::string_view("none"), options), "each");
    CheckEquals(1, toml.GetValueOptionsAsked(), "which are then not asked for");
    CheckEquals(0U, toml.GetReported().size(), "nothing should be reported");
    CheckEquals(std::string("80"), toml.GetValue("port", "80"), "a value that isn't a string should give the default");
    CheckEquals(1U, toml.GetReported().size(), "and be reported");
    if (toml.GetReported().size() == 1)
    {
        CheckEmpty(MessageProblem(toml.GetReported()[0], ESeverity::Warning, ETomlCode::TypeMismatch,
            "The value 80 is an integer, not a string.", "port"), "as a string that doesn't convert");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, GetValue_UsesTheValueOptions)
{
    // Arrange
    TASWTomlOptions hexadecimal;
    hexadecimal.Values.IntegerBase = EIntegerBase::Hexadecimal;
    TTestToml toml(SettingsText, hexadecimal);
    TASWTomlValueOptions octal;
    octal.IntegerBase = EIntegerBase::Octal;

    // Act
    TTestSeenOptions const own = toml.GetValue("port", TTestSeenOptions());
    TTestSeenOptions const given = toml.GetValue("port", TTestSeenOptions(), octal);

    // Assert
    CheckTrue(own.Base == EIntegerBase::Hexadecimal, "the converter should get the object's value options");
    CheckTrue(given.Base == EIntegerBase::Octal, "or the ones given");
    CheckEquals(1, toml.GetValueOptionsAsked(), "which should be asked for only when none are given");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, Helpers_PathFailure)
{
    // Arrange
    TASWTomlResult failure;
    failure.Code = ETomlCode::TypeMismatch;
    failure.Message = "The value 1 is an integer, not a string.";
    TASWTomlResult element = failure;
    element.Path = "[1].port";
    TASWTomlResult key = failure;
    key.Path = "port";

    // Act & Assert
    CheckEmpty(ResultProblem(ASWToml::Detail::PathFailure(element, "servers"), failure.Code, failure.Message,
        "servers[1].port"), "an index should follow the path directly");
    CheckEmpty(ResultProblem(ASWToml::Detail::PathFailure(key, "server"), failure.Code, failure.Message,
        "server.port"), "a key after a dot");
    CheckEmpty(ResultProblem(ASWToml::Detail::PathFailure(key, { "a b", 0 }), failure.Code, failure.Message,
        "\"a b\"[0].port"), "after the path as ToString() writes it");
    CheckEmpty(ResultProblem(ASWToml::Detail::PathFailure(key, ""), failure.Code, failure.Message, "port"),
        "the root path should leave the path as it was");
    CheckEmpty(ResultProblem(ASWToml::Detail::PathFailure(failure, "server.port"), failure.Code, failure.Message,
        "server.port"), "an empty path should become the path");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, ReportFailure_CopiesEveryField)
{
    // Arrange
    TTestToml toml(SettingsText);
    TASWTomlResult failure;
    failure.Code = ETomlCode::ParseFailed;
    failure.Message = "The text isn't valid TOML.";
    failure.FilePath = "app.toml";
    failure.Line = 3;
    failure.Column = 7;
    failure.Path = "server.port";
    failure.SystemError = 5;

    // Act
    toml.ReportResult(failure);

    // Assert
    CheckEquals(1U, toml.GetReported().size(), "a failure should be reported once");
    if (toml.GetReported().size() == 1)
    {
        TASWTomlMessage const& message = toml.GetReported()[0];
        CheckTrue(message.Severity == ESeverity::Error && message.Code == failure.Code, "as an Error with its code");
        CheckEquals(failure.Message, message.Text, "its message");
        CheckEquals(failure.FilePath, message.FilePath, "file path");
        CheckEquals(3U, message.Line, "line");
        CheckEquals(7U, message.Column, "column");
        CheckEquals(failure.Path, message.Path, "path");
        CheckEquals(5, message.SystemError, "and system error");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, SetValue_ConversionFailureIsReported)
{
    // Arrange
    TTestToml toml(SettingsText);
    std::string const before = toml.ToText();
    std::string const unnamedText = "The value 2 has no name in the enum's ASWTomlEnumNames(), so it can't be written.";
    TASWTomlValue written;
    std::string const invalidUtf8Text = TASWTomlConverter<std::string>::ToValue("\xFF", written,
        TASWTomlValueOptions()).Message;
    std::string const invalidPathText = TASWTomlPath("a..b").GetError();
    std::vector<ETestMode> const modes = { ETestMode::Fast, ETestMode::Unnamed };

    // Act
    TASWTomlResult const unnamed = toml.SetValue("mode", ETestMode::Unnamed);
    TASWTomlResult const element = toml.SetValue("modes", modes);
    TASWTomlResult const invalidUtf8 = toml.SetValue("name", "\xFF");
    TASWTomlResult const invalidUtf8View = toml.SetValue("name", std::string_view("\xFF"));
    TASWTomlResult const invalidPath = toml.SetValue("a..b", ETestMode::Unnamed);

    // Assert
    CheckEmpty(ResultProblem(unnamed, ETomlCode::InvalidValue, unnamedText, "mode"),
        "a value the converter can't write should fail with its code and message, at the path");
    CheckEmpty(ResultProblem(element, ETomlCode::InvalidValue, unnamedText, "modes[1]"),
        "at the place in the value that failed");
    CheckEmpty(ResultProblem(invalidUtf8, ETomlCode::InvalidValue, invalidUtf8Text, "name"),
        "a text should be checked as any string is");
    CheckEmpty(ResultProblem(invalidUtf8View, ETomlCode::InvalidValue, invalidUtf8Text, "name"), "a view too");
    CheckEmpty(ResultProblem(invalidPath, ETomlCode::InvalidArgument, invalidPathText, ""),
        "a path that isn't valid should fail with its error, before the value is converted");
    CheckEquals(before, toml.ToText(), "nothing should change");
    CheckEquals(5U, toml.GetReported().size(), "each failure should be reported once");
    if (toml.GetReported().size() == 5)
    {
        CheckEmpty(MessageProblem(toml.GetReported()[1], ESeverity::Error, ETomlCode::InvalidValue, unnamedText,
            "modes[1]"), "as an Error with the path");
        CheckEmpty(MessageProblem(toml.GetReported()[4], ESeverity::Error, ETomlCode::InvalidArgument,
            invalidPathText, ""), "the path's too");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, SetValue_ConverterExceptionIsReported)
{
    // Arrange
    TTestToml toml(SettingsText);
    std::string const before = toml.ToText();
    TTestThrows other;
    other.Standard = false;

    // Act
    TASWTomlResult const standard = toml.SetValue("port", TTestThrows());
    TASWTomlResult const unknown = toml.SetValue({ "a b" }, other);

    // Assert
    CheckEmpty(ResultProblem(standard, ETomlCode::UnexpectedException,
        "An unexpected exception was thrown: converter failed", "port"),
        "a std::exception should be UnexpectedException with its what(), at the path");
    CheckEmpty(ResultProblem(unknown, ETomlCode::UnexpectedException,
        "An exception that isn't a std::exception was thrown.", "\"a b\""), "any other exception too");
    CheckEquals(before, toml.ToText(), "nothing should change");
    CheckEquals(2U, toml.GetReported().size(), "each should be reported");
    if (toml.GetReported().size() == 2)
    {
        CheckEmpty(MessageProblem(toml.GetReported()[0], ESeverity::Error, ETomlCode::UnexpectedException,
            "An unexpected exception was thrown: converter failed", "port"), "as an Error with the path");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, SetValue_ConvertsTheValue)
{
    // Arrange
    TTestToml toml("port = 80\n");
    TTestPoint point;
    point.X = 1;
    point.Y = 2;
    std::vector<int> const ports = { 80, 443 };

    // Act
    TASWTomlResult const port = toml.SetValue("port", 8080);
    TASWTomlResult const host = toml.SetValue("server.host", std::string("example.com"));
    TASWTomlResult const mode = toml.SetValue("mode", ETestMode::Fast);
    TASWTomlResult const array = toml.SetValue({ "server", "ports" }, ports);
    TASWTomlResult const table = toml.SetValue("point", point);
    TASWTomlResult const flag = toml.SetValue("flag", true);
    TASWTomlResult const value = toml.SetValue("value", TASWTomlValue(1.5));

    // Assert
    CheckTrue(port.IsOk() && host.IsOk() && mode.IsOk() && array.IsOk() && table.IsOk() && flag.IsOk() &&
        value.IsOk(), "each should succeed");
    CheckEquals(std::string("port = 8080\n"
        "mode = \"fast\"\n"
        "flag = true\n"
        "value = 1.5\n"
        "\n"
        "[server]\n"
        "host = \"example.com\"\n"
        "ports = [80, 443]\n"
        "\n"
        "[point]\n"
        "x = 1\n"
        "y = 2\n"
        "label = \"none\"\n"), toml.ToText(), "each value should be converted and set at its path");
    CheckEquals(0U, toml.GetReported().size(), "nothing should be reported");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, SetValue_DocumentFailureIsReportedOnce)
{
    // Arrange
    TTestToml toml(SettingsText);
    std::string const before = toml.ToText();

    // Act
    TASWTomlResult const throughValue = toml.SetValue("port.x", 1);
    TASWTomlResult const pastEnd = toml.SetValue("ports[5]", 1);

    // Assert
    CheckTrue(throughValue.Code == ETomlCode::InvalidArgument && throughValue.Path == "port.x",
        "the document's failure should be returned, with its path");
    CheckTrue(pastEnd.Code == ETomlCode::NotFound && pastEnd.Path == "ports[5]", "each");
    CheckEquals(before, toml.ToText(), "nothing should change");
    CheckEquals(2U, toml.GetReported().size(), "each should be reported once, by the operation that failed");
    if (toml.GetReported().size() == 2)
    {
        CheckEmpty(MessageProblem(toml.GetReported()[0], ESeverity::Error, throughValue.Code, throughValue.Message,
            "port.x"), "as the result says");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, SetValue_TextIsSetAsAString)
{
    // Arrange
    TTestToml toml("first = 1\n");
    char buffer[] = "buffer";

    // Act
    TASWTomlResult const literal = toml.SetValue("literal", "x");
    TASWTomlResult const array = toml.SetValue("array", buffer);
    TASWTomlResult const view = toml.SetValue("view", std::string_view("y"));
    TASWTomlResult const null = toml.SetValue("null", static_cast<char const*>(nullptr));
    TASWTomlResult const path = toml.SetValue("path", "C:\\x");

    // Assert
    CheckTrue(literal.IsOk() && array.IsOk() && view.IsOk() && null.IsOk() && path.IsOk(), "each should succeed");
    CheckEquals(std::string("first = 1\n"
        "literal = \"x\"\n"
        "array = \"buffer\"\n"
        "view = \"y\"\n"
        "null = \"\"\n"
        "path = 'C:\\x'\n"), toml.ToText(), "each text should be set as a string, a nullptr as \"\"");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, SetValue_UsesTheValueOptions)
{
    // Arrange
    TASWTomlOptions basic;
    basic.Values.StringStyle = EStringStyle::Basic;
    basic.Values.IntegerBase = EIntegerBase::Hexadecimal;
    TTestToml toml("first = 1\n", basic);
    TASWTomlValueOptions const automatic;

    // Act
    TASWTomlResult const textOwn = toml.SetValue("a", "C:\\x");
    TASWTomlResult const textGiven = toml.SetValue("b", "C:\\x", automatic);
    TASWTomlResult const viewGiven = toml.SetValue("c", std::string_view("C:\\y"), automatic);
    TASWTomlResult const numberOwn = toml.SetValue("d", 255);
    TASWTomlResult const numberGiven = toml.SetValue("e", 255, automatic);
    TASWTomlResult const seenOwn = toml.SetValue("f", TTestSeenOptions());
    TASWTomlResult const seenGiven = toml.SetValue("g", TTestSeenOptions(), automatic);

    // Assert
    CheckTrue(textOwn.IsOk() && textGiven.IsOk() && viewGiven.IsOk() && numberOwn.IsOk() && numberGiven.IsOk() &&
        seenOwn.IsOk() && seenGiven.IsOk(), "each should succeed");
    CheckEquals(std::string("first = 1\n"
        "a = \"C:\\\\x\"\n"
        "b = 'C:\\x'\n"
        "c = 'C:\\y'\n"
        "d = 0xFF\n"
        "e = 255\n"
        "f = 0x2\n"
        "g = 0\n"), toml.ToText(),
        "the converter and the document should get the object's value options, or the ones given");
    CheckEquals(3, toml.GetValueOptionsAsked(), "the object's should be asked for once for each SetValue() without");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, TryGetValue_ConverterExceptionIsReturned)
{
    // Arrange
    TTestToml toml(SettingsText);
    TTestThrows standard;
    TTestThrows other;
    other.Standard = false;

    // Act
    TASWTomlResult const standardResult = toml.TryGetValue("port", standard);
    TASWTomlResult const unknownResult = toml.TryGetValue("server.port", other);

    // Assert
    CheckEmpty(ResultProblem(standardResult, ETomlCode::UnexpectedException,
        "An unexpected exception was thrown: converter failed", "port"),
        "a std::exception should be UnexpectedException with its what(), at the path");
    CheckEmpty(ResultProblem(unknownResult, ETomlCode::UnexpectedException,
        "An exception that isn't a std::exception was thrown.", "server.port"), "any other exception too");
    CheckEquals(0U, toml.GetReported().size(), "neither should be reported");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, TryGetValue_ConvertsTheValue)
{
    // Arrange
    TTestToml toml(SettingsText);
    std::uint16_t port = 0;
    std::string host;
    std::vector<int> ports;
    TTestPoint point;
    TASWTomlValueOptions const options;

    // Act
    TASWTomlResult const portResult = toml.TryGetValue("port", port);
    TASWTomlResult const hostResult = toml.TryGetValue("server.host", host, options);
    TASWTomlResult const portsResult = toml.TryGetValue("ports", ports);
    TASWTomlResult const pointResult = toml.TryGetValue({ "points", 0 }, point);

    // Assert
    CheckEmpty(ResultProblem(portResult, ETomlCode::Ok, "", ""), "a number should be read");
    CheckEmpty(ResultProblem(hostResult, ETomlCode::Ok, "", ""), "a string in a table, with value options");
    CheckEmpty(ResultProblem(portsResult, ETomlCode::Ok, "", ""), "an array");
    CheckEmpty(ResultProblem(pointResult, ETomlCode::Ok, "", ""), "a struct");
    CheckEquals(80, static_cast<int>(port), "as the value is");
    CheckEquals(std::string("example.com"), host, "each");
    CheckTrue(ports == std::vector<int>{ 80 }, "each");
    CheckTrue(point.X == 1 && point.Y == 2 && point.Label == "none", "each");
    CheckEquals(0U, toml.GetReported().size(), "nothing should be reported");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, TryGetValue_FailureKeepsTheValue)
{
    // Arrange
    TTestToml toml(SettingsText);
    TTestToml missingKey("[[points]]\ny = 1\n");
    int port = 7;
    TTestPoint initialPoint;
    initialPoint.X = 9;
    std::vector<TTestPoint> const initialPoints = { initialPoint };
    std::vector<TTestPoint> points = initialPoints;
    std::vector<TTestPoint> missing = initialPoints;
    std::map<std::string, int> const initialLimits = { { "kept", 1 } };
    std::map<std::string, int> limits = initialLimits;

    // Act
    TASWTomlResult const portResult = toml.TryGetValue("server.port", port);
    TASWTomlResult const pointsResult = toml.TryGetValue("points", points);
    TASWTomlResult const limitsResult = toml.TryGetValue("limits", limits);
    TASWTomlResult const missingResult = missingKey.TryGetValue("points", missing);

    // Assert
    CheckEmpty(ResultProblem(portResult, ETomlCode::TypeMismatch, "The value \"abc\" is a string, not an integer.",
        "server.port"), "a value that doesn't convert should fail with the converter's code and message, at the path");
    CheckEmpty(ResultProblem(pointsResult, ETomlCode::TypeMismatch, "The value \"four\" is a string, not an integer.",
        "points[1].y"), "at the place in the document that failed");
    CheckEmpty(ResultProblem(limitsResult, ETomlCode::TypeMismatch, "The value \"high\" is a string, not an integer.",
        "limits.max"), "a key of an inline table");
    CheckEmpty(ResultProblem(missingResult, ETomlCode::NotFound, "The table has no key x.", "points[0].x"),
        "a key a struct's table lacks");
    CheckEquals(7, port, "the value should stay as it was");
    CheckTrue(points == initialPoints && missing == initialPoints, "each");
    CheckTrue(limits == initialLimits, "each");
    CheckEquals(0U, toml.GetReported().size() + missingKey.GetReported().size(), "nothing should be reported");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, TryGetValue_InvalidPathIsInvalidArgument)
{
    // Arrange
    TTestToml toml(SettingsText);
    int value = 7;

    // Act
    TASWTomlResult const result = toml.TryGetValue("a..b", value);

    // Assert
    CheckEmpty(ResultProblem(result, ETomlCode::InvalidArgument, TASWTomlPath("a..b").GetError(), ""),
        "a path that isn't valid should fail with its error");
    CheckEquals(7, value, "the value should stay as it was");
    CheckEquals(0U, toml.GetReported().size(), "nothing should be reported");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, TryGetValue_MissingValueIsNotFound)
{
    // Arrange
    TTestToml toml(SettingsText);
    int value = 7;

    // Act
    TASWTomlResult const key = toml.TryGetValue("server.missing", value);
    TASWTomlResult const element = toml.TryGetValue({ "points", 5 }, value);
    TASWTomlResult const quoted = toml.TryGetValue({ "a b", "c" }, value);

    // Assert
    CheckEmpty(ResultProblem(key, ETomlCode::NotFound, "There is no value at server.missing.", "server.missing"),
        "a missing key should be NotFound, naming the path");
    CheckEmpty(ResultProblem(element, ETomlCode::NotFound, "There is no value at points[5].", "points[5]"),
        "an index past the end");
    CheckEmpty(ResultProblem(quoted, ETomlCode::NotFound, "There is no value at \"a b\".c.", "\"a b\".c"),
        "the path as ToString() writes it");
    CheckEquals(7, value, "the value should stay as it was");
    CheckEquals(0U, toml.GetReported().size(), "nothing should be reported");
    CheckEquals(0, toml.GetValueOptionsAsked(), "and the value options not asked for");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Interface, TryGetValue_UsesTheValueOptions)
{
    // Arrange
    TASWTomlOptions hexadecimal;
    hexadecimal.Values.IntegerBase = EIntegerBase::Hexadecimal;
    TTestToml toml(SettingsText, hexadecimal);
    TASWTomlValueOptions octal;
    octal.IntegerBase = EIntegerBase::Octal;
    TTestSeenOptions own;
    TTestSeenOptions given;

    // Act
    TASWTomlResult const ownResult = toml.TryGetValue("port", own);
    TASWTomlResult const givenResult = toml.TryGetValue("port", given, octal);

    // Assert
    CheckTrue(ownResult.IsOk() && givenResult.IsOk(), "each should succeed");
    CheckTrue(own.Base == EIntegerBase::Hexadecimal, "the converter should get the object's value options");
    CheckTrue(given.Base == EIntegerBase::Octal, "or the ones given");
    CheckEquals(1, toml.GetValueOptionsAsked(), "which should be asked for only when none are given");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWToml_Interface)
