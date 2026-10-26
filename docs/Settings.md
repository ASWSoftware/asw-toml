# Settings classes

An application declares each of its settings once, as a member of its settings class: its table,
key, type, default value, comment and options. Loading the file brings it up to date, and each
setting reads and writes its value in the loaded document, from any thread. This replaces the code
that finds, converts, checks, repairs and comments each key by hand. A setting can be a struct (a
table) or a `std::vector` of one (an array of tables), which is brought up to date part by part.
`example/src/main.cpp` shows a settings class in use.

## Declaring the settings

A settings class derives from `ASWToml::TASWBaseToml` (`ASWToml_Base.h`) and declares its tables
(`TASWTomlTable`) and settings (`TASWTomlSetting<T>`) as members (`ASWToml_Settings.h`):

```cpp
#include <cstdint>
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
    bool Secure = true;
};

inline auto ASWTomlFields(TServer const&)
{
    return std::tuple{ ASWToml::Field("host", &TServer::Host), ASWToml::Field("port", &TServer::Port),
        ASWToml::Field("secure", &TServer::Secure) };
}

class TAppSettings : public ASWToml::TASWBaseToml
{
private:
    // The validator of a text that can't be empty: "" when it is valid, else what is wrong with it
    static std::string NotEmpty(std::string const& value)
    {
        return value.empty() ? "it can't be empty" : "";
    }

public:
    // The root table "" holds the keys before the first header; its comment is the file comment
    ASWToml::TASWTomlTable FileTop{ *this, "", "Settings of MyApp." };

    ASWToml::TASWTomlTable General{ *this, "general", "What the application looks like." };
    ASWToml::TASWTomlSetting<std::string> UserName{ General, "user_name", "Guest", "The name in the title bar.",
        { .Validator = NotEmpty } };
    // Not added to the file when it is missing: a hidden setting a user may add by hand
    ASWToml::TASWTomlSetting<bool> EnableCheats{ General, "enable_cheats", false, "", { .Optional = true } };

    ASWToml::TASWTomlTable Logging{ *this, "logging" };
    ASWToml::TASWTomlSetting<unsigned> Level{ Logging, "level", 2, "From 0 (errors only) to 4 (everything).",
        { .Min = 0, .Max = 4 } };
    // Called log_days in an earlier version of the application, so Load() renames that key
    ASWToml::TASWTomlSetting<unsigned> DaysKept{ Logging, "days_kept", 30,
        "How many days the logs are kept; 0 keeps them forever.", { .OldKeys = { "log_days" } } };

    ASWToml::TASWTomlTable Network{ *this, "network", "Where the application connects." };
    // An array of tables, [[network.servers]]
    ASWToml::TASWTomlSetting<std::vector<TServer> > Servers{ Network, "servers",
        { TServer{ "main.example.com", 443, true } }, "The servers, tried in order." };

    TAppSettings()
    {
        // Options are set before the first Load(), which reads the file with them (see Options.md)
        ASWToml::TASWTomlOptions options;
        options.Saving.Backup = true;
        static_cast<void>(SetOptions(options));
    }
};
```

On the first run, when there is no file, the settings class writes:

```toml
# Settings of MyApp.

# What the application looks like.
[general]
# The name in the title bar.
user_name = "Guest"

[logging]
# From 0 (errors only) to 4 (everything).
level = 2
# How many days the logs are kept; 0 keeps them forever.
days_kept = 30

# The servers, tried in order.
[[network.servers]]
host = "main.example.com"
port = 443
secure = true
```

- **A table** has its owner (the settings object, `*this`), its path (`"logging"`, or a dotted path
  such as `"view.window"`, key parts only) and an optional comment, written above its header. The
  root table `""` holds the keys before the first header, and its comment is the file comment.
  `network` has no line of its own here (`[[network.servers]]` makes it), so its comment isn't
  written; a `[network]` header would carry it.
