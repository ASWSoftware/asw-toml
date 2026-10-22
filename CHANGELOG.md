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
- The shared types in `ASWToml_Types.h`: `ETomlCode` (what happened, with values that never change),
  `ELineBreak` (`None`, `LF` or `CRLF`: the line break that ends a line), `ESeverity`,
  `TASWTomlMessage` (a warning or error for the application's log callback, with the file, line,
  column, path of the key or table, and system error it is about), `TMessageVector` (a list of
  messages) and `TASWTomlResult` (an operation's outcome, with the file, line and column of an
  error, which converts to `true` when it succeeded). A column counts characters (Unicode code
  points) from 1, and a path is written in TOML's dotted-key syntax (`server."host name".port`).
  `ToString()` names a code, a line break or a severity, and writes a message as one line of text.

[Unreleased]: https://github.com/ASWSoftware/asw-toml/commits/develop
