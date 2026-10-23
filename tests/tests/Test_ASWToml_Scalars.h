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
#include <cstdint>
#include <source_location>
#include <string>
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------
#include "ASWToml_Options.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
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


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Integers
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_Integers : public TAutoTestGroup<TTest_ASWToml_Integers>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_Integers> inherited;

private:
    // Checks that ReadNumber() fails on 'text' (read from offset 0) with 'code' at 'offset', with a message that
    // contains 'expectedText', and leaves the value as it was
    void CheckReadFails(std::string const& text, ASWToml::ETomlCode code, std::size_t offset,
        std::string const& expectedText, std::string const& msg,
        std::source_location loc = std::source_location::current());

    // Checks that ReadNumber() reads all of 'text' (from offset 0) as the integer 'expected'
    void CheckReads(std::string const& text, std::int64_t expected, std::string const& msg,
        std::source_location loc = std::source_location::current());

    // Checks that ReadNumber() reads 'text' from 'start' as the integer 'expected', stopping at 'end'
    void CheckReadsUpTo(std::string const& text, std::size_t start, std::size_t end, std::int64_t expected,
        std::string const& msg, std::source_location loc = std::source_location::current());

    // Checks that WriteInteger() writes 'value' in 'base' as 'expected', and that it reads back as 'value'
    void CheckWrites(std::int64_t value, ASWToml::EIntegerBase base, std::string const& expected,
        std::string const& msg, std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_ReadNumber_DecimalIntegers();
    void Test_ReadNumber_PrefixedIntegers();
    void Test_ReadNumber_QuotesLongNumbersInMessages();
    void Test_ReadNumber_RejectsInvalidIntegers();
    void Test_ReadNumber_RejectsOutOfRangeIntegers();
    void Test_ReadNumber_RejectsTextThatIsNotANumber();
    void Test_ReadNumber_ReportsLineAndColumn();
    void Test_ReadNumber_StopsAtTheEndOfItsRun();
    void Test_WriteInteger_AppendsToText();
    void Test_WriteInteger_Bases();

public:
    TTest_ASWToml_Integers();
    ~TTest_ASWToml_Integers() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Floats
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_Floats : public TAutoTestGroup<TTest_ASWToml_Floats>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_Floats> inherited;

private:
    // Checks that ReadNumber() fails on 'text' (read from offset 0) with 'code' at 'offset', with a message that
    // contains 'expectedText', and leaves the value as it was
    void CheckReadFails(std::string const& text, ASWToml::ETomlCode code, std::size_t offset,
        std::string const& expectedText, std::string const& msg,
        std::source_location loc = std::source_location::current());

    // Checks that ReadNumber() reads all of 'text' (from offset 0) as a float with the bits of 'expected'
    void CheckReads(std::string const& text, double expected, std::string const& msg,
        std::source_location loc = std::source_location::current());

    // Checks that WriteFloat() writes 'value' as 'expected', and that it reads back with the same bits
    void CheckWrites(double value, std::string const& expected, std::string const& msg,
        std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_ReadNumber_Floats();
    void Test_ReadNumber_InfAndNan();
    void Test_ReadNumber_RejectsInvalidFloats();
    void Test_ReadNumber_RejectsOutOfRangeFloats();
    void Test_ReadNumber_UnderflowReadsAsZero();
    void Test_WriteFloat_InfAndNan();
    void Test_WriteFloat_ReadsBackEveryExponent();
    void Test_WriteFloat_ShortestText();

public:
    TTest_ASWToml_Floats();
    ~TTest_ASWToml_Floats() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Booleans
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_Booleans : public TAutoTestGroup<TTest_ASWToml_Booleans>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_Booleans> inherited;

private:
    // Checks that ReadScalar() reads 'text' from 'start' as the boolean 'expected', stopping at 'end'
    void CheckReadsUpTo(std::string const& text, std::size_t start, std::size_t end, bool expected,
        std::string const& msg, std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_ReadScalar_Booleans();
    void Test_ReadScalar_RejectsOtherWords();
    void Test_WriteBoolean_ReadsBack();

public:
    TTest_ASWToml_Booleans();
    ~TTest_ASWToml_Booleans() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DateTimeTexts
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_DateTimeTexts : public TAutoTestGroup<TTest_ASWToml_DateTimeTexts>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_DateTimeTexts> inherited;

private:
    // Checks that ReadDateTime() fails on 'text' (read from offset 0) with ParseFailed at 'offset', with a message
    // that contains 'expectedText', and leaves the value as it was
    void CheckReadFails(std::string const& text, std::size_t offset, std::string const& expectedText,
        std::string const& msg, std::source_location loc = std::source_location::current());

    // Checks that ReadDateTime() reads all of 'text' (from offset 0) as 'expected'
    void CheckReads(std::string const& text, ASWToml::TASWTomlValue const& expected, std::string const& msg,
        std::source_location loc = std::source_location::current());

    // Checks that ReadDateTime() reads 'text' from 'start' as 'expected', stopping at 'end'
    void CheckReadsUpTo(std::string const& text, std::size_t start, std::size_t end,
        ASWToml::TASWTomlValue const& expected, std::string const& msg,
        std::source_location loc = std::source_location::current());

    // Checks that the writer of the date or time 'value' fails with InvalidValue and a message that contains
    // 'expectedText', appending nothing
    void CheckWriteFails(ASWToml::TASWTomlValue const& value, std::string const& expectedText, std::string const& msg,
        std::source_location loc = std::source_location::current());

    // Checks that the writer of the date or time 'value' writes it as 'expected', and that it reads back as 'value'
    void CheckWrites(ASWToml::TASWTomlValue const& value, std::string const& expected, std::string const& msg,
        std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_ReadDateTime_FractionsOfASecond();
    void Test_ReadDateTime_LeapSeconds();
    void Test_ReadDateTime_LocalDates();
    void Test_ReadDateTime_LocalDateTimes();
    void Test_ReadDateTime_LocalTimes();
    void Test_ReadDateTime_OffsetDateTimes();
    void Test_ReadDateTime_RejectsInvalidDates();
    void Test_ReadDateTime_RejectsInvalidOffsets();
    void Test_ReadDateTime_RejectsInvalidTimes();
    void Test_ReadDateTime_RejectsTextThatIsNotADateOrTime();
    void Test_ReadDateTime_RejectsTrailingCharacters();
    void Test_ReadDateTime_ReportsLineAndColumn();
    void Test_ReadDateTime_StopsAtTheEndOfItsToken();
    void Test_WriteDateTimes_AppendToText();
    void Test_WriteDateTimes_RejectInvalidValues();
    void Test_WriteLocalDate_Dates();
    void Test_WriteLocalDateTime_DateTimes();
    void Test_WriteLocalTime_Times();
    void Test_WriteOffsetDateTime_Offsets();

public:
    TTest_ASWToml_DateTimeTexts();
    ~TTest_ASWToml_DateTimeTexts() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_ScalarKinds
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_ScalarKinds : public TAutoTestGroup<TTest_ASWToml_ScalarKinds>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_ScalarKinds> inherited;

private:
    // Checks that ReadScalar() fails on 'text' (read from offset 0) with 'code' at 'offset', with a message that
    // contains 'expectedText', and leaves the value as it was
    void CheckReadFails(std::string const& text, ASWToml::ETomlCode code, std::size_t offset,
        std::string const& expectedText, std::string const& msg,
        std::source_location loc = std::source_location::current());

    // Checks that ReadScalar() reads 'text' from 'start' as a value of type 'type' that writes as 'expected' (see
    // ScalarText()), stopping at 'end'
    void CheckReadsUpTo(std::string const& text, std::size_t start, std::size_t end, ASWToml::ETomlType type,
        std::string const& expected, std::string const& msg,
        std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_ReadScalar_ChoosesTheKind();
    void Test_ReadScalar_FailsAsTheKindsReader();
    void Test_ReadScalar_RejectsTextThatIsNotAScalar();
    void Test_ReadScalar_StartsAndStopsAtItsToken();

public:
    TTest_ASWToml_ScalarKinds();
    ~TTest_ASWToml_ScalarKinds() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWToml_ScalarsH
