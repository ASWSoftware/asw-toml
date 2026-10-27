/* **************************************************************************
ASWToml_Json.h
Author: Anthony S. West - ASW Software

A TOML file reader/writer library.

JSON export: ToJson() writes a TOML value (a document's root table, a value
found in it, or a value of its own) as JSON text, for logs, diagnostics and
other tools, in the Plain style (TOML's values as JSON's) or toml-test's
Tagged style (each value with its TOML type, which loses nothing), with
TASWTomlJsonOptions. There is no JSON import.

Requires C++ 20 or higher.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

#pragma once

#ifndef ASWToml_JsonH
#define ASWToml_JsonH
//---------------------------------------------------------------------------
#include <string>
//---------------------------------------------------------------------------
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

// How ToJson() writes TOML's values. In both, a table is an object with its keys in the table's order, and an array
// is an array. The values never change (an application may store or compare them).
enum class EJsonStyle : int
{
    // TOML's values as JSON's: a string a string, an integer a number in decimal (whatever base the file wrote; one
    // beyond 2^53 loses precision in a reader, such as JavaScript's, that holds numbers as doubles), a float a number
    // (inf and nan as TASWTomlJsonOptions::NonFinite says), a boolean true or false, and a date or time a string in
    // RFC 3339, as TOML writes it (e.g. "1979-05-27T07:32:00Z" or "07:32:00"), so which of the four date and time
    // types it was is lost
    Plain = 0,

    // toml-test's form: every value but a table or an array is an object of its TOML type and its value as a string,
    // e.g. {"type": "integer", "value": "42"}, with the types string, integer, float, bool, datetime (an offset
    // date-time), datetime-local, date-local and time-local, and the value as Plain writes it (a float as TOML writes
    // it, inf, -inf, nan and -nan too), so a reader can rebuild the value exactly
    Tagged = 1,
};

// How the Plain style writes a float JSON has no number for: inf, -inf or nan
enum class EJsonNonFinite : int
{
    Fail = 0, // ToJson() fails with ETomlCode::InvalidValue
    Null = 1, // null
    String = 2, // A string of the float as TOML writes it: "inf", "-inf", "nan", or "-nan" for a NaN with its sign set
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlJsonOptions
//
// How ToJson() writes JSON: the style, the floats JSON has no number for, and the indentation. Given with designated
// initializers, in the order of the members, e.g.
//
//     { .Style = ASWToml::EJsonStyle::Tagged, .Indent = 0 }
//
// New options are added at the end.
/////////////////////////////////////////////////////////////////////////////
struct TASWTomlJsonOptions
{
    // The largest Indent
    static constexpr unsigned MaxIndent = 16;

    // TOML's values as JSON's (Plain), or toml-test's form, every value with its type (Tagged); see EJsonStyle
    EJsonStyle Style = EJsonStyle::Plain;

    // How the Plain style writes inf, -inf and nan (see EJsonNonFinite); the Tagged style always writes them
    EJsonNonFinite NonFinite = EJsonNonFinite::Fail;

    // 0 for the whole text on one line, without blanks, e.g. {"a":1,"b":[1,2]}; else the spaces each level is
    // indented by, from 1 to MaxIndent, as JavaScript's JSON.stringify(value, null, Indent) and Python's
    // json.dumps(value, indent=Indent) lay it out: each key and each element on a line of its own, ": " after a key,
    // and an empty table or array as {} or []. A tagged value is always on one line, e.g.
    // {"type": "integer", "value": "42"}.
    unsigned Indent = 2;

    // Ok if these options can be used, else ETomlCode::InvalidOptions with the first problem found, e.g. an Indent
    // greater than MaxIndent
    TASWTomlResult Validate() const;
};

//---------------------------------------------------------------------------

// Writes 'value', of any type, as JSON text into 'json' in the form 'options' asks for (see EJsonStyle and
// TASWTomlJsonOptions): a table, the root table of a document (TASWTomlDocument::GetRoot()) for one, is an object, and
// any other value is the JSON value it is written as. Strings and keys are written as UTF-8, with JSON's escapes for
// '"', '\', and the control characters U+0000 to U+001F (\b, \t, \n, \f, \r, and \u00XX, e.g. \u001B, for the
// others); line breaks are LF, and the text doesn't end with one.
// On success, 'json' is replaced by the text. On failure, 'json' is left as it was, and the result has the Path of the
// value that failed inside 'value' (e.g. servers[1].ratio, or [0] for the first element of an array; empty for
// 'value' itself): InvalidOptions for options Validate() refuses, InvalidValue for a string or key that isn't valid
// UTF-8, a date or time that isn't valid, or (Plain, with NonFinite Fail) inf or nan, and NestingTooDeep for a value
// inside more than TASWTomlReadingOptions::MaxNestingDepthLimit tables and arrays of 'value' ('value' itself not
// counted, as the root table isn't), which no document holds. Throws only std::bad_alloc.
TASWTomlResult ToJson(TASWTomlValue const& value, std::string& json,
    TASWTomlJsonOptions const& options = TASWTomlJsonOptions());

// The option's name, e.g. "Null"; "Unknown" for a value that isn't an EJsonNonFinite
char const* ToString(EJsonNonFinite nonFinite) noexcept;

// The style's name, e.g. "Tagged"; "Unknown" for a value that isn't an EJsonStyle
char const* ToString(EJsonStyle style) noexcept;

} // namespace ASWToml

//---------------------------------------------------------------------------
#endif // #ifndef ASWToml_JsonH
