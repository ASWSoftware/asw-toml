# C++ types and TOML values

How an application's C++ values become TOML values and back: the converters the library has, what
each reads and writes and how it fails, dates and times, enums, containers, maps, structs,
`TASWTomlValue` itself, a converter of your own, and the JSON a value is exported as. The TOML text
each value is written as is in [Format.md](Format.md).

## Converters

`TASWTomlConverter<T>` converts a value of the type `T` to and from a `TASWTomlValue`, a TOML value
of any type. Every typed operation uses it: `GetValue()`, `TryGetValue()`, `SetValue()` and
`Append()` of a TOML object, and the settings (`TASWTomlSetting<T>`, see
[Settings.md](Settings.md)). The concept `TomlConvertible<T>` says whether `T` has one; a type
without one is a compile error where it is used.

```cpp
int const width = toml.GetValue("window.width", 640); // 640 when missing or not an int
std::vector<std::string> recent;
ASWToml::TASWTomlResult const read = toml.TryGetValue("recent_files", recent);
ASWToml::TASWTomlResult const set = toml.SetValue("window.position", std::pair<int, int>(100, 50));
```

The last line writes `position = [100, 50]` in the `[window]` table.

A converter reads only the TOML types it names: `port = "80"` is a string, so reading it as an `int`
fails, and so does `3.0` (a float); an integer is read as a float only when the float holds it
exactly. A failure has its code, a message that shows the value as TOML text, and, for a failure
inside the value, the place in it in `Path`, which the typed operations put after the value's own
path (`servers[1].port`):

| Code | When | Example message |
|---|---|---|
| `TypeMismatch` | The value has a TOML type the C++ type doesn't read | The value "80" is a string, not an integer. |
| `ValueOutOfRange` | A number outside the type's range | The value 300 is out of range: it must be from 0 to 255. |
| `InvalidValue` | A value the type doesn't accept, or one TOML can't hold, so it can't be written | The value "Purple" isn't a name of the enum: it must be Red, Green or Blue. |
| `NotFound` | A struct's table lacks a field's key | The table has no key port. |

On failure the C++ value (or the TOML value being written) is left as it was, so `GetValue()` gives
its default, which it reports as a Warning to the log callback, and `TryGetValue()` returns the
failure and reports nothing.

## The library's converters

