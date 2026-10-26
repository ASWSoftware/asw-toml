/* **************************************************************************
ASWToml_Writer.h
Author: Anthony S. West - ASW Software

A TOML file reader/writer library.

Internal: the TOML text the edits write, from values: a value on one line, a
value in the form of the one it replaces, key/value lines, a table as dotted
keys, and a table as headers and their lines. The counterpart of the parser
(ASWToml_Parser.h), on top of the scalar writers of ASWToml_Scalars.h. New
text uses only the syntax TOML 1.0 reads.

Requires C++ 20 or higher.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

#pragma once

#ifndef ASWToml_WriterH
#define ASWToml_WriterH
//---------------------------------------------------------------------------
#include <cstddef>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWToml_Options.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

//---------------------------------------------------------------------------
// Internals of the library. Not part of the public interface.
//
// Each writer appends to a std::string and returns Ok, or fails with InvalidValue for a value that can't be written (a
// string or key that isn't valid UTF-8, a date or time that isn't valid, see ASWToml_Scalars.h; or a value that isn't
// a table, given to a writer of tables) or NestingTooDeep for one whose arrays and tables are nested deeper than
// TWriteStyle::MaxDepth, both without a Line or Column. On failure, and when std::bad_alloc is thrown (the only
// exception), the text may hold part of what was being written.
//---------------------------------------------------------------------------
namespace Detail
{

/////////////////////////////////////////////////////////////////////////////
// TWriteStyle
//
// How the writers write new text
/////////////////////////////////////////////////////////////////////////////
struct TWriteStyle
{
    TASWTomlValueOptions Values; // The form of new strings and integers
    ELineBreak LineBreak = ELineBreak::LF; // Ends each line written, and each line of a multi-line string; LF for None

    // Whether a header gets a blank line before it (Writing.BlankLineBeforeNewTable); see WriteTableBlocks()
    bool BlankLineBeforeTable = true;

    // How deep the arrays and tables a writer writes may be nested, the outermost one it writes counted as 1 (a
    // scalar is 0 deep): the reading options' MaxNestingDepth less the depth of the place the text goes
    std::size_t MaxDepth = 0;
};


// Appends a line for each key of 'table' (a table value) as dotted keys after 'prefix', the key parts of the table as
// written (e.g. server or a."b c"), each line 'indent', the key, " = ", its value (as WriteValue() writes it) and the
// line break: e.g. server.port = 8080. The keys of a table in 'table' get more parts (server.tls.port = 443), in the
// order of 'table', depth first; an empty table, 'table' included, is a line with {} (server.tls = {}), so it exists.
// An array of tables is an array of inline tables, as dotted keys can't make one. 'table' itself counts as one level
// of MaxDepth, and each table in it as one more.
TASWTomlResult WriteDottedKeys(std::string_view indent, std::string_view prefix, TASWTomlValue const& table,
    TWriteStyle const& style, std::string& text);

// Appends a key/value line: 'indent', 'key' (the key as written, one part or a dotted key), " = ", 'value' as
// WriteValue() writes it, and the line break.
TASWTomlResult WriteKeyValueLine(std::string_view indent, std::string_view key, TASWTomlValue const& value,
    TWriteStyle const& style, std::string& text);

// Appends 'table' (a table value) as the table 'path' (its key parts as written, e.g. servers or a."b c"; empty for
// the root table) in the form a header gives it: the header ([path], or [[path]] when 'isElement' says it is a new
// element of an array of tables), then a key/value line for each of its keys that isn't a table or an array of tables
// (its value as WriteValue() writes it, in the order of 'table'), then each of its tables as its own header and lines
// (path.key), and each of its arrays of tables (an array that isn't empty and holds only tables) as a [[path.key]]
// header and lines per element, in the order of 'table', depth first. A table whose only keys are tables and arrays
// of tables gets no header (it stays implicit, made by its sub-tables' headers), unless it is an element; an empty
// table gets one, so it exists. The root table never gets a header. A header gets a blank line before it when
// BlankLineBeforeTable is set and it isn't the first line written, or 'blankLineFirst' is set. 'table' counts as one
// level of MaxDepth (the root table as none, as in Reading.MaxNestingDepth), and each table and array in it as one
// more (an element of an array of tables as one more than its array).
TASWTomlResult WriteTableBlocks(std::string_view path, bool isElement, bool blankLineFirst, TASWTomlValue const& table,
    TWriteStyle const& style, std::string& text);

// Appends 'value' on one line: a scalar as the scalar writers write it (strings and integers in the form Values asks
// for), an array as [1, 2, 3] ([] when empty), a table as an inline table, { x = 1, y = 2 } ({} when empty), with each
// key as WriteKey() writes it, and the arrays and tables in them the same way. An array or table counts as one level
// of MaxDepth, and each one in it as one more.
TASWTomlResult WriteValue(TASWTomlValue const& value, TWriteStyle const& style, std::string& text);

// Appends 'value' as WriteValue() does, but a scalar in the form of 'oldText', the text of the scalar it replaces,
// where it can, when Values asks for Auto: a string in the form of an old string (a basic one stays basic, a literal
// one literal when a literal string can hold the new text, a multi-line one multi-line of its kind, as
// WriteMultiLineString() writes it, with a line break after the opening delimiter when the old one had one), an
// integer in the base of an old integer (hexadecimal, octal or binary, unless the new value is negative). A
// StringStyle or IntegerBase other than Auto writes that form, as for a new value; every other kind of value, or one
// of another type than the old one, is written as WriteValue() writes it.
TASWTomlResult WriteValueLike(TASWTomlValue const& value, std::string_view oldText, TWriteStyle const& style,
    std::string& text);

} // namespace Detail

} // namespace ASWToml

//---------------------------------------------------------------------------
#endif // #ifndef ASWToml_WriterH
