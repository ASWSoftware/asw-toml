/* **************************************************************************
Test_ASWToml_Options.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "Test_ASWToml_Options.h"
//---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

using TIntegerBaseVector = std::vector<ASWToml::EIntegerBase>;
using TStringStyleVector = std::vector<ASWToml::EStringStyle>;
using TStringVector = std::vector<std::string>;

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_ReadingOptions
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_ReadingOptions::TTest_ASWToml_ReadingOptions()
    : inherited("ASWToml_ReadingOptions_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_ReadingOptions::~TTest_ASWToml_ReadingOptions()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ReadingOptions::CheckInvalid(ASWToml::TASWTomlReadingOptions const& options,
    std::string const& expectedText, std::string const& msg, std::source_location loc)
{
    ASWToml::TASWTomlResult const result = options.Validate();

    CheckEquals(ASWToml::ETomlCode::InvalidOptions, result.Code, msg + ": the code should be InvalidOptions", loc);
    CheckContains(result.Message, expectedText, msg + ": the message should name the problem", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ReadingOptions::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ReadingOptions::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ReadingOptions::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ReadingOptions::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ReadingOptions, Defaults_AreValid)
{
    // Arrange
    ASWToml::TASWTomlReadingOptions const options;

    // Act
    ASWToml::TASWTomlResult const result = options.Validate();

    // Assert
    CheckTrue(result.IsOk(), "the default reading options should be valid: " + result.Message);
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ReadingOptions, Defaults_MatchDocumentedValues)
{
    // Act
    ASWToml::TASWTomlReadingOptions const options;

    // Assert
    // The defaults the roadmap's decisions document (decision 11)
    CheckEquals(static_cast<std::uint64_t>(16 * 1024 * 1024), static_cast<std::uint64_t>(options.MaxFileSize),
        "MaxFileSize should be 16 MiB");
    CheckEquals(static_cast<std::uint64_t>(ASWToml::TASWTomlReadingOptions::DefaultMaxFileSize),
        static_cast<std::uint64_t>(options.MaxFileSize), "MaxFileSize should be DefaultMaxFileSize");
    CheckEquals(static_cast<std::uint64_t>(100), static_cast<std::uint64_t>(options.MaxNestingDepth),
        "MaxNestingDepth should be 100");
    CheckEquals(static_cast<std::uint64_t>(ASWToml::TASWTomlReadingOptions::DefaultMaxNestingDepth),
        static_cast<std::uint64_t>(options.MaxNestingDepth), "MaxNestingDepth should be DefaultMaxNestingDepth");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ReadingOptions, Validate_AcceptsLargestMaxNestingDepth)
{
    // Arrange
    ASWToml::TASWTomlReadingOptions options;
    options.MaxNestingDepth = ASWToml::TASWTomlReadingOptions::MaxNestingDepthLimit;

    // Act
    ASWToml::TASWTomlResult const result = options.Validate();

    // Assert
    CheckEquals(static_cast<std::uint64_t>(128), static_cast<std::uint64_t>(options.MaxNestingDepth),
        "MaxNestingDepthLimit should be 128");
    CheckTrue(result.IsOk(), "MaxNestingDepthLimit should be valid: " + result.Message);
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ReadingOptions, Validate_AcceptsSmallestLimits)
{
    // Arrange
    ASWToml::TASWTomlReadingOptions options;
    options.MaxFileSize = 1;
    options.MaxNestingDepth = 1;

    // Act
    ASWToml::TASWTomlResult const result = options.Validate();

    // Assert
    CheckTrue(result.IsOk(), "limits of 1 should be valid: " + result.Message);
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ReadingOptions, Validate_RejectsMaxNestingDepthAboveLimit)
{
    // Arrange
    ASWToml::TASWTomlReadingOptions options;
    options.MaxNestingDepth = ASWToml::TASWTomlReadingOptions::MaxNestingDepthLimit + 1;

    // Act & Assert
    CheckInvalid(options, "Reading.MaxNestingDepth is 129; it can be at most 128", "a MaxNestingDepth above the limit");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ReadingOptions, Validate_RejectsZeroMaxFileSize)
{
    // Arrange
    ASWToml::TASWTomlReadingOptions options;
    options.MaxFileSize = 0;

    // Act & Assert
    CheckInvalid(options, "Reading.MaxFileSize", "a zero MaxFileSize");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ReadingOptions, Validate_RejectsZeroMaxNestingDepth)
{
    // Arrange
    ASWToml::TASWTomlReadingOptions options;
    options.MaxNestingDepth = 0;

    // Act & Assert
    CheckInvalid(options, "Reading.MaxNestingDepth", "a zero MaxNestingDepth");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_WritingOptions
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_WritingOptions::TTest_ASWToml_WritingOptions()
    : inherited("ASWToml_WritingOptions_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_WritingOptions::~TTest_ASWToml_WritingOptions()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_WritingOptions::CheckInvalid(ASWToml::TASWTomlWritingOptions const& options,
    std::string const& expectedText, std::string const& msg, std::source_location loc)
{
    ASWToml::TASWTomlResult const result = options.Validate();

    CheckEquals(ASWToml::ETomlCode::InvalidOptions, result.Code, msg + ": the code should be InvalidOptions", loc);
    CheckContains(result.Message, expectedText, msg + ": the message should name the problem", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_WritingOptions::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_WritingOptions::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_WritingOptions::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_WritingOptions::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_WritingOptions, Defaults_AreValid)
{
    // Arrange
    ASWToml::TASWTomlWritingOptions const options;

    // Act
    ASWToml::TASWTomlResult const result = options.Validate();

    // Assert
    CheckTrue(result.IsOk(), "the default writing options should be valid: " + result.Message);
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_WritingOptions, Defaults_MatchDocumentedValues)
{
    // Act
    ASWToml::TASWTomlWritingOptions const options;

    // Assert
    // The defaults the roadmap's decisions document (decisions 9 and 10)
    CheckEquals(std::string("# "), options.CommentPrefix, "CommentPrefix");
    CheckEquals(ASWToml::ELineEnding::Auto, options.LineEnding, "LineEnding");
    CheckEquals(ASWToml::EBomMode::Auto, options.Bom, "Bom");
    CheckTrue(options.BlankLineBeforeNewTable, "BlankLineBeforeNewTable should be on");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_WritingOptions, ToString_EnumNames)
{
    CheckEquals(std::string("Auto"), std::string(ASWToml::ToString(ASWToml::ELineEnding::Auto)), "ELineEnding::Auto");
    CheckEquals(std::string("Native"), std::string(ASWToml::ToString(ASWToml::ELineEnding::Native)),
        "ELineEnding::Native");
    CheckEquals(std::string("LF"), std::string(ASWToml::ToString(ASWToml::ELineEnding::LF)), "ELineEnding::LF");
    CheckEquals(std::string("CRLF"), std::string(ASWToml::ToString(ASWToml::ELineEnding::CRLF)), "ELineEnding::CRLF");
    CheckEquals(std::string("Unknown"), std::string(ASWToml::ToString(static_cast<ASWToml::ELineEnding>(99))),
        "an unknown ELineEnding");
    CheckEquals(std::string("Auto"), std::string(ASWToml::ToString(ASWToml::EBomMode::Auto)), "EBomMode::Auto");
    CheckEquals(std::string("Always"), std::string(ASWToml::ToString(ASWToml::EBomMode::Always)), "EBomMode::Always");
    CheckEquals(std::string("Never"), std::string(ASWToml::ToString(ASWToml::EBomMode::Never)), "EBomMode::Never");
    CheckEquals(std::string("Unknown"), std::string(ASWToml::ToString(static_cast<ASWToml::EBomMode>(99))),
        "an unknown EBomMode");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_WritingOptions, Validate_AcceptsCommentPrefixVariants)
{
    // Arrange
    // A '#' followed by nothing, or by spaces and tabs
    TStringVector const prefixes = { "#", "# ", "#\t", "#  ", "# \t " };

    for (std::string const& prefix : prefixes)
    {
        ASWToml::TASWTomlWritingOptions options;
        options.CommentPrefix = prefix;

        // Act
        ASWToml::TASWTomlResult const result = options.Validate();

        // Assert
        CheckTrue(result.IsOk(), "CommentPrefix \"" + prefix + "\" should be valid: " + result.Message);
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_WritingOptions, Validate_RejectsCommentPrefixWithOtherText)
{
    // Arrange
    // Text after the '#' would read back as part of the comment ("## a" reads as "# a"), and TOML doesn't allow
    // control characters in a comment
    TStringVector const prefixes = { "## ", "#! ", "# x", "# \xC2\xBB", "# \xFF", "#\n", "# \r",
        std::string("#\0", 2), "#\x01", "# \x1F", "#\x7F" };

    for (std::string const& prefix : prefixes)
    {
        ASWToml::TASWTomlWritingOptions options;
        options.CommentPrefix = prefix;

        // Act & Assert
        CheckInvalid(options, "may hold only spaces and tabs after its '#'",
            "a prefix ending with the byte " + std::to_string(static_cast<unsigned char>(prefix.back())));
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_WritingOptions, Validate_RejectsCommentPrefixWithoutHash)
{
    // Arrange
    // '#' is the only comment character in TOML, and must come first
    TStringVector const prefixes = { "", " #", "; ", "//", "\t# " };

    for (std::string const& prefix : prefixes)
    {
        ASWToml::TASWTomlWritingOptions options;
        options.CommentPrefix = prefix;

        // Act & Assert
        CheckInvalid(options, "must start with '#'", "CommentPrefix \"" + prefix + "\"");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_WritingOptions, Validate_RejectsUnknownEnumValues)
{
    // Arrange & Act & Assert
    ASWToml::TASWTomlWritingOptions options;
    options.LineEnding = static_cast<ASWToml::ELineEnding>(7);
    CheckInvalid(options, "Writing.LineEnding", "an unknown ELineEnding");

    options = ASWToml::TASWTomlWritingOptions();
    options.Bom = static_cast<ASWToml::EBomMode>(7);
    CheckInvalid(options, "Writing.Bom", "an unknown EBomMode");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_SavingOptions
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_SavingOptions::TTest_ASWToml_SavingOptions()
    : inherited("ASWToml_SavingOptions_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_SavingOptions::~TTest_ASWToml_SavingOptions()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SavingOptions::CheckInvalid(ASWToml::TASWTomlSavingOptions const& options,
    std::string const& expectedText, std::string const& msg, std::source_location loc)
{
    ASWToml::TASWTomlResult const result = options.Validate();

    CheckEquals(ASWToml::ETomlCode::InvalidOptions, result.Code, msg + ": the code should be InvalidOptions", loc);
    CheckContains(result.Message, expectedText, msg + ": the message should name the problem", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SavingOptions::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SavingOptions::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SavingOptions::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_SavingOptions::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SavingOptions, Defaults_AreValid)
{
    // Arrange
    ASWToml::TASWTomlSavingOptions const options;

    // Act
    ASWToml::TASWTomlResult const result = options.Validate();

    // Assert
    CheckTrue(result.IsOk(), "the default saving options should be valid: " + result.Message);
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SavingOptions, Defaults_MatchDocumentedValues)
{
    // Act
    ASWToml::TASWTomlSavingOptions const options;

    // Assert
    CheckTrue(options.AtomicSave, "AtomicSave should be on");
    CheckFalse(options.Backup, "Backup should be off");
    CheckEquals(std::string(".bak"), options.BackupSuffix, "BackupSuffix");
    CheckTrue(options.CreateFolders, "CreateFolders should be on");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_SavingOptions, Validate_ChecksBackupSuffixOnlyWithBackup)
{
    // Arrange
    ASWToml::TASWTomlSavingOptions options;
    options.BackupSuffix = "";

    // Act & Assert
    CheckTrue(options.Validate().IsOk(), "an empty suffix should be valid without Backup");

    options.Backup = true;
    CheckInvalid(options, "Saving.BackupSuffix is empty", "an empty suffix with Backup");

    options.BackupSuffix = "/../x";
    CheckInvalid(options, "Saving.BackupSuffix \"/../x\"", "a suffix with a path separator");

    options.BackupSuffix = ".old";
    CheckTrue(options.Validate().IsOk(), "\".old\" should be a valid suffix");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_ValueOptions
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_ValueOptions::TTest_ASWToml_ValueOptions()
    : inherited("ASWToml_ValueOptions_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_ValueOptions::~TTest_ASWToml_ValueOptions()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ValueOptions::CheckInvalid(ASWToml::TASWTomlValueOptions const& options,
    std::string const& expectedText, std::string const& msg, std::source_location loc)
{
    ASWToml::TASWTomlResult const result = options.Validate();

    CheckEquals(ASWToml::ETomlCode::InvalidOptions, result.Code, msg + ": the code should be InvalidOptions", loc);
    CheckContains(result.Message, expectedText, msg + ": the message should name the problem", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ValueOptions::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ValueOptions::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ValueOptions::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_ValueOptions::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ValueOptions, Defaults_AreValid)
{
    // Arrange
    ASWToml::TASWTomlValueOptions const options;

    // Act
    ASWToml::TASWTomlResult const result = options.Validate();

    // Assert
    CheckTrue(result.IsOk(), "the default value options should be valid: " + result.Message);
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ValueOptions, Defaults_MatchDocumentedValues)
{
    // Act
    ASWToml::TASWTomlValueOptions const options;

    // Assert
    // The defaults phase 2's decisions document (decision 10)
    CheckEquals(ASWToml::EStringStyle::Auto, options.StringStyle, "StringStyle");
    CheckEquals(ASWToml::EIntegerBase::Auto, options.IntegerBase, "IntegerBase");
    CheckEquals(0, static_cast<int>(ASWToml::EStringStyle::Auto), "EStringStyle::Auto should be 0");
    CheckEquals(0, static_cast<int>(ASWToml::EIntegerBase::Auto), "EIntegerBase::Auto should be 0");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ValueOptions, ToString_EnumNames)
{
    CheckEquals(std::string("Auto"), std::string(ASWToml::ToString(ASWToml::EStringStyle::Auto)),
        "EStringStyle::Auto");
    CheckEquals(std::string("Basic"), std::string(ASWToml::ToString(ASWToml::EStringStyle::Basic)),
        "EStringStyle::Basic");
    CheckEquals(std::string("Literal"), std::string(ASWToml::ToString(ASWToml::EStringStyle::Literal)),
        "EStringStyle::Literal");
    CheckEquals(std::string("Unknown"), std::string(ASWToml::ToString(static_cast<ASWToml::EStringStyle>(99))),
        "an unknown EStringStyle");
    CheckEquals(std::string("Auto"), std::string(ASWToml::ToString(ASWToml::EIntegerBase::Auto)),
        "EIntegerBase::Auto");
    CheckEquals(std::string("Decimal"), std::string(ASWToml::ToString(ASWToml::EIntegerBase::Decimal)),
        "EIntegerBase::Decimal");
    CheckEquals(std::string("Hexadecimal"), std::string(ASWToml::ToString(ASWToml::EIntegerBase::Hexadecimal)),
        "EIntegerBase::Hexadecimal");
    CheckEquals(std::string("Octal"), std::string(ASWToml::ToString(ASWToml::EIntegerBase::Octal)),
        "EIntegerBase::Octal");
    CheckEquals(std::string("Binary"), std::string(ASWToml::ToString(ASWToml::EIntegerBase::Binary)),
        "EIntegerBase::Binary");
    CheckEquals(std::string("Unknown"), std::string(ASWToml::ToString(static_cast<ASWToml::EIntegerBase>(99))),
        "an unknown EIntegerBase");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ValueOptions, Validate_AcceptsEveryEnumValue)
{
    // Arrange
    TStringStyleVector const styles = { ASWToml::EStringStyle::Auto, ASWToml::EStringStyle::Basic,
        ASWToml::EStringStyle::Literal };
    TIntegerBaseVector const bases = { ASWToml::EIntegerBase::Auto, ASWToml::EIntegerBase::Decimal,
        ASWToml::EIntegerBase::Hexadecimal, ASWToml::EIntegerBase::Octal, ASWToml::EIntegerBase::Binary };

    for (ASWToml::EStringStyle const style : styles)
    {
        for (ASWToml::EIntegerBase const base : bases)
        {
            ASWToml::TASWTomlValueOptions options;
            options.StringStyle = style;
            options.IntegerBase = base;

            // Act
            ASWToml::TASWTomlResult const result = options.Validate();

            // Assert
            CheckTrue(result.IsOk(), std::string("StringStyle ") + ASWToml::ToString(style) + " with IntegerBase " +
                ASWToml::ToString(base) + " should be valid: " + result.Message);
        }
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_ValueOptions, Validate_RejectsUnknownEnumValues)
{
    // Arrange & Act & Assert
    ASWToml::TASWTomlValueOptions options;
    options.StringStyle = static_cast<ASWToml::EStringStyle>(3);
    CheckInvalid(options, "Values.StringStyle", "an unknown EStringStyle");

    options.StringStyle = static_cast<ASWToml::EStringStyle>(-1);
    CheckInvalid(options, "Values.StringStyle", "a negative EStringStyle");

    options = ASWToml::TASWTomlValueOptions();
    options.IntegerBase = static_cast<ASWToml::EIntegerBase>(5);
    CheckInvalid(options, "Values.IntegerBase", "an unknown EIntegerBase");

    options.IntegerBase = static_cast<ASWToml::EIntegerBase>(-1);
    CheckInvalid(options, "Values.IntegerBase", "a negative EIntegerBase");
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Options
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_Options::TTest_ASWToml_Options()
    : inherited("ASWToml_Options_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_Options::~TTest_ASWToml_Options()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Options::CheckInvalid(ASWToml::TASWTomlOptions const& options, std::string const& expectedText,
    std::string const& msg, std::source_location loc)
{
    ASWToml::TASWTomlResult const result = options.Validate();

    CheckEquals(ASWToml::ETomlCode::InvalidOptions, result.Code, msg + ": the code should be InvalidOptions", loc);
    CheckContains(result.Message, expectedText, msg + ": the message should name the problem", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Options::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Options::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Options::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Options::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Options, Defaults_AreValid)
{
    // Arrange
    ASWToml::TASWTomlOptions const options;

    // Act
    ASWToml::TASWTomlResult const result = options.Validate();

    // Assert
    CheckTrue(result.IsOk(), "the default options should be valid: " + result.Message);
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Options, Validate_ReportsEachGroupsProblems)
{
    // Arrange & Act & Assert
    // A problem within one group comes back from the whole options' Validate(), naming the group
    ASWToml::TASWTomlOptions options;
    options.Reading.MaxNestingDepth = 0;
    CheckInvalid(options, "Reading.MaxNestingDepth is 0", "a reading problem");

    options = ASWToml::TASWTomlOptions();
    options.Writing.CommentPrefix = "; ";
    CheckInvalid(options, "Writing.CommentPrefix \"; \"", "a writing problem");

    options = ASWToml::TASWTomlOptions();
    options.Saving.Backup = true;
    options.Saving.BackupSuffix.clear();
    CheckInvalid(options, "Saving.BackupSuffix is empty", "a saving problem");

    options = ASWToml::TASWTomlOptions();
    options.Values.IntegerBase = static_cast<ASWToml::EIntegerBase>(9);
    CheckInvalid(options, "Values.IntegerBase", "a value problem");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Options, Validate_ReportsFirstGroupsProblem)
{
    // Arrange
    // A problem in every group: Reading's is reported, then Writing's, then Saving's, then Values'
    ASWToml::TASWTomlOptions options;
    options.Reading.MaxFileSize = 0;
    options.Writing.CommentPrefix.clear();
    options.Saving.Backup = true;
    options.Saving.BackupSuffix.clear();
    options.Values.StringStyle = static_cast<ASWToml::EStringStyle>(9);

    // Act & Assert
    CheckInvalid(options, "Reading.MaxFileSize", "with every group invalid");

    options.Reading = ASWToml::TASWTomlReadingOptions();
    CheckInvalid(options, "Writing.CommentPrefix", "with Writing, Saving and Values invalid");

    options.Writing = ASWToml::TASWTomlWritingOptions();
    CheckInvalid(options, "Saving.BackupSuffix", "with Saving and Values invalid");

    options.Saving = ASWToml::TASWTomlSavingOptions();
    CheckInvalid(options, "Values.StringStyle", "with Values invalid");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_Options, "Options")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_ReadingOptions, "Options")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_SavingOptions, "Options")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_ValueOptions, "Options")
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_WritingOptions, "Options")
