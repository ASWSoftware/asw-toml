# ASWToml - https://github.com/ASWSoftware/asw-toml

ASWToml is a thread-safe, dependency-free C++20 library for reading and writing TOML config files on
Windows and Linux, which keeps the file's comments and layout.

## Status

ASWToml is in development toward 1.0.0; the `develop` branch carries `1.0.0-dev.N` versions. So far
it has the project structure, the version (`ASWToml_Version.h`), the CMake target and package, the
unit-test projects and CI, and the foundations the rest builds on: the shared result and message
types (`ASWToml_Types.h`), the options (`ASWToml_Options.h`) and the file layer with its UTF-8 path
conversions (`ASWToml_File.h`). It also has the TOML values: `TASWTomlValue` and the date and time
types (`ASWToml_Value.h`), and the reading and writing of TOML's strings, numbers, booleans, dates
and times. And it reads whole TOML 1.1 documents: `TASWTomlDocument` (`ASWToml_Document.h`) keeps
every byte of a text and writes it back unchanged, applies TOML's table rules, and gives each value
by its path (`TASWTomlPath`, `ASWToml_Path.h`), checked against every file of toml-test's TOML 1.1
list. And it edits them, changing only the text an edit needs: values set in place in their own
form, new keys and tables where their table is written, keys, tables and elements removed and
renamed, whole tables and arrays merged so only what differs changes, and the comments above keys
and tables and at the end of their lines. And it converts TOML values to and from C++ types
(`TASWTomlConverter<T>`, `ASWToml_Converters.h`): strings, paths, booleans, every integer type,
`float` and `double`, enums by name, the `std::chrono` dates, time points and times of day,
`std::vector`, `std::array`, sets, pairs and tuples as arrays, maps (with string, integer or enum
keys) as tables, and structs through a list of their fields, so a `std::vector` of a struct is an
array of tables. `TASWBaseToml` (`ASWToml_Base.h`) is the TOML object an application uses, from any
number of threads: it loads a file and saves it back (atomically, with an optional backup), reads
and writes typed values by path (`GetValue()`, `TryGetValue()`, `SetValue()` and `Append()`), and
reports warnings and errors to a log callback. Code that only reads and writes values can take its
interface, `IASWToml` (`ASWToml_Interface.h`). An application's settings class derives from
`TASWBaseToml` and declares each setting once (`TASWTomlTable` and `TASWTomlSetting<T>`,
`ASWToml_Settings.h`), with its table, key, type, default value, comment and options. Loading brings
the file up to date: missing keys are added with their comments, keys renamed from their earlier
names, values that aren't valid replaced by their defaults, and structs and arrays of tables brought
up to date field by field, keeping the keys the application doesn't know. Each setting reads and
writes its value in the document, from any thread. An example program and the rest of the
documentation come next. [CHANGELOG.md](CHANGELOG.md) lists what each change adds. The interface
may change in any way until 1.0.0.

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

## Planned for 1.0

- TOML 1.1.0 read, and so every TOML 1.0 file; text the library writes uses only syntax that TOML
  1.0 also reads. Checked against the official [toml-test](https://github.com/toml-lang/toml-test)
  suite.
- A file that isn't valid TOML (a syntax error, a key or table defined twice, invalid UTF-8) fails
  to load with the line and column of the first error, and nothing changes.
- Tables however they are written (`[header]`, dotted keys, inline tables) and arrays of tables,
  addressed by a path in TOML's dotted-key syntax (`server."host name".port`) or as a list of parts,
  with indexes into arrays of tables (`products[0].name`). Keys compare case-sensitively, as TOML
  specifies.
- Edits in the form the file already uses: a new key after the last key of its table, as a dotted
  key or inside the braces of an inline table when the table is written that way; a new table as a
  `[header]`. The file's, a table's and a key's comments, and end-of-line comments.
- Typed values, the same in any locale: strings, booleans, every integer type with range checks,
  `float` and `double`, TOML's four date and time types as C++20 `std::chrono` types, enums by name,
  `std::filesystem::path`, `std::u8string`, `std::vector<T>`, `std::array`, sets, pairs and tuples
  as arrays, `std::map` and `std::unordered_map` (string, integer or enum keys) as tables, and
  structs through a list of their fields, so a `std::vector` of a struct is an array of tables; plus
  your own types.
- An interface, `ASWToml::IASWToml`, and its implementation, `ASWToml::TASWBaseToml`, which an
  application's settings class derives from, declaring its tables and settings
  (`TASWTomlTable`, `TASWTomlSetting<T>`), arrays of tables included.
- UTF-8, with or without a byte order mark (kept as the file had it); LF and CRLF line endings
  (kept, even mixed); a size limit on loads and a nesting limit for tables, arrays and inline
  tables.
- Atomic saves with an optional backup, and UTF-8 file paths on Windows and Linux.

# Donations:

If you find this library helpful, donations are always appreciated:

PayPal: donate@aswsoftware.com

Bitcoin: 15rKqL1numHJyE36ottMbhs5cmCjJkuowV

# How to Use

## Adding ASWToml to a project

### Copied sources (any build system)

Add every `.cpp` file in `ASWToml/` to the project, and the `ASWToml` folder to its include path,
and compile as C++20 or later. `Deploy.bat <folder>` copies the library sources, the build files and
the docs to a folder, e.g. for a release package or to copy into another project.

### RAD Studio

Add the `ASWToml/*.cpp` files to the project (Project > Add to Project) and the `ASWToml` folder to
the include path (Project > Options > Building > C++ Shared Options > Include path). RAD Studio 13.1
or later, with the modern Clang toolchain (Win64x), which compiles at C++23 by default.
`tests/rad370/ASWTomlTests.cbproj` is a working project to copy settings from.

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
    GIT_TAG develop     # A release tag once there is one, e.g. v1.0.0
    GIT_SUBMODULES ""   # Skip the unit-test framework submodule
)
FetchContent_MakeAvailable(ASWToml)
target_link_libraries(MyApp PRIVATE ASWToml::ASWToml)
```

Either way, only the library is built. When ASWToml is the top-level project,
`-DASWTOML_BUILD_TESTS=ON` also builds the unit tests, and `ASWTOML_WARNINGS` (on by default only
there) compiles the library with extra warnings.

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

## Versions

ASWToml follows [Semantic Versioning](https://semver.org). Releases are tagged on `main` (e.g.
`v1.0.0`). Between releases, the `develop` branch carries the next planned version with a
pre-release, e.g. `1.0.0-dev.1`, which comes before `1.0.0`. `ASWToml/ASWToml_Version.h` has the
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
one must fail.

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
