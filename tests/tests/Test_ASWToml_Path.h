/* **************************************************************************
Test_ASWToml_Path.h
Author: Anthony S. West - ASW Software

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#ifndef Test_ASWToml_PathH
#define Test_ASWToml_PathH
//---------------------------------------------------------------------------
#include <cstddef>
#include <source_location>
#include <string>
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------
#include "ASWToml_Path.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Path
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_Path : public TAutoTestGroup<TTest_ASWToml_Path>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_Path> inherited;

private:
    // Checks that 'path' is valid and has the parts 'expected'
    void CheckParts(ASWToml::TASWTomlPath const& path, ASWToml::TASWTomlPath::TPartVector const& expected,
        std::string const& msg, std::source_location loc = std::source_location::current());

    // Checks that the path made from 'text' isn't valid, with no parts, and that its error says so at 'column' and
    // then starts with 'reason'
    void CheckRejects(std::string const& text, std::size_t column, std::string const& reason, std::string const& msg,
        std::source_location loc = std::source_location::current());

public: // Test methods, each defined with ASW_TEST
    void Test_Append_AddsParts();
    void Test_Append_KeepsFirstError();
    void Test_Default_IsRoot();
    void Test_Equality_ComparesParts();
    void Test_FromList_MakesParts();
    void Test_FromList_RejectsInvalidParts();
    void Test_FromString_Blanks();
    void Test_FromString_EmptyIsRoot();
    void Test_FromString_Indexes();
    void Test_FromString_Keys();
    void Test_FromString_RejectsInvalidPaths();
    void Test_FromString_RejectsInvalidUtf8();
    void Test_Part_KeysAndIndexes();
    void Test_ToString_ReadsBackRandomPaths();
    void Test_ToString_WritesTomlSyntax();

public:
    TTest_ASWToml_Path();
    ~TTest_ASWToml_Path() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWToml_PathH
