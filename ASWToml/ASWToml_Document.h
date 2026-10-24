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

    // Reads 'text' (a whole document, with or without a byte order mark) into 'model', 'index' and 'root' with the
    // reading options of 'options', as Parse() describes; on failure, they are left as they were
    static TASWTomlResult ReadText(std::string_view text, TASWTomlOptions const& options, Detail::TDocModel& model,
        Detail::TDocIndex& index, TASWTomlValue& root);

public:
    // An empty document with the default options: no keys, tables or byte order mark, so ToText() is empty
    TASWTomlDocument();

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
