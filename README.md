# ASWToml - https://github.com/ASWSoftware/asw-toml

ASWToml is a thread-safe, dependency-free C++20 library for reading and writing TOML config files on
Windows and Linux, which keeps the file's comments and layout, and lets an application declare its
settings in one place.

## Design

ASWToml is a folder of standard C++20 sources, or one CMake target, with no third-party code. It
builds with RAD Studio (bcc64x, Win64x), MSVC, MinGW, GCC and Clang. For INI files, or for C++17 and
RAD Studio's 32-bit compiler, see its sibling [ASWIni](https://github.com/ASWSoftware/asw-ini),
whose design it follows. Its goals:

- **Nothing in the file is lost.** A file that is loaded and saved without changes is written back
  byte for byte: comments, blank lines, spacing, the order of tables and keys, how each key is
  quoted and how each string and number is written. Changing a value rewrites only that value.
- **Settings in one place.** An application declares each setting once, with its table, key, type,
  default value, comment and valid range. Loading brings the file up to date (it adds missing keys
  with their comments, renames keys from their earlier names, and replaces values that aren't valid
  with the defaults, saying why; a struct or a list of them, such as the servers of an array of
  tables, field by field, keeping what the application doesn't know), and each setting reads and
  writes its value in the file's document, from any thread.
- **Same file on every system.** Files are read and written in binary mode, so the line ending
  (LF or CRLF) is chosen by the library, not by the operating system.
- **Safe to use from several threads**, and saving replaces the file atomically, so a crash while
  saving leaves the previous file intact.
- **Problems are reported, not thrown.** Warnings and errors (a file that isn't valid TOML, an
  invalid value, a file that can't be written) go to a log callback, so the application logs them
  its own way.

## Features

- Settings declared once, as members of an application's settings class (`ASWToml::TASWTomlTable`
  and `ASWToml::TASWTomlSetting<T>`), each with its table, key, type, default value and comment,
  and optionally a range, a validator, value options of its own (e.g. an integer written in
  hexadecimal) and the earlier names of a renamed key. A struct is a table, and a `std::vector` of
  one an array of tables (`[[servers]]`).
- Loading brings the file up to date: a missing key is added with its default value and comment, a
  value that isn't valid is replaced by the default, an old key is renamed, and a struct or an
  array of tables is repaired field by field, keeping the user's own keys; each change is reported,
  so `IsModified()` says whether the file needs saving.
- TOML 1.1.0 read, and so every TOML 1.0 file; the text the library writes uses only syntax that
  TOML 1.0 also reads. Checked against every file of the official
  [toml-test](https://github.com/toml-lang/toml-test) suite's TOML 1.1 list, by the unit tests and
  by toml-test's own runner. A text that isn't valid TOML fails to load at its first error, with
  its line and column, and nothing changes.
- A file that is loaded and saved without changes is written back byte for byte. An edit changes
  only the text it needs, in the form the file already uses: a value in place, in its own form
  (`0xFF` stays hexadecimal); a new key after the last key of its table, as a dotted key, or inside
  the braces of an inline table; a new table as a `[header]`; a whole table or array merged, so
  only what differs changes.
- Several changes as one: a batch (`TASWTomlEdits`) of settings and edits by path, which `Apply()`
  makes all at once or not at all, under one lock, at about the cost of one edit, writing exactly
  the text its edits one after the other write.
- Tables however they are written (`[header]`, dotted keys, inline tables) and arrays of tables,
  addressed by a path in TOML's dotted-key syntax (`server."host name".port`, `products[0].name`)
  or as a list of parts. Keys compare byte for byte, as TOML says, and a key of a large table is
  found through the document's index. The file's, a table's and a key's comments, and end-of-line
  comments.
- UTF-8, with or without a byte order mark (kept as the file had it). LF and CRLF line endings,
  kept even when mixed; a new line gets the file's own (the platform's for a new file), or one
  chosen. A size limit on loads and a nesting limit that keeps a file from overflowing the stack.
- Typed values, the same in any locale: strings, file paths (UTF-8 on every platform), booleans,
  every integer type with range checks, `float` and `double` (the shortest text that reads back the
  same), TOML's four date and time types and the `std::chrono` ones, enums by name, `std::vector`,
  `std::array`, sets, pairs and tuples, maps with string, integer or enum keys, and structs, plus
  your own types (`TASWTomlConverter<T>`).
- Values by path too (`GetValue()`, `TryGetValue()`, `SetValue()` and `Append()`), e.g. for tables
  a settings class doesn't declare, and `ASWToml::TASWTomlDocument`, the document on its own, for
  TOML text without a file or a lock.
- JSON export (`ToJson()`) of a document or any value, for logs, diagnostics and other tools: plain
  JSON, or toml-test's tagged form, which keeps each value's TOML type.
- Safe to use from any number of threads with no locking by the application. Loading and saving
  read and write the file without holding the lock that lookups and edits use.
- Atomic saves (a crash while saving leaves the previous file whole), an optional backup, missing
  folders created, and UTF-8 file paths on Windows and Linux (`ASWToml::PathToUtf8()` and
  `Utf8ToPath()` convert a `std::filesystem::path`).
- No exceptions from loading, saving or editing: each returns a `TASWTomlResult`, and warnings and
  errors (with the file, line, column and path) go to the application's log callback.
  [docs/Settings.md](docs/Settings.md) has a callback that sends them to ASWLog.
- A CMake target and package, and an example program with CMake and RAD Studio (Win64x) projects.

# Donations:

If you find this library helpful, donations are always appreciated:

PayPal: donate@aswsoftware.com

Bitcoin: 15rKqL1numHJyE36ottMbhs5cmCjJkuowV

# How to Use

## Quick start

```cpp
#include <cstdint>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

#include "ASWToml_Base.h"
#include "ASWToml_Settings.h"

// A server the application connects to; a std::vector of them is an array of tables
struct TServer
{
    std::string Host;
    std::uint16_t Port = 443;
};

inline auto ASWTomlFields(TServer const&)
{
    return std::tuple{ ASWToml::Field("host", &TServer::Host), ASWToml::Field("port", &TServer::Port) };
}

class TAppSettings : public ASWToml::TASWBaseToml
{
public:
    // The root table "" holds the keys before the first header; its comment is the file comment
    ASWToml::TASWTomlTable FileTop{ *this, "", "Settings of MyApp." };

    ASWToml::TASWTomlTable General{ *this, "general", "What the application looks like." };
    ASWToml::TASWTomlSetting<std::string> UserName{ General, "user_name", "Guest", "The name in the title bar." };
    ASWToml::TASWTomlSetting<bool> ShowToolbar{ General, "show_toolbar", true };

    ASWToml::TASWTomlTable Logging{ *this, "logging" };
    ASWToml::TASWTomlSetting<unsigned> Level{ Logging, "level", 2, "From 0 (errors only) to 4 (everything).",
        { .Min = 0, .Max = 4 } };
    // Called log_days in an earlier version of the application, so Load() renames that key
    ASWToml::TASWTomlSetting<unsigned> DaysKept{ Logging, "days_kept", 30,
        "How many days the logs are kept; 0 keeps them forever.", { .OldKeys = { "log_days" } } };

    // An array of tables, [[servers]]
    ASWToml::TASWTomlSetting<std::vector<TServer> > Servers{ FileTop, "servers",
        { TServer{ "main.example.com", 443 } }, "The servers, tried in order." };
};

// Where the library reports warnings, errors and notes; an application writes them to its log
void LogMessage(ASWToml::TASWTomlMessage const& message)
{
    std::cout << ASWToml::ToString(message) << "\n";
}

int main()
{
    TAppSettings settings;
    settings.SetLogCallback(LogMessage);

    // On the first run the file is missing: Load() returns FileNotFound, reported as a warning, and the document
    // holds every setting with its default value and comment
    ASWToml::TASWTomlResult const loaded = settings.Load("MyApp.toml");
    if (!loaded.IsOk() && loaded.Code != ASWToml::ETomlCode::FileNotFound)
        return 1; // A file that can't be read, or isn't valid TOML: reported to LogMessage too

    if (settings.IsModified()) // Load() added or repaired a setting: the file needs saving
        static_cast<void>(settings.Save());

    std::cout << "Hello, " << settings.UserName.Get() << "\n";
    static_cast<void>(settings.ShowToolbar.Set(false));
    static_cast<void>(settings.Level.Set(7)); // Out of range: fails, is reported as an error, changes nothing

    for (TServer const& server : settings.Servers.Get())
        std::cout << "Server " << server.Host << ":" << server.Port << "\n";

    return settings.Save().IsOk() ? 0 : 1;
}
```

writes `MyApp.toml` (with the platform's line endings):

```toml
# Settings of MyApp.

# What the application looks like.
[general]
# The name in the title bar.
user_name = "Guest"
show_toolbar = false

[logging]
# From 0 (errors only) to 4 (everything).
level = 2
# How many days the logs are kept; 0 keeps them forever.
days_kept = 30

# The servers, tried in order.
[[servers]]
host = "main.example.com"
port = 443
```

On the next run the file holds every setting, so `Load()` changes nothing. When a user has edited
it, `Load()` repairs it and reports each change: with `level = 9`, `log_days = 7` and a server's
`port = 70000` in the file, it reports that the default value 2 is used for `logging.level` and 443
for `servers[0].port`, and that `log_days` is renamed to `days_kept`, adds any missing key and
comment, and `IsModified()` is `true`. What the settings class doesn't declare (a user's own keys
and tables with their comments, and a server's other keys) stays as it is; the comments it
declares are written as the code has them (see `Writing.CommentPolicy` in
[docs/Options.md](docs/Options.md)).

The `example/src/main.cpp` program shows more: every kind of setting, a file a user edited, two
threads, values the settings class doesn't declare, and the document on its own.
[docs/Settings.md](docs/Settings.md) describes settings classes in full.

## Adding ASWToml to a project

### Copied sources (any build system)

Add every `.cpp` file in `ASWToml/` to the project, and the `ASWToml` folder to its include path,
and compile as C++20 or later. `Deploy.bat <folder>` copies the library sources, the build files and
the docs to a folder, e.g. for a release package or to copy into another project.

### RAD Studio

Add the `ASWToml/*.cpp` files to the project (Project > Add to Project) and the `ASWToml` folder to
the include path (Project > Options > Building > C++ Shared Options > Include path). RAD Studio 13.1
or later, with the modern Clang toolchain (Win64x), which compiles at C++23 by default.
`example/rad370/ASWTomlExample.cbproj` (with `Build_*.bat` scripts) is a working project to copy
settings from.

### CMake

The repository's root `CMakeLists.txt` defines the static library target `ASWToml::ASWToml`.
Linking to it adds the `ASWToml` include folder and C++20. Add it from a copy of the repository,
such as a git submodule:

```
add_subdirectory(third_party/asw-toml)
target_link_libraries(MyApp PRIVATE ASWToml::ASWToml)
```

or let CMake download it with `FetchContent`:

```
include(FetchContent)
FetchContent_Declare(ASWToml
    GIT_REPOSITORY https://github.com/ASWSoftware/asw-toml.git
    GIT_TAG v1.0.0      # A release tag
    GIT_SUBMODULES ""   # Skip the unit-test framework submodule
)
FetchContent_MakeAvailable(ASWToml)
target_link_libraries(MyApp PRIVATE ASWToml::ASWToml)
```

Either way, only the library is built. When ASWToml is the top-level project,
`-DASWTOML_BUILD_TESTS=ON` also builds the unit tests, `-DASWTOML_BUILD_EXAMPLE=ON` the example
program (`example/cmake`), and `ASWTOML_WARNINGS` (on by default only there) compiles the library
with extra warnings.

To use a prebuilt copy instead, build and install the library from the repository root (a Debug
build is named `ASWTomld`, so Debug and Release can share one install prefix):

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cmake --install build --config Release --prefix C:/Libraries/ASWToml
```

and find it with `find_package` (pass the prefix in `CMAKE_PREFIX_PATH`):

```
find_package(ASWToml 1.0 CONFIG REQUIRED)
target_link_libraries(MyApp PRIVATE ASWToml::ASWToml)
```

The headers are installed to `<prefix>/include/ASWToml`, the target's include folder, so code
includes them as it does from the sources (`#include "ASWToml_Version.h"`). A requested version is
met by any later release with the same major version. The install rules are on by default only when
ASWToml is the top-level project; set `ASWTOML_INSTALL` to change that. `tests/package` is a small
project that builds against an installed copy; CI runs it.

## API overview

**Classes.** Everything is in the `ASWToml` namespace; a name in `ASWToml::Detail` is internal.

| Class | Header | What it is |
|---|---|---|
| `TASWBaseToml` | `ASWToml_Base.h` | The TOML object: a document behind a reader/writer lock, with its file, options and log callback. An application's settings class derives from it. |
| `IASWToml` | `ASWToml_Interface.h` | The interface `TASWBaseToml` implements, for code that only reads and writes values. |
| `TASWTomlTable`, `TASWTomlSetting<T>` | `ASWToml_Settings.h` | A table and a setting, declared as members of a settings class. `TASWTomlSettingOptions<T>` gives a setting its range, validator and other options. |
| `TASWTomlDocument` | `ASWToml_Document.h` | The lossless model of a TOML text, with its lookups, edits and comments. A copyable value, with no file and no lock. |
| `TASWTomlEdits` | `ASWToml_Edits.h` | A batch: edits of a document, in order, which `Apply()` makes as one, all of them or none. |
| `TASWTomlValue` | `ASWToml_Value.h` | A TOML value of any type (`ETomlType`), e.g. the value at a key or a whole table; `TASWTomlOffsetDateTime`, `TASWTomlLocalDateTime` and `TASWTomlLocalTime` are TOML's date and time types. |
| `TASWTomlPath` | `ASWToml_Path.h` | The path of a table, a key or an element of an array, made from a string (`server."host name".port`) or a list of parts. |
| `TASWTomlOptions` | `ASWToml_Options.h` | How text is read, written and saved, and how values are written. |
| `TASWTomlConverter<T>` | `ASWToml_Converters.h` | Converts a C++ value to and from a `TASWTomlValue`; specialize it for a type of your own. An enum is written by name when it has `ASWTomlEnumNames()` (`TASWTomlEnumName<T>`), and a struct is a table when it has `ASWTomlFields()` (`Field()`, `OptionalField()`). |
| `TASWTomlResult`, `TASWTomlMessage` | `ASWToml_Types.h` | The outcome of an operation, and a warning, error or note reported to the log callback, each with its `ETomlCode`. |

`PathToUtf8()` and `Utf8ToPath()` (`ASWToml_File.h`) convert a `std::filesystem::path` to and from
the UTF-8 path text the library takes, `ToJson()` (`ASWToml_Json.h`) writes a value as JSON with
`TASWTomlJsonOptions`, and `ASWToml_Version.h` has the version.

**Files.** On a `TASWBaseToml`: `Load(path)`, `Save()`, `SaveAs(path)`, `IsModified()` and
`GetFilePath()`; `LoadText(text)` and `ToText()` for a text; `SetOptions()` (before `Load()`) and
`GetOptions()`; `SetLogCallback()`.

**Settings.** On a `TASWTomlSetting<T>`: `Get()`, `Set(value)` (which fails for a value out of the
range or one the validator refuses), `Reset()` and `IsPresent()`, and `Set(edits, value)`, which
adds the change to a batch. Its options, with designated initializers: `{ .Min = 0, .Max = 4 }`,
and `.Validator`, `.Optional`, `.OldKeys` and `.Values`.

**Values by path**, e.g. of tables a settings class doesn't declare: `GetValue(path, default)`,
`TryGetValue(path, value)` (which returns why it couldn't), `SetValue(path, value)` and
`Append(path, value)`. `Contains()`, `GetKeyNames()`, `Remove()` and `Rename()`. The comments:
`GetComment()` and `GetTrailingComment()` (the root path's comment is the file comment), and their
`Set` methods. `Apply(edits)` makes a batch of them and of settings' changes, all or nothing.

**The document.** `GetDocument()` gives a copy of a TOML object's document. A `TASWTomlDocument` of
its own has `Parse()` and `ToText()`, `Find()` and `GetRoot()`, and the same edits and comments
(with `SetComments()` for several at once) and `Apply()`. `ToJson(document.GetRoot(), json)`
writes its values as JSON.

**Results.** Each operation that can fail returns a `TASWTomlResult` (`IsOk()` when it succeeded,
else with its `Code` and `Message`) and reports the failure to the log callback, without throwing.
`ToString()` names a code and writes a message as one line. The header comments document every
class and operation.

## Documentation

- [docs/Format.md](docs/Format.md): the TOML ASWToml reads and writes: what a text with an error
  does, tables and paths, comments, encoding and line endings, limits, what an edit and a batch
  change, and how each new value is written.
- [docs/Types.md](docs/Types.md): the C++ types ASWToml converts and their TOML values: numbers,
  strings, dates and times, enums, containers, maps, structs, `TASWTomlValue`, a converter of your
  own, and the JSON `ToJson()` writes.
- [docs/Options.md](docs/Options.md): every option, its default and what it does, when options
  apply, and `ToJson()`'s options.
- [docs/Settings.md](docs/Settings.md): settings classes: declaring and using settings, what
  `Load()` does, structs and arrays of tables, comments, threads, the raw API alongside, several
  settings as one, the messages, and sending them to ASWLog.
- [docs/Guarantees.md](docs/Guarantees.md): what is returned and reported instead of thrown, thread
  safety, what callbacks may do, object lifetimes, nesting and stack, saving and crashes, what
  costs what, and every `ETomlCode`.
- [CHANGELOG.md](CHANGELOG.md): the changes in each release.

## Versions

ASWToml follows [Semantic Versioning](https://semver.org). Releases are tagged on `main` (e.g.
`v1.0.0`). Between releases, the `develop` branch carries the next planned version with a
pre-release, e.g. `1.1.0-dev.1`, which comes before `1.1.0`. `ASWToml/ASWToml_Version.h` has the
version as macros, for code that supports several ASWToml versions, and as constants in the
`ASWToml` namespace:

```
#include "ASWToml_Version.h"

#if ASWTOML_VERSION_MAJOR > 1 || (ASWTOML_VERSION_MAJOR == 1 && ASWTOML_VERSION_MINOR >= 1)
    // Uses something added in 1.1 (also present in 1.1.0-dev.N builds)
#endif
```

# Unit Tests

Using the library only requires the `ASWToml` source. The unit tests in `tests` additionally use
the [ASWUnitTests](https://github.com/ASWSoftware/asw-unit-tests) framework, included as a git
submodule in `third_party/asw-unit-tests`. Clone with submodules:

```
git clone --recurse-submodules https://github.com/ASWSoftware/asw-toml.git
```

or, in an existing clone:

```
git submodule update --init
```

Build and run the tests with CMake from the repository root:

```
cmake -S tests/cmake -B tests/cmake/build
cmake --build tests/cmake/build --config Release
```

The executable is written to `tests/cmake/build/bin` (in a `Release` sub folder for multi-config
generators such as Visual Studio). The tests build at C++20 by default; add
`-DASWTOML_CXX_STANDARD=23` (or `26`, in another build folder) to build them, and the library, at a
later standard.

The tests include every file of [toml-test](https://github.com/toml-lang/toml-test)'s TOML 1.1 list,
compiled in from `tests/toml-test` (its README says which version and how to update it): each valid
file must read as toml-test's expected values and be written back byte for byte, and each invalid
one must fail. CI also runs toml-test's own program on that list, with the decoder the tests' CMake
project builds (`ASWTomlTestDecoder`, from `tests/toml-test/decoder`): the "Linux (GCC, toml-test)"
job. `tests/toml-test/README.md` says how to run it by hand.

For RAD Studio, use `tests/rad370/ASWTomlTests.cbproj` or its scripts, `Build_Win64x_Debug.bat` and
`Build_Win64x_Release.bat` (bcc64x). Output is written to `tests/build/bin/Win64x/<Config>`. GitHub
Actions builds and runs the tests on Windows and Linux for each push and pull request to `main` and
`develop`.

With GCC or Clang (on Linux), the tests can also be built with a sanitizer, which fails the run with
a report when it finds a data race (`thread`), or a memory error or undefined behavior (`address`,
which includes UndefinedBehaviorSanitizer):

```
cmake -S tests/cmake -B tests/cmake/build-tsan -DCMAKE_BUILD_TYPE=Release -DASWTOML_SANITIZE=thread
cmake --build tests/cmake/build-tsan
```

CI runs both with Clang. `ASWTOML_SANITIZE` applies to the library and everything that links to it.

CI also measures which lines of the library the tests run (a Linux GCC build with `--coverage`, read
by gcovr): the "Linux (GCC, coverage)" job shows a table on its summary page and keeps an HTML
report as the `coverage-report` artifact. It is informational and never fails the run.

On Windows with RAD Studio, the tests can also run in the ASWUnitTests VCL GUI runner, a window for
choosing tests and reading their results: build `tests/vcl/gui/rad370/ASWTomlTests_VCL_GUI.cbproj`
(or its `Build_*.bat` scripts), which writes `ASWTomlTests_VCL_GUI.exe` to
`tests/build/bin/Win64x/<Config>`. `ASWTomlTests_Group.groupproj` in the same folder opens it
together with the console test project. The GUI takes the console runner's command line options
plus a few of its own, e.g. `--run --exit` to run every test and exit with the console runner's exit
code; see the ASWUnitTests README's "VCL GUI Runner" section.

# Coding Standards

To use uncrustify (for coding standards (pretty formatting) for this repo):

1. Get `uncrustify` and ensure that the path to `uncrustify.exe` is added to the Windows `Path`
   environment variable. https://github.com/uncrustify/uncrustify

2. Run the following command locally to set the git hooks directory (uncrustify will run upon
   commit):
```
git config --local core.hooksPath .githooks/
```

# Contributing

Contributions are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for how to propose a change, build
and test it, and report a bug.

# License

ASWToml is licensed under the [MIT License](LICENSE). The unit tests include the files of
[toml-test](https://github.com/toml-lang/toml-test), also under the MIT License
(`tests/toml-test/LICENSE-toml-test`).
