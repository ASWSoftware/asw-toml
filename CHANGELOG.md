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
- `TASWTomlOptions` (`ASWToml_Options.h`), the settings for TOML files in three groups:
  - `Reading` (`TASWTomlReadingOptions`): the largest file loaded (16 MiB) and how many arrays and
    inline tables a value may have inside one another (100; a deeper value fails the load with
    `NestingTooDeep`).
  - `Writing` (`TASWTomlWritingOptions`): the comment prefix of new comment lines (`"# "`), the
    line ending (`ELineEnding`) and byte order mark (`EBomMode`), both as the loaded file had them
    by default, and a blank line before a new table (on).
  - `Saving` (`TASWTomlSavingOptions`): atomic saves (on), backups (off, `.bak`) and creating
    missing folders (on).

  Each group's `Validate()` checks its own settings (e.g. the comment prefix must start with `#`
  and hold only what a TOML comment may), and `TASWTomlOptions::Validate()` checks the three; a
  problem is `ETomlCode::InvalidOptions` with a message naming the setting.
- The file layer that loading and saving will use (`ASWToml_File.cpp`, internal): whole files read
  and written in binary mode with the operating system's own file APIs, a size limit on reads, and
  saves that replace the file atomically (keeping its attributes on Windows, and its permissions
  and symbolic link elsewhere), with an optional backup and missing folders created. File paths
  are UTF-8: `ASWToml::PathToUtf8()` and `ASWToml::Utf8ToPath()` (`ASWToml_File.h`, its only
  public part) convert a `std::filesystem::path` to and from that form on every platform, e.g.
  `ASWToml::PathToUtf8(folder / "app.toml")` (on Windows through the path's wide form, since a
  narrow `std::filesystem::path` string is in the ANSI code page with MSVC and libc++; text that
  isn't valid UTF-8 or UTF-16 converts with U+FFFD instead of failing).

[Unreleased]: https://github.com/ASWSoftware/asw-toml/commits/develop
