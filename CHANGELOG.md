# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Batches: `TASWTomlEdits` (`ASWToml_Edits.h`) lists edits of a document (`SetValue()`, `Append()`,
  `Remove()`, `Rename()`, `SetComment()` and `SetTrailingComment()`, chained), and
  `TASWTomlDocument::Apply()` makes them as one: all of them, giving exactly the text the edits one
  after the other give, or none, failing with the result of the first edit that fails. Edits of
  different places are written together and the text is read once, so a batch costs about as much as
  one edit. A typed value is converted when it is added; one that doesn't convert fails the batch.
  In a batch, a `Remove()` of nothing changes nothing, and one of a path that isn't valid or of the
  root fails. A TOML object makes a batch with `IASWToml::Apply()`: `TASWBaseToml` makes it under
  one lock, so other threads see all of it or none, and reports a failure once. `IASWToml::Apply()`
  isn't pure virtual, so an application's own implementation of `IASWToml` still compiles; it gets a
  default that makes the edits one after the other through its operations and stops at the first
  that fails, keeping the edits before it. Settings join a batch with
  `TASWTomlSetting<T>::Set(edits, value)`, which checks and converts the value as `Set()` does (a
  value it refuses adds no edit, and is reported); the owner's `Apply()` then makes the edit as
  `Set()` would at its place in the batch (a value taken by parts set over the value there, a key it
  adds given the setting's comment), and any other `Apply()` refuses it with `InvalidArgument`.

### Changed

- `TASWTomlDocument::Find()`, and so every lookup through a TOML object (`GetValue()`,
  `TryGetValue()`, `Contains()`) and a setting's `Get()`, finds a key of a large table (more than 16
  keys) through the document's index, in O(log n) instead of comparing the table's keys one by one,
  so lookups in large tables are much faster; in a small table they are as fast as before. What a
  lookup finds is unchanged. `TASWTomlValue::Find()`, on a value of its own, still compares the keys
  one by one.

## [1.0.0] - 2026-10-26

The first release.

### Added

- The ASWToml version in `ASWToml_Version.h`, as macros (`ASWTOML_VERSION_MAJOR`, `_MINOR`,
  `_PATCH`, `_PRERELEASE` and `_STRING`) and as constants in the `ASWToml` namespace
  (`VersionMajor`, `VersionMinor`, `VersionPatch`, `VersionPreRelease` and `Version`).
- The `ASWToml::ASWToml` CMake target (a static library that needs C++20 and links to
  `Threads::Threads`), with install rules and a CMake package, so `find_package(ASWToml CONFIG)`
  finds an installed copy (and the threads library).
- `Deploy.bat`, which copies the library sources, build files, docs and example to a folder.
- The shared types in `ASWToml_Types.h`: `ETomlCode` (what happened, with values that never change),
  `ELineBreak` (`None`, `LF` or `CRLF`: the line break that ends a line), `ESeverity`,
  `TASWTomlMessage` (a warning or error for the application's log callback, with the file, line,
  column, path of the key or table, and system error it is about), `TMessageVector` (a list of
  messages), `TStringVector` (a list of names) and `TASWTomlResult` (an operation's outcome, with
  the file, line, column and path of the key or value an error is about, which converts to `true`
  when it succeeded; an edit of a document that fails gives the path it was given). A column counts
  characters (Unicode code points) from 1, and a path is written in TOML's dotted-key syntax
  (`server."host name".port`). `ToString()` names a code, a line break or a severity, and writes a
  message as one line of text.
