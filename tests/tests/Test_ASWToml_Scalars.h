/* **************************************************************************
Test_ASWToml_Scalars.h
Author: Anthony S. West - ASW Software

Copyright 2026 ASW Software

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

************************************************************************** */

//---------------------------------------------------------------------------
#ifndef Test_ASWToml_ScalarsH
#define Test_ASWToml_ScalarsH
//---------------------------------------------------------------------------
#include <cstddef>
#include <source_location>
#include <string>
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------
#include "ASWToml_Options.h"
#include "ASWToml_Types.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Strings
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_Strings : public TAutoTestGroup<TTest_ASWToml_Strings>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_Strings> inherited;

private:
    // Checks that ReadString() fails on 'text' (read from offset 0) with 'code' at 'offset', with a message that
    // contains 'expectedText', and leaves the value as it was
    void CheckReadFails(std::string const& text, ASWToml::ETomlCode code, std::size_t offset,
        std::string const& expectedText, std::string const& msg,
        std::source_location loc = std::source_location::current());

    // Checks that ReadString() reads all of 'text' (from offset 0) as 'expected'
    void CheckReads(std::string const& text, std::string const& expected, std::string const& msg,
        std::source_location loc = std::source_location::current());

    // Checks that ReadString() reads 'text' (from offset 0) as 'expected', stopping at 'end'
    void CheckReadsUpTo(std::string const& text, std::size_t end, std::string const& expected, std::string const& msg,
        std::source_location loc = std::source_location::current());

    // Checks that WriteString() writes 'value' with 'style' as 'expected', and that it reads back as 'value'
    void CheckWrites(std::string const& value, ASWToml::EStringStyle style, std::string const& expected,
        std::string const& msg, std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_ReadString_BasicStrings();
    void Test_ReadString_Escapes();
    void Test_ReadString_LiteralStrings();
    void Test_ReadString_MultiLineBasicStrings();
    void Test_ReadString_MultiLineLiteralStrings();
    void Test_ReadString_RejectsControlCharacters();
    void Test_ReadString_RejectsInvalidEscapes();
    void Test_ReadString_RejectsInvalidUtf8();
    void Test_ReadString_RejectsLineEndingBackslashWithText();
    void Test_ReadString_RejectsTextThatIsNotAString();
    void Test_ReadString_RejectsUnclosedStrings();
    void Test_ReadString_ReportsLineAndColumn();
    void Test_ReadString_StartsAndStopsAtItsToken();
    void Test_WriteString_AppendsToText();
    void Test_WriteString_AutoStyle();
    void Test_WriteString_BasicStyle();
    void Test_WriteString_LiteralStyle();
    void Test_WriteString_ReadsBackEveryCharacter();
    void Test_WriteString_RejectsInvalidUtf8();

public:
    TTest_ASWToml_Strings();
    ~TTest_ASWToml_Strings() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Keys
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_Keys : public TAutoTestGroup<TTest_ASWToml_Keys>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_Keys> inherited;

private:
    // Checks that WriteKey() writes 'key' as 'expected'
    void CheckWrites(std::string const& key, std::string const& expected, std::string const& msg,
        std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_WriteKey_BareKeys();
    void Test_WriteKey_QuotedKeys();
    void Test_WriteKey_RejectsInvalidUtf8();

public:
    TTest_ASWToml_Keys();
    ~TTest_ASWToml_Keys() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWToml_ScalarsH
