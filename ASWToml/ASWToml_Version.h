/* **************************************************************************
ASWToml_Version.h
Author: Anthony S. West - ASW Software

A TOML file reader/writer library.

The ASWToml version (https://semver.org), as macros and constants: the only
place where the version is set.

Requires C++ 20 or higher.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

#pragma once

#ifndef ASWToml_VersionH
#define ASWToml_VersionH
//---------------------------------------------------------------------------

// The version, as macros so code can check it with #if (e.g. to leave out a call to a method an older version doesn't
// have). A release has an empty pre-release; between releases, the develop branch has the next planned version with a
// pre-release such as "dev.1", which comes before that release in version order (e.g. 1.1.0-dev.1 < 1.1.0).
// ASWTOML_VERSION_STRING is "MAJOR.MINOR.PATCH", followed by "-PRERELEASE" if the pre-release isn't empty; change
// them together (a unit test checks that they match).
#define ASWTOML_VERSION_MAJOR 1
#define ASWTOML_VERSION_MINOR 1
#define ASWTOML_VERSION_PATCH 0
#define ASWTOML_VERSION_PRERELEASE "dev.2"
#define ASWTOML_VERSION_STRING "1.1.0-dev.2"

namespace ASWToml
{

// The same version, for C++ code
inline constexpr unsigned int VersionMajor = ASWTOML_VERSION_MAJOR;
inline constexpr unsigned int VersionMinor = ASWTOML_VERSION_MINOR;
inline constexpr unsigned int VersionPatch = ASWTOML_VERSION_PATCH;
inline constexpr char VersionPreRelease[] = ASWTOML_VERSION_PRERELEASE;
inline constexpr char Version[] = ASWTOML_VERSION_STRING;

} // namespace ASWToml

//---------------------------------------------------------------------------
#endif // #ifndef ASWToml_VersionH