- `TASWTomlOptions` (`ASWToml_Options.h`), the settings for TOML files in four groups:
  - `Reading` (`TASWTomlReadingOptions`): the largest file loaded (16 MiB) and how many tables,
    arrays and inline tables a value may be inside (100, and at most 128; a deeper value fails the
    load with `NestingTooDeep`, so a file can't overflow the stack).
  - `Writing` (`TASWTomlWritingOptions`): the comment prefix of new comment lines (`"# "`), the
    line ending (`ELineEnding`) and byte order mark (`EBomMode`), both as the loaded file had them
    by default, a blank line before a new table (on), and whose comments win when a settings class
    is applied to a loaded file (`ECommentPolicy`: `CodeWins`, the default, or `FileWins`).
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
- The file layer that loading and saving use (`ASWToml_File.cpp`, internal): whole files read
  and written in binary mode with the operating system's own file APIs, a size limit on reads, and
  saves that replace the file atomically (keeping its attributes on Windows, and its permissions and
  symbolic link elsewhere; a save that fails, even for lack of memory, leaves the file as it was and
  deletes its temporary file), with an optional backup and missing folders created. File paths are
  UTF-8: `ASWToml::PathToUtf8()` and `ASWToml::Utf8ToPath()` (`ASWToml_File.h`, its only public
  part) convert a `std::filesystem::path` to and from that form on every platform, e.g.
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
- The reading and writing of TOML's scalar values that the parser and the edits use
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
  document, from the root table, which the lookups and edits take. It is made implicitly from a
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
  comment gets a blank line under it), and an empty text removes them. `SetComments(comments)`
  sets several (a `TPathCommentVector` of `TASWTomlPathComment`, each a path and a text) in one
  edit, so the text is written and read again once however many there are; it refuses two of the
  same comment (`servers` and `servers[0]`) with `InvalidArgument`, and on any failure changes
  nothing. `SetTrailingComment(path, text)` replaces the comment at the end of the line, or adds
  `  # text` there, and an empty text removes it. A text that reads the same as the comment there
  changes nothing (and, for `SetComment()` and `SetComments()`, is found without writing the
  text). A table that only dotted keys or longer headers made and a value inside an array or
  inline table have no line of their own, so their comments can't be set (`InvalidArgument`); a
  comment that isn't valid UTF-8 or holds a control character other than a tab fails with
  `InvalidValue`.
- `TASWTomlConverter<T>` (`ASWToml_Converters.h`), which converts a value of an application's type
  to and from a `TASWTomlValue`: `FromValue(value, out, options)` and `ToValue(in, value, options)`,
  each returning a `TASWTomlResult` and leaving `out` or `value` as it was on failure. A value of a
  TOML type the C++ type doesn't read fails with `TypeMismatch`, a number outside the type's range
  with `ValueOutOfRange`, one it doesn't accept (or that TOML can't hold) with `InvalidValue`, each
  with a message that shows the value as TOML text, e.g.
  `The value 80 is an integer, not a string.`; a failure inside a value gives its place in `Path`.
  The concept `TomlConvertible<T>` says whether a type has a converter. Converters come for
  `TASWTomlValue` (as it is), `bool` (booleans only), every integer type but `bool` and the
  character types (`char`, `wchar_t`, `char8_t`, `char16_t`, `char32_t`; integers only, e.g.
  `The value 300 is out of range: it must be from 0 to 255.`, with the numbers in the options'
  integer base, so `0x12C` and `0xFF` in hexadecimal, and an unsigned 64-bit value above `INT64_MAX`
  can't be written), `float` and `double` (floats, and integers they hold exactly; inf and nan keep
  their sign; a float beyond the range of `float` is `ValueOutOfRange` for it, and a `float` is
  written as the double nearest its shortest text, so `0.1f` is written as `0.1` and reads back the
  same), enums (by name when the application declares `ASWTomlEnumNames()` next to the enum,
  returning its `TASWTomlEnumName<T>`s: a string that is a name with the ASCII letters' case
  ignored, or the number of a value listed with a name, reads; the first name listed for a value is
  written, and a value without one can't be; an enum without names converts as an integer of its
  underlying type, so `std::byte` is an integer from 0 to 255), the TOML date and time types (as
  they are) and the `std::chrono` ones (`year_month_day`, `sys_days` and `local_days` as a local
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
  reads and writes an array of mixed types), and `std::map` and `std::unordered_map` of them, as
  tables, with `std::string`, integer or enum keys (an integer key is its decimal digits, `0` or
  digits that don't start with 0 after a `-`, so `01` and `+1` are `InvalidValue`, in the whole
  range of its type, an unsigned 64-bit one's too; an enum key is one of its names, the case
  ignored, or for an enum without names its number; each value is read into a new `T{}`, and the
  first key or value that fails fails the map, with the key in `Path`, e.g. `port` or
  `"host name"[0]`; a key that reads as one the map already has, such as `Red` and `red`, is
  `InvalidValue`; a map is written in its order, and an unordered map with its keys sorted), and
  structs, as tables, through a list of their fields that the application declares next to the
  struct (`ASWTomlFields()`, found by argument-dependent lookup, returning a `std::tuple` of
  `Field("port", &TServer::Port)` and `OptionalField(...)`; a member of a base class and, through a
  friend, a private member can be a field; a struct is read into a copy of `out`, so a key an
  optional field lacks keeps what `out` held, a `std::optional` member is `std::nullopt` when its
  key is missing and isn't written then, a missing key of any other field is `NotFound` ("The table
  has no key port."), and keys the list doesn't name are ignored; it is written with every field in
  the list's order, and a key named twice in the list is `InvalidValue`; so a `std::vector` of a
  struct is an array of tables). An application adds its own by specializing the template in
  namespace `ASWToml`, for one type or, with a requires clause, for a group of types.
