/* **************************************************************************
Test_ASWToml_Version.h
Author: Anthony S. West - ASW Software

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#ifndef Test_ASWToml_VersionH
#define Test_ASWToml_VersionH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Version
/////////////////////////////////////////////////////////////////////////////
class TTest_ASWToml_Version : public TAutoTestGroup<TTest_ASWToml_Version>
{
private:
    typedef TAutoTestGroup<TTest_ASWToml_Version> inherited;

public: // Test methods, each defined with ASW_TEST
    void Test_Constants_MatchMacros();
    void Test_PreRelease_IsValidSemVer();
    void Test_VersionString_MatchesParts();

public:
    TTest_ASWToml_Version();
    ~TTest_ASWToml_Version() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWToml_VersionH
