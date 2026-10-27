/* **************************************************************************
ASWToml_Index.h
Author: Anthony S. West - ASW Software

A TOML file reader/writer library.

Internal: the index of a document's model (see ASWToml_Document.h): the tree
of its tables, keys and values, built by applying TOML's table rules, with
where each of them is written in the text, which the edits use. Built again
each time the text is read.

Requires C++ 20 or higher.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

#pragma once

#ifndef ASWToml_IndexH
#define ASWToml_IndexH
//---------------------------------------------------------------------------
#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWToml_Path.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

//---------------------------------------------------------------------------
// Internals of the library. Not part of the public interface.
//
// Offsets and ranges are bytes of the model's text without a byte order mark, as TASWTomlDocument::ToText() writes it
// with ELineEnding::Auto.
//---------------------------------------------------------------------------
namespace Detail
{

struct TDocModel;

// The index of no node, line or block
constexpr std::size_t NoIndex = static_cast<std::size_t>(-1);

// What a node of the index is, which says what may be added to it
enum class ENodeKind : int
{
    Scalar = 0, // The value of a key or an element of an array: a string, number, boolean, date or time
    Array = 1, // An array value, e.g. [1, 2]: closed
    InlineTable = 2, // An inline table: closed
    ImplicitTable = 3, // A table made only by a longer header (a of [a.b]), which a header may still define
    HeaderTable = 4, // The root table, or a table a header ([a]) defined
    DottedTable = 5, // A table dotted keys made (a of a.b = 1), open to more dotted keys and to sub-tables' headers
    ArrayOfTables = 6, // An array of tables, which each [[a]] adds an element to
    ElementTable = 7, // An element of an array of tables
};

// What a line of the index is
enum class ELineKind : int
{
    Blank = 0, // A line with nothing but blanks, or nothing at all
    Comment = 1, // A line with a comment and nothing else but blanks
    KeyValue = 2, // A key and its value, on one line or on all the lines a multi-line value spans
    Header = 3, // The header of a table or of an element of an array of tables
};

// Indexes of nodes, lines or blocks
using TIndexVector = std::vector<std::size_t>;

// The keys of a table and the indexes of their nodes
using TNodeIndexMap = std::map<std::string, std::size_t, std::less<> >;


/////////////////////////////////////////////////////////////////////////////
// TTextRange
//
// Bytes [Begin, End) of a text
/////////////////////////////////////////////////////////////////////////////
struct TTextRange
{
    std::size_t Begin = 0;
    std::size_t End = 0;

    bool operator==(TTextRange const& other) const = default;
};

// Ranges of a text, in the order they are in it
using TTextRangeVector = std::vector<TTextRange>;


/////////////////////////////////////////////////////////////////////////////
// TIndexNode
//
// A table, array or value of the tree of tables, with where it is written
/////////////////////////////////////////////////////////////////////////////
struct TIndexNode
{
    ENodeKind Kind = ENodeKind::HeaderTable;
    std::string Key; // Its key in the table holding it; empty for an element of an array and for the root
    std::size_t Parent = 0; // The index of the table or array holding it (the root's is its own)

    // Its index in its parent's Children, which is its place in the value of its parent (see FindValue()); 0 for the
    // root
    std::size_t Position = 0;

    std::size_t Depth = 0; // How many tables and arrays it is inside, itself included (the root not counted)

    // Where its first definition is, for messages: the key part that made it, in a key or a header
    std::size_t Offset = 0;

    TIndexVector Children; // A table's keys in the order they were first defined, or an array's elements
    TNodeIndexMap Keys; // A table's keys

    // The line that defines it: the key/value line of a key's value (not one in an inline table or an array), or the
    // header of a header table or an element; NoIndex for the others
    std::size_t Line = NoIndex;

    std::size_t Block = NoIndex; // A header table's or an element's block in the model; NoIndex for the others
    TTextRange Value; // Scalar, Array and InlineTable: its text, an array's or inline table's brackets included

    // An element of an array, or the value of an entry of an inline table: the element or entry with the text around
    // it, from after the opening bracket or the comma before it, through its comma (or, without one, its After)
    TTextRange Item;

    // Array and InlineTable: the text before the closing bracket or brace (after the last comma), and the bracket
    TTextRange Close;

    // Each key part that names it, as written (a quoted one with its quotes), in the order they are in the text: the
    // last part of a key; the parts of dotted keys and headers that go through or define a table (for an array of
    // tables, the parts of its [[...]] headers and of its elements' sub-tables' headers)
    TTextRangeVector Names;
};


/////////////////////////////////////////////////////////////////////////////
// TIndexLine
//
// A line of the model: a blank or comment line, a key and its value (with all the lines a multi-line value spans), or
// a header
/////////////////////////////////////////////////////////////////////////////
struct TIndexLine
{
    ELineKind Kind = ELineKind::Blank;
    std::size_t Block = 0; // Its block in the model
    std::size_t Entry = NoIndex; // Its entry in the block; NoIndex for a header

    // KeyValue: the node of its key's last part; Header: the table it defines (for [[a]], its new element)
    std::size_t Node = NoIndex;

    TTextRange Text; // From its first byte (its indentation) to its line break (not included)
    std::size_t Next = 0; // The offset after its line break: the next line's first byte

    // KeyValue and Header: the text after the value or the closing bracket, up to the line break: blanks, and a
    // comment; for the others, empty at the end of Text
    TTextRange Trailing;
};


/////////////////////////////////////////////////////////////////////////////
// TDocIndex
//
// The index of a model: its tree of tables (the root first) and its lines in order
/////////////////////////////////////////////////////////////////////////////
struct TDocIndex
{
    // The nodes of the tree
    using TNodeVector = std::vector<TIndexNode>;

    // The lines of a text, in order
    using TLineVector = std::vector<TIndexLine>;

    TNodeVector Nodes; // The root table first; empty before the index is built
    TLineVector Lines;

    // For each block of the model, the index of its first line in Lines (its header, or its first entry; for an empty
    // block without a header, the index of the next block's first line)
    TIndexVector BlockLines;
};

//---------------------------------------------------------------------------

// Builds 'index' and 'root' from 'model', whose text without a byte order mark is 'text': applies TOML's table rules
// as TASWTomlDocument::Parse() describes them (DuplicateKey) and the limit 'maxDepth' (NestingTooDeep), with the Line
// and Column of 'text' in an error. On success, 'root' is the decoded document (see TASWTomlDocument::GetRoot()); on
// failure, 'index' and 'root' are left as they were. Throws only std::bad_alloc, and then they are left as they were
// too.
TASWTomlResult BuildIndex(TDocModel const& model, std::string_view text, std::size_t maxDepth, TDocIndex& index,
    TASWTomlValue& root);

// The node of 'index' at 'path', as TASWTomlDocument::Find() finds a value; NoIndex when there is none or 'path' isn't
// valid
std::size_t FindNode(TDocIndex const& index, TASWTomlPath const& path) noexcept;

// The value at 'path' in 'root', the decoded document that BuildIndex() built with 'index', as
// TASWTomlDocument::Find() describes it; nullptr when there is none or 'path' isn't valid. A key of a table of more
// than 16 keys is found through the index (its node's map of keys and the key's node's Position), in O(log n); a
// smaller table's keys are compared one by one, which is faster there.
TASWTomlValue const* FindValue(TDocIndex const& index, TASWTomlValue const& root, TASWTomlPath const& path) noexcept;

} // namespace Detail

} // namespace ASWToml

//---------------------------------------------------------------------------
#endif // #ifndef ASWToml_IndexH
