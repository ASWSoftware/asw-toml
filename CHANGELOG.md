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
  messages) and `TASWTomlResult` (an operation's outcome, with the file, line, column and path of
  the key or value an error is about, which converts to `true` when it succeeded; an edit of a
  document that fails gives the path it was given). A column counts characters (Unicode code
  points) from 1, and a path is written in TOML's dotted-key syntax (`server."host name".port`).
  `ToString()` names a code, a line break or a severity, and writes a message as one line of text.
- `TASWTomlOptions` (`ASWToml_Options.h`), the settings for TOML files in four groups:
  - `Reading` (`TASWTomlReadingOptions`): the largest file loaded (16 MiB) and how many tables,
    arrays and inline tables a value may be inside (100, and at most 128; a deeper value fails the
    load with `NestingTooDeep`, so a file can't overflow the stack).
  - `Writing` (`TASWTomlWritingOptions`): the comment prefix of new comment lines (`"# "`), the
    line ending (`ELineEnding`) and byte order mark (`EBomMode`), both as the loaded file had them
    by default, and a blank line before a new table (on).
  - `Saving` (`TASWTomlSavingOptions`): atomic saves (on), backups (off, `.bak`) and creating
    missing folders (on).
  - `Values` (`TASWTomlValueOptions`): how a string is written (`EStringStyle`: `Auto`, a literal
    string such as `'C:\Users\me'` when the text holds a backslash and a literal string can hold
    it, else a basic string, and for a string that replaces one, the form of that one; or always
    `Basic`; or `Literal` whenever it can hold the text) and the base of an integer
    (`EIntegerBase`: `Auto`, which is decimal, or for an integer that replaces one, the base of that
    one; `Decimal`, `Hexadecimal`, `Octal` or `Binary`; a negative number is always decimal). Every
    form is read, whichever these say.

  Each group's `Validate()` checks its own settings (e.g. the comment prefix must be `#` followed
  only by spaces and tabs, so a comment reads back as the text written after it), and
  `TASWTomlOptions::Validate()` checks the four; a problem is `ETomlCode::InvalidOptions` with a
  message naming the setting.
