/* **************************************************************************
Test_ASWToml_Types.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "Test_ASWToml_Types.h"
//---------------------------------------------------------------------------
#include <cstddef>
#include <set>
#include <string>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWToml_Types.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

using TNameSet = std::set<std::string>;
using TTomlCodeVector = std::vector<ASWToml::ETomlCode>;

TTomlCodeVector AllTomlCodes();

//---------------------------------------------------------------------------
/*
    AllTomlCodes

    Every ETomlCode, in declaration order
*/
TTomlCodeVector AllTomlCodes()
{
    using ASWToml::ETomlCode;

    return TTomlCodeVector{
        ETomlCode::Ok,
        ETomlCode::InvalidArgument,
        ETomlCode::InvalidOptions,
        ETomlCode::NoFilePath,
        ETomlCode::FileNotFound,
        ETomlCode::AccessDenied,
        ETomlCode::FileTooLarge,
        ETomlCode::OpenFailed,
        ETomlCode::ReadFailed,
        ETomlCode::UnsupportedEncoding,
        ETomlCode::InvalidUtf8,
        ETomlCode::ParseFailed,
        ETomlCode::DuplicateKey,
        ETomlCode::NestingTooDeep,
        ETomlCode::CreateFolderFailed,
        ETomlCode::WriteFailed,
        ETomlCode::ReplaceFailed,
        ETomlCode::UnexpectedException,
        ETomlCode::NotFound,
        ETomlCode::AlreadyExists,
        ETomlCode::TypeMismatch,
        ETomlCode::InvalidValue,
        ETomlCode::ValueOutOfRange,
        ETomlCode::BackupFailed,
        ETomlCode::TempFileCleanupFailed,
        ETomlCode::KeyAdded,
        ETomlCode::KeyRenamed,
    };
}

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Types
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_Types::TTest_ASWToml_Types()
    : inherited("ASWToml_Types_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_Types::~TTest_ASWToml_Types()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Types::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Types::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Types::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Types::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Types, Message_DefaultsToEmptyInfo)
{
    // Act
    ASWToml::TASWTomlMessage const message;

    // Assert
    CheckEquals(ASWToml::ESeverity::Info, message.Severity, "the severity should be Info");
    CheckEquals(ASWToml::ETomlCode::Ok, message.Code, "the code should be Ok");
    CheckEmpty(message.Text, "the text should be empty");
    CheckEmpty(message.FilePath, "the file path should be empty");
    CheckEquals(0, static_cast<int>(message.Line), "the line should be 0");
    CheckEquals(0, static_cast<int>(message.Column), "the column should be 0");
    CheckEmpty(message.Path, "the path should be empty");
    CheckEquals(0, message.SystemError, "the system error should be 0");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Types, Result_DefaultsToOk)
{
    // Act
    ASWToml::TASWTomlResult const result;

    // Assert
    CheckEquals(ASWToml::ETomlCode::Ok, result.Code, "the code should be Ok");
    CheckTrue(result.IsOk(), "IsOk() should be true");
    CheckTrue(static_cast<bool>(result), "the result should convert to true");
    CheckEmpty(result.Message, "the message should be empty");
    CheckEmpty(result.FilePath, "the file path should be empty");
    CheckEquals(0, static_cast<int>(result.Line), "the line should be 0");
    CheckEquals(0, static_cast<int>(result.Column), "the column should be 0");
    CheckEmpty(result.Path, "the path should be empty");
    CheckEquals(0, result.SystemError, "the system error should be 0");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Types, Result_FailsForAnyOtherCode)
{
    for (ASWToml::ETomlCode const code : AllTomlCodes())
    {
        if (code == ASWToml::ETomlCode::Ok)
            continue;

        // Arrange
        ASWToml::TASWTomlResult result;
        result.Code = code;

        // Act & Assert
        std::string const name = ASWToml::ToString(code);
        CheckFalse(result.IsOk(), "IsOk() should be false for " + name);
        CheckFalse(static_cast<bool>(result), "the result should convert to false for " + name);
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Types, TomlCode_ValuesNeverChange)
{
    // Arrange
    // Applications may store or compare the values, so each code keeps its number and new codes are added at the
    // end, numbered in declaration order from 0
    TTomlCodeVector const codes = AllTomlCodes();

    // Act & Assert
    for (std::size_t i = 0; i < codes.size(); ++i)
    {
        CheckEquals(static_cast<int>(i), static_cast<int>(codes[i]),
            std::string("ETomlCode::") + ASWToml::ToString(codes[i]) + " should keep its value");
    }

    // A code after the last one listed means a new code is missing from AllTomlCodes()
    CheckEquals(std::string("Unknown"), std::string(ASWToml::ToString(static_cast<ASWToml::ETomlCode>(codes.size()))),
        "AllTomlCodes() should list every code");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Types, ToString_LineBreakNames)
{
    CheckEquals(std::string("None"), std::string(ASWToml::ToString(ASWToml::ELineBreak::None)), "None's name");
    CheckEquals(std::string("LF"), std::string(ASWToml::ToString(ASWToml::ELineBreak::LF)), "LF's name");
    CheckEquals(std::string("CRLF"), std::string(ASWToml::ToString(ASWToml::ELineBreak::CRLF)), "CRLF's name");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Types, ToString_MessageWithAllParts)
{
    // Arrange
    ASWToml::TASWTomlMessage message;
    message.Severity = ASWToml::ESeverity::Error;
    message.Code = ASWToml::ETomlCode::DuplicateKey;
    message.Text = "The key \"port\" is defined twice.";
    message.FilePath = "app.toml";
    message.Line = 4;
    message.Column = 1; // The first column is written too
    message.Path = "server.\"host name\".port";
    message.SystemError = 5;

    // Act
    std::string const text = ASWToml::ToString(message);

    // Assert
    // The path is written as it is, since it quotes its own parts
    CheckEquals(std::string("Error DuplicateKey: The key \"port\" is defined twice. (file \"app.toml\", line 4, "
        "column 1, path server.\"host name\".port, system error 5)"), text, "every part should be written, in order");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Types, ToString_MessageWithLineButNoColumn)
{
    // Arrange
    ASWToml::TASWTomlMessage message;
    message.Severity = ASWToml::ESeverity::Warning;
    message.Code = ASWToml::ETomlCode::BackupFailed;
    message.Text = "The backup couldn't be made.";
    message.Line = 7;

    // Act
    std::string const text = ASWToml::ToString(message);

    // Assert
    CheckEquals(std::string("Warning BackupFailed: The backup couldn't be made. (line 7)"), text,
        "a column of 0 should be left out");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Types, ToString_MessageWithTextOnly)
{
    // Arrange
    ASWToml::TASWTomlMessage message;
    message.Severity = ASWToml::ESeverity::Error;
    message.Code = ASWToml::ETomlCode::InvalidOptions;
    message.Text = "Reading.MaxFileSize is 0.";

    // Act
    std::string const text = ASWToml::ToString(message);

    // Assert
    CheckEquals(std::string("Error InvalidOptions: Reading.MaxFileSize is 0."), text,
        "the parts that aren't set should be left out, with no parentheses");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Types, ToString_SeverityNames)
{
    CheckEquals(std::string("Info"), std::string(ASWToml::ToString(ASWToml::ESeverity::Info)), "Info's name");
    CheckEquals(std::string("Warning"), std::string(ASWToml::ToString(ASWToml::ESeverity::Warning)),
        "Warning's name");
    CheckEquals(std::string("Error"), std::string(ASWToml::ToString(ASWToml::ESeverity::Error)), "Error's name");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Types, ToString_TomlCodeNamesAreUnique)
{
    // Arrange
    TNameSet names;

    for (ASWToml::ETomlCode const code : AllTomlCodes())
    {
        // Act
        std::string const name = ASWToml::ToString(code);

        // Assert
        CheckNotEquals(std::string("Unknown"), name, "code " + std::to_string(static_cast<int>(code)) +
            " should have a name");
        CheckTrue(names.insert(name).second, "the name \"" + name + "\" should be unique");
    }

    CheckEquals(std::string("FileNotFound"), std::string(ASWToml::ToString(ASWToml::ETomlCode::FileNotFound)),
        "the name should be the enumerator's");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Types, ToString_UnknownValues)
{
    // Arrange
    // Values that aren't enumerators, e.g. read from a newer version or cast from a corrupt number
    ASWToml::ETomlCode const code = static_cast<ASWToml::ETomlCode>(9999);
    ASWToml::ELineBreak const lineBreak = static_cast<ASWToml::ELineBreak>(3);
    ASWToml::ESeverity const severity = static_cast<ASWToml::ESeverity>(-1);

    // Act & Assert
    CheckEquals(std::string("Unknown"), std::string(ASWToml::ToString(code)), "an unknown code should be \"Unknown\"");
    CheckEquals(std::string("Unknown"), std::string(ASWToml::ToString(lineBreak)),
        "an unknown line break should be \"Unknown\"");
    CheckEquals(std::string("Unknown"), std::string(ASWToml::ToString(severity)),
        "an unknown severity should be \"Unknown\"");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWToml_Types)
