# Options reference

How TOML text is read, written and saved, and how values are written, is set by one
`ASWToml::TASWTomlOptions` (`ASWToml_Options.h`). Its settings are in four groups: `Reading`,
`Writing`, `Saving` and `Values`. TOML has one syntax, so there is no dialect to choose: the
reading options are limits. The defaults keep each line's line ending and the file's byte order
mark, write `# comment` lines, save atomically, and write an integer in decimal and a path as a
literal string, so most applications change nothing, or a setting or two. `ToJson()` has options of
its own, given to each call: [`TASWTomlJsonOptions`](#taswtomljsonoptions-how-tojson-writes-json).

```cpp
ASWToml::TASWBaseToml toml;
ASWToml::TASWTomlOptions options = toml.GetOptions();
options.Writing.LineEnding = ASWToml::ELineEnding::LF; // Every line ends with LF, on every platform
options.Saving.Backup = true;                          // Each save keeps the previous file as app.toml.bak

ASWToml::TASWTomlResult const set = toml.SetOptions(options);
if (!set.IsOk())
    std::cerr << set.Message << "\n"; // What is wrong, naming the setting

ASWToml::TASWTomlResult const loaded = toml.Load("app.toml");
```

What each option changes in the text is described in [Format.md](Format.md).

## When options apply

- A TOML object (`TASWBaseToml`) reads, writes and saves with its options, which start as the
  defaults. `SetOptions()` replaces them and reads the loaded text again with them, so the reading
  options apply to it; the text itself, the file path and `IsModified()` stay. Set the options
  before the first `Load()`: a settings class does it in its constructor (see
  [Settings.md](Settings.md)), and `SetOptions()` doesn't apply the settings again. `GetOptions()`
  gives a copy.
- `SetOptions()` checks the options first (see [Validation](#validation)), and invalid ones change
  nothing: it returns `InvalidOptions`, with a message that names the setting, and reports it to
  the log callback. So does a text the new reading options refuse (e.g. `NestingTooDeep` for a
  lower `Reading.MaxNestingDepth`). A `TASWTomlDocument`'s `Parse()`, `Reset()` and
  `SetOptions()` check them the same way.
- `Load()`, `LoadText()`, `Save()`, `SaveAs()` and `SetOptions()` never overlap, so a load or a
  save uses one set of options from start to end.
- The value options can also be given for one call (`GetValue()`, `TryGetValue()`, `SetValue()`
  and `Append()` take a `TASWTomlValueOptions` after their other arguments) or for one setting
  (`.Values` in its options; see [Settings.md](Settings.md)), e.g. for a colour written in
  hexadecimal:

```cpp
ASWToml::TASWTomlValueOptions hexadecimal = toml.GetValueOptions();
hexadecimal.IntegerBase = ASWToml::EIntegerBase::Hexadecimal;
ASWToml::TASWTomlResult const result = toml.SetValue("view.accent", 0x3366CC, hexadecimal); // accent = 0x3366CC
```

## `Reading`: how text is read (`TASWTomlReadingOptions`)

| Setting | Default | What it does |
|---|---|---|
| `MaxFileSize` | 16 MiB | The largest file a load reads, in bytes; a larger one fails with `FileTooLarge`. `DefaultMaxFileSize` is the default. `Parse()` and `LoadText()` read a text of any size. |
| `MaxNestingDepth` | 100 | How many tables, arrays and inline tables a value may be inside, the root table not counted, from 1 to `MaxNestingDepthLimit` (128): e.g. 4 for the `1` of `a.b.c = [[1]]`, 2 for a key under `[server.http]` or of an element of `[[products]]`. A deeper text fails with `NestingTooDeep`, and so does an edit that would nest a value deeper, so a file can't overflow the stack. `DefaultMaxNestingDepth` is the default. |

## `Writing`: how lines are written (`TASWTomlWritingOptions`)

Text read from a file is written back as it was, apart from what an edit changes. These settings
are for the lines an edit adds, and for the text as a whole.

| Setting | Default | What it does |
|---|---|---|
| `CommentPrefix` | `"# "` | What is written before the text of a new comment line, so the comment `Valid range: 0-4` is `# Valid range: 0-4`, and before a new comment at the end of a line. It must be `#`, TOML's only comment character, followed by nothing but spaces and tabs (`"#"`, `"#  "`), so a comment reads back as the text written after it. |
| `LineEnding` | `Auto` | The line endings of the text (`ELineEnding`): `Auto` keeps each line's own, and gives a new line the text's most common one (the platform's for a new text); `Native` gives every line the platform's (CRLF on Windows, LF elsewhere); `LF` and `CRLF` give every line that one, in comments and multi-line strings too. |
| `Bom` | `Auto` | Whether the text starts with a UTF-8 byte order mark (`EBomMode`): `Auto`, as the loaded text did (none for a new one), `Always` or `Never`. |
| `BlankLineBeforeNewTable` | `true` | Whether a new table's header is written after a blank line (unless it is the first thing in the text). |
| `CommentPolicy` | `CodeWins` | Whose comment a setting's key, a table and the file get when the loaded file's differs from the one the code declares (`ECommentPolicy`): `CodeWins`, the code's, written over a comment edited in the file, or `FileWins`, the file's, with the code's written only where the file has none. A comment that reads the same is left as it is, and a comment the code doesn't declare is never changed. See [Settings.md](Settings.md). |

## `Saving`: how files are saved (`TASWTomlSavingOptions`)

| Setting | Default | What it does |
|---|---|---|
| `AtomicSave` | `true` | Whether a save writes a temporary file in the same folder, flushes it to the disk and then replaces the file with it, so a failure or a crash while saving leaves the previous file whole. Turn it off only when the folder can't be written to: the file itself is then overwritten in place. |
| `Backup` | `false` | Whether a save first copies the previous file to its name followed by `BackupSuffix`, replacing an older backup. A backup that fails is reported as a warning (`BackupFailed`), and the file is still saved. |
| `BackupSuffix` | `".bak"` | Added to the file name for the backup, e.g. `app.toml.bak`. |
| `CreateFolders` | `true` | Whether a save creates the file's folder, and the folders above it, when they don't exist. When off, a save to a missing folder fails with `FileNotFound`. |

An atomic save keeps the file's attributes and security on Windows, and its permissions (and, when
allowed, its owner) elsewhere, where a file that is a symbolic link stays one: the file it points to
is replaced.

## `Values`: how values are written (`TASWTomlValueOptions`)

Every form is read, whichever these say; they choose how a new value is written. A value that
replaces one keeps that one's form with `Auto` (see [Format.md](Format.md)).

| Setting | Default | What it does |
|---|---|---|
| `StringStyle` | `Auto` | How a string is written (`EStringStyle`): `Auto`, as a literal string (`'C:\Users\me'`) when the text holds a backslash and a literal string can hold it, else a basic one, and in the form of the string it replaces; `Basic`, always a basic string (`"C:\\Users\\me"`); `Literal`, a literal string whenever one can hold the text (no `'` and no control character but a tab). |
| `IntegerBase` | `Auto` | The base an integer is written in (`EIntegerBase`): `Auto`, decimal, or the base of the integer it replaces; `Decimal` (`255`), `Hexadecimal` (`0xFF`), `Octal` (`0o377`) or `Binary` (`0b11111111`). A negative number is always written in decimal, as TOML's other bases have no sign. |

## `TASWTomlJsonOptions`: how `ToJson()` writes JSON

`ToJson()` (`ASWToml_Json.h`, see [Types.md](Types.md#json)) takes its options with each call, not
from `TASWTomlOptions`: exporting is an operation, not a way of reading or writing TOML. They are
given with designated initializers, in the order of the members, e.g.
`{ .Style = ASWToml::EJsonStyle::Tagged, .Indent = 0 }`.

| Setting | Default | What it does |
|---|---|---|
| `Style` | `Plain` | The form of the values (`EJsonStyle`): `Plain`, TOML's values as JSON's (a date or time as a string); `Tagged`, toml-test's form, every value but a table or an array as `{"type": "integer", "value": "42"}`, which loses nothing. |
| `NonFinite` | `Fail` | How `Plain` writes `inf`, `-inf` and `nan`, which JSON has no number for (`EJsonNonFinite`): `Fail`, `ToJson()` fails with `InvalidValue`; `Null`, `null`; `String`, the float as TOML writes it, as a string (`"inf"`). `Tagged` always writes them. |
| `Indent` | 2 | 0 for the whole text on one line, without blanks (`{"a":1,"b":[1,2]}`); else the spaces each level is indented by, from 1 to `MaxIndent` (16), laid out as JavaScript's `JSON.stringify(value, null, Indent)` and Python's `json.dumps(value, indent=Indent)` lay it out: each key and each element on a line of its own, `": "` after a key, and an empty table or array as `{}` or `[]`. A tagged value is always on one line. |

## Validation

Each group's `Validate()` checks its own settings, and `TASWTomlOptions::Validate()` checks the four
groups in the order `Reading`, `Writing`, `Saving`, `Values`. Each returns `Ok`, or `InvalidOptions`
with a message about the first problem it finds. The rules:

- `Reading.MaxFileSize` isn't 0.
- `Reading.MaxNestingDepth` is from 1 to `MaxNestingDepthLimit` (128).
- `Writing.CommentPrefix` starts with `#` and holds nothing else but spaces and tabs.
- With `Saving.Backup`, `Saving.BackupSuffix` isn't empty and holds no `/`, `\`, `:`, line break or
  NUL.
- Each enumeration (`LineEnding`, `Bom`, `CommentPolicy`, `StringStyle`, `IntegerBase`) is one of
  its values, in case one was made from a number.

`TASWTomlJsonOptions::Validate()` checks the JSON options the same way: `Indent` is at most
`MaxIndent` (16), and `Style` and `NonFinite` are among their values. `ToJson()` calls it, and fails
with its `InvalidOptions` before writing anything.

`ToString()` names a value of each enumeration, e.g. `ToString(ASWToml::ELineEnding::CRLF)` is
`"CRLF"` and `ToString(ASWToml::EJsonStyle::Tagged)` is `"Tagged"`, and `"Unknown"` for a number
that isn't one of its values.