- The file layer that loading and saving will use (`ASWToml_File.cpp`, internal): whole files read
  and written in binary mode with the operating system's own file APIs, a size limit on reads, and
  saves that replace the file atomically (keeping its attributes on Windows, and its permissions
  and symbolic link elsewhere), with an optional backup and missing folders created. File paths
  are UTF-8: `ASWToml::PathToUtf8()` and `ASWToml::Utf8ToPath()` (`ASWToml_File.h`, its only
  public part) convert a `std::filesystem::path` to and from that form on every platform, e.g.
  `ASWToml::PathToUtf8(folder / "app.toml")` (on Windows through the path's wide form, since a
  narrow `std::filesystem::path` string is in the ANSI code page with MSVC and libc++; text that
  isn't valid UTF-8 or UTF-16 converts with U+FFFD instead of failing).
- `TASWTomlValue` (`ASWToml_Value.h`), a decoded TOML value of any type, detached from a document:
  a string, integer (`std::int64_t`), float (`double`), boolean, one of the four date and time
  types, an array (`TValueVector`, its values of any types) or a table (`TKeyValueVector` of
  `TASWTomlKeyValue`, in the order its keys were read or set). `ETomlType` names the type (with
  values that never change). A default value is an empty table. Nothing throws: `AsString()` and
  the others give `nullptr` for another type, and `Find()`, `Set()`, `Remove()` and `Append()` do
  nothing on a value that isn't a table or an array. `operator==` compares tables by their keys in
  any order, and floats as IEEE 754 numbers.
- The date and time types keep each part as written, with `std::chrono` parts: a local date is a
  `std::chrono::year_month_day`, `TASWTomlLocalTime` holds the hour, minute, second (60 for a
  leap second, at any hour) and fraction, `TASWTomlLocalDateTime` a date and a time, and
  `TASWTomlOffsetDateTime` a date, a time and an offset from UTC, so two offsets of the same
  instant stay apart. `IsValid()` checks the parts (a real date with a year from 0 to 9999, an
  offset within plus or minus 23:59). `ToDuration()`, `ToLocalTime()` and `ToSysTime()` convert
  to `std::chrono` durations and time points of any unit from nanoseconds to years (nanoseconds
  unless one is given, e.g. `ToSysTime<std::chrono::seconds>()`), rounded down to it, giving
  `std::nullopt` for a value that isn't valid or that the unit's count can't hold: nanoseconds
  count only from 1677-09-21T00:12:43.145224192 to 2262-04-11T23:47:16.854775807, and so does
  `std::chrono::system_clock::time_point` with libstdc++ (GCC, MinGW), while seconds hold every
  date and time TOML writes (use `std::chrono::sys_seconds` for a date such as 9999-12-31).
  `FromDuration()`, `FromLocalTime()` and `FromSysTime()` convert back from any of them;
  `FromLocalTime()` and `FromSysTime()` give `std::nullopt` outside the years 0 to 9999.
- The reading and writing of TOML's scalar values that the parser and the edits will use
  (`ASWToml_Scalars.cpp`, internal), as TOML 1.1.0 defines them. An error gives the line and column
  of the character at fault.
  - Strings in all four forms, with every escape (1.1's `\xHH` and `\e` included); a line break in
    a multi-line string reads as `\n` whether the file has LF or CRLF. A string is written as
    `Values.StringStyle` says, with only the escapes TOML 1.0 reads (`\u001B`, not `\e`); text that
    isn't valid UTF-8 isn't written (`InvalidValue`).
  - Keys (one part of a dotted key): read as a bare key or as a basic or literal string on one
    line, and written as a bare key when they can be one, else as a basic string.
  - Integers in decimal, hexadecimal (`0x`), octal (`0o`) and binary (`0b`), with `_` between
    digits; one outside the 64-bit range is `ValueOutOfRange`. Written in decimal, or in the base
    `Values.IntegerBase` asks for (hexadecimal digits in upper case).
  - Floats, `inf` and `nan` included; one beyond a double's range is `ValueOutOfRange`, and one
    below the smallest double reads as 0.0. Written as the shortest text that reads back as the same
    double, with `.0` added when it has neither a `.` nor an exponent (`1.0`, `0.1`, `1e+16`).
  - Booleans, `true` and `false`; another word (`True`, or a string without quotes) fails with a
    message saying so.
  - The four date and time types, with `T`, `t` or a space between the date and the time, the
    offset `Z`, `z`, `+HH:MM` or `-HH:MM`, the seconds optional (`:00` then), second 60 (a leap
    second) at any time, and a fraction of a second kept to nanoseconds (more digits are cut off,
    not rounded). A date that doesn't exist (`2023-02-29`) or a field out of its range is refused.
    Written with a `T` and the seconds always, a fraction only when it isn't 0 (with as few digits
    as it needs), and `Z` for offset 0, e.g. `1979-05-27T00:32:00.5-07:00`; a value that isn't
    valid isn't written (`InvalidValue`).
- `TASWTomlPath` (`ASWToml_Path.h`), the place of a table, a key or an element of an array in a
  document, from the root table, for the lookups and edits to come. It is made implicitly from a
  string in TOML's dotted-key syntax, with `[n]` for an element of an array
  (`server."host name".port`, `products[0].name`; the empty string is the root table), or from a
  list of parts, each a key or an index of any integer type (`{ "products", 0, "name" }`). A braced
  list is always a list of parts, so `{ "a.b" }` is the one key `a.b`. A path that can't be made
  (text that isn't a valid path, a negative index, a key that isn't valid UTF-8, or an index first)
  doesn't throw: `IsValid()` is false and `GetError()` says why (and at which column of the text).
  `Append()` adds a part, `GetParts()` gives them (`TASWTomlPathPart`), and `ToString()` writes the
  path as messages show it, each key bare when it can be, else a basic string, so it reads back as
  the same path.
- `TASWTomlDocument` (`ASWToml_Document.h`), a TOML text read into a model that keeps every byte of
  it: comments, blank lines, spacing, how each key is quoted, how each value is written, the layout
  of arrays and inline tables, and each line's own line break. `Parse(text, options)` reads TOML 1.1
  (UTF-8, with or without a byte order mark) and fails on the first error, changing nothing:
  `InvalidOptions`, `UnsupportedEncoding` (a UTF-16 or UTF-32 byte order mark), `InvalidUtf8`
  (wherever it is in the text), `ParseFailed`, `ValueOutOfRange`, `NestingTooDeep` or
  `DuplicateKey`, with the line and column of the error. `DuplicateKey` reports a break of TOML's
  table rules: a key or table defined twice, a header for a table that a header or dotted keys
  defined, dotted keys that add to a table a header defined, a key or table added to an inline table
  or an array value, and `[[x]]` for an `x` that isn't an array of tables; its message gives the
  path and the line of the first definition, e.g. `The table server is already defined on line 3.`
  `GetRoot()` gives the whole document as a `TASWTomlValue` table (each table's keys in the order
  they are first defined, however the table is written: by a header, by dotted keys or inline), and
  `Find(path)` the value at a `TASWTomlPath` (e.g. `products[1].name` or `ports[0]`), or `nullptr`
  when there is none. `ToText()` writes the text back byte for byte with the default options;
  `Writing.Bom` adds or removes the byte order mark, and `Writing.LineEnding` writes every line
  break as LF, CRLF or the platform's, in comments and multi-line strings too. `HasBom()`,
  `GetLineBreak()` (the text's most common line break, the platform's when it has none) and
  `GetOptions()` tell what was read. `Reset(options)` empties the document and gives it new
  options; `SetOptions(options)` gives it new options and reads its text again with them, changing
  nothing when the new reading options refuse the text (e.g. a lower `MaxNestingDepth`).
  `IsModified()` tells whether an edit changed the text; `Parse()`, `Reset()` and `ClearModified()`
  clear it. A document is copyable; it isn't thread-safe.
- Editing a `TASWTomlDocument`, which changes only the text an edit needs and keeps the rest byte
  for byte; new text uses only the syntax TOML 1.0 reads. `SetValue(path, value)` (and with
  `TASWTomlValueOptions` for the call) sets a value in place, keeping its key, spacing and
  end-of-line comment, and a scalar's form where it can (a hexadecimal mask stays hexadecimal, a
  literal or multi-line string stays one). A missing key goes where its table is written: after the
  table's last key line (copying its indentation), as a dotted key after the table's last dotted
  key, or inside an inline table's braces; a table that only longer headers made gets a header of
  its own. A table value becomes a `[path]` header with its keys, its sub-tables as headers and its
  arrays of tables as `[[path]]` headers, placed after its parent table and the tables under it,
  with a blank line before it when `Writing.BlankLineBeforeNewTable` is on; missing tables on the
  way are made as the place allows (a header, dotted keys, or an inline table).
  `Append(path, value)` adds an element to an array in its layout (on the same line, or on a line of
  its own when the elements are one per line) or a `[[path]]` element to an array of tables after
  its last element, and gives a missing key an array of the one value. `Remove(path)` removes a key,
  table, array of tables or element with the text that defines it, and returns `false` when there is
  nothing to remove: a key's line with its comment (the comment lines directly above it) and the
  comment at its end; an element of an array or an entry of an inline table with one comma (a
  trailing comma stays, and removing the last one leaves `[]` or `{}`), and with its line when it is
  on a line of its own; a table's header with its comment and lines, the tables under it and the
  dotted keys that define it; an element of an array of tables with the tables under it. Blank lines
  don't pile up where text is removed, and a table that only dotted keys or longer headers made goes
  with its last key, and an array of tables with its last element. `Rename(path, newKey)` gives a
  key, table or array of tables another name in its table, in its key line and in every header and
  dotted key that names it, each part bare when it can be (a part that was a literal string stays
  one when it can hold the name); renaming onto a key the table has fails with `AlreadyExists`.
  Setting a whole table or array over one that exists (the root included) merges them, so only what
  differs changes: keys are matched by name, those that stay keep their lines, comments and order,
  the others are removed or added; the elements of arrays and arrays of tables that are the same
  stay (the longest common subsequence of the old and new ones), the others between them are set in
  order, and the ones left over are removed or added in their places. An inline table stays inline,
  an array on several lines keeps one element per line with its comments, and an array of tables
  keeps its `[[...]]` blocks, so changing one server of a list of servers changes only its lines. A
  table that isn't inline (one with a header, of dotted keys, or made by longer headers), or an
  array of tables, set to a value of another kind is removed, and the value written as a new key of
  its table (`t = 5` for `[t]`). Setting a value identical to the one there changes nothing; every
  edit's text is read again, and an edit that wouldn't read back as meant changes nothing. A path
  that doesn't fit (through a value that isn't a table, an index past the end) fails with
  `InvalidArgument` or `NotFound`, a value that can't be written with `InvalidValue`, and one nested
  too deep with `NestingTooDeep`.
