/* **************************************************************************
Test_ASWToml_Converters.h
Author: Anthony S. West - ASW Software

The tests of ASWToml_Converters: the converters of text, booleans, numbers,
enums, dates and times. The tests of the other converters are in
Test_ASWToml_Converters_Containers (arrays and tables), _Structs and _Fuzz,
and the test types and helpers they share in UT_Helper_Converters.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#ifndef Test_ASWToml_ConvertersH
#define Test_ASWToml_ConvertersH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_BoolConverter
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_BoolConverter : public TAutoTestGroup<TTest_ASWToml_BoolConverter>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_BoolConverter> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_FromValue_ReadsBooleansOnly();
    void Test_RoundTrip_ThroughADocument();
    void Test_ToValue_ReplacesTheValue();

public:
    TTest_ASWToml_BoolConverter();
    ~TTest_ASWToml_BoolConverter() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_DateTimeConverters
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_DateTimeConverters : public TAutoTestGroup<TTest_ASWToml_DateTimeConverters>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_DateTimeConverters> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_Date_ReadsLocalDatesOnly();
    void Test_Date_WritesTomlsYearsOnly();
    void Test_Helpers_DateTimeOutOfRange();
    void Test_Helpers_TimeOfDayFromCount();
    void Test_LocalTime_ReadsLocalDateTimesOnly();
    void Test_LocalTime_ReadsWhatItsDurationCounts();
    void Test_RoundTrip_EveryType_ThroughADocument();
    void Test_SysTime_ReadsTheInstantAtAnyOffset();
    void Test_SysTime_ReadsWhatItsDurationCounts();
    void Test_SysTime_WritesInUtc();
    void Test_TimeOfDay_ReadsLocalTimesOnly();
    void Test_TimeOfDay_WritesUpToTheLeapSecond();
    void Test_TomlTypes_AsTheyAre();

public:
    TTest_ASWToml_DateTimeConverters();
    ~TTest_ASWToml_DateTimeConverters() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_EnumConverters
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_EnumConverters : public TAutoTestGroup<TTest_ASWToml_EnumConverters>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_EnumConverters> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_FromValue_IgnoresTheCaseOfLetters();
    void Test_FromValue_ReadsEveryName();
    void Test_FromValue_ReadsTheNumberOfAListedValue();
    void Test_FromValue_RefusesOtherValues();
    void Test_Messages_NameTheProblem();
    void Test_RoundTrip_EveryListedValue_ThroughADocument();
    void Test_ToValue_RefusesWhatCantBeReadBack();
    void Test_ToValue_WritesTheFirstName();
    void Test_WithoutNames_ConvertsAsTheUnderlyingType();
    void Test_WithoutNames_RefusesWhatTheUnderlyingTypeRefuses();

public:
    TTest_ASWToml_EnumConverters();
    ~TTest_ASWToml_EnumConverters() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_FloatConverters
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_FloatConverters : public TAutoTestGroup<TTest_ASWToml_FloatConverters>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_FloatConverters> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_Double_FromValue_ReadsFloatsAndExactIntegers();
    void Test_Double_RoundTrip_ThroughADocument();
    void Test_Float_FromValue_ReadsTheNearestFloat();
    void Test_Float_FromValue_RefusesWhatIsBeyondItsRange();
    void Test_Float_RandomValues_ReadBackTheSame();
    void Test_Float_RoundTrip_ThroughADocument();
    void Test_Float_ToValue_WritesTheShortestText();
    void Test_InfinityAndNan_KeepTheirSign();

public:
    TTest_ASWToml_FloatConverters();
    ~TTest_ASWToml_FloatConverters() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_IntegerConverters
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_IntegerConverters : public TAutoTestGroup<TTest_ASWToml_IntegerConverters>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_IntegerConverters> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_FromValue_ReadsEachTypesRange();
    void Test_FromValue_ReadsIntegersOnly();
    void Test_Messages_GiveTheRange();
    void Test_Messages_WriteTheNumbersInTheOptionsBase();
    void Test_RoundTrip_EveryType_ThroughADocument();
    void Test_ToValue_RefusesWhatATomlIntegerCantHold();

public:
    TTest_ASWToml_IntegerConverters();
    ~TTest_ASWToml_IntegerConverters() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};


/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_TextConverters
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_TextConverters : public TAutoTestGroup<TTest_ASWToml_TextConverters>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_TextConverters> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_ApplicationConverter_IsUsedAsTheLibrarys();
    void Test_Helpers_PathOfAPart();
    void Test_Helpers_TypeMismatch();
    void Test_Helpers_ValueText();
    void Test_Path_ReadsAndWritesUtf8();
    void Test_Path_RefusesWhatDoesntReadBack();
    void Test_String_ReadsStringsOnly();
    void Test_String_RoundTrip_ThroughADocument();
    void Test_String_WritesValidUtf8Only();
    void Test_U8String_AsTheStringsBytes();
    void Test_Value_AsItIs();

public:
    TTest_ASWToml_TextConverters();
    ~TTest_ASWToml_TextConverters() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWToml_ConvertersH
