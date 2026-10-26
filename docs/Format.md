# The TOML format

What ASWToml reads and writes: the TOML it reads and what a text with an error does, tables and the
paths that name them, comments, encoding and line endings, limits, what an edit changes, and how
each new value is written. The C++ types a value converts to and from are in [Types.md](Types.md),
and the options named here in [Options.md](Options.md).

## What is read

ASWToml reads [TOML 1.1.0](https://toml.io/en/v1.1.0), and so every TOML 1.0 file, which 1.1 reads
the same way. TOML 1.1 adds line breaks, comments and a trailing comma in inline tables, the
escapes `\e` and `\xHH` in basic strings, and times without seconds (`07:32`, read as `07:32:00`).
TOML has one syntax, so there is no dialect to choose. The tests check every file of
[toml-test](https://github.com/toml-lang/toml-test) 2.2.0's TOML 1.1 list: each of its 214 valid
files reads as the values toml-test expects and is written back byte for byte, and each of its 467
invalid files fails.

A text with an error fails to load at its first error; there is no partial document. The result
(and the message the log callback gets) has the code, the `Line` and `Column` of the error (both
from 1, the column in characters, a byte order mark not counted) and a message, and the document
stays as it was: `TASWTomlDocument::Parse()` keeps the text it had, and `Load()` keeps the loaded
document and its file path, so `Save()` doesn't overwrite a file with an error. For example,

```toml
[server]
port = 8080

[server]
host = "example.com"
```

fails with `DuplicateKey` at line 4, column 2: "The table server is already defined on line 1."

| Code | What it is |
|---|---|
| `ParseFailed` | Text that isn't TOML, e.g. `name = Joe` (a string must be in quotes), a value missing after `=`, or a carriage return without a line feed |
| `DuplicateKey` | A key or table defined twice, including a `[a]` header for a table that dotted keys (`a.b = 1`) already made, and `[[x]]` for an `x` that isn't an array of tables |
| `ValueOutOfRange` | An integer outside the 64-bit range, or a float outside a double's (about plus or minus 1.8e308) |
| `InvalidUtf8` | Bytes that aren't valid UTF-8, anywhere in the text, comments included |
| `UnsupportedEncoding` | A UTF-16 or UTF-32 byte order mark |
| `NestingTooDeep` | Tables, arrays and inline tables nested deeper than `Reading.MaxNestingDepth` (see [Limits](#limits)) |
| `FileTooLarge` | A file larger than `Reading.MaxFileSize`, which a load doesn't read |

The whole text is read before its tables are checked, so `DuplicateKey` is reported only for a
text with no other error.

## Tables, keys and paths

TOML writes a table in several forms, and each is one table to every lookup and edit:

```toml
title = "Example"
owner.name = "Tom"

[database]
ports = [8000, 8001]
limits = { connections = 50, "time out" = 30 }

[servers.alpha]
ip = "10.0.0.1"

[[products]]
name = "Hammer"

[[products]]
name = "Nail"
```

- `title` is a key of the root table, the keys before the first header.
- `owner` is a table that the dotted key `owner.name` makes.
- `database` is a table with a `[database]` header, `database.limits` an inline table, and
  `database.ports` an array value.
- `servers` is a table that only the longer header `[servers.alpha]` makes.
- `products` is an array of tables: each `[[products]]` header starts an element, a table.

A `TASWTomlPath` names a table, a key or an element of an array, from the root table. It is made
from a string in TOML's dotted-key syntax, with `[n]` after a key for an element of an array, or
from a list of parts, each a key or an index:

| Path | Names |
|---|---|
| `""` or `{}` | The root table |
| `"owner.name"` | The string `"Tom"` |
| `"database.ports[1]"` | The integer `8001` |
| `R"(database.limits."time out")"` or `{ "database", "limits", "time out" }` | The integer `30` |
| `"servers"` | The table holding `alpha` |
| `"products"` | The array of tables, an array of two tables |
| `"products[1].name"` or `{ "products", 1, "name" }` | The string `"Nail"` |

- A key part is a bare key (`A-Z`, `a-z`, `0-9`, `-` and `_`) or a quoted string, and blanks may
  surround the dots, as in a TOML key. In a list, each string is one key as it is, unquoted:
  `{ "a.b" }` is the key `a.b`, while `"a.b"` is the key `b` of the table `a`.
- An array of tables needs an index: `products.name` names nothing.
- Keys are compared byte for byte, so `Title` isn't `title`, as TOML says.
- A path that isn't valid, e.g. `"a..b"`, makes an operation fail with `InvalidArgument` and the
  reason, e.g. The path "a..b" isn't valid at column 3. A key must be a bare key (A-Z, a-z, 0-9, '-'
  and '_') or a quoted string.
- `GetRoot()` gives the whole document as one table, each table with its keys in the order they
  are first defined in the text.

## Comments

A comment starts with `#` and runs to the end of the line. ASWToml reads and sets the comment lines
above a key or table, and the comment at the end of its line:

```toml
# Settings of MyApp
# (two lines)

# The title
title = "Example"  # shown in the title bar

# The window
[window]  # the main window
# In pixels
width = 640
height = 480
position.x = 10

# The servers
[[servers]]
host = "alpha"

# The backup
[[servers]]
host = "beta"
ports = [
    80,  # http
]
```

| Path | `GetComment()` | `GetTrailingComment()` |
|---|---|---|
| `""` | `Settings of MyApp`, `(two lines)` | |
| `title` | `The title` | `shown in the title bar` |
| `window` | `The window` | `the main window` |
| `window.width` | `In pixels` | |
| `window.height` | | |
| `servers`, `servers[0]` | `The servers` | |
| `servers[1]` | `The backup` | |

- The **file comment** (the root path's) is the comment lines at the start of the text when a
  blank line or the end of the text follows them. Comment lines that lead straight into the first
  key or header are that key's or table's comment instead.
- The **comment** of a key, a table or an element of an array of tables is the comment lines
  directly above its line: the key's line, the table's header, the element's `[[...]]` header. An
  array of tables' is its first element's.
- The **trailing comment** is the comment at the end of that line, after the value (after the
  closing bracket of an array on several lines) or the header.
- Each comment line is read without its `#` and the blanks around the rest, and the lines are
  joined with `"\n"`.
- What has no line of its own has no comment: a table that only dotted keys or longer headers make
  (`window.position` above, or `servers` of `[servers.alpha]`), and a value inside an array or an
  inline table. Setting one fails with `InvalidArgument`. The comments inside an array or an inline
  table (`# http` above) are kept, but can't be read or set.

Setting a comment replaces its lines with one line for each line of the new text, each with the
indentation of the line below and `Writing.CommentPrefix` (`# `); an empty text removes them. A
new file comment goes at the start of the text with a blank line under it. A trailing comment
replaces the one there, or is added after two spaces, and can't hold a line break. A comment can't
hold a control character other than a tab and a line break (`InvalidValue`), as TOML says. A
settings class's comments are written over the file's, or only where it has none, as
`Writing.CommentPolicy` says (see [Settings.md](Settings.md)).

## Encoding and line endings

- Text is UTF-8, with or without a byte order mark, which is kept when the text is written
  (`Writing.Bom`). A text that starts with a UTF-16 or UTF-32 byte order mark isn't read
  (`UnsupportedEncoding`), and one that isn't valid UTF-8 fails with `InvalidUtf8` at its first
  such byte.
- Lines end with LF or CRLF, even mixed in one text; a carriage return without a line feed is an
  error, as TOML says. Files are read and written in binary mode, so the library chooses the line
  endings, not the C runtime: each line keeps its own, and a new line gets the text's most common
  one (the first one found in a tie), or the platform's in a new text (CRLF on Windows, LF
  elsewhere). `Writing.LineEnding` can give every line one line ending instead, in comments and
  multi-line strings too, which read the same either way (a multi-line string reads each line
  break as `"\n"`).
- A last line without a line break keeps it so, until an edit adds a line after it: it then gets
  one first.

## Limits

- A file larger than 16 MiB isn't read (`FileTooLarge`; `Reading.MaxFileSize`). The limit is only
  for files: `Parse()` and `LoadText()` read a text of any size.
- Values may be nested 100 deep (`Reading.MaxNestingDepth`, at most 128): how many tables, arrays
  and inline tables a value is inside, the root table not counted. The `1` of `a.b.c = [[1]]` is 4
  deep (the tables `a` and `b`, and two arrays), a key under `[server.http]` 2, and a key of an
  element of `[[products]]` 2 (the array and the element). A deeper text fails with
  `NestingTooDeep`, so a file can't overflow the stack, and an edit that would nest a value deeper
  fails with it too.

## What is kept, and what an edit changes

A text that is read and written without changes is written back byte for byte: comments, blank
lines, spacing, indentation, line endings, the byte order mark, the order of keys and tables, and
the form of each value (`0xFF`, `1_000`, `+1.5e3`, a literal or multi-line string, escapes, a date
and time written with a space, the layout of an array or inline table). An edit writes only the
text it changes, and new text uses only the syntax TOML 1.0 reads:

- **A new value of a key** replaces only the value's text: the key, the blanks around `=` and a
  comment at the end of the line stay. A string keeps its form (basic, literal or multi-line; a
  literal string that can't hold the new text becomes basic), and an integer its base
  (hexadecimal, octal or binary) unless it is negative, as long as the value options say `Auto`
  (see [How a new value is written](#how-a-new-value-is-written)). Setting the value a key has
  changes nothing.
- **A new key** goes after the last key line of its table, with that line's indentation; after
  the last dotted key of a table that dotted keys make (`log.file` after `log.level`); inside the
  braces of an inline table, in the spacing of its entries.
- **A new table** gets a `[header]` after the last header of its parent table and the tables under
  it (at the end of the text for a table of the root), after a blank line
  (`Writing.BlankLineBeforeNewTable`). A table that only longer headers make gets its own header,
  before the first of theirs.
- **Appending** to an array adds the element after the last one, in its layout: after `, ` in an
  array on one line, on a line of its own in an array with one element per line (keeping a comment
  at the end of the last line, and a trailing comma). An element of an array of tables gets a new
  `[[header]]` after the last element and the tables under it.
- **Removing** a key removes its line, with its comment lines and the comment at its end; a table,
  its header and lines and the tables under it, and the dotted keys that make it. A blank line
  goes with what is removed when blank lines would otherwise pile up.
- **Renaming** a key or table changes only that key part, in each place that names it: its line,
  each header and each dotted key that goes through it. A name that isn't a bare key is written as
  a basic string, or as a literal one where the old key part was literal and one can hold it.

For example, setting `window.color` to 0xFF0000, `window.title` to `D:\Apps`, `window.width` to 800
(a new key), `server.font.size` to 11 and `server.log.file` to `app.log` (new keys of an inline
table and of a table of dotted keys), appending 8080 to `server.ports` and `gamma` to
`server.hosts`, and setting `view.zoom` to 1.5 (a new table) in

```toml
# Settings

[window]
  color = 0x3366CC  # set by the installer
  title = 'C:\Apps'

[server]
ports = [ 80, 443 ]
hosts = [
    "alpha",
    "beta",  # the backup
]
font = { name = "Consolas" }
log.level = 2
```

gives

```toml
# Settings

[window]
  color = 0xFF0000  # set by the installer
  title = 'D:\Apps'
  width = 800

[server]
ports = [ 80, 443, 8080 ]
hosts = [
    "alpha",
    "beta",  # the backup
    "gamma",
]
font = { name = "Consolas", size = 11 }
log.level = 2
log.file = "app.log"

[view]
zoom = 1.5
```

### Setting a whole table or array

Setting a table or an array that is there to a new table or array merges the two, so only what
differs changes, and a file's comments and layout survive a settings class writing a struct or a
vector of structs (see [Settings.md](Settings.md)):

- Each key of the new table is set as above (a table in it is merged too), each key it doesn't
  have is removed, and each new one added; the keys that stay keep their order, lines and comments.
- Of the elements of an array or an array of tables, those that are the same as new ones stay (the
  longest common subsequence of the two arrays), the others between them are set to the new ones in
  order, and the ones left over are removed, or added in their place. An array of tables keeps its
  `[[header]]` blocks with their comments.

For example, setting `window` to a table without `old`, with `width` 800 and a new key
`maximized`, and `servers` to the same servers with a new one first, in

```toml
[window]
# In pixels
width = 640  # wide
height = 480
old = true

[[servers]]
host = "alpha"

# The backup
[[servers]]
host = "beta"
```

gives

```toml
[window]
# In pixels
width = 800  # wide
height = 480
maximized = false

[[servers]]
host = "new"

[[servers]]
host = "alpha"

# The backup
[[servers]]
host = "beta"
```

A value of another type replaces the one there: a table or an array of tables set to a value they
can't hold is removed and the value written as a key (`t = 5` for `[t]`), and a key set to a table
gets an inline table (`t = { x = 1 }`).

## How a new value is written

A value that replaces one keeps the form of that one where it can (see above); a new value is
written as this table says. A value that can't be written fails with `InvalidValue`: a string or
key that isn't valid UTF-8, or a date or time that isn't valid.

| Type | Written | Examples |
|---|---|---|
| String | A basic string, with `\"`, `\\`, `\b`, `\t`, `\n`, `\f` and `\r`, and the other control characters as `\u00XX`; a literal string when the text has a backslash and a literal string can hold it (`Values.StringStyle`) | `"it's"`, `"a\nb\u001B"`, `'C:\Users\me'` |
| Integer | Decimal, or the base of `Values.IntegerBase` (hexadecimal in upper case); a negative number always in decimal | `-42`, `0xFF`, `0o755`, `0b101` |
| Float | The shortest text that reads back as the same double, with `.0` when it has no `.` or exponent | `1.0`, `0.1`, `1e+16`, `-0.0`, `inf`, `-inf`, `nan` |
| Boolean | `true` or `false` | `true` |
| Offset date-time | The date, `T`, the time, and `Z` or the offset | `1979-05-27T07:32:00Z`, `1979-05-27T00:32:00-07:00` |
| Local date-time | The date, `T` and the time | `1979-05-27T07:32:00` |
| Local date | `YYYY-MM-DD` | `1979-05-27` |
| Local time | `HH:MM:SS`, with a fraction of a second only when it isn't 0, in as few digits as it needs | `07:32:00`, `07:32:00.5` |
| Array | On one line, its tables as inline tables | `[1, 2]`, `["a", { x = 1 }]`, `[]` |
| Table | A `[header]` and its keys, then its tables as headers of their own and its arrays of tables as `[[header]]`s; an inline table where a header can't go (in an array or an inline table, or as a key's new value) | `[window]`, `{ x = 1 }` |

- A key is written bare when it can be (not empty, and only `A-Z`, `a-z`, `0-9`, `-` and `_`),
  else as a basic string: `port`, `"host name"`, `""`.
- A table whose only keys are tables gets no header of its own: setting `view.panel` to an empty
  table in an empty text writes `[view.panel]`, not `[view]`.
- An array that isn't empty and holds only tables is written as an array of tables where a header
  can go, and an empty array as `[]`.
- New text uses only TOML 1.0's syntax: an inline table on one line without a trailing comma,
  `\u001B` for the escape character, and a time with its seconds. A new entry of an inline table
  that the file writes on several lines (TOML 1.1) goes on a line of its own, as the others.