- Comments of a `TASWTomlDocument`, read and set by path. `GetComment(path)` gives the comment
  lines directly above a key's line or a table's header (for an array of tables, its first
  element's), and for the root path the file comment (the comment lines at the start of the file
  when a blank line follows them), each line without its `#` and the blanks around the rest, joined
  with `\n`. `GetTrailingComment(path)` gives the comment at the end of that line.
  `SetComment(path, text)` replaces the comment lines with one line per line of the text, written
  as `Writing.CommentPrefix` and the line with the indentation of the line below them (a new file
  comment gets a blank line under it), and an empty text removes them.
  `SetTrailingComment(path, text)` replaces the comment at the end of the line, or adds `  # text`
  there, and an empty text removes it. A text that reads the same as the comment there changes
  nothing. A table that only dotted keys or longer headers made and a value inside an array or
  inline table have no line of their own, so their comments can't be set (`InvalidArgument`); a
  comment that isn't valid UTF-8 or holds a control character other than a tab fails with
  `InvalidValue`.
- `TASWTomlConverter<T>` (`ASWToml_Converters.h`), which converts a value of an application's type
  to and from a `TASWTomlValue`: `FromValue(value, out, options)` and `ToValue(in, value,
  options)`, each returning a `TASWTomlResult` and leaving `out` or `value` as it was on failure. A
  value of a TOML type the C++ type doesn't read fails with `TypeMismatch`, a number outside the
  type's range with `ValueOutOfRange`, one it doesn't accept (or that TOML can't hold) with
  `InvalidValue`, each with a message that shows the value as TOML text, e.g.
  `The value 80 is an integer, not a string.`; a failure inside a value gives its place in `Path`.
  The concept `TomlConvertible<T>` says whether a type has a converter. Converters come for
  `TASWTomlValue` (as it is), `bool` (booleans only), every integer type but `bool` and the
  character types (`char`, `wchar_t`, `char8_t`, `char16_t`, `char32_t`; integers only, e.g.
  `The value 300 is out of range: it must be from 0 to 255.`, and an unsigned 64-bit value above
  `INT64_MAX` can't be written), `float` and `double` (floats, and integers they hold exactly; inf
  and nan keep their sign; a float beyond the range of `float` is `ValueOutOfRange` for it, and a
  `float` is written as the double nearest its shortest text, so `0.1f` is written as `0.1` and
  reads back the same), enums (by name when the application declares `ASWTomlEnumNames()` next to
  the enum, returning its `TASWTomlEnumName<T>`s: a string that is a name with the ASCII letters'
  case ignored, or the number of a value listed with a name, reads; the first name listed for a
  value is written, and a value without one can't be; an enum without names converts as an integer
  of its underlying type, so `std::byte` is an integer from 0 to 255), the TOML date and time types
  (as they are) and the `std::chrono` ones (`year_month_day`, `sys_days` and `local_days` as a local
  date; `sys_time<D>`, such as `system_clock::time_point` and `sys_seconds`, as an offset date-time,
  read as the instant at any offset and written in UTC; `local_time<D>` as a local date-time;
  `hh_mm_ss<D>` as a local time, 24:00:00, which 23:59:60 reads as, written as 23:59:60; each read
  rounded down to D; another TOML date or time type is `TypeMismatch`, and a value outside the years
  0 to 9999 or one D can't count is `ValueOutOfRange`: `system_clock` counts nanoseconds with
  libstdc++, so it holds only 1677 to 2262 there, while `sys_seconds` holds every instant TOML
  writes), `std::string` and `std::u8string` (strings only; text that isn't valid UTF-8 can't be
  written) and `std::filesystem::path` (a string, the path in UTF-8 on every platform, so a Windows
  path is written as `'C:\Users\me'`; a path that wouldn't read back the same can't be written), and
  `std::vector`, `std::array`, `std::set`, `std::unordered_set`, `std::pair` and `std::tuple` of
  types that have converters and default constructors, as arrays (an array of tables too; each
  element is read into a new `T{}`, and the first element that fails fails the container, with its
  index in `Path`, e.g. `[1]` or `[1][0]`; a `std::array`, pair or tuple reads only an array of its
  length; a set refuses an element read twice, and an unordered set is written with its elements
  sorted by their TOML text, so a file doesn't change from run to run; `std::vector<TASWTomlValue>`
  reads and writes an array of mixed types). An application adds its own by specializing the
  template in namespace `ASWToml`, for one type or, with a requires clause, for a group of types.

[Unreleased]: https://github.com/ASWSoftware/asw-toml/commits/develop
