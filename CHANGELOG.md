# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- The ASWToml version in `ASWToml_Version.h`, as macros (`ASWTOML_VERSION_MAJOR`, `_MINOR`,
  `_PATCH`, `_PRERELEASE` and `_STRING`) and as constants in the `ASWToml` namespace
  (`VersionMajor`, `VersionMinor`, `VersionPatch`, `VersionPreRelease` and `Version`), starting at
  `1.0.0-dev.1`.
- The `ASWToml::ASWToml` CMake target (a static library that needs C++20 and links to
  `Threads::Threads`), with install rules and a CMake package, so `find_package(ASWToml CONFIG)`
  finds an installed copy (and the threads library).
- `Deploy.bat`, which copies the library sources, build files and docs to a folder.

[Unreleased]: https://github.com/ASWSoftware/asw-toml/commits/develop
