/* **************************************************************************
UT_Helper_Json.h
Author: Anthony S. West - ASW Software

Test helpers of the JSON export (Test_ASWToml_Json): a strict reader of
JSON text (RFC 8259), which tells whether ToJson() wrote valid JSON and
what it holds, and a reader of toml-test's tagged JSON, which rebuilds the
TOML value it stands for with the library's own readers.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#ifndef UT_Helper_JsonH
#define UT_Helper_JsonH
//---------------------------------------------------------------------------
#include <string>
#include <string_view>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

// The kind of a JSON value
enum class EJsonKind : int
{
    Null = 0,
    Boolean = 1,
    Number = 2,
    String = 3,
    Array = 4,
    Object = 5,
};

struct TJsonMember;
struct TJsonValue;

// An array's elements, in order
using TJsonValueVector = std::vector<TJsonValue>;

// An object's members, in the order of the text
using TJsonMemberVector = std::vector<TJsonMember>;


/////////////////////////////////////////////////////////////////////////////
// TJsonValue
//
// A JSON value as ReadJson() reads it
/////////////////////////////////////////////////////////////////////////////
struct TJsonValue
{
    EJsonKind Kind = EJsonKind::Null;
    std::string Text; // A string's text (UTF-8, unescaped), a number as written, or true or false
    TJsonValueVector Elements; // An array's
    TJsonMemberVector Members; // An object's
};


/////////////////////////////////////////////////////////////////////////////
// TJsonMember
//
// A key of a JSON object and its value
/////////////////////////////////////////////////////////////////////////////
struct TJsonMember
{
    std::string Key; // UTF-8, unescaped
    TJsonValue Value;
};

//---------------------------------------------------------------------------

// Reads 'text' as one JSON value, with only blanks (space, tab, LF and CR) around it, into 'value' (RFC 8259: a
// number as its grammar has it, a string of valid UTF-8 without a control character, with only JSON's escapes, and a
// \u escape of a surrogate only in a pair); a key twice in an object is refused too. Empty when it reads, else what
// is wrong, with the offset in 'text' where it is.
std::string ReadJson(std::string_view text, TJsonValue& value);

// Rebuilds the TOML value 'json' stands for in toml-test's tagged JSON into 'value': an object of exactly the members
// "type" and "value", both strings, is a scalar of that type (string, integer, float, bool, datetime, datetime-local,
// date-local or time-local) whose text the library's readers read (an integer must be in decimal, as std::to_string()
// writes it); any other object is a table, and an array an array. Empty when it is one, else what is wrong, with the
// path of the place.
std::string TaggedToValue(TJsonValue const& json, ASWToml::TASWTomlValue& value);

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef UT_Helper_JsonH