- `IASWToml` (`ASWToml_Interface.h`), the interface of a thread-safe TOML object (which
  `TASWBaseToml` implements), so code that only reads and writes values can take an
  `IASWToml&`. Its operations by path are each complete in itself and give out copies, never
  pointers into the document: `Contains()`, `GetKeyNames()` (a `TStringVector`), `GetComment()`,
  `GetTrailingComment()`, `Remove()`, `Rename()`, `SetComment()`, `SetTrailingComment()`,
  `GetDocument()`, `LoadText()`, `ToText()`, `Load()`, `Save()`, `SaveAs()`, `IsModified()`,
  `GetFilePath()`, `GetOptions()`, `SetOptions()` and `SetLogCallback()` (a `TASWTomlLogCallback`).
  Typed values are built on them with `TASWTomlConverter<T>`: `GetValue(path, defaultValue)` gives
  the default when there is no value at the path, or when it doesn't convert, which is reported as
  a Warning, so a setting reads in one line; `TryGetValue(path, value)` fails with `NotFound` or the
  converter's code, and isn't reported; `SetValue(path, value)` sets a value (merging a table or
  array that is there) and `Append(path, value)` adds one to an array (a struct is a new `[[path]]`
  element of an array of tables). Each takes value options too, and text (a string literal, a char
  pointer or a `std::string_view`) is read and written as a TOML string. A failure's `Path` is the
  place in the document that failed, e.g. `servers[1].port`, and every failed operation but
  `TryGetValue()` is reported to the log callback as an Error (a path that isn't valid for
  `GetValue()` too).
