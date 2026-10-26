/* **************************************************************************
Test_ASWToml_Converters_Fuzz.h
Author: Anthony S. West - ASW Software

The fuzz tests of ASWToml_Converters: random C++ values and random TOML
values through the converters of many types (see Test_ASWToml_Converters).

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#ifndef Test_ASWToml_Converters_FuzzH
#define Test_ASWToml_Converters_FuzzH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_ConverterFuzz
//
// The converters of many types with random C++ values and random TOML values, each from a generator whose seed a
// failure names
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_ConverterFuzz : public TAutoTestGroup<TTest_ASWToml_ConverterFuzz>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_ConverterFuzz> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_RandomTomlValues_ReadAsArraysAndTables();
    void Test_RandomTomlValues_ReadAsScalars();
    void Test_RandomValues_ArraysAndTables_RoundTrip();
    void Test_RandomValues_Scalars_RoundTrip();

public:
    TTest_ASWToml_ConverterFuzz();
    ~TTest_ASWToml_ConverterFuzz() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWToml_Converters_FuzzH