- **A setting** has its table, its key, its default value, an optional comment (written above the
  key, or above the first `[[...]]` header of an array of tables) and options. Its type is any type
  with a converter (see [Types.md](Types.md)): strings, `bool`, numbers, enums (by name with
  `ASWTomlEnumNames()`), paths, dates and times, containers, maps, structs with `ASWTomlFields()`,
  and your own types. A struct is a table (`[view.window]`), and a `std::vector` of one an array of
  tables.
- Declare a table before its settings. Tables and settings are bound to their owner from their
  constructors to their destructors, in the order they are declared, which is the order `Load()`
  applies them in.
- Neither may outlive its owner, and neither can be copied or moved. Both hold for members of the
  settings class.

### The options of a setting

`TASWTomlSettingOptions<T>` is an aggregate, given with designated initializers in the order of its
members, e.g. `{ .Min = 0, .Max = 4 }` or `{ .Validator = NotEmpty, .Values = hexadecimal }`:

| Option | What it does |
|---|---|
| `.Min`, `.Max` | The smallest and the largest value the setting takes, compared with `operator<` (numbers, enums, strings, a struct with `operator<=>`, ...). A value outside them is out of range. For a type that `operator<` doesn't compare, giving one is a compile error. |
| `.Validator` | A function that gets a value and returns `""` when it is valid, or else what is wrong with it, as the end of a sentence (`"it can't be empty"`). It must only look at the value, not call the TOML object. |
| `.Optional` | A missing key is left missing when a file is loaded (e.g. a hidden setting a user may add); it is written only when `Set()` writes it. |
| `.OldKeys` | Earlier names of the key, in the same table, e.g. `{ .OldKeys = { "log_days" } }`: when a file has one and not the key, the first one it has is renamed to the key. |
| `.Values` | Value options of the setting's own, instead of its owner's, e.g. an integer written in hexadecimal (see [Options.md](Options.md)). |

A value is valid when it converts, is in the range and the validator accepts it. The default value
is expected to be valid.

## Using the settings

```cpp
TAppSettings settings;
settings.SetLogCallback(LogMessage); // Where warnings, errors and notes go (see Messages below)

// On the application's first run the file is missing: Load() returns FileNotFound, reported as a warning, and the
// document holds every setting with its default value
ASWToml::TASWTomlResult const loaded = settings.Load(ASWToml::PathToUtf8(folder / "MyApp.toml"));
if (!loaded.IsOk() && loaded.Code != ASWToml::ETomlCode::FileNotFound)
    return false; // A file that can't be read: reported to LogMessage too

if (settings.IsModified()) // Load() added or repaired something: the file needs saving
    static_cast<void>(settings.Save());

unsigned const level = settings.Level.Get();
ASWToml::TASWTomlResult const set = settings.Level.Set(level + 1); // Fails (and reports it) above 4

std::vector<TServer> servers = settings.Servers.Get();
servers.push_back(TServer{ "backup.example.com", 8443, true });
ASWToml::TASWTomlResult const added = settings.Servers.Set(servers); // A second [[network.servers]]
```

- **`Get()`** reads the value from the owner's document. It gives the default value for a missing
  key, and for a value that isn't valid (e.g. one a raw edit made invalid), which it also reports as
  a warning that names the default.
