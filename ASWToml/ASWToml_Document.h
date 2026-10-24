/* **************************************************************************
ASWToml_Document.h
Author: Anthony S. West - ASW Software

A TOML file reader/writer library.

TASWTomlDocument: a TOML text parsed into a model that keeps every byte of
it (comments, blank lines, spacing, quoting, the form of each value and the
layout of arrays and inline tables), so it is written back byte for byte.

Requires C++ 20 or higher.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

#pragma once

#ifndef ASWToml_DocumentH
#define ASWToml_DocumentH
//---------------------------------------------------------------------------
#include <string>
#include <string_view>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWToml_Index.h"
#include "ASWToml_Options.h"
#include "ASWToml_Path.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

//---------------------------------------------------------------------------
// Internals of the library: the document's model. Not part of the public interface.
//
// The model holds a TOML text as pieces in file order, each owning its own text, so that every byte of the text is in
// exactly one piece and writing the pieces in order gives the text back. An edit replaces a piece's text.
//---------------------------------------------------------------------------
namespace Detail
{

// What an entry of a block is
enum class EEntryKind : int
{
    Blank = 0, // A line with nothing but blanks (spaces and tabs), or nothing at all
    Comment = 1, // A line with a comment and nothing else but blanks
    KeyValue = 2, // A key and its value
};

// What a value is
enum class EValueKind : int
{
    Scalar = 0, // A string, number, boolean, date or time
    Array = 1,
    InlineTable = 2,
};


/////////////////////////////////////////////////////////////////////////////
// TDocKeyPart
//
// One part of a key as written, e.g. "host name" of server."host name".port, with the blanks around it
/////////////////////////////////////////////////////////////////////////////
struct TDocKeyPart
{
    // The blanks before the part: after the dot before it, or before the first part, the indentation of a key/value
    // line or the blanks after the opening bracket of a header (none in an inline table, whose entry's Before holds
    // them)
    std::string Before;

    std::string Text; // As written: a bare key, or a basic or literal string with its quotes
    std::string Name; // What Text reads as: the key without quotes, its escapes read (UTF-8)
    std::string After; // The blanks after the part: before the dot after it, '=' or the closing bracket of a header
};


/////////////////////////////////////////////////////////////////////////////
// TDocKey
//
// A key as written: one part, or the parts of a dotted key, with a '.' between two parts
/////////////////////////////////////////////////////////////////////////////
struct TDocKey
{
    // The parts of a key, in order
    using TPartVector = std::vector<TDocKeyPart>;

    TPartVector Parts; // Never empty
};


struct TDocElement;
struct TDocInlineEntry;

/////////////////////////////////////////////////////////////////////////////
// TDocValue
//
// A value as written: a scalar (its text and the value it reads as), an array (its values) or an inline table (its
// keys and values), each element and entry with the text around it
/////////////////////////////////////////////////////////////////////////////
struct TDocValue
{
    // The values of an array, in order
    using TElementVector = std::vector<TDocElement>;

    // The keys and values of an inline table, in order
    using TInlineEntryVector = std::vector<TDocInlineEntry>;

    EValueKind Kind = EValueKind::Scalar;
    std::string Text; // Scalar: as written, e.g. 0xFF or 'C:\Users'
    TASWTomlValue Scalar; // Scalar: what Text reads as
    TElementVector Elements; // Array
    TInlineEntryVector Entries; // InlineTable

    // Array and InlineTable: the text before the closing bracket or brace, after the opening one or the last value or
    // entry and its comma: blanks, and in an array (or, in TOML 1.1, an inline table) comments and line breaks
    std::string Close;
};


/////////////////////////////////////////////////////////////////////////////
// TDocKeyValue
//
// A key and its value as written: the key, '=', the blanks after it and the value
/////////////////////////////////////////////////////////////////////////////
struct TDocKeyValue
{
    TDocKey Key; // Its last part's After holds the blanks before '='
    std::string BeforeValue; // The blanks after '='
    TDocValue Value;
};


/////////////////////////////////////////////////////////////////////////////
// TDocElement
//
// A value of an array, with the text around it
/////////////////////////////////////////////////////////////////////////////
struct TDocElement
{
    std::string Before; // After the opening bracket or the comma before it: blanks, comments and line breaks
    TDocValue Value;
    std::string After; // Between the value and its comma, as Before; empty when no comma follows (see Close)
    bool Comma = false; // Whether a comma follows the value
};


/////////////////////////////////////////////////////////////////////////////
// TDocInlineEntry
//
// A key and its value in an inline table, with the text around them
/////////////////////////////////////////////////////////////////////////////
struct TDocInlineEntry
{
    std::string Before; // After the opening brace or the comma before it: blanks (and in TOML 1.1, comments and line
                        // breaks)
    TDocKeyValue KeyValue;
    std::string After; // Between the value and its comma, as Before; empty when no comma follows (see Close)
    bool Comma = false; // Whether a comma follows the value
};


/////////////////////////////////////////////////////////////////////////////
// TDocEntry
//
// An entry of a block: a blank line, a comment line, or a key and its value with the rest of the line they end on
/////////////////////////////////////////////////////////////////////////////
struct TDocEntry
{
    EEntryKind Kind = EEntryKind::Blank;
    TDocKeyValue KeyValue; // KeyValue only

    // Without the line break: for a blank or comment line, all of it; for a key and its value, what follows the value:
    // blanks and a comment
    std::string Text;

    ELineBreak LineBreak = ELineBreak::None; // None for the last line of a text that doesn't end with a line break
};


/////////////////////////////////////////////////////////////////////////////
// TDocHeader
//
// The header of a table ([server]) or of an element of an array of tables ([[products]]), with the rest of its line
/////////////////////////////////////////////////////////////////////////////
struct TDocHeader
{
    std::string Indent; // The blanks before the opening bracket
    bool IsArray = false; // Whether it is [[...]], the header of an element of an array of tables
    TDocKey Key;
    std::string Text; // After the closing bracket, without the line break: blanks and a comment
    ELineBreak LineBreak = ELineBreak::None;
};


/////////////////////////////////////////////////////////////////////////////
// TDocBlock
//
// A header and the entries after it, up to the next header; or for the first block of a document, the entries before
// the first header, without a header
/////////////////////////////////////////////////////////////////////////////
struct TDocBlock
{
    // The entries of a block, in order
    using TEntryVector = std::vector<TDocEntry>;

    bool HasHeader = false;
    TDocHeader Header; // HasHeader only
    TEntryVector Entries;
};


/////////////////////////////////////////////////////////////////////////////
// TDocModel
//
// A whole TOML text: a byte order mark, and the blocks
/////////////////////////////////////////////////////////////////////////////
struct TDocModel
{
    // The blocks of a text, in order
    using TBlockVector = std::vector<TDocBlock>;

    bool HasBom = false; // Whether the text started with a UTF-8 byte order mark
    ELineBreak LineBreak = ELineBreak::None; // The text's most common line break (the first in a tie); None if none
    TBlockVector Blocks = TBlockVector(1); // Never empty: the block without a header first
};

} // namespace Detail


/////////////////////////////////////////////////////////////////////////////
// TASWTomlDocument
//
// A TOML text, parsed into a model that keeps every byte of it: the tables in file order, each with its comments,
// blank lines, keys and values as written (quoting, spacing, the form of each number and string, the layout of arrays
// and inline tables, and each line's own line break). With the default options, ToText() writes a parsed text back
// byte for byte. Its values can be read by path (Find()) or as one table (GetRoot()). It keeps the options it was
// parsed with. Not thread-safe; copyable and movable.
/////////////////////////////////////////////////////////////////////////////
class TASWTomlDocument
{
private:
    Detail::TDocModel m_Model;
    Detail::TDocIndex m_Index; // The index of m_Model
    TASWTomlOptions m_Options; // Valid (TASWTomlOptions::Validate() accepts them)
    TASWTomlValue m_Root; // The values of m_Model, as GetRoot() describes
    bool m_Modified = false; // See IsModified()

    // The model's text without a byte order mark, each line break as it is (what ToText() writes with the default
    // writing options, but for the byte order mark)
    std::string BodyText() const;

    // Makes 'model', 'index' and 'root' the document's, and sets the modified flag
    void Commit(Detail::TDocModel& model, Detail::TDocIndex& index, TASWTomlValue& root) noexcept;

    // Reads 'body', the document's text after an edit (without a byte order mark), with the document's byte order mark
    // and options into 'model', 'index' and 'root', as ReadText() does. Fails with the parser's code and message
    // (but no Line or Column, as they would be about a text the caller never saw) when the text doesn't parse.
    TASWTomlResult ReadEdited(std::string const& body, Detail::TDocModel& model, Detail::TDocIndex& index,
        TASWTomlValue& root) const;

    // Reads 'text' (a whole document, with or without a byte order mark) into 'model', 'index' and 'root' with the
    // reading options of 'options', as Parse() describes; on failure, they are left as they were
    static TASWTomlResult ReadText(std::string_view text, TASWTomlOptions const& options, Detail::TDocModel& model,
        Detail::TDocIndex& index, TASWTomlValue& root);

public:
    // An empty document with the default options: no keys, tables or byte order mark, so ToText() is empty
    TASWTomlDocument();

    // Adds 'value' at the end of the array at 'path', written with the document's Values options (the second form:
    // with 'values'), as SetValue() writes a value:
    // - An array value gets a new element after its last one, in the layout of the elements before it: on the same
    //   line after ", " in an array on one line, or on a line of its own in an array with one element per line,
    //   keeping a comment at the end of the last one's line and a comma after the last element if there was one.
    // - An array of tables ([[path]]) gets a new [[path]] header with the keys of 'value', which must be a table,
    //   after the last element and its sub-tables.
    // - A missing key gets an array of the one value, as SetValue() would write it: an array of tables when 'value'
    //   is a table and a header can go there.
    // Fails with InvalidArgument when 'path' isn't valid, names a value that isn't an array (or the root), goes through
    // one that isn't a table, or 'value' isn't a table for an array of tables; InvalidOptions for 'values' that aren't
    // valid; NotFound for an index past the end of an array; InvalidValue or NestingTooDeep as SetValue() does. On
    // failure nothing changes. Throws only std::bad_alloc, and then nothing changes either.
    TASWTomlResult Append(TASWTomlPath const& path, TASWTomlValue const& value);
    TASWTomlResult Append(TASWTomlPath const& path, TASWTomlValue const& value, TASWTomlValueOptions const& values);

    // Clears the modified flag (see IsModified()), e.g. after the text was saved
    void ClearModified() noexcept;

    // The value at 'path': a table, an array, an element of an array (of an array of tables, e.g. products[1], or of
    // an array value, e.g. ports[0]) or the value of a key; the root table for the root path. nullptr when there is no
    // such value or the path isn't valid. An index needs an array: products.name is nullptr when products is an array
    // of tables. The pointer is valid until the document is parsed again, changed or destroyed.
    TASWTomlValue const* Find(TASWTomlPath const& path) const noexcept;

    // The line break new lines get with ELineEnding::Auto: the most common one of the text that was read (the one
    // found first in a tie), or the platform's (CRLF on Windows, LF elsewhere) when it had none
    ELineBreak GetLineBreak() const noexcept;

    // The options the document was parsed with; the default ones for a new document
    TASWTomlOptions const& GetOptions() const noexcept;

    // The whole document as a table (ETomlType::Table): each table with its keys in the order they are first defined
    // in the text, however the table was written (by a header, by dotted keys, as an inline table, or only as part of
    // a longer header), and each array of tables as an array of tables. An empty table for a new document.
    TASWTomlValue const& GetRoot() const noexcept;

    // Whether the text that was read started with a UTF-8 byte order mark
    bool HasBom() const noexcept;

    // Whether an edit changed the text since the document was made, parsed or reset, or the flag was cleared
    // (ClearModified()). An edit that changes nothing leaves the flag as it was.
    bool IsModified() const noexcept;

    // Reads 'text', a TOML 1.1 document (UTF-8, with or without a byte order mark; lines end with LF or CRLF), into
    // this document, replacing what it held, and keeps 'options' for ToText() and the edits. Fails on the first error,
    // and then nothing changes: InvalidOptions (see TASWTomlOptions::Validate()), UnsupportedEncoding (a UTF-16 or
    // UTF-32 byte order mark), InvalidUtf8 (at the first byte that isn't valid UTF-8, wherever it is), ParseFailed
    // (text that isn't TOML, e.g. a carriage return without a line feed), ValueOutOfRange (an integer outside the
    // 64-bit range, or a float outside a double's), NestingTooDeep (tables, arrays and inline tables inside one another
    // deeper than Reading.MaxNestingDepth), or DuplicateKey (a key or table defined twice, which includes a header for
    // a table that dotted keys or another header defined, dotted keys that add to a table a header defined, a key or
    // table added to an inline table or an array value, and [[x]] for an x that isn't an array of tables). The whole
    // text is read before the tables are checked, so DuplicateKey (and NestingTooDeep for a table under an array of
    // tables) is reported only when the text has no other error. An error in the text has its Line and Column (both
    // 1-based, the column in characters, a byte order mark not counted); DuplicateKey's are those of the second
    // definition's first key part, and its message gives the path and the line of the first, e.g. The table server is
    // already defined on line 3. Reading.MaxFileSize limits only the files a load reads. On success, the modified flag
    // is cleared (see IsModified()). Throws only std::bad_alloc, and then nothing changes either.
    TASWTomlResult Parse(std::string_view text, TASWTomlOptions const& options = TASWTomlOptions());

    // Removes the key, table, array of tables or element of an array at 'path' with the text that defines it, keeping
    // the rest byte for byte:
    // - A key: its line, with its comment (the comment lines directly above it) and the comment at its end. An entry
    //   of an inline table or an element of an array value: it and one comma (the one after it, or for the last one
    //   the one before it, so a trailing comma stays), and when it is on a line of its own, that line with the
    //   comment lines directly above it and the comment at its end; removing the last one leaves {} or [].
    // - A table: its header with its comment and lines, the headers and lines of the tables under it, and the dotted
    //   keys that define it (a.x = 1 for the table a). An array of tables: all its elements. An element of an array
    //   of tables (servers[1]): its [[...]] header with its comment and lines, and the tables under it.
    // A blank line before what is removed goes with it when a blank line or the end of the text follows it, and at the
    // start of the text a blank line after it goes, so blank lines don't pile up. A table that only dotted keys or
    // longer headers made goes with its last key, and an array of tables with its last element, as nothing defines
    // them any more; a table with a header stays, empty. Returns whether anything was removed (and then sets the
    // modified flag): false when 'path' isn't valid, names the root table or names nothing. Throws only
    // std::bad_alloc, and then nothing changes.
    bool Remove(TASWTomlPath const& path);

    // Renames the key, table or array of tables at 'path' to 'newKey' (one key part, unquoted, UTF-8) in its table,
    // in every place that names it: the key of its line or of its entry in an inline table, and the part of each
    // header and dotted key that goes through it ([a], [a.b], [[a.c]] and a.x = 1 for the table a). Only those key
    // parts change: each is written bare when it can be, else as a basic string, or as a literal string where the old
    // one was literal and one can hold the new name. Renaming to the same name changes nothing. Fails with
    // InvalidArgument when 'path' isn't valid, names the root table or an element of an array, or goes through a value
    // that isn't a table; NotFound when nothing is at 'path'; InvalidValue when 'newKey' isn't valid UTF-8;
    // AlreadyExists when the table already has a key 'newKey'. On failure nothing changes. Throws only std::bad_alloc,
    // and then nothing changes either.
    TASWTomlResult Rename(TASWTomlPath const& path, std::string_view newKey);

    // Empties the document, as a new one is, and gives it 'options', so ToText() is empty, GetRoot() an empty table
    // and the modified flag clear. Fails with InvalidOptions (see TASWTomlOptions::Validate()), and then nothing
    // changes. Throws only std::bad_alloc, and then nothing changes either.
    TASWTomlResult Reset(TASWTomlOptions const& options);

    // Gives the document 'options', which may be its own, and reads its text again with them, as Parse() does, so the
    // reading options apply to it. The text doesn't change, nor do the byte order mark (HasBom()) or the modified
    // flag. Fails with InvalidOptions (see TASWTomlOptions::Validate()), or with what Parse() reports for a text the
    // new reading options refuse (e.g. NestingTooDeep for a lower MaxNestingDepth); then nothing changes. Throws only
    // std::bad_alloc, and then nothing changes either.
    TASWTomlResult SetOptions(TASWTomlOptions const& options);

    // Sets the key, table or element of an array at 'path' to 'value', changing only the text it needs, with the
    // document's Values options (the second form: with 'values') for new strings and integers. New text uses only the
    // syntax TOML 1.0 reads; the rest of the text stays byte for byte.
    // - A value that exists (a key's value, an element of an array, or a key of an inline table) is replaced in place:
    //   its key, the blanks around '=' and a comment at the end of its line stay. A scalar keeps its form where it can
    //   when Values asks for Auto: a basic, literal or multi-line string stays one (a literal one that can't hold the
    //   new text becomes basic), and an integer keeps its hexadecimal, octal or binary base unless the new value is
    //   negative. A value of another type is written in its place, an array as [1, 2] and a table as an inline table,
    //   { x = 1 }, on one line.
    // - A table or array that exists, set to a table or array, is merged with it, so only what differs changes: each
    //   key of the value is set as this says (a table in it is merged too), each key the value doesn't have is removed
    //   as Remove() removes it, and each new one added, in the order of the value; the keys that stay keep their
    //   order, lines and comments. Of the elements of an array or an array of tables, those that are the same as new
    //   ones stay (the longest common subsequence of the two), the others between them are set to the new ones in
    //   order, and the ones left over are removed or added in their place: a new element before the next one that
    //   stays, in its layout (above its comment lines when it starts a line), or at the end. An inline table stays
    //   inline (a new entry takes the place of a removed one at its end), an array on several lines keeps one element
    //   per line, and an array of tables keeps its [[path]] blocks with their comments.
    // - A table that isn't inline, or an array of tables, set to a value it can't hold is removed, and the value
    //   written as a new key of its table (t = 5 for [t]); an element of an array of tables set to a value that isn't
    //   a table makes the whole array an array value.
    // - A missing key is added where its table is written: after the last key line of a [header] table, an element of
    //   an array of tables or the root table (the root's after the file comment and the blank line under it when it
    //   has no keys yet), copying that line's indentation; as a dotted key after the last one of a table dotted keys
    //   made (server.host = "x" after server.port = 80); inside the braces of an inline table, in the spacing of its
    //   entries. A table that only longer headers made ([a] of [a.b]) gets its own [a] header, before the first of
    //   theirs. Missing tables on the way are made in the form their table allows: a header ([a.b]) under the root, a
    //   header table, an element or an implicit table; dotted keys under a dotted-key table; an inline table inside an
    //   inline table.
    // - A table value where a header can go is written as a [path] header with its keys, then its sub-tables as headers
    //   of their own and its arrays of tables (arrays that aren't empty and hold only tables) as [[path]] headers; a
    //   table whose only keys are tables gets no header of its own. A new header goes after the last header of its
    //   parent table and the tables under it (at the end of the file for the root), after a blank line when
    //   Writing.BlankLineBeforeNewTable is on.
    // - New lines get GetLineBreak(); a last line without a line break gets one first.
    // A value identical to the one there (floats by their sign too, so -0.0 isn't 0.0, and a NaN is a NaN of the same
    // sign; tables with their keys in any order) changes nothing, and leaves the modified flag as it was. Fails with
    // InvalidArgument when 'path' isn't valid, goes through a value that isn't a table (port.x when port is an
    // integer), puts a key after an array or an index after a table, or names the root for a value that isn't a
    // table; InvalidOptions for 'values' that aren't valid; NotFound for an index past the end of an array (or into a
    // missing key); InvalidValue for a value that can't be written (a string that isn't valid UTF-8, a date or time
    // that isn't valid); NestingTooDeep when the result would be nested deeper than Reading.MaxNestingDepth. On
    // failure nothing changes. Throws only std::bad_alloc, and then nothing changes either.
    TASWTomlResult SetValue(TASWTomlPath const& path, TASWTomlValue const& value);
    TASWTomlResult SetValue(TASWTomlPath const& path, TASWTomlValue const& value, TASWTomlValueOptions const& values);

    // The document as text, as its writing options say: a UTF-8 byte order mark (with EBomMode::Auto, when the text
    // that was read had one), then the text. With ELineEnding::Auto every line break stays as it was; LF, CRLF and
    // Native write every one as that line break instead, in comments and multi-line strings too (a multi-line string
    // reads the same either way). A text that doesn't end with a line break never gets one. With the default options,
    // a parsed text is written back byte for byte. Throws only std::bad_alloc.
    std::string ToText() const;
};

} // namespace ASWToml

//---------------------------------------------------------------------------
#endif // #ifndef ASWToml_DocumentH