- `TASWBaseToml` (`ASWToml_Base.h`), the thread-safe TOML object that implements `IASWToml`, which
  an application's settings class derives from: a `TASWTomlDocument` with the default options until
  `SetOptions()`, `LoadText()` or `Load()`, whose operations any thread may call at any time, each
  complete in itself (a lookup sees the document before or after an edit or a load, never during
  one). Lookups share a reader/writer lock and edits take it alone; `Load()` reads and parses the
  file (and `LoadText()` the text) without it, and `Save()` writes the file without it, so lookups
  and edits go on meanwhile. `Load(path)` makes the file the object's file path, which `Save()`
  writes, and `SaveAs(path)` writes another file and makes it the file path; a save writes the
  document even when it isn't modified, atomically by default, and clears the modified flag unless
  an edit succeeded while the file was being written. A failed load, save, edit or `SetOptions()`
  changes nothing, a load of a file with a syntax error included: the file path stays, so `Save()`
  doesn't write over that file unless it already was the file path. Only a missing file is
  different: `Load()` fails with `FileNotFound`, reported as a Warning, but empties the document and
  makes the file the file path, so an application's first run creates it with `Save()`. Every
  operation that returns a `TASWTomlResult` catches what is thrown (`std::bad_alloc`, or what an
  application's converter throws) and returns it as `UnexpectedException`. Failures and the warnings
  of a save (`BackupFailed`, `TempFileCleanupFailed`) are reported to the log callback
  (`SetLogCallback()`) after the locks are released, each with the file it is about, so the callback
  may call the object, even `Load()`, `Save()` or `SetLogCallback()`, and an exception the callback
  throws is dropped. Any thread may replace the callback at any time; a report already under way may
  still go to the callback it replaced. The files are read and written by the protected virtual
  `ReadFile()` and `WriteFile()`, which a derived class may override.
- The settings binding (`ASWToml_Settings.h`), so an application declares each setting once, as a
  member of its settings class (derived from `TASWBaseToml`): a `TASWTomlTable` (its owner, its path
  from the root, e.g. `"logging"`, and a comment) and, for each key of it, a `TASWTomlSetting<T>`
  with its key, its type (any type `TASWTomlConverter` converts, a struct or a `std::vector` of one
  included), its default value, a comment and `TASWTomlSettingOptions<T>`, given with designated
  initializers: a range (`{ .Min = 0, .Max = 4 }`, only for a type that `operator<` compares), a
  validator that says what is wrong with a value, whether the setting is optional, earlier names of
  its key, and value options of its own. A setting holds no value: `Get()` reads the owner's
  document, giving the default for a missing key and, with a Warning, for a value that doesn't
  convert, is out of range or that the validator refuses; `Set()` writes it, adding a missing key
  with the setting's comment above it in the same edit, and fails (an Error, nothing changed) for a
  value that is out of range, refused or can't be written; `Reset()` sets the default and
  `IsPresent()` says whether the key is there. The messages show a setting's values (the one read or
  refused, its default, its range) as the setting writes them, e.g. `0x3366CC` for an integer
  written in hexadecimal. `Load()` and `LoadText()` apply the settings to the document they read
  before anyone sees it, so it holds every setting with a valid value and `IsModified()` says
  whether it needs saving: a missing key whose earlier name (`OldKeys`) its table has is renamed
  from it, keeping its line or header, value and comment (an Info, `KeyRenamed`), and then applied
  as any other; any other missing key is added with its default value and comment (an Info,
  `KeyAdded`), unless the setting is optional; a value that isn't valid is replaced by the default
  (a Warning), a valid one left as written; a table of settings the file holds as another kind of
  value is replaced by a table of them (a Warning); the comments of the settings, of the tables and
  the file comment (the root table's) are set as `Writing.CommentPolicy` says, where they have a
  line of their own. A struct, a `std::vector` or a map is taken by parts: `Load()` and `Get()`
  replace only the parts that aren't valid by their defaults (a Warning each, with the part's path,
  e.g. `servers[1].port`), `Load()` adds the missing fields that aren't optional (an Info each), and
  the keys the struct doesn't know stay; `Set()` keeps them too, pairing a vector's elements with
  the file's by what the struct knows of them, so removing a server removes its block and the others
  keep their keys and comments (`IASWToml::SetValue()` sets a value as it is). A file that needs
  nothing stays byte for byte, and a first run (a missing file) gets everything. The values are
  written with one edit per table and the comments with one more, so applying 200 settings to an
  empty file takes a few milliseconds. Any thread may call `Get()` and `Set()`, as the object's
  other operations: a load applies the settings before another thread sees the document, and `Set()`
  adds a key with its comment in one edit, so no thread sees a setting half applied. A table or
  setting is bound to its object from its constructor to its destructor (a destructor waits for a
  load that is applying it) and must not outlive it; converters and validators run with the object's
  file lock held while a load applies them, so they must not call its `Load()`, `LoadText()`,
  `Save()`, `SaveAs()` or `SetOptions()`. A load that fails, even for lack of memory, changes
  nothing and reports none of what applying the settings did.
- An example program, `example/src/main.cpp`: an application's settings class (with a range, a
  validator, an enum written by name, an integer written in hexadecimal, a date, an optional
  setting, an old key, a struct as a table and a `std::vector` of a struct as an array of tables),
  its first run, a file a user edited by hand (repaired, a key renamed, new fields added, the
  user's own keys and tables kept), a setting used from two threads, values the class doesn't
  declare, by path, and a `TASWTomlDocument` on its own. It prints each step, the messages the
  library reports and the file, and returns 0 when every step did what it shows. Build it with
  `example/cmake`, with the root project and `-DASWTOML_BUILD_EXAMPLE=ON`, or with RAD Studio's
  `example/rad370/ASWTomlExample.cbproj` (Win64x).
- The documentation, in `docs/`: `Format.md` (the TOML ASWToml reads and writes, what an edit
  changes and how each new value is written), `Types.md` (the C++ types it converts, and a
  converter of your own), `Options.md` (every option, its default and what it does), `Settings.md`
  (a guide to settings classes, with a log callback that sends the messages to ASWLog) and
  `Guarantees.md` (errors, threads, callbacks, lifetimes, nesting and stack, saving and crashes,
  and every `ETomlCode`). The README has a quick start, an API overview and links to them.

[Unreleased]: https://github.com/ASWSoftware/asw-toml/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/ASWSoftware/asw-toml/releases/tag/v1.0.0