- **`Set()`** writes the value to the document. A missing key is added with the setting's comment
  above it, in the same edit. A value out of the range fails with `ValueOutOfRange`, and one the
  validator refuses (or that can't be written) with `InvalidValue`; the failure is returned,
  reported as an error, and changes nothing.
- **`Reset()`** sets the default value. **`IsPresent()`** says whether the key is in the document,
  e.g. for an optional setting. `GetDefault()`, `GetKey()`, `GetPath()`, `GetComment()` and
  `GetOptions()` give the declaration.
- A setting holds no value of its own: it always agrees with the document, also after a `Load()`
  or a raw `SetValue()` of its key. A `Get()` of a number costs a lookup and a conversion (well
  under a microsecond); code that reads a setting in a tight loop keeps a copy.
- `Save()` writes the document; it doesn't need to write the settings, which are in the document
  already.

## What Load() does

When `Load()` or `LoadText()` has read a document, and before anyone sees it, each setting is
applied to it, in the order the settings are declared:

1. A missing key whose earlier name (`.OldKeys`) its table has is renamed from it, keeping its line
   or header, value and comment, reported as a note (`KeyRenamed`), and is then applied as a key
   that is there. When the key is there too, the earlier name is left alone.
2. Any other missing key is added with the default value and the setting's comment, reported as a
   note (`KeyAdded`), unless the setting is optional. It goes where its table is written (after the
   table's last key, or as a new `[header]`; see [Format.md](Format.md)), so missing tables come in
   the order of their first setting, and missing keys in the order they are declared.
3. A value that isn't valid is replaced by the default value, reported as a warning (the
   converter's code, or the setting's `ValueOutOfRange` or `InvalidValue`) that says what was read
   and that the default is used. A valid value is never rewritten: `0x10` stays `0x10`.
4. A struct, a vector or a map is applied by parts (see the next section).
5. The key's comment is set to the setting's, as the comment policy says (see
   [Comments](#comments)).

Then each table's comment is applied, for the tables that are in the document and have a line of
their own (applying never adds a table for its comment alone), and the root table's is the file
comment. A table of settings that the file holds as another kind of value (`logging = 5`) is
replaced by a table of its settings, with a warning.

So the loaded document holds every setting with a valid value, and `IsModified()` says whether
applying changed it: whether the file needs saving. A file that holds every setting, valid and with
the code's comments, stays byte for byte, and a missing file (the first run) gets everything. The
notes and warnings are reported after the load's own warnings, with the file's path, and a load
that fails (e.g. a file with a syntax error, or out of memory) changes nothing and reports none of
them.

A default value that can't be written (an enum value with no name), or that the setting's own range
or validator refuses, is the application's bug: it is reported as an error, and the key is left as
it is.

Settings are applied by `Load()` and `LoadText()` only. Before the first load, a new settings
object's document is empty, and `Get()` gives the default values; `LoadText("")` fills it with
them. `SetOptions()` doesn't apply the settings, so set the options first, in the constructor.

The changes are written as few edits: the earlier names are renamed first, then the values are
written with one edit per table (a setting with value options of its own with one of its own), and
the comments with one more.

## Structs, vectors and maps

A setting of a struct with fields, a `std::vector` or a map (of the library's converters) is taken
by parts, so one bad value in a user's file costs that value, not the rest:

- **`Load()` and `Get()`** take the document's value with each part that is missing or isn't valid
  replaced by its default: a field's default is the member of the setting's default value for the
  setting's own struct, and of a value-initialized element for an element of a vector or a value of
  a map. Then the whole value is checked by the range and the validator, and only when they refuse
  it does the setting give its whole default value. `Load()` writes the repaired parts into the
  document, with a warning for each part that wasn't valid and a note (`KeyAdded`) for each field
  that was missing and isn't optional; `Get()` reports the parts that weren't valid. Keys that no
  field names stay as they are, so a user's own keys and notes survive, and a file that a newer
  version of the application wrote still reads.
- **`Set()`** sets the value over the document's, under one lock: the keys the struct's fields
  don't name stay, at any depth; the elements of a vector are paired with the document's by what
  their type knows of them, so the block of an element removed goes and the others keep their own
  keys and comments; a map's values are set by key. (`IASWToml::SetValue()` writes the whole value
  as it is, so such keys are removed.)

For example, a user edited the file of an earlier version, which had `log_days` and no `secure`
field in a server, gave the first server a port out of range and a note of their own, and added a
`theme`:

```toml
# Settings of MyApp.

[general]
user_name = ""
theme = "Dark"  # my own

[logging]
level = 9
log_days = 7

# My servers
[[network.servers]]
host = "main.example.com"
port = 70000
note = "the old one"

[[network.servers]]
host = "backup.example.com"
port = 8443
```

`Load()` reports what it does (see [Messages](#messages)):

```text
Warning InvalidValue: The value "" isn't valid: it can't be empty. The default value "Guest" is used. (file "MyApp.toml", path general.user_name)
Warning ValueOutOfRange: The value 9 is out of the setting's range: it must be from 0 to 4. The default value 2 is used. (file "MyApp.toml", path logging.level)
Info KeyRenamed: The key logging.days_kept is missing: its earlier name log_days is renamed to it. (file "MyApp.toml", path logging.days_kept)
Warning ValueOutOfRange: The value 70000 is out of range: it must be from 0 to 65535. The default value 443 is used. (file "MyApp.toml", path network.servers[0].port)
Info KeyAdded: The key network.servers[0].secure is missing: it is added with its default value true. (file "MyApp.toml", path network.servers[0].secure)
Info KeyAdded: The key network.servers[1].secure is missing: it is added with its default value true. (file "MyApp.toml", path network.servers[1].secure)
```

and the document is then (with the default `CodeWins` comment policy, so the code's comments
replace `# My servers`):

```toml
# Settings of MyApp.

# What the application looks like.
[general]
# The name in the title bar.
user_name = "Guest"
theme = "Dark"  # my own

[logging]
# From 0 (errors only) to 4 (everything).
level = 2
# How many days the logs are kept; 0 keeps them forever.
days_kept = 7

# The servers, tried in order.
[[network.servers]]
host = "main.example.com"
port = 443
note = "the old one"
secure = true

[[network.servers]]
host = "backup.example.com"
port = 8443
secure = true
```

## Comments

The comments a settings class declares (a setting's, a table's, and the root table's, which is the
file comment) are written as `Writing.CommentPolicy` says when a file is loaded:

- `ECommentPolicy::CodeWins` (the default): the code's comment is written over the file's, so a
  comment edited by hand comes back as the code has it. A comment that reads the same already is
  left as it is (whatever spacing its lines use), so it doesn't make the document modified.
- `ECommentPolicy::FileWins`: the code's comment is written only where the file has none, e.g. for a
  key that was just added; a user's own comment stays.

A setting or table declared with no comment (`""`) never changes the file's comment, under either
policy. A key that `Set()` adds gets the setting's comment. A key or table without a line of its own
(e.g. a key of an inline table, or a table that dotted keys make) gets none.

## Threads

- `Get()`, `Set()`, `Reset()` and `IsPresent()` may be called from any thread at any time, as the
  owner's own operations: each one is a single lookup or edit under the owner's lock, so it sees the
  document before or after another thread's edit or load, never during one.
- Several `Set()`s are separate edits: another thread may see some of them done and not the others,
  which is harmless for settings. There is no batch edit in 1.0.
- The converters and validators run while `Load()` holds the owner's file lock, so they must not
  call its `Load()`, `LoadText()`, `Save()`, `SaveAs()` or `SetOptions()`. A validator should only
  look at the value it gets.
- A table or setting made or destroyed while another thread loads waits for the load to finish.
- [Guarantees.md](Guarantees.md) describes the locks and the log callback in full.

### Not a store for fast-changing state

Settings are read often and written now and then. Each `Set()` is an edit of the document: it
writes the changed text and parses the whole document again (5 to 15 microseconds for the file
above on a current PC, more for a larger file), under the owner's exclusive lock, which every
`Get()` and edit on other threads waits for. So don't use a setting as a counter, a progress value
or anything that changes many times a second: keep such state in your own variables (e.g. a
`std::atomic`), and `Set()` it now and then, e.g. when the application exits. Also, the
`std::shared_mutex` of libstdc++ (GCC's standard library, on Linux and with MinGW) lets a thread
waiting to write lose to other threads that write back to back: with several threads calling
`Set()` in a loop, one `Set()` can wait hundreds of milliseconds, even seconds.

## Settings and the raw API

A settings class is a `TASWBaseToml`, so the keys it doesn't declare are read and written by path,
with the typed `GetValue()`, `TryGetValue()`, `SetValue()` and `Append()`, and the other operations
(`Contains()`, `GetKeyNames()`, `Remove()`, `Rename()`, the comments, ...). They see the settings'
keys too, and a setting sees their edits at once:

```cpp
// Window positions, a table per window, which the settings class doesn't declare
ASWToml::TASWTomlResult const stored = settings.SetValue("windows.main.width", 1024);
for (std::string const& window : settings.GetKeyNames("windows"))
    RestoreWindow(window, settings.GetValue({ "windows", window, "width" }, 800));
```

Code that only reads and writes values can take an `ASWToml::IASWToml&` (`ASWToml_Interface.h`),
the interface `TASWBaseToml` implements.

Compared with doing it by hand: without settings, each key needs its own code to read it, check it,
repair it and keep its comment, e.g. for `logging.level` alone

```cpp
unsigned level = 2;
ASWToml::TASWTomlResult const read = toml.TryGetValue("logging.level", level);
if (!read.IsOk() || level > 4)
{
    if (read.IsOk())
        LogMessageText("logging.level is out of range; using 2");

    level = 2;
    static_cast<void>(toml.SetValue("logging.level", level));
    needsSave = true;
}

if (toml.GetComment("logging.level") != "From 0 (errors only) to 4 (everything).")
{
    static_cast<void>(toml.SetComment("logging.level", "From 0 (errors only) to 4 (everything)."));
    needsSave = true;
}
```

and the same again wherever it is written, while the setting is one declaration. For a vector of
structs, the hand-written repair of each element and field is longer still.

## Your own types

Enums by name (`ASWTomlEnumNames()`), structs (`ASWTomlFields()`) and converters of your own are
described in [Types.md](Types.md). Define an enum's names before its first setting, or the setting
converts the enum as its number. A setting's range needs a type that `operator<` compares; a
validator can check anything else.

## Messages

The TOML object reports to its log callback (`SetLogCallback()`): the warnings of loading and
saving, the notes about what applying the settings changed, each operation that failed, and a
`Get()` or `GetValue()` that gave its default for a value that isn't valid. Each `TASWTomlMessage`
has a `Severity` (`Info`, `Warning` or `Error`), a `Code` (`ETomlCode`, whose values never change),
a `Text` that is a complete sentence, and the `FilePath`, `Line`, `Column`, `Path` and `SystemError`
it is about. `ToString(message)` writes it as one line, as in the example above.

The messages about settings:

| Code | Severity | When |
|---|---|---|
| `KeyAdded` | Info | `Load()` added a missing key, or a missing field of a struct, with its default value |
| `KeyRenamed` | Info | `Load()` renamed an earlier name of a missing key to it |
| `TypeMismatch`, `InvalidValue`, `ValueOutOfRange` | Warning | `Load()` replaced a value (or a part of one) that isn't valid, or a table of settings the file held as another value, or `Get()` gave the default for one |
| `InvalidValue`, `ValueOutOfRange` | Error | `Set()` failed |
| The failure's code | Error | A default value that can't be applied, or a key that can't be added or renamed |

The callback is called on the thread of the operation, after it, with none of the object's locks
held, so it may call the object, even `Load()` or `Save()`. On a first run, it gets a note for each
setting that isn't optional: filter on `Severity` to log only warnings and errors.

### Sending the messages to ASWLog

ASWToml doesn't depend on a logging library. With [ASWLog](https://github.com/ASWSoftware/asw-log)
(1.1 or later), a callback passes each message on at its severity:

```cpp
#include "ASWLog_Interface.h"
#include "ASWToml_Interface.h"

// A log callback that writes ASWToml's messages to 'log', which must outlive the TOML object (or its callback)
ASWToml::TASWTomlLogCallback ToASWLog(ASWLog::IASWLog& log)
{
    return [&log](ASWToml::TASWTomlMessage const& message)
        {
            std::string const text = ASWToml::ToString(message);
            switch (message.Severity)
            {
                case ASWToml::ESeverity::Info:
                    log.LogInfo(text);
                    break;

                case ASWToml::ESeverity::Warning:
                    log.LogWarn(text);
                    break;

                default:
                    log.LogError(text);
                    break;
            }
        };
}

// settings.SetLogCallback(ToASWLog(ASWLog::TASWFileLog::GetInstance()));
```