| C++ type | TOML value | Notes |
|---|---|---|
| `TASWTomlValue` | Any | As it is; never fails |
| `std::string`, `std::u8string` | String | UTF-8; writing text that isn't valid UTF-8 is `InvalidValue` |
| `std::filesystem::path` | String | The path in UTF-8, with its separators as it holds them |
| `bool` | Boolean | `1` and `"true"` are `TypeMismatch` |
| Integer types | Integer | `signed char` to `long long` and `unsigned char` to `unsigned long long`, so `std::int8_t` to `std::uint64_t`; a number outside the type's range is `ValueOutOfRange` |
| `float`, `double` | Float, or an integer the type holds exactly | `inf` and `nan` keep their sign |
| An enum with names | String | One of its names (see [Enums](#enums)) |
| An enum without names | Integer | Its underlying type's number, e.g. `std::byte` from 0 to 255 |
| `TASWTomlOffsetDateTime`, `TASWTomlLocalDateTime`, `std::chrono::year_month_day`, `TASWTomlLocalTime` | Offset date-time, local date-time, local date, local time | As they are |
| `std::chrono::sys_time<D>`, e.g. `system_clock::time_point` and `sys_seconds` | Offset date-time | An instant (see [Dates and times](#dates-and-times)) |
| `std::chrono::local_time<D>`, e.g. `local_seconds` | Local date-time | |
| `std::chrono::sys_days`, `local_days` | Local date | |
| `std::chrono::hh_mm_ss<D>` | Local time | |
| `std::vector<T>`, `std::array<T, N>`, `std::set<T>`, `std::unordered_set<T>` | Array | Of any `T` with a converter (see [Containers](#containers)) |
| `std::pair<T1, T2>`, `std::tuple<T...>` | Array | Of exactly as many elements, each its own type |
| `std::map<K, T>`, `std::unordered_map<K, T>` | Table | Keys of a string, integer or enum type (see [Maps](#maps)) |
| A struct with `ASWTomlFields()` | Table | Its fields by key (see [Structs](#structs)) |

There is no converter for `char`, `wchar_t`, `char8_t`, `char16_t` and `char32_t` (a number or a
character?), `std::string_view` and `char const*` (a value read into them would dangle; the typed
operations take a text as a string), `std::optional<T>` on its own (only as a struct's member),
`std::variant`, durations such as `std::chrono::seconds` (TOML has no duration; write a number of
seconds as an integer) and `long double`. A converter of your own can add any of them.

## Numbers

- An integer type reads an integer only. TOML integers are 64-bit and signed, so an unsigned 64-bit
  type holds 0 to 9223372036854775807 both ways: a larger value can't be written
  (`ValueOutOfRange`). `std::int8_t` and `std::uint8_t` (`signed char` and `unsigned char`) are
  numbers.
- `double` reads a float as it is, or an integer of at most 53 significant bits (every integer from
  -2^53 to 2^53, and larger ones with enough trailing zero bits), so `ratio = 1` reads as 1.0;
  another integer is `InvalidValue` ("The value 9007199254740993 can't be held exactly: it has more
  than 53 significant bits."). `float` reads the nearest float, an integer of at most 24
  significant bits, and fails with `ValueOutOfRange` for a float beyond its range.
- A float is written as the shortest text that reads back as the same number: `0.1f` is written as
  `0.1`, not `0.10000000149011612`.

## Strings and paths

- `std::string` holds UTF-8, as TOML does; `std::u8string` holds the same bytes as `char8_t`.
- `std::filesystem::path` is written in UTF-8 on every platform, with the separators as the path
  holds them, so a Windows path is `'C:\Users\me'` (a literal string, with the default value
  options). A path whose UTF-8 wouldn't read back as the same path (an unpaired surrogate in a
  Windows path, or bytes that aren't UTF-8 in a POSIX one) is `InvalidValue`, so what is written
  always reads back. `ASWToml::PathToUtf8()` and `Utf8ToPath()` (`ASWToml_File.h`) convert a path
  the same way.

## Dates and times

TOML has four date and time types, which the library holds as they are written, each field in a
`std::chrono` type:

| TOML | C++ | Fields |
|---|---|---|
| Offset date-time, `1979-05-27T00:32:00-07:00` | `TASWTomlOffsetDateTime` | `Date` (`year_month_day`), `Time`, `Offset` (`minutes`, -7 hours here) |
| Local date-time, `1979-05-27T07:32:00` | `TASWTomlLocalDateTime` | `Date`, `Time` |
| Local date, `1979-05-27` | `std::chrono::year_month_day` | |
| Local time, `07:32:00.5` | `TASWTomlLocalTime` | `Hour`, `Minute`, `Second`, `Fraction` (`nanoseconds`) |

The library's types keep a leap second (second 60) and the offset as written, and convert to and
from `std::chrono`: `TASWTomlOffsetDateTime::FromSysTime(utc, offset)` and `ToSysTime<D>()`,
`TASWTomlLocalDateTime::FromLocalTime()` and `ToLocalTime<D>()`, and
`TASWTomlLocalTime::FromDuration()` and `ToDuration<D>()`, each giving `std::nullopt` for what the
other side can't hold.

The `std::chrono` converters:

- `std::chrono::sys_time<D>` (`system_clock::time_point`, `sys_seconds`) is an instant: it reads an
  offset date-time at any offset as the instant in UTC, and writes it in UTC (`Z`). A local
  date-time is `TypeMismatch`, since its offset is unknown.
- `std::chrono::local_time<D>` reads and writes a local date-time; an offset date-time is
  `TypeMismatch`, since its offset would be lost.
- `sys_days`, `local_days` and `year_month_day` read and write a local date (a date has no offset,
  so `sys_days` is the day it names in UTC). Writing a date that isn't real (2023-02-30) is
  `InvalidValue`.
- `std::chrono::hh_mm_ss<D>` reads and writes a local time; a leap second, 23:59:60, reads as
  24:00:00, which is written as 23:59:60 again.
- Each reads a time rounded down to its duration `D` (a fraction of a second is cut to whole
  seconds for `sys_seconds`), and a leap second is the next minute's second 0. `D` is any duration
  of a whole number of seconds or a second divided by a number that divides 10^9 (milliseconds,
  microseconds, nanoseconds, MSVC's 100 ns).

TOML's years are 0 to 9999: writing a date or time outside them is `ValueOutOfRange`, and so is
reading one that `D`'s count can't hold. **`system_clock` counts nanoseconds with libstdc++ (GCC,
MinGW)**, so there it holds only 1677-09-21T00:12:43.145224192Z to 2262-04-11T23:47:16.854775807Z,
and a licence that expires on 9999-12-31 fails ("The value 9999-12-31T00:00:00Z is out of range:
it must be from 1677-09-21T00:12:43.145224192Z to 2262-04-11T23:47:16.854775807Z."). Use
`sys_seconds` for an instant, or `year_month_day` for a day, which hold every date TOML writes on
every standard library.

## Enums

An enum is converted by name when the application gives its names: it declares a function
`ASWTomlEnumNames()` next to the enum, in the enum's namespace (or, for an enum declared in a class,
as a friend in that class), so argument-dependent lookup finds it. Its argument only selects it,
and it returns the names, e.g. as a reference to an array of `TASWTomlEnumName<T>`:

```cpp
enum class EColor
{
    Red,
    Green,
    Blue,
};

inline auto const& ASWTomlEnumNames(EColor)
{
    static constexpr ASWToml::TASWTomlEnumName<EColor> names[] = {
        { EColor::Red, "Red" }, { EColor::Green, "Green" }, { EColor::Blue, "Blue" },
        { EColor::Green, "Lime" }, // An earlier name: read as Green, never written
    };

    return names;
}
```

- A value is written as the first name listed for it, as a string (`color = "Green"`); a value
  with no name can't be written (`InvalidValue`), since it couldn't be read back.
- Reading takes any of its names, with the ASCII letters' case ignored (`"lime"` is Green), and
  the number of a value listed with a name, so a file that stored the number still reads. Another
  string or number is `InvalidValue`, with a message that lists the names.
- An enum without `ASWTomlEnumNames()` is an integer of its underlying type, whether the number is
  one of its enumerators or not (bit flags: 5 is the value 5).

**Define `ASWTomlEnumNames()` before the enum's first use, and spell it exactly.** The converter
looks for it where the enum is first converted (a `TASWTomlSetting<EColor>`, a `GetValue()` of an
`EColor`, or `TomlConvertible<EColor>`), and needs its return type there. One defined after that
use, one only declared there with an `auto` return type and defined later, or a misspelled one
(`ASWTomlEnumName()`) isn't found, and the enum converts as its number (`color = 1`), without an
error, and mostly without a warning (recent GCC versions warn about the one declared with `auto`,
`-Wsfinae-incomplete`; Clang says nothing). A test that writes one value and checks that it is a
string catches it:

```cpp
ASWToml::TASWTomlValue value;
assert(ASWToml::TASWTomlConverter<EColor>::ToValue(EColor::Green, value, {}).IsOk() && value.IsString());
```

## Containers

- `std::vector<T>` reads any array (an array value or an array of tables) and writes an array of
  its elements, so a vector of a struct is an array of tables (`[[servers]]`), and an empty vector
  `[]`. A vector of vectors works too, and `std::vector<TASWTomlValue>` holds an array whose
  elements' types differ (`[1, "a", 2.5]`). `T` needs a converter and a default constructor.
- `std::array<T, N>`, `std::pair` and `std::tuple` need an array of exactly that many elements
  ("The value [1, 2] has 2 elements, not 3."); each element of a pair or tuple is converted as its
  own type (`["example.com", 443]` for `std::pair<std::string, int>`).
- `std::set` and `std::unordered_set` fail with `InvalidValue` for an element that reads as one the
  set already has, as that is likely a mistake in the file ("The value 1 reads as an element the
  set already has.", or `"red"` after `"Red"` for an enum with names). An unordered set is written
  sorted by its elements' TOML text, so a file doesn't change from run to run: 9, 10 and 80 are
  written as `[10, 80, 9]`.
- The first element that fails fails the container, with its index in `Path` (`[1]`, or
  `[1].port` for a key inside it).

## Maps

`std::map<K, T>` and `std::unordered_map<K, T>` read any table (a `[header]` table, dotted keys or
an inline table) and write a table of their keys, for a key type `K` that is:

- `std::string`: the key as it is.
- An integer type: the number's decimal digits, `0` or digits that don't start with 0 after a `-`
  for a negative number (`01`, `+1` and `1_000` are `InvalidValue`), in the type's whole range: a
  key is text, so 18446744073709551615 is a key of `std::uint64_t`, though a TOML integer can't
  hold it.
- An enum: one of its names with the case ignored, or for an enum without names, a number of its
  underlying type.

A key that reads as one the map already has (`red` after `Red`) is `InvalidValue`, and so is
writing two values of an enum that would be written as one name. A failure has the key in its
`Path` (`port`, or `"host name".port` for a key inside its value). A `std::map` is written in its
order, a `std::unordered_map` with its keys sorted by their text, byte by byte (the keys 9, 10 and
80 in the order 10, 80, 9).

## Structs

A struct is a table when the application lists its fields: it declares `ASWTomlFields()` next to
the struct, found as `ASWTomlEnumNames()` is, returning a `std::tuple` of `Field(key, member)` and
`OptionalField(key, member)`:

```cpp
struct TServer
{
    std::string Host;
    std::uint16_t Port = 80;
    bool Enabled = true;
    std::optional<std::string> Comment;
};

inline auto ASWTomlFields(TServer const&)
{
    return std::tuple{ ASWToml::Field("host", &TServer::Host), ASWToml::Field("port", &TServer::Port),
        ASWToml::OptionalField("enabled", &TServer::Enabled), ASWToml::Field("comment", &TServer::Comment) };
}
```

- Each member is converted by its own type's converter (a compile error names a member type
  without one), and a `std::optional<M>` member as `M`. So `std::vector<TServer>` is an array of
  tables and a `TServer` member of another struct a table in it.
- Reading needs a table, and reads it into a copy of the struct, field by field, which replaces
  the struct only when every field read. A key the table lacks is `NotFound` for a `Field()` ("The
  table has no key port."), keeps the member's value for an `OptionalField()`, and makes a
  `std::optional` member `std::nullopt`. Keys the fields don't name are ignored, so a file that a
  newer version of the application wrote still reads.
- Writing gives a table of every field in the order listed, an optional one too, but leaves out a
  `std::optional` member that is `std::nullopt`.
- A key named by two fields is `InvalidValue`. For private members, declare `ASWTomlFields()` as a
  friend in the struct. A class derived from one with `ASWTomlFields()` uses that list, unless it
  declares its own.

A setting of a struct, a vector or a map is brought up to date by parts when a file is loaded,
each missing or invalid part getting its default, and its `Set()` keeps the keys of the file's
table that the fields don't name (see [Settings.md](Settings.md)); `IASWToml::SetValue()` writes
the whole value, so such keys are removed.

## TASWTomlValue

`TASWTomlValue` holds a TOML value of any type (`ETomlType`): `GetType()`, `IsString()` and the
others, and `AsString()` and the others, which return a pointer to the value, or `nullptr` for
another type. A table (`TKeyValueVector`, keys in order) has `Find(key)`, `Set(key, value)` and
`Remove(key)`, and an array (`TValueVector`) `Append(value)`. A default `TASWTomlValue` is an
empty table. `operator==` compares tables with their keys in any order, as TOML tables are
unordered. A document's values are `TASWTomlValue`s (`TASWTomlDocument::Find()` and `GetRoot()`),
and `GetValue<TASWTomlValue>()` reads a value of any type:

```cpp
ASWToml::TASWTomlValue const plugins = toml.GetValue("plugins", ASWToml::TASWTomlValue());
ASWToml::TASWTomlValue const* spell = plugins.Find("spell");
ASWToml::TASWTomlValue const* enabled = spell != nullptr ? spell->Find("enabled") : nullptr;
bool const spellChecking = enabled != nullptr && enabled->AsBoolean() != nullptr && *enabled->AsBoolean();
```

`Find()` of a value that isn't a table is `nullptr`, so `spell` needs no other check. It compares
the table's keys one by one, so in a large table, find through the document instead:
`TASWTomlDocument::Find()`, and every lookup of a TOML object (`GetValue()`, `TryGetValue()`,
`Contains()`, a setting's `Get()`), finds a key of a large table through the document's index, in
O(log n).

## A converter of your own

Any other type gets a converter by specializing `ASWToml::TASWTomlConverter`, with a `FromValue()`
that reads a value and a `ToValue()` that writes one `FromValue()` reads back. On failure they
return the error and leave the C++ value or the TOML value as it was. Declare it before the type's
first use, since `TomlConvertible<T>` is decided there, as for the enum names. A colour written as
a string, `"#3366CC"`:

```cpp
struct TColor
{
    std::uint8_t Red = 0;
    std::uint8_t Green = 0;
    std::uint8_t Blue = 0;
};

namespace ASWToml
{

template <>
struct TASWTomlConverter<TColor>
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, TColor& out, TASWTomlValueOptions const& options)
    {
        std::string text;
        TASWTomlResult result = TASWTomlConverter<std::string>::FromValue(value, text, options);
        if (!result.IsOk())
            return result; // TypeMismatch for a value that isn't a string

        unsigned rgb = 0;
        auto const [end, error] = std::from_chars(text.data() + 1, text.data() + text.size(), rgb, 16);
        if (text.size() != 7 || text[0] != '#' || error != std::errc() || end != text.data() + text.size())
        {
            result.Code = ETomlCode::InvalidValue;
            result.Message = "The value \"" + text + "\" isn't a colour, e.g. \"#3366CC\".";
            return result;
        }

        out = TColor{ std::uint8_t(rgb >> 16), std::uint8_t(rgb >> 8), std::uint8_t(rgb) };
        return result;
    }

    static TASWTomlResult ToValue(TColor const& in, TASWTomlValue& value, TASWTomlValueOptions const& /*options*/)
    {
        char text[8];
        std::snprintf(text, sizeof(text), "#%02X%02X%02X", unsigned(in.Red), unsigned(in.Green), unsigned(in.Blue));
        value = TASWTomlValue(text);
        return TASWTomlResult();
    }
};

} // namespace ASWToml
```

Then `TASWTomlSetting<TColor>`, `GetValue()` of a `TColor`, `std::vector<TColor>` and a struct
member of type `TColor` work as for the library's types (a container's elements need a default
constructor, as `TColor` has). A setting's range needs an `operator<`; a validator can check
anything else.

A partial specialization converts a group of types, e.g. every identifier type made from a
template, as an integer; a full specialization wins over a partial one, so a type the library
converts (an enum, say) can have a converter of its own:

```cpp
template <typename TTag>
struct TId
{
    std::int64_t Value = 0;
};

namespace ASWToml
{

template <typename TTag>
struct TASWTomlConverter<TId<TTag> >
{
    static TASWTomlResult FromValue(TASWTomlValue const& value, TId<TTag>& out, TASWTomlValueOptions const& options)
    {
        return TASWTomlConverter<std::int64_t>::FromValue(value, out.Value, options);
    }

    static TASWTomlResult ToValue(TId<TTag> const& in, TASWTomlValue& value, TASWTomlValueOptions const& options)
    {
        return TASWTomlConverter<std::int64_t>::ToValue(in.Value, value, options);
    }
};

} // namespace ASWToml
```

The value options passed in are those of the operation or setting (see [Options.md](Options.md));
pass them on to the converters of a value's parts. A setting's converter is called with its TOML
object's file lock held, so it mustn't call that object's `Load()`, `LoadText()`, `Save()`,
`SaveAs()` or `SetOptions()` (see [Guarantees.md](Guarantees.md)).

## JSON

`ToJson()` (`ASWToml_Json.h`) writes a `TASWTomlValue` as JSON text, for logs, diagnostics and
tools that read JSON: a document's root table, a value found in it, or a value of its own. There is
no JSON import. A TOML object's file

```toml
[server]  # The main server
host = "example.com"
port = 0x1F90
started = 2026-10-27 08:00Z
ratio = 0.75
tags = ["main", "eu"]
```

written with

```cpp
std::string json;
ASWToml::TASWTomlResult const written = ASWToml::ToJson(toml.GetDocument().GetRoot(), json);
if (!written.IsOk())
    std::cerr << written.Message << "\n"; // e.g. for a float inf, which JSON has no number for

std::string port;
ASWToml::TASWTomlResult const tagged = ASWToml::ToJson(toml.GetValue("server.port", ASWToml::TASWTomlValue()), port,
    { .Style = ASWToml::EJsonStyle::Tagged });
```

is, in the default style, `Plain`, indented by 2 spaces:

```json
{
  "server": {
    "host": "example.com",
    "port": 8080,
    "started": "2026-10-27T08:00:00Z",
    "ratio": 0.75,
    "tags": [
      "main",
      "eu"
    ]
  }
}
```

and `port` is `{"type": "integer", "value": "8080"}`. The JSON holds the values, not how the file
writes them (`0x1F90` is 8080) or its comments. `TASWTomlJsonOptions` (see
[Options.md](Options.md#taswtomljsonoptions-how-tojson-writes-json)) chooses the style, how `Plain`
writes the floats JSON has no number for, and the indentation (`{ .Indent = 0 }` writes it all on
one line).

What each value becomes:

| TOML value | `Plain` | `Tagged` |
|---|---|---|
| String `"Tab\there"` | `"Tab\there"` | `{"type": "string", "value": "Tab\there"}` |
| Integer `0xFF` | `255` | `{"type": "integer", "value": "255"}` |
| Float `0.1`, `1e300`, `-0.0` | `0.1`, `1e+300`, `-0.0` | `{"type": "float", "value": "1e+300"}` |
| Float `inf`, `-inf`, `nan`, `-nan` | As `NonFinite` says: a failure (the default), `null`, or a string (`"-inf"`) | `{"type": "float", "value": "-inf"}` |
| Boolean `true` | `true` | `{"type": "bool", "value": "true"}` |
| Offset date-time `1979-05-27 07:32Z` | `"1979-05-27T07:32:00Z"` | `{"type": "datetime", "value": "1979-05-27T07:32:00Z"}` |
| Local date-time `1979-05-27T07:32:00` | `"1979-05-27T07:32:00"` | `{"type": "datetime-local", "value": "1979-05-27T07:32:00"}` |
| Local date `1979-05-27` | `"1979-05-27"` | `{"type": "date-local", "value": "1979-05-27"}` |
| Local time `07:32:00` | `"07:32:00"` | `{"type": "time-local", "value": "07:32:00"}` |
| Array, table | An array, an object | An array, an object |

- A table is an object with its keys in the table's order, and an array an array, in both styles;
  an empty one is `{}` or `[]`. `Plain` writes the other values as JSON's, so which of the four
  date and time types a value was is lost. `Tagged` is the form of
  [toml-test](https://github.com/toml-lang/toml-test), the test suite of TOML parsers: every other
  value is its TOML type and its value as a string, so a reader can rebuild the value exactly.
- An integer is a number in decimal. A reader that holds JSON's numbers as doubles, as
  JavaScript's does, loses precision beyond 2^53 (9007199254740993 reads as 9007199254740992);
  `Tagged` keeps it, as a string.
- A float is the shortest text that reads back as the same number, as in TOML, which is always a
  JSON number. JSON has none for `inf`, `-inf` and `nan`, so `Plain` fails with `InvalidValue` for
  them by default; with `NonFinite` `Null` it writes `null`, and with `String` the float as TOML
  writes it, as a string (`"inf"`, `"-inf"`, `"nan"`, or `"-nan"` for a NaN with its sign set).
  `Tagged` always writes them.
- A date or time is a string in RFC 3339, as the library writes it in TOML: a `T` between the date
  and the time, the seconds always, a fraction of a second only when it isn't 0, and the offset
  (`Z`, `-07:00`). A leap second stays second 60 (`"23:59:60"`).
- Strings and keys are UTF-8, as they are, with JSON's escapes for `"`, `\` and the control
  characters U+0000 to U+001F (`\t`, `\n`, `\u001B`, ...). Line breaks are LF, and the text doesn't
  end with one.

On failure, `json` is left as it was, and the result's `Path` is the place of the value that failed
inside the value written (e.g. `servers[1].ratio`, or `[0]` for the first element of an array):

| Code | When |
|---|---|
| `InvalidValue` | `inf`, `-inf` or `nan` in `Plain` with `NonFinite` `Fail`; a string or a key that isn't valid UTF-8, or a date or time that isn't valid, which only a value of the application's own can hold |
| `NestingTooDeep` | A value inside more than `MaxNestingDepthLimit` (128) tables and arrays of the value written, which isn't counted, as the root table isn't, so a document's values never fail (see [Guarantees.md](Guarantees.md#nesting-and-stack)) |
| `InvalidOptions` | Options that `TASWTomlJsonOptions::Validate()` refuses |

Like the other operations, it throws only `std::bad_alloc`.
