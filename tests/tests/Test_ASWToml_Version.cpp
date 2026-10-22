/* **************************************************************************
Test_ASWToml_Version.cpp
Author: Anthony S. West - ASW Software

See header for info.

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
// Module header
#include "Test_ASWToml_Version.h"
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include <string>
//---------------------------------------------------------------------------
#include "ASWToml_Version.h"
//---------------------------------------------------------------------------

// The version macros must be usable in #if, so code can check which version it is built against
#if ASWTOML_VERSION_MAJOR < 0 || ASWTOML_VERSION_MINOR < 0 || ASWTOML_VERSION_PATCH < 0
#error "The ASWToml version macros must be usable in #if"
#endif

// The constants must be usable at compile time
static_assert(ASWToml::VersionMajor == ASWTOML_VERSION_MAJOR, "VersionMajor should be ASWTOML_VERSION_MAJOR");
static_assert(ASWToml::VersionMinor == ASWTOML_VERSION_MINOR, "VersionMinor should be ASWTOML_VERSION_MINOR");
static_assert(ASWToml::VersionPatch == ASWTOML_VERSION_PATCH, "VersionPatch should be ASWTOML_VERSION_PATCH");

// The checks take their function and line from std::source_location, which every C++20 toolchain the tests are
// built with has (the framework enables it from __cpp_lib_source_location)
#if !defined(ASWUNITTESTS_SOURCE_LOCATION_ENABLED)
#error "The ASWToml unit tests need std::source_location (C++20)"
#endif

namespace ASWUnitTests
{

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Version
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_Version::TTest_ASWToml_Version()
    : inherited("ASWToml_Version_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_Version::~TTest_ASWToml_Version()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Version::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Version::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Version::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Version::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Version, Constants_MatchMacros)
{
    // Arrange
    std::string const expectedPreRelease = ASWTOML_VERSION_PRERELEASE;
    std::string const expectedVersion = ASWTOML_VERSION_STRING;

    // Act
    std::string const preRelease = ASWToml::VersionPreRelease;
    std::string const version = ASWToml::Version;

    // Assert
    CheckEquals(expectedPreRelease, preRelease, "VersionPreRelease should be ASWTOML_VERSION_PRERELEASE");
    CheckEquals(expectedVersion, version, "Version should be ASWTOML_VERSION_STRING");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Version, PreRelease_IsValidSemVer)
{
    // Arrange: a pre-release is empty (a release) or dot-separated identifiers, e.g. "dev.1". An identifier is
    // [0-9A-Za-z-], not empty, and has no leading zero if it is all digits (SemVer 2.0.0).
    std::string const identifier = "(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)";

    // Act
    std::string const preRelease = ASWToml::VersionPreRelease;

    // Assert
    CheckMatches(preRelease, "(?:" + identifier + "(?:\\." + identifier + ")*)?",
        "ASWTOML_VERSION_PRERELEASE should be empty or SemVer pre-release identifiers");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Version, VersionString_MatchesParts)
{
    // Arrange
    std::string const preRelease = ASWToml::VersionPreRelease;
    std::string expected = std::to_string(ASWToml::VersionMajor) + "." + std::to_string(ASWToml::VersionMinor) + "." +
        std::to_string(ASWToml::VersionPatch);
    if (!preRelease.empty())
        expected += "-" + preRelease;

    // Act
    std::string const version = ASWToml::Version;

    // Assert
    CheckEquals(expected, version,
        "ASWTOML_VERSION_STRING should be MAJOR.MINOR.PATCH, plus -PRERELEASE if there is one");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWToml_Version)
