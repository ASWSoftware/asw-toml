/* **************************************************************************
ASWToml_Document.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "ASWToml_Document.h"
//---------------------------------------------------------------------------
#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWToml_Parser.h"
#include "ASWToml_Scalars.h"
#include "ASWToml_Utils.h"
#include "ASWToml_Writer.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

namespace
{

// The UTF-8 byte order mark
constexpr std::string_view Utf8Bom = "\xEF\xBB\xBF";

// The most cells of the table Partners() fills to find the longest common subsequence of the elements of two arrays
// (after the ones that are the same at their start and end): 500 by 500 elements, 2 MB with 64-bit indexes
constexpr std::size_t MaxAlignCells = 250000;

using Detail::ENodeKind;
using Detail::NoIndex;
using Detail::TIndexVector;
using Detail::TTextRange;

// For each line of a text, or each element or entry of an array or inline table, whether an edit removes it
using TFlagVector = std::vector<bool>;

// Texts in order: of new elements or entries, or the lines of a comment
using TStringVector = std::vector<std::string>;


/////////////////////////////////////////////////////////////////////////////
// TSplice
//
// A change of a text: the bytes [Begin, End) replaced with Text (inserted, when Begin is End)
/////////////////////////////////////////////////////////////////////////////
struct TSplice
{
    std::size_t Begin = 0;
    std::size_t End = 0;
    std::string Text;
};

// The changes of an edit
using TSpliceVector = std::vector<TSplice>;


/////////////////////////////////////////////////////////////////////////////
// TItemInsert
//
// A new element of an array, to go before one that is there
/////////////////////////////////////////////////////////////////////////////
struct TItemInsert
{
    std::size_t Before = NoIndex; // The node of the element it goes before
    std::string Text; // The new element as written
};

// New elements, in order
using TItemInsertVector = std::vector<TItemInsert>;


/////////////////////////////////////////////////////////////////////////////
// TContainerPlan
//
// What an edit does to the elements of an array or the entries of an inline table, worked out before any of it is
// written, as the places of new ones depend on which ones stay
/////////////////////////////////////////////////////////////////////////////
struct TContainerPlan
{
    TIndexVector Removed; // The nodes of the elements or entries (their values) removed
    TItemInsertVector Inserted; // New elements before ones that stay (an array only)
    TStringVector Appended; // New elements or "key = value" entries at the end, in order
};

// The plans of the arrays and inline tables an edit changes, by their nodes
using TContainerPlanMap = std::map<std::size_t, TContainerPlan>;


/////////////////////////////////////////////////////////////////////////////
// TEditor
//
// Works out how an edit changes a document's text, from its index: what text goes, where new text goes and what it
// is, in the forms TASWTomlDocument::SetValue(), Append(), Remove(), Rename(), SetComment() and SetTrailingComment()
// describe. Every change is worked out on the text as it is: the lines removed and the plans of the arrays and inline
// tables are collected first, and Finish() turns them into the changes of the text. It doesn't change the text: the
// document makes the changes and reads the text again.
/////////////////////////////////////////////////////////////////////////////
class TEditor
{
private:
    std::string const& m_Text; // The document's text without a byte order mark
    Detail::TDocIndex const& m_Index; // Its index
    TASWTomlValue const& m_Root; // Its values
    std::size_t m_MaxDepth; // Reading.MaxNestingDepth
    std::string m_CommentPrefix; // Writing.CommentPrefix
    Detail::TWriteStyle m_Style; // How new text is written; MaxDepth is set for each piece
    std::string m_LineBreak; // The text of the line break new lines get
    std::string m_Verb; // What the edit does to a path, for messages: "set" or "append to"
    TSpliceVector m_Splices;
    TFlagVector m_RemovedLines; // The lines of m_Index the edit removes
    TContainerPlanMap m_Containers; // What the edit does to arrays and inline tables

    // Adds to 'items' the values 'node' stands for in an array or inline table: itself for a value, or the values of
    // the dotted keys under a dotted-key table
    void AddValues(std::size_t node, TIndexVector& items) const;

    // Adds 'items' (values, or "key = value" entries) at the end of the array or inline table 'container', after its
    // element or entry 'last' (NoIndex when it has none), in their layout: on the same line after the last one, or,
    // when they are one per line, each on a line of its own after the last one's line
    void AppendItems(std::size_t container, std::size_t last, TStringVector const& items);

    // Adds 'value', a table, to the array of tables 'array' as a new element: a [[...]] header after the last element
    // and its sub-tables
    TASWTomlResult AppendTable(std::size_t array, TASWTomlValue const& value);

    // Whether a new header at 'offset' gets a blank line before it: when Writing.BlankLineBeforeNewTable is on and
    // the line before 'offset' isn't blank (none is at the start of the text)
    bool BlankLineBefore(std::size_t offset) const;

    // The offset after the last key line or header of 'block', before the blank and comment lines that end it; 0 for
    // an empty block
    std::size_t BlockEnd(std::size_t block) const;

    // Fails, for messages about the edit of 'path', with InvalidValue when 'text' can't be written in a comment: it
    // isn't valid UTF-8, or holds a control character other than a tab (see HasCommentControlCharacter())
    TASWTomlResult CheckCommentText(TASWTomlPath const& path, std::string_view text) const;

    // The first byte of the comment lines directly above the line that starts at 'lineStart', in an array or inline
    // table whose text before that line starts at 'begin' (after the bracket or comma before it); 'lineStart' when
    // there are none
    std::size_t CommentHead(std::size_t begin, std::size_t lineStart) const;

    // The array or inline table that 'node' is an element or entry of (through the dotted-key tables it is in);
    // NoIndex when it is written in key lines and headers instead
    std::size_t Container(std::size_t node) const;

    // The elements of the array 'container', or the entries of the inline table 'container' (the values of its keys,
    // through dotted keys), in the order they are in the text
    TIndexVector ContainerItems(std::size_t container) const;

    // Adds a change of the text: the bytes [begin, end) removed
    void Erase(std::size_t begin, std::size_t end);

    // Whether 'value' can take the place of 'node' in the text: any value can for a value (of a key line, an element
    // or an entry), only a table for a table that isn't an inline table, and an array of tables for one
    bool Holds(std::size_t node, TASWTomlValue const& value) const;

    // The blanks at the start of 'line'
    std::string_view Indentation(std::size_t line) const;

    // Adds a change of the text: 'text' inserted at 'offset'
    void Insert(std::size_t offset, std::string text);

    // Adds 'block', headers and their lines (with the blank line before them that BlankLineBefore() asks for), at
    // 'offset', with a blank line after it when it goes before a line that isn't blank and
    // Writing.BlankLineBeforeNewTable is on
    void InsertBlock(std::size_t offset, std::string block);

    // Adds 'item', a new element of the array 'container', before its element 'before', in its layout: on a line of
    // its own above it and the comment lines above it when it starts a line, else before it on its line
    void InsertItem(std::size_t container, std::size_t before, std::string const& item);

    // Adds 'lines', which end with a line break, at 'offset', the start of a line or the end of the text (where a
    // line break comes first when the last line has none)
    void InsertLines(std::size_t offset, std::string lines);

    // Whether the edit removes the value 'node' (of a key line, or an element or entry)
    bool IsRemoved(std::size_t node) const;

    // Where the element or entry 'item' of an array or inline table starts: its value, or its key
    std::size_t ItemStart(std::size_t item) const;

    // The last key line of 'block' that the edit doesn't remove; NoIndex when it has none
    std::size_t LastKeyLine(std::size_t block) const;

    // The last value (a scalar, array or inline table) whose key reaches 'table' through dotted keys only: the last
    // key line under a dotted-key table, or the last entry of an inline table, that the edit doesn't remove (when it
    // removes all of them, the last one); NoIndex when there is none
    std::size_t LastValue(std::size_t table) const;

    // The line that starts at 'offset', or when the edit removes it, the first line after it that the edit doesn't
    // remove; NoIndex when there is none
    std::size_t LineAt(std::size_t offset) const;

    // The line that ends at 'offset', its line break included, or when the edit removes it, the last line before it
    // that the edit doesn't remove; NoIndex when there is none
    std::size_t LineBefore(std::size_t offset) const;

    // The line the byte at 'offset' is on; NoIndex when it is past the end
    std::size_t LineOf(std::size_t offset) const;

    // Follows 'path' (valid) through the nodes that exist: sets 'node' to the node at 'path' and 'missing' to the
    // number of its parts, or at the first key that doesn't exist, 'node' to the table that would hold it and
    // 'missing' to its part. Fails, for messages about the edit of 'path', with InvalidArgument for an index of what
    // isn't an array, a key of an array or of a scalar, and NotFound for an index past the end of an array.
    TASWTomlResult Locate(TASWTomlPath const& path, std::size_t& node, std::size_t& missing) const;

    // Follows 'path' (valid) to its node, and sets 'line' to the line that defines it (see DefiningLine()). Fails as
    // Locate() does, with NotFound when nothing is at 'path', and InvalidArgument when what is there has no line of
    // its own.
    TASWTomlResult LocateLine(TASWTomlPath const& path, std::size_t& line) const;

    // Marks in 'lines' the lines of 'block' (which has a header) with the header's comment, up to the comment lines
    // of the next header
    void MarkBlock(std::size_t block, TFlagVector& lines) const;

    // Marks in 'lines' the key/value line 'line' with its comment
    void MarkLine(std::size_t line, TFlagVector& lines) const;

    // Marks in 'lines' the lines that define 'node' (written in key lines and headers) and the tables under it
    void MarkSubtree(std::size_t node, TFlagVector& lines) const;

    // Sets the array value 'array', which is 'old', to the array 'value', changing the least text: the elements that
    // are the same as new ones stay (the longest common subsequence, see Partners()), the others between them are
    // set to the new ones in order, and what is left of them is removed or added in its place
    TASWTomlResult MergeArray(std::size_t array, TASWTomlValue const& old, TASWTomlValue const& value);

    // Sets the array of tables 'array', which is 'old', to 'value', an array of tables, as MergeArray() sets an array:
    // an element that stays keeps its [[...]] block, its comment and the keys that stay, a new one gets a block of its
    // own before the next one's comment, or after the last one's tables
    TASWTomlResult MergeElements(std::size_t array, TASWTomlValue const& old, TASWTomlValue const& value);

    // Sets the table 'table' (of any form), which is 'old', to the table 'value': each key it has in 'value' is set
    // (MergeValue()), each it doesn't is removed, and each 'value' has that it doesn't is added, in the order of
    // 'value'; the order of the keys that stay stays
    TASWTomlResult MergeTable(std::size_t table, TASWTomlValue const& old, TASWTomlValue const& value);

    // Sets 'node', which is 'old', to 'value', changing the least text: nothing when they are identical; a table or
    // array is merged with a table or array (MergeTable(), MergeArray(), MergeElements()); a value of another type
    // replaces a value in place (Replace()), and a table that isn't inline, or an array of tables, that can't hold it
    // is removed and 'value' written as a new key of its table
    TASWTomlResult MergeValue(std::size_t node, TASWTomlValue const& old, TASWTomlValue const& value);

    // The name of 'node' (one of its Names) inside 'range'; an empty range when it has none there
    TTextRange NameIn(std::size_t node, TTextRange range) const;

    // Adds the keys of 'keys' (a table) that are tables or arrays of tables to 'table' (the root, a header table, an
    // element or an implicit table) as headers, after the last block of 'table' and of the tables under it, or at the
    // end of the text for the root
    TASWTomlResult NewBlocks(std::size_t table, TASWTomlValue const& keys);

    // Adds the keys of 'keys' (a table), which 'table' doesn't have, in the form and place 'table' allows
    TASWTomlResult NewKeys(std::size_t table, TASWTomlValue const& keys);

    // Adds the keys of 'keys' (a table) that aren't tables or arrays of tables to 'table' (the root, a header table, an
    // element or an implicit table, which then gets a header of its own) as key/value lines
    TASWTomlResult NewKeyLines(std::size_t table, TASWTomlValue const& keys);

    // The failure 'code' of the edit of 'path', because of 'reason'
    TASWTomlResult PathFailure(ETomlCode code, TASWTomlPath const& path, std::string const& reason) const;

    // The key parts, as written, of a header that names 'table' (a header table, an element or an implicit table),
    // e.g. a."b c"; empty for the root
    std::string PathText(std::size_t table) const;

    // Removes 'items' (some of the elements or entries of the array or inline table 'container'), each run of them
    // with one comma, or with the lines they are on when a run starts a line; all of them leave [] or {}
    void RemoveItems(std::size_t container, TIndexVector const& items);

    // Removes the lines marked in 'lines', each run of them with a blank line next to it when blank lines would pile
    // up, and keeping a text without a line break at its end without one. A new line inserted inside a run goes at
    // its start instead (the same text), and a run with new lines at either end keeps the blank lines next to it.
    void RemoveLines(TFlagVector const& lines);

    // Plans the removal of 'node' (not the root) with the text that defines it, as TASWTomlDocument::Remove() does
    void RemoveNode(std::size_t node);

    // Replaces the text of 'node', a value (of a key line, an element or an entry), with 'value', a scalar in the form
    // of the old one where it can
    TASWTomlResult Replace(std::size_t node, TASWTomlValue const& value);

    // How many levels of tables and arrays may be nested in a value inside 'depth' levels
    std::size_t Room(std::size_t depth) const noexcept;

    // Sets 'node', which exists at 'path', to 'value'
    TASWTomlResult SetExisting(std::size_t node, TASWTomlPath const& path, TASWTomlValue const& value);

    // Makes the changes of the text that 'plan' asks of the array or inline table 'container': removed items with
    // their commas, new ones in the places of removed ones at its end, then at its end and before the ones they go
    // before
    void SpliceItems(std::size_t container, TContainerPlan const& plan);

    // The first header line of 'table' or of a table under it; NoIndex when there is none
    std::size_t SubtreeFirstHeader(std::size_t table) const;

    // The last block of 'table' or of a table under it; NoIndex when there is none
    std::size_t SubtreeLastBlock(std::size_t table) const;

    // The text in 'range'
    std::string_view TextOf(TTextRange range) const;

    // Sets the value at 'path' to 'value', as SetValue() does
    TASWTomlResult Walk(TASWTomlPath const& path, TASWTomlValue const& value);

    // Appends a key/value line for each key of 'keys' (a table) to 'text': 'indent', the key, " = " and its value
    TASWTomlResult WriteKeyLines(std::string_view indent, TASWTomlValue const& keys, std::string& text) const;

public:
    // An editor of 'text' (the document's text without a byte order mark), whose index is 'index' and values 'root',
    // with the document's 'options', the value options 'values' and the line break 'lineBreak' for new lines
    TEditor(std::string const& text, Detail::TDocIndex const& index, TASWTomlValue const& root,
        TASWTomlOptions const& options, TASWTomlValueOptions const& values, ELineBreak lineBreak);

    // The changes of TASWTomlDocument::Append()
    TASWTomlResult Append(TASWTomlPath const& path, TASWTomlValue const& value);

    // The text with the changes made
    std::string EditedText() const;

    // Turns the lines removed and the plans of the arrays and inline tables into changes of the text, and puts all of
    // them in the order of their places in it. Call it once, after Append(), Remove(), Rename(), SetComment(),
    // SetTrailingComment() or SetValue() succeeded, before IsEmpty() and EditedText().
    void Finish();

    // Whether the edit changes nothing
    bool IsEmpty() const noexcept;

    // The changes of TASWTomlDocument::Remove() of 'node', which isn't the root
    void Remove(std::size_t node);

    // The changes of TASWTomlDocument::Rename()
    TASWTomlResult Rename(TASWTomlPath const& path, std::string_view newKey);

    // The changes of TASWTomlDocument::SetComment()
    TASWTomlResult SetComment(TASWTomlPath const& path, std::string_view text);

    // The changes of TASWTomlDocument::SetTrailingComment()
    TASWTomlResult SetTrailingComment(TASWTomlPath const& path, std::string_view text);

    // The changes of TASWTomlDocument::SetValue()
    TASWTomlResult SetValue(TASWTomlPath const& path, TASWTomlValue const& value);
};


void AppendKey(Detail::TDocKey const& key, std::string& text);
void AppendKeyValue(Detail::TDocKeyValue const& keyValue, std::string& text);
void AppendValue(Detail::TDocValue const& value, std::string& text);
TStringVector CommentLines(std::string_view text);
std::string CommentOf(Detail::TDocModel const& model, Detail::TDocIndex const& index, TASWTomlPath const& path);
std::size_t CommentStart(Detail::TDocIndex const& index, std::size_t line) noexcept;
std::string_view CommentText(std::string_view text) noexcept;
std::string ConvertLineBreaks(std::string const& text, ELineBreak lineBreak);
std::size_t DefiningLine(Detail::TDocIndex const& index, std::size_t node) noexcept;
Detail::TDocIndex EmptyIndex();
TASWTomlResult Failure(ETomlCode code, std::string message);
std::size_t FileCommentLines(Detail::TDocIndex const& index) noexcept;
TASWTomlValue const* FindValue(TASWTomlValue const& root, TASWTomlPath const& path) noexcept;
TASWTomlValue* FindValue(TASWTomlValue& root, TASWTomlPath const& path) noexcept;
bool HasCommentControlCharacter(std::string_view text) noexcept;
bool IsArrayOfTables(TASWTomlValue const& value) noexcept;
std::string Joined(TStringVector const& lines);
std::size_t KeyStart(std::string_view text, std::size_t offset) noexcept;
std::string Layout(std::string_view before);
std::string MessagePath(TASWTomlPath const& path);
std::string_view ModelText(Detail::TDocModel const& model, Detail::TIndexLine const& line) noexcept;
TIndexVector Partners(TASWTomlValue::TValueVector const& old, TASWTomlValue::TValueVector const& values);
TASWTomlPath Prefix(TASWTomlPath const& path, std::size_t count);
void RemoveValue(TASWTomlValue& root, Detail::TDocIndex const& index, TASWTomlPath const& path);
bool SpliceBefore(TSplice const& left, TSplice const& right) noexcept;
std::string TrailingCommentOf(Detail::TDocModel const& model, Detail::TDocIndex const& index,
    TASWTomlPath const& path);
std::string_view Trimmed(std::string_view text) noexcept;
TASWTomlValue Wrapped(TASWTomlPath::TPartVector const& parts, std::size_t first, TASWTomlValue const& value);

//---------------------------------------------------------------------------
/*
    AppendKey

    Appends 'key' to 'text' as it was written: each part with the blanks around it, and a '.' between two parts
*/
void AppendKey(Detail::TDocKey const& key, std::string& text)
{
    for (std::size_t i = 0; i < key.Parts.size(); ++i)
    {
        Detail::TDocKeyPart const& part = key.Parts[i];
        if (i > 0)
            text += '.';

        text += part.Before;
        text += part.Text;
        text += part.After;
    }
}
//---------------------------------------------------------------------------
/*
    AppendKeyValue

    Appends 'keyValue' to 'text' as it was written: the key, '=', the blanks after it and the value
*/
void AppendKeyValue(Detail::TDocKeyValue const& keyValue, std::string& text)
{
    AppendKey(keyValue.Key, text);
    text += '=';
    text += keyValue.BeforeValue;
    AppendValue(keyValue.Value, text);
}
//---------------------------------------------------------------------------
/*
    AppendValue

    Appends 'value' to 'text' as it was written: a scalar's text, or an array's or an inline table's brackets or
    braces with each element or entry, the text around it and its comma
*/
void AppendValue(Detail::TDocValue const& value, std::string& text)
{
    switch (value.Kind)
    {
        case Detail::EValueKind::Scalar:
            text += value.Text;
            break;

        case Detail::EValueKind::Array:
            text += '[';
            for (Detail::TDocElement const& element : value.Elements)
            {
                text += element.Before;
                AppendValue(element.Value, text);
                text += element.After;
                if (element.Comma)
                    text += ',';
            }

            text += value.Close;
            text += ']';
            break;

        case Detail::EValueKind::InlineTable:
            text += '{';
            for (Detail::TDocInlineEntry const& entry : value.Entries)
            {
                text += entry.Before;
                AppendKeyValue(entry.KeyValue, text);
                text += entry.After;
                if (entry.Comma)
                    text += ',';
            }

            text += value.Close;
            text += '}';
            break;
    }
}
//---------------------------------------------------------------------------
/*
    CommentLines

    The lines of the comment 'text', as TASWTomlDocument::SetComment() splits it: at each LF, CRLF or CR (one at its
    end starting no line), each without the blanks around it; none for an empty 'text'
*/
TStringVector CommentLines(std::string_view text)
{
    TStringVector lines;
    std::size_t begin = 0;
    while (begin < text.size())
    {
        std::size_t const end = std::min(text.find_first_of("\r\n", begin), text.size());
        lines.emplace_back(Trimmed(text.substr(begin, end - begin)));

        begin = end + 1;
        if (end + 1 < text.size() && text[end] == '\r' && text[end + 1] == '\n')
            ++begin;
    }

    return lines;
}
//---------------------------------------------------------------------------
/*
    CommentOf

    The comment of 'path' in the text whose model is 'model' and index 'index', as TASWTomlDocument::GetComment()
    describes it
*/
std::string CommentOf(Detail::TDocModel const& model, Detail::TDocIndex const& index, TASWTomlPath const& path)
{
    std::size_t const node = Detail::FindNode(index, path);
    if (node == NoIndex)
        return std::string();

    // The file comment, or the comment lines above the line that defines the node
    std::size_t const end = node == 0 ? FileCommentLines(index) : DefiningLine(index, node);
    if (end == NoIndex)
        return std::string();

    std::size_t const begin = node == 0 ? 0 : CommentStart(index, end);
    std::string comment;
    for (std::size_t line = begin; line < end; ++line)
    {
        if (line > begin)
            comment += '\n';

        comment += CommentText(ModelText(model, index.Lines[line]));
    }

    return comment;
}
//---------------------------------------------------------------------------
/*
    CommentStart

    The first of the comment lines of 'index' directly above its line 'line'; 'line' itself when there are none
*/
std::size_t CommentStart(Detail::TDocIndex const& index, std::size_t line) noexcept
{
    while (line > 0 && index.Lines[line - 1].Kind == Detail::ELineKind::Comment)
        --line;

    return line;
}
//---------------------------------------------------------------------------
/*
    CommentText

    The text of the comment in 'text', a comment line or what follows a key's value or a header's closing bracket on
    its line: what follows its '#', without the blanks around it; empty when it has no comment
*/
std::string_view CommentText(std::string_view text) noexcept
{
    std::size_t const hash = text.find('#');
    if (hash == std::string_view::npos)
        return std::string_view();

    return Trimmed(text.substr(hash + 1));
}
//---------------------------------------------------------------------------
/*
    ConvertLineBreaks

    'text' with each line break ("\n" or "\r\n") written as 'lineBreak'
*/
std::string ConvertLineBreaks(std::string const& text, ELineBreak lineBreak)
{
    std::string_view const written = Detail::LineBreakText(lineBreak);
    std::string converted;
    converted.reserve(text.size() + text.size() / 8);

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
        {
            converted += written;
            ++i;
        }
        else if (text[i] == '\n')
        {
            converted += written;
        }
        else
        {
            converted += text[i];
        }
    }

    return converted;
}
//---------------------------------------------------------------------------
/*
    DefiningLine

    The line of 'index' that defines 'node', whose comment and trailing comment are the node's: the key line of a
    key, the header of a header table or an element of an array of tables, and the header of an array of tables'
    first element; NoIndex when it has none (the root, a table only dotted keys or longer headers made, and a value
    inside an array or inline table)
*/
std::size_t DefiningLine(Detail::TDocIndex const& index, std::size_t node) noexcept
{
    Detail::TIndexNode const& defined = index.Nodes[node];
    if (defined.Kind == ENodeKind::ArrayOfTables)
        return index.Nodes[defined.Children.front()].Line;

    return defined.Line;
}
//---------------------------------------------------------------------------
/*
    EmptyIndex

    The index of an empty model (see Detail::BuildIndex()): the root table, with no keys, and the first block, with no
    lines
*/
Detail::TDocIndex EmptyIndex()
{
    Detail::TDocIndex index;
    index.Nodes.emplace_back();
    index.BlockLines.push_back(0);

    return index;
}
//---------------------------------------------------------------------------
/*
    Failure

    A failure with 'code' and 'message', not about a place in a text
*/
TASWTomlResult Failure(ETomlCode code, std::string message)
{
    TASWTomlResult result;
    result.Code = code;
    result.Message = std::move(message);

    return result;
}
//---------------------------------------------------------------------------
/*
    FileCommentLines

    The number of lines of the file comment of 'index': the comment lines at its start, when a blank line or the end
    of the text follows them; 0 when there is none
*/
std::size_t FileCommentLines(Detail::TDocIndex const& index) noexcept
{
    std::size_t count = 0;
    while (count < index.Lines.size() && index.Lines[count].Kind == Detail::ELineKind::Comment)
        ++count;

    if (count < index.Lines.size() && index.Lines[count].Kind != Detail::ELineKind::Blank)
        return 0;

    return count;
}
//---------------------------------------------------------------------------
/*
    FindValue

    The value at 'path' in 'root', as TASWTomlDocument::Find() describes it
*/
TASWTomlValue const* FindValue(TASWTomlValue const& root, TASWTomlPath const& path) noexcept
{
    if (!path.IsValid())
        return nullptr;

    TASWTomlValue const* value = &root;
    for (TASWTomlPathPart const& part : path.GetParts())
    {
        if (part.IsKey())
        {
            value = value->Find(part.GetKey());
        }
        else
        {
            TASWTomlValue::TValueVector const* const array = value->AsArray();
            value = array != nullptr && part.GetIndex() < array->size() ? &(*array)[part.GetIndex()] : nullptr;
        }

        if (value == nullptr)
            return nullptr;
    }

    return value;
}
//---------------------------------------------------------------------------
/*
    FindValue

    The value at 'path' in 'root', as the other FindValue() finds it, to be changed
*/
TASWTomlValue* FindValue(TASWTomlValue& root, TASWTomlPath const& path) noexcept
{
    return const_cast<TASWTomlValue*>(FindValue(static_cast<TASWTomlValue const&>(root), path));
}
//---------------------------------------------------------------------------
/*
    HasCommentControlCharacter

    Whether 'text' holds a control character that TOML doesn't allow in a comment: U+0000 to U+0008, U+000A to U+001F
    or U+007F (a tab is allowed)
*/
bool HasCommentControlCharacter(std::string_view text) noexcept
{
    for (char const c : text)
    {
        unsigned char const byte = static_cast<unsigned char>(c);

        if ((byte < 0x20 && byte != '\t') || byte == 0x7F)
            return true;
    }

    return false;
}
//---------------------------------------------------------------------------
/*
    IsArrayOfTables

    Whether 'value' is written as an array of tables where a header can go: an array that isn't empty and holds only
    tables (as Detail::WriteTableBlocks() writes one)
*/
bool IsArrayOfTables(TASWTomlValue const& value) noexcept
{
    TASWTomlValue::TValueVector const* const array = value.AsArray();
    if (array == nullptr || array->empty())
        return false;

    for (TASWTomlValue const& element : *array)
    {
        if (!element.IsTable())
            return false;
    }

    return true;
}
//---------------------------------------------------------------------------
/*
    Joined

    'lines' joined with "\n"
*/
std::string Joined(TStringVector const& lines)
{
    std::string joined;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        if (i > 0)
            joined += '\n';

        joined += lines[i];
    }

    return joined;
}
//---------------------------------------------------------------------------
/*
    KeyStart

    The offset in 'text' of the first byte at or after 'offset' that isn't a blank, a line break or part of a comment:
    where the key of an inline table's entry starts after the text before it
*/
std::size_t KeyStart(std::string_view text, std::size_t offset) noexcept
{
    while (offset < text.size())
    {
        char const c = text[offset];
        if (c == '#')
        {
            while (offset < text.size() && text[offset] != '\n')
                ++offset;
        }
        else if (Detail::IsBlank(c) || c == '\n' || c == '\r')
        {
            ++offset;
        }
        else
        {
            break;
        }
    }

    return offset;
}
//---------------------------------------------------------------------------
/*
    Layout

    The text a new element or entry gets before it, from 'before', the text before the last one (after the bracket
    or comma before it): the same, or when it holds line breaks, its last line break and the indentation after it
*/
std::string Layout(std::string_view before)
{
    std::size_t const lineFeed = before.rfind('\n');
    if (lineFeed == std::string_view::npos)
        return std::string(before);

    std::string layout = lineFeed > 0 && before[lineFeed - 1] == '\r' ? "\r\n" : "\n";
    layout += before.substr(lineFeed + 1);

    return layout;
}
//---------------------------------------------------------------------------
/*
    MessagePath

    'path' as messages show it (TASWTomlPath::ToString()), the root path as "the root table"
*/
std::string MessagePath(TASWTomlPath const& path)
{
    return path.IsRoot() ? std::string("the root table") : path.ToString();
}
//---------------------------------------------------------------------------
/*
    ModelText

    The text in 'model' of the line 'line' of its index, without its line break: all of a blank or comment line, what
    follows the value of a key line (blanks and a comment), and what follows the closing bracket of a header
*/
std::string_view ModelText(Detail::TDocModel const& model, Detail::TIndexLine const& line) noexcept
{
    Detail::TDocBlock const& block = model.Blocks[line.Block];
    if (line.Entry == NoIndex)
        return block.Header.Text;

    return block.Entries[line.Entry].Text;
}
//---------------------------------------------------------------------------
/*
    Partners

    For each element of 'values', the element of 'old' that becomes it (NoIndex for a new one), so that a merge of
    'old' into 'values' changes the least: first the elements that are identical to new ones, as the longest common
    subsequence (the ones that are the same at the start and the end, then the longest common subsequence of the rest
    when it has at most MaxAlignCells cells, else none of the rest), then between two of those, the others paired in
    order. The elements of 'old' that no element of 'values' has are removed.
*/
TIndexVector Partners(TASWTomlValue::TValueVector const& old, TASWTomlValue::TValueVector const& values)
{
    std::size_t const oldSize = old.size();
    std::size_t const size = values.size();
    TIndexVector partners(size, NoIndex);

    // The same ones at the start and the end
    std::size_t head = 0;
    while (head < oldSize && head < size && Detail::IdenticalValues(old[head], values[head]))
    {
        partners[head] = head;
        ++head;
    }

    std::size_t tail = 0;
    while (tail < oldSize - head && tail < size - head)
    {
        if (!Detail::IdenticalValues(old[oldSize - 1 - tail], values[size - 1 - tail]))
            break;

        partners[size - 1 - tail] = oldSize - 1 - tail;
        ++tail;
    }

    // The longest common subsequence of the rest: lengths[i * columns + j] is the length of the one of the old ones
    // from i and the new ones from j
    std::size_t const rows = oldSize - head - tail + 1;
    std::size_t const columns = size - head - tail + 1;
    if (rows > 1 && columns > 1 && rows * columns <= MaxAlignCells)
    {
        TIndexVector lengths(rows * columns, 0);
        for (std::size_t i = rows - 1; i-- > 0;)
        {
            for (std::size_t j = columns - 1; j-- > 0;)
            {
                if (Detail::IdenticalValues(old[head + i], values[head + j]))
                    lengths[i * columns + j] = lengths[(i + 1) * columns + j + 1] + 1;
                else
                    lengths[i * columns + j] = std::max(lengths[(i + 1) * columns + j], lengths[i * columns + j + 1]);
            }
        }

        std::size_t i = 0;
        std::size_t j = 0;
        while (i + 1 < rows && j + 1 < columns)
        {
            if (Detail::IdenticalValues(old[head + i], values[head + j]) &&
                lengths[i * columns + j] == lengths[(i + 1) * columns + j + 1] + 1)
            {
                partners[head + j] = head + i;
                ++i;
                ++j;
            }
            else if (lengths[(i + 1) * columns + j] >= lengths[i * columns + j + 1])
            {
                ++i;
            }
            else
            {
                ++j;
            }
        }
    }

    // Between two that are the same, the others in order
    std::size_t oldNext = 0;
    std::size_t next = 0;
    while (next < size)
    {
        std::size_t same = next;
        while (same < size && partners[same] == NoIndex)
            ++same;

        std::size_t const oldSame = same < size ? partners[same] : oldSize;
        for (std::size_t k = 0; next + k < same && oldNext + k < oldSame; ++k)
            partners[next + k] = oldNext + k;

        if (same == size)
            break;

        oldNext = oldSame + 1;
        next = same + 1;
    }

    return partners;
}
//---------------------------------------------------------------------------
/*
    Prefix

    The path of the first 'count' parts of 'path' (valid), e.g. a.b for a.b[2].c and 2
*/
TASWTomlPath Prefix(TASWTomlPath const& path, std::size_t count)
{
    TASWTomlPath prefix;
    for (std::size_t i = 0; i < count; ++i)
        prefix.Append(path.GetParts()[i]);

    return prefix;
}
//---------------------------------------------------------------------------
/*
    RemoveValue

    Removes the value at 'path' (not the root, and in 'root', the values of 'index') from 'root', as the text without
    it reads: then each table on the way that only dotted keys or longer headers made, and each array of tables, that
    is left empty goes too, as nothing defines it any more
*/
void RemoveValue(TASWTomlValue& root, Detail::TDocIndex const& index, TASWTomlPath const& path)
{
    TASWTomlPath::TPartVector const& parts = path.GetParts();
    for (std::size_t count = parts.size(); count > 0; --count)
    {
        TASWTomlPath const holderPath = Prefix(path, count - 1);
        TASWTomlValue* const holder = FindValue(root, holderPath);
        TASWTomlPathPart const& part = parts[count - 1];
        bool empty = false;

        if (part.IsKey())
        {
            holder->Remove(part.GetKey());
            empty = holder->AsTable()->empty();
        }
        else
        {
            TASWTomlValue::TValueVector& array = *holder->AsArray();
            array.erase(array.begin() + static_cast<std::ptrdiff_t>(part.GetIndex()));
            empty = array.empty();
        }

        ENodeKind const kind = index.Nodes[Detail::FindNode(index, holderPath)].Kind;
        if (count == 1 || !empty ||
            (kind != ENodeKind::DottedTable && kind != ENodeKind::ImplicitTable && kind != ENodeKind::ArrayOfTables))
        {
            break;
        }
    }
}
//---------------------------------------------------------------------------
/*
    SpliceBefore

    Whether 'left' comes before 'right' in the text: it starts before it, or at the same place and ends before it (so
    an insertion comes before a change that starts where it is)
*/
bool SpliceBefore(TSplice const& left, TSplice const& right) noexcept
{
    return left.Begin < right.Begin || (left.Begin == right.Begin && left.End < right.End);
}
//---------------------------------------------------------------------------
/*
    TrailingCommentOf

    The trailing comment of 'path' in the text whose model is 'model' and index 'index', as
    TASWTomlDocument::GetTrailingComment() describes it
*/
std::string TrailingCommentOf(Detail::TDocModel const& model, Detail::TDocIndex const& index,
    TASWTomlPath const& path)
{
    std::size_t const node = Detail::FindNode(index, path);
    std::size_t const line = node != NoIndex ? DefiningLine(index, node) : NoIndex;
    if (line == NoIndex)
        return std::string();

    return std::string(CommentText(ModelText(model, index.Lines[line])));
}
//---------------------------------------------------------------------------
/*
    Trimmed

    'text' without the blanks at its start and end
*/
std::string_view Trimmed(std::string_view text) noexcept
{
    std::size_t begin = 0;
    while (begin < text.size() && Detail::IsBlank(text[begin]))
        ++begin;

    std::size_t end = text.size();
    while (end > begin && Detail::IsBlank(text[end - 1]))
        --end;

    return text.substr(begin, end - begin);
}
//---------------------------------------------------------------------------
/*
    Wrapped

    'value' inside a table for each of the keys parts[first] to the last part: { a = { b = value } } for the keys a
    and b; 'value' itself when 'first' is the number of parts. The parts from 'first' on must be keys.
*/
TASWTomlValue Wrapped(TASWTomlPath::TPartVector const& parts, std::size_t first, TASWTomlValue const& value)
{
    TASWTomlValue wrapped = value;
    for (std::size_t i = parts.size(); i > first; --i)
    {
        TASWTomlValue table;
        table.Set(parts[i - 1].GetKey(), std::move(wrapped));
        wrapped = std::move(table);
    }

    return wrapped;
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TEditor
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TEditor::TEditor(std::string const& text, Detail::TDocIndex const& index, TASWTomlValue const& root,
    TASWTomlOptions const& options, TASWTomlValueOptions const& values, ELineBreak lineBreak)
    : m_Text(text),
      m_Index(index),
      m_Root(root),
      m_MaxDepth(options.Reading.MaxNestingDepth),
      m_CommentPrefix(options.Writing.CommentPrefix),
      m_LineBreak(Detail::LineBreakText(lineBreak)),
      m_RemovedLines(index.Lines.size(), false)
{
    m_Style.Values = values;
    m_Style.LineBreak = lineBreak;
    m_Style.BlankLineBeforeTable = options.Writing.BlankLineBeforeNewTable;
}
//---------------------------------------------------------------------------
void TEditor::AddValues(std::size_t node, TIndexVector& items) const
{
    if (m_Index.Nodes[node].Kind != ENodeKind::DottedTable)
    {
        items.push_back(node);
        return;
    }

    for (std::size_t const child : m_Index.Nodes[node].Children)
        AddValues(child, items);
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::Append(TASWTomlPath const& path, TASWTomlValue const& value)
{
    m_Verb = "append to";
    if (!path.IsValid())
        return Failure(ETomlCode::InvalidArgument, path.GetError());

    // A missing key gets an array of the one value
    std::size_t const node = Detail::FindNode(m_Index, path);
    if (node == NoIndex)
        return Walk(path, TASWTomlValue(TASWTomlValue::TValueVector{ value }));

    Detail::TIndexNode const& array = m_Index.Nodes[node];
    if (array.Kind == ENodeKind::ArrayOfTables)
        return AppendTable(node, value);

    if (array.Kind != ENodeKind::Array)
        return PathFailure(ETomlCode::InvalidArgument, path, "it isn't an array.");

    m_Style.MaxDepth = Room(array.Depth);
    std::string item;
    TASWTomlResult const result = Detail::WriteValue(value, m_Style, item);
    if (result.IsOk())
        AppendItems(node, array.Children.empty() ? NoIndex : array.Children.back(), TStringVector{ item });

    return result;
}
//---------------------------------------------------------------------------
void TEditor::AppendItems(std::size_t container, std::size_t last, TStringVector const& items)
{
    Detail::TIndexNode const& holder = m_Index.Nodes[container];
    bool const isArray = holder.Kind == ENodeKind::Array;

    if (last == NoIndex)
    {
        // An empty one: the items replace the blanks inside the brackets, or go first before what else is there
        std::string joined = items[0];
        for (std::size_t i = 1; i < items.size(); ++i)
            joined += ", " + items[i];

        TTextRange const inside{ holder.Close.Begin, holder.Close.End - 1 };
        bool blanksOnly = true;
        for (char const c : TextOf(inside))
        {
            if (!Detail::IsBlank(c))
                blanksOnly = false;
        }

        if (blanksOnly)
            m_Splices.push_back(TSplice{ inside.Begin, inside.End, isArray ? joined : " " + joined + " " });
        else
            Insert(inside.Begin, isArray ? joined : " " + joined);

        return;
    }

    Detail::TIndexNode const& previous = m_Index.Nodes[last];
    std::string_view const previousItem = TextOf(previous.Item);
    bool const hasComma = !previousItem.empty() && previousItem.back() == ',';
    std::size_t const start = isArray ? previous.Value.Begin : KeyStart(m_Text, previous.Item.Begin);
    std::string layout = Layout(TextOf(TTextRange{ previous.Item.Begin, start }));
    bool const oneLine = layout.find('\n') == std::string::npos;

    // On one line, the first one's blanks are the brackets', so one blank
    if (oneLine && previous.Item.Begin == holder.Value.Begin + 1)
        layout = " ";

    std::string joined = items[0];
    for (std::size_t i = 1; i < items.size(); ++i)
        joined += "," + layout + items[i];

    if (oneLine)
    {
        // Right after the last one
        if (hasComma)
            Insert(previous.Item.End, layout + joined + ",");
        else
            Insert(previous.Item.End, "," + layout + joined);

        return;
    }

    // One per line: on lines of their own after the last one's line, so a comment at its end stays there
    std::string_view const close = TextOf(holder.Close);
    std::size_t const lineBreak = close.find_first_of("\r\n");
    std::size_t const offset = holder.Close.Begin + (lineBreak == std::string_view::npos ? 0 : lineBreak);
    if (!hasComma)
        Insert(previous.Item.End, ",");

    Insert(offset, layout + joined + (hasComma ? "," : ""));
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::AppendTable(std::size_t array, TASWTomlValue const& value)
{
    if (!value.IsTable())
    {
        return Failure(ETomlCode::InvalidArgument, "An element of an array of tables must be a table, not a value of "
            "type " + std::string(ToString(value.GetType())) + ".");
    }

    std::size_t const element = m_Index.Nodes[array].Children.back();
    std::size_t const offset = BlockEnd(SubtreeLastBlock(element));
    m_Style.MaxDepth = Room(m_Index.Nodes[array].Depth);

    std::string block;
    TASWTomlResult const result =
        Detail::WriteTableBlocks(PathText(element), true, BlankLineBefore(offset), value, m_Style, block);
    if (result.IsOk())
        InsertBlock(offset, std::move(block));

    return result;
}
//---------------------------------------------------------------------------
bool TEditor::BlankLineBefore(std::size_t offset) const
{
    if (!m_Style.BlankLineBeforeTable)
        return false;

    // None at the start of the text, or after the lines the edit removes there
    std::size_t const line = LineBefore(offset);

    return line != NoIndex && m_Index.Lines[line].Kind != Detail::ELineKind::Blank;
}
//---------------------------------------------------------------------------
std::size_t TEditor::BlockEnd(std::size_t block) const
{
    std::size_t const begin = m_Index.BlockLines[block];
    std::size_t end = block + 1 < m_Index.BlockLines.size() ? m_Index.BlockLines[block + 1] : m_Index.Lines.size();

    for (; end > begin; --end)
    {
        Detail::ELineKind const kind = m_Index.Lines[end - 1].Kind;
        if (kind == Detail::ELineKind::KeyValue || kind == Detail::ELineKind::Header)
            return m_Index.Lines[end - 1].Next;
    }

    return 0;
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::CheckCommentText(TASWTomlPath const& path, std::string_view text) const
{
    if (!Detail::IsValidUtf8(text))
        return PathFailure(ETomlCode::InvalidValue, path, "the comment isn't valid UTF-8.");

    if (HasCommentControlCharacter(text))
    {
        return PathFailure(ETomlCode::InvalidValue, path, "the comment holds a control character other than a tab, "
            "which TOML doesn't allow in a comment.");
    }

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
std::size_t TEditor::CommentHead(std::size_t begin, std::size_t lineStart) const
{
    std::size_t head = lineStart;
    for (;;)
    {
        // The line above ends with the line feed before 'head'; it is a whole line when a line feed after 'begin'
        // comes before it (the text before that one is the end of the line of the bracket or comma before)
        std::size_t const lineFeed = TextOf(TTextRange{ begin, head - 1 }).rfind('\n');
        if (lineFeed == std::string_view::npos)
            break;

        std::size_t const above = begin + lineFeed + 1;
        std::size_t first = above;
        while (Detail::IsBlank(m_Text[first]))
            ++first;

        if (m_Text[first] != '#')
            break;

        head = above;
    }

    return head;
}
//---------------------------------------------------------------------------
std::size_t TEditor::Container(std::size_t node) const
{
    std::size_t holder = m_Index.Nodes[node].Parent;
    while (m_Index.Nodes[holder].Kind == ENodeKind::DottedTable)
        holder = m_Index.Nodes[holder].Parent;

    ENodeKind const kind = m_Index.Nodes[holder].Kind;

    return kind == ENodeKind::Array || kind == ENodeKind::InlineTable ? holder : NoIndex;
}
//---------------------------------------------------------------------------
TIndexVector TEditor::ContainerItems(std::size_t container) const
{
    Detail::TIndexNode const& holder = m_Index.Nodes[container];
    if (holder.Kind == ENodeKind::Array)
        return holder.Children;

    TIndexVector items;
    for (std::size_t const child : holder.Children)
        AddValues(child, items);

    // In the order of the text, as the dotted keys of a table may be apart from each other
    for (std::size_t i = 1; i < items.size(); ++i)
    {
        std::size_t const item = items[i];
        std::size_t j = i;
        for (; j > 0 && m_Index.Nodes[items[j - 1]].Item.Begin > m_Index.Nodes[item].Item.Begin; --j)
            items[j] = items[j - 1];

        items[j] = item;
    }

    return items;
}
//---------------------------------------------------------------------------
std::string TEditor::EditedText() const
{
    std::string text;
    std::size_t done = 0;
    for (TSplice const& splice : m_Splices)
    {
        text.append(m_Text, done, splice.Begin - done);
        text += splice.Text;
        done = splice.End;
    }

    text.append(m_Text, done, std::string::npos);

    return text;
}
//---------------------------------------------------------------------------
void TEditor::Erase(std::size_t begin, std::size_t end)
{
    m_Splices.push_back(TSplice{ begin, end, std::string() });
}
//---------------------------------------------------------------------------
void TEditor::Finish()
{
    for (TContainerPlanMap::value_type const& container : m_Containers)
        SpliceItems(container.first, container.second);

    // Last, as it moves new lines inside the lines it removes
    RemoveLines(m_RemovedLines);
    std::stable_sort(m_Splices.begin(), m_Splices.end(), SpliceBefore);
}
//---------------------------------------------------------------------------
bool TEditor::Holds(std::size_t node, TASWTomlValue const& value) const
{
    switch (m_Index.Nodes[node].Kind)
    {
        case ENodeKind::Scalar:
        case ENodeKind::Array:
        case ENodeKind::InlineTable:
            return true;

        case ENodeKind::ArrayOfTables:
            return IsArrayOfTables(value);

        case ENodeKind::ImplicitTable:
        case ENodeKind::HeaderTable:
        case ENodeKind::DottedTable:
        case ENodeKind::ElementTable:
            break;
    }

    return value.IsTable();
}
//---------------------------------------------------------------------------
std::string_view TEditor::Indentation(std::size_t line) const
{
    std::size_t const begin = m_Index.Lines[line].Text.Begin;
    std::size_t end = begin;
    while (Detail::IsBlank(m_Text[end]))
        ++end;

    return TextOf(TTextRange{ begin, end });
}
//---------------------------------------------------------------------------
void TEditor::Insert(std::size_t offset, std::string text)
{
    m_Splices.push_back(TSplice{ offset, offset, std::move(text) });
}
//---------------------------------------------------------------------------
void TEditor::InsertBlock(std::size_t offset, std::string block)
{
    if (m_Style.BlankLineBeforeTable && offset < m_Text.size())
    {
        std::size_t const line = LineAt(offset);
        if (line != NoIndex && m_Index.Lines[line].Kind != Detail::ELineKind::Blank)
            block += m_LineBreak;
    }

    InsertLines(offset, std::move(block));
}
//---------------------------------------------------------------------------
void TEditor::InsertItem(std::size_t container, std::size_t before, std::string const& item)
{
    Detail::TIndexNode const& next = m_Index.Nodes[before];
    std::size_t const start = ItemStart(before);
    std::string_view const ahead = TextOf(TTextRange{ next.Item.Begin, start });
    std::size_t const lineFeed = ahead.rfind('\n');

    if (lineFeed != std::string_view::npos)
    {
        // It starts a line: a line of its own above it and its comment lines, with its indentation and line break
        std::size_t const lineStart = next.Item.Begin + lineFeed + 1;
        std::string const lineBreak = lineFeed > 0 && ahead[lineFeed - 1] == '\r' ? "\r\n" : "\n";
        Insert(CommentHead(next.Item.Begin, lineStart),
            std::string(TextOf(TTextRange{ lineStart, start })) + item + "," + lineBreak);
        return;
    }

    // On its line, before it, with the blanks between it and the one before it; the first one's blanks are the
    // bracket's, so those after the comma after it (one blank when it is the only one)
    std::string blanks(ahead);
    Detail::TIndexNode const& holder = m_Index.Nodes[container];
    if (next.Item.Begin == holder.Value.Begin + 1)
    {
        blanks = " ";
        if (holder.Children.size() > 1)
        {
            std::size_t const second = holder.Children[1];
            blanks = TextOf(TTextRange{ m_Index.Nodes[second].Item.Begin, ItemStart(second) });
        }
    }

    Insert(start, item + "," + blanks);
}
//---------------------------------------------------------------------------
void TEditor::InsertLines(std::size_t offset, std::string lines)
{
    if (offset == m_Text.size() && !m_Text.empty() && m_Text.back() != '\n')
        lines.insert(0, m_LineBreak);

    Insert(offset, std::move(lines));
}
//---------------------------------------------------------------------------
bool TEditor::IsEmpty() const noexcept
{
    return m_Splices.empty();
}
//---------------------------------------------------------------------------
bool TEditor::IsRemoved(std::size_t node) const
{
    Detail::TIndexNode const& value = m_Index.Nodes[node];
    if (value.Line != NoIndex)
        return m_RemovedLines[value.Line];

    std::size_t const container = Container(node);
    TContainerPlanMap::const_iterator const plan = m_Containers.find(container);
    if (plan == m_Containers.end())
        return false;

    TIndexVector const& removed = plan->second.Removed;

    return std::find(removed.begin(), removed.end(), node) != removed.end();
}
//---------------------------------------------------------------------------
std::size_t TEditor::ItemStart(std::size_t item) const
{
    Detail::TIndexNode const& node = m_Index.Nodes[item];
    if (m_Index.Nodes[node.Parent].Kind == ENodeKind::Array)
        return node.Value.Begin;

    return KeyStart(m_Text, node.Item.Begin);
}
//---------------------------------------------------------------------------
std::size_t TEditor::LastKeyLine(std::size_t block) const
{
    std::size_t const begin = m_Index.BlockLines[block];
    std::size_t end = block + 1 < m_Index.BlockLines.size() ? m_Index.BlockLines[block + 1] : m_Index.Lines.size();

    for (; end > begin; --end)
    {
        if (m_Index.Lines[end - 1].Kind == Detail::ELineKind::KeyValue && !m_RemovedLines[end - 1])
            return end - 1;
    }

    return NoIndex;
}
//---------------------------------------------------------------------------
std::size_t TEditor::LastValue(std::size_t table) const
{
    std::size_t last = NoIndex;
    std::size_t lastOffset = 0;
    bool lastKept = false;

    for (std::size_t const child : m_Index.Nodes[table].Children)
    {
        std::size_t value = NoIndex;
        ENodeKind const kind = m_Index.Nodes[child].Kind;
        if (kind == ENodeKind::DottedTable)
            value = LastValue(child);
        else if (kind == ENodeKind::Scalar || kind == ENodeKind::Array || kind == ENodeKind::InlineTable)
            value = child;

        if (value == NoIndex)
            continue;

        // One that stays comes before one that goes, else the later one
        Detail::TIndexNode const& node = m_Index.Nodes[value];
        std::size_t const offset = node.Line != NoIndex ? m_Index.Lines[node.Line].Text.Begin : node.Item.Begin;
        bool const kept = !IsRemoved(value);
        if (last == NoIndex || (kept && !lastKept) || (kept == lastKept && offset > lastOffset))
        {
            last = value;
            lastOffset = offset;
            lastKept = kept;
        }
    }

    return last;
}
//---------------------------------------------------------------------------
std::size_t TEditor::LineAt(std::size_t offset) const
{
    for (std::size_t i = 0; i < m_Index.Lines.size(); ++i)
    {
        if (m_Index.Lines[i].Text.Begin != offset)
            continue;

        while (i < m_Index.Lines.size() && m_RemovedLines[i])
            ++i;

        return i < m_Index.Lines.size() ? i : NoIndex;
    }

    return NoIndex;
}
//---------------------------------------------------------------------------
std::size_t TEditor::LineBefore(std::size_t offset) const
{
    for (std::size_t i = 0; i < m_Index.Lines.size(); ++i)
    {
        if (m_Index.Lines[i].Next != offset)
            continue;

        for (; m_RemovedLines[i]; --i)
        {
            if (i == 0)
                return NoIndex;
        }

        return i;
    }

    return NoIndex;
}
//---------------------------------------------------------------------------
std::size_t TEditor::LineOf(std::size_t offset) const
{
    for (std::size_t i = 0; i < m_Index.Lines.size(); ++i)
    {
        if (offset < m_Index.Lines[i].Next || (i + 1 == m_Index.Lines.size() && offset <= m_Index.Lines[i].Next))
            return i;
    }

    return NoIndex;
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::Locate(TASWTomlPath const& path, std::size_t& node, std::size_t& missing) const
{
    TASWTomlPath::TPartVector const& parts = path.GetParts();
    node = 0;

    for (std::size_t i = 0; i < parts.size(); ++i)
    {
        Detail::TIndexNode const& holder = m_Index.Nodes[node];
        bool const isArray = holder.Kind == ENodeKind::Array || holder.Kind == ENodeKind::ArrayOfTables;
        TASWTomlPathPart const& part = parts[i];
        TASWTomlPath const walked = Prefix(path, i);

        if (part.IsIndex())
        {
            if (!isArray)
                return PathFailure(ETomlCode::InvalidArgument, path, MessagePath(walked) + " isn't an array.");

            if (part.GetIndex() >= holder.Children.size())
            {
                std::size_t const count = holder.Children.size();
                return PathFailure(ETomlCode::NotFound, path, "the array " + MessagePath(walked) + " has " +
                    std::to_string(count) + (count == 1 ? " element." : " elements."));
            }

            node = holder.Children[part.GetIndex()];
            continue;
        }

        if (isArray)
        {
            return PathFailure(ETomlCode::InvalidArgument, path, MessagePath(walked) +
                " is an array, so an index must come before a key.");
        }

        if (holder.Kind == ENodeKind::Scalar)
        {
            TASWTomlValue const* const scalar = FindValue(m_Root, walked);
            return PathFailure(ETomlCode::InvalidArgument, path, MessagePath(walked) + " is a value of type " +
                std::string(ToString(scalar->GetType())) + ", not a table.");
        }

        Detail::TNodeIndexMap::const_iterator const found = holder.Keys.find(part.GetKey());
        if (found == holder.Keys.end())
        {
            missing = i;
            return TASWTomlResult();
        }

        node = found->second;
    }

    missing = parts.size();

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::LocateLine(TASWTomlPath const& path, std::size_t& line) const
{
    std::size_t node = 0;
    std::size_t missing = 0;
    TASWTomlResult const result = Locate(path, node, missing);
    if (!result.IsOk())
        return result;

    if (missing < path.GetParts().size())
        return PathFailure(ETomlCode::NotFound, path, MessagePath(Prefix(path, missing + 1)) + " doesn't exist.");

    line = DefiningLine(m_Index, node);
    if (line != NoIndex)
        return result;

    // What has no line of its own
    if (node == 0)
        return PathFailure(ETomlCode::InvalidArgument, path, "it has no line of its own.");

    if (Container(node) != NoIndex)
    {
        return PathFailure(ETomlCode::InvalidArgument, path,
            "it is inside an array or inline table, whose comments can't be set.");
    }

    if (m_Index.Nodes[node].Kind == ENodeKind::DottedTable)
    {
        return PathFailure(ETomlCode::InvalidArgument, path,
            "it is a table that dotted keys make, which has no line of its own.");
    }

    return PathFailure(ETomlCode::InvalidArgument, path,
        "it is a table that only longer headers make, which has no line of its own.");
}
//---------------------------------------------------------------------------
void TEditor::MarkBlock(std::size_t block, TFlagVector& lines) const
{
    std::size_t end = m_Index.Lines.size();
    if (block + 1 < m_Index.BlockLines.size())
        end = CommentStart(m_Index, m_Index.BlockLines[block + 1]);

    for (std::size_t line = CommentStart(m_Index, m_Index.BlockLines[block]); line < end; ++line)
        lines[line] = true;
}
//---------------------------------------------------------------------------
void TEditor::MarkLine(std::size_t line, TFlagVector& lines) const
{
    for (std::size_t i = CommentStart(m_Index, line); i <= line; ++i)
        lines[i] = true;
}
//---------------------------------------------------------------------------
void TEditor::MarkSubtree(std::size_t node, TFlagVector& lines) const
{
    Detail::TIndexNode const& marked = m_Index.Nodes[node];
    if (marked.Block != NoIndex)
        MarkBlock(marked.Block, lines);
    else if (marked.Line != NoIndex)
        MarkLine(marked.Line, lines);

    // The values in an array or inline table are on its lines
    if (marked.Kind == ENodeKind::Scalar || marked.Kind == ENodeKind::Array || marked.Kind == ENodeKind::InlineTable)
        return;

    for (std::size_t const child : marked.Children)
        MarkSubtree(child, lines);
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::MergeArray(std::size_t array, TASWTomlValue const& old, TASWTomlValue const& value)
{
    TASWTomlValue::TValueVector const& oldElements = *old.AsArray();
    TASWTomlValue::TValueVector const& elements = *value.AsArray();
    TIndexVector const partners = Partners(oldElements, elements);
    TIndexVector const& nodes = m_Index.Nodes[array].Children;
    TContainerPlan& plan = m_Containers[array];

    // The old ones that no new one becomes go
    TFlagVector stays(oldElements.size(), false);
    for (std::size_t const partner : partners)
    {
        if (partner != NoIndex)
            stays[partner] = true;
    }

    for (std::size_t i = 0; i < oldElements.size(); ++i)
    {
        if (!stays[i])
            plan.Removed.push_back(nodes[i]);
    }

    // The others become the new ones; a new one without an old one goes before the next one that stays, or at the end
    TASWTomlResult result;
    for (std::size_t j = 0; j < elements.size(); ++j)
    {
        if (partners[j] != NoIndex)
        {
            result = MergeValue(nodes[partners[j]], oldElements[partners[j]], elements[j]);
            if (!result.IsOk())
                return result;

            continue;
        }

        std::string text;
        m_Style.MaxDepth = Room(m_Index.Nodes[array].Depth);
        result = Detail::WriteValue(elements[j], m_Style, text);
        if (!result.IsOk())
            return result;

        std::size_t next = j + 1;
        while (next < elements.size() && partners[next] == NoIndex)
            ++next;

        if (next < elements.size())
            plan.Inserted.push_back(TItemInsert{ nodes[partners[next]], std::move(text) });
        else
            plan.Appended.push_back(std::move(text));
    }

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::MergeElements(std::size_t array, TASWTomlValue const& old, TASWTomlValue const& value)
{
    TASWTomlValue::TValueVector const& oldElements = *old.AsArray();
    TASWTomlValue::TValueVector const& elements = *value.AsArray();
    TIndexVector const partners = Partners(oldElements, elements);
    TIndexVector const& nodes = m_Index.Nodes[array].Children;

    // The old ones that no new one becomes go, then the others become the new ones, so the places of new blocks are
    // worked out without what goes
    TFlagVector stays(oldElements.size(), false);
    for (std::size_t const partner : partners)
    {
        if (partner != NoIndex)
            stays[partner] = true;
    }

    for (std::size_t i = 0; i < oldElements.size(); ++i)
    {
        if (!stays[i])
            RemoveNode(nodes[i]);
    }

    TASWTomlResult result;
    for (std::size_t j = 0; j < elements.size(); ++j)
    {
        if (partners[j] == NoIndex)
            continue;

        result = MergeValue(nodes[partners[j]], oldElements[partners[j]], elements[j]);
        if (!result.IsOk())
            return result;
    }

    // Each run of new ones goes before the comment and header of the next one that stays, or after the last one and
    // its tables (which stays, as only the old ones before the last one that stays can go when new ones come after it)
    m_Style.MaxDepth = Room(m_Index.Nodes[array].Depth);
    std::size_t first = 0;
    while (first < elements.size())
    {
        if (partners[first] != NoIndex)
        {
            ++first;
            continue;
        }

        std::size_t next = first;
        while (next < elements.size() && partners[next] == NoIndex)
            ++next;

        std::size_t const neighbour = next < elements.size() ? nodes[partners[next]] : nodes.back();
        std::size_t offset = 0;
        if (next < elements.size())
            offset = m_Index.Lines[CommentStart(m_Index, m_Index.Nodes[neighbour].Line)].Text.Begin;
        else
            offset = BlockEnd(SubtreeLastBlock(neighbour));

        std::string const path = PathText(neighbour);
        std::string block;
        for (std::size_t j = first; j < next; ++j)
        {
            result = Detail::WriteTableBlocks(path, true, !block.empty() || BlankLineBefore(offset), elements[j],
                m_Style, block);
            if (!result.IsOk())
                return result;
        }

        InsertBlock(offset, std::move(block));
        first = next;
    }

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::MergeTable(std::size_t table, TASWTomlValue const& old, TASWTomlValue const& value)
{
    Detail::TIndexNode const& holder = m_Index.Nodes[table];
    TASWTomlValue::TKeyValueVector const& keys = *value.AsTable();

    // A table only dotted keys or longer headers made goes with its keys: emptied, it is written as an empty table
    if (keys.empty() && (holder.Kind == ENodeKind::DottedTable || holder.Kind == ENodeKind::ImplicitTable))
    {
        RemoveNode(table);
        TASWTomlValue added;
        added.Set(holder.Key, TASWTomlValue());

        return NewKeys(holder.Parent, added);
    }

    // The keys that go first, so the places of new keys are worked out without them
    for (std::size_t const child : holder.Children)
    {
        if (value.Find(m_Index.Nodes[child].Key) == nullptr)
            RemoveNode(child);
    }

    TASWTomlValue added;
    TASWTomlResult result;
    for (TASWTomlKeyValue const& item : keys)
    {
        Detail::TNodeIndexMap::const_iterator const found = holder.Keys.find(item.Key);
        if (found == holder.Keys.end())
        {
            added.Set(item.Key, item.Value);
            continue;
        }

        // A table that isn't inline, or an array of tables, that can't hold the new value goes, and the value is
        // written as a new key
        if (!Holds(found->second, item.Value))
        {
            RemoveNode(found->second);
            added.Set(item.Key, item.Value);
            continue;
        }

        result = MergeValue(found->second, *old.Find(item.Key), item.Value);
        if (!result.IsOk())
            return result;
    }

    if (added.AsTable()->empty())
        return result;

    return NewKeys(table, added);
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::MergeValue(std::size_t node, TASWTomlValue const& old, TASWTomlValue const& value)
{
    if (Detail::IdenticalValues(old, value))
        return TASWTomlResult();

    Detail::TIndexNode const& merged = m_Index.Nodes[node];
    switch (merged.Kind)
    {
        case ENodeKind::Scalar:
            return Replace(node, value);

        case ENodeKind::Array:
            return value.IsArray() ? MergeArray(node, old, value) : Replace(node, value);

        case ENodeKind::InlineTable:
            return value.IsTable() ? MergeTable(node, old, value) : Replace(node, value);

        case ENodeKind::ArrayOfTables:
            if (IsArrayOfTables(value))
                return MergeElements(node, old, value);

            break;

        case ENodeKind::ImplicitTable:
        case ENodeKind::HeaderTable:
        case ENodeKind::DottedTable:
        case ENodeKind::ElementTable:
            if (value.IsTable())
                return MergeTable(node, old, value);

            break;
    }

    // A table that isn't inline, or an array of tables, can't hold the value: it goes, and the value is written as a
    // new key of its table
    RemoveNode(node);
    TASWTomlValue added;
    added.Set(merged.Key, value);

    return NewKeys(merged.Parent, added);
}
//---------------------------------------------------------------------------
TTextRange TEditor::NameIn(std::size_t node, TTextRange range) const
{
    for (TTextRange const& name : m_Index.Nodes[node].Names)
    {
        if (name.Begin >= range.Begin && name.End <= range.End)
            return name;
    }

    return TTextRange{ range.Begin, range.Begin };
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::NewBlocks(std::size_t table, TASWTomlValue const& keys)
{
    Detail::TIndexNode const& holder = m_Index.Nodes[table];
    std::size_t offset = m_Text.size();
    if (table != 0)
    {
        std::size_t const last = SubtreeLastBlock(table);
        if (last != NoIndex)
            offset = BlockEnd(last);
    }

    std::string const parent = PathText(table);
    std::string block;
    TASWTomlResult result;

    for (TASWTomlKeyValue const& item : *keys.AsTable())
    {
        std::string path = parent;
        if (!path.empty())
            path += '.';

        result = Detail::WriteKey(item.Key, path);
        if (!result.IsOk())
            return result;

        if (IsArrayOfTables(item.Value))
        {
            // Each element is inside the array, one level more
            m_Style.MaxDepth = Room(holder.Depth + 1);
            for (TASWTomlValue const& element : *item.Value.AsArray())
            {
                result = Detail::WriteTableBlocks(path, true, !block.empty() || BlankLineBefore(offset), element,
                    m_Style, block);
                if (!result.IsOk())
                    return result;
            }
        }
        else
        {
            m_Style.MaxDepth = Room(holder.Depth);
            result = Detail::WriteTableBlocks(path, false, !block.empty() || BlankLineBefore(offset), item.Value,
                m_Style, block);
            if (!result.IsOk())
                return result;
        }
    }

    InsertBlock(offset, std::move(block));

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::NewKeyLines(std::size_t table, TASWTomlValue const& keys)
{
    Detail::TIndexNode const& holder = m_Index.Nodes[table];
    m_Style.MaxDepth = Room(holder.Depth);
    TASWTomlResult result;

    if (holder.Kind == ENodeKind::ImplicitTable)
    {
        // The table gets its own header with the keys, before the first header of its sub-tables and the comment
        // lines above it
        std::size_t const offset = m_Index.Lines[CommentStart(m_Index, SubtreeFirstHeader(table))].Text.Begin;
        std::string block = BlankLineBefore(offset) ? m_LineBreak : std::string();
        block += "[" + PathText(table) + "]" + m_LineBreak;
        result = WriteKeyLines("", keys, block);
        if (result.IsOk())
            InsertBlock(offset, std::move(block));

        return result;
    }

    std::size_t const block = table == 0 ? 0 : holder.Block;
    std::size_t const last = LastKeyLine(block);
    std::string lines;
    std::string indent;
    std::size_t offset = 0;

    if (last != NoIndex)
    {
        // After the table's last key line, with its indentation
        indent = Indentation(last);
        offset = m_Index.Lines[last].Next;
    }
    else if (table != 0)
    {
        // Right after the header
        offset = m_Index.Lines[holder.Line].Next;
    }
    else
    {
        // The root without keys: after the file comment (see FileCommentLines()) and the blank lines under it, or at
        // the start of the text
        std::size_t i = FileCommentLines(m_Index);
        if (i > 0 && i == m_Index.Lines.size())
        {
            // A file comment that ends the text gets the blank line under it first
            offset = m_Text.size();
            lines = m_LineBreak;
        }
        else if (i > 0)
        {
            while (i < m_Index.Lines.size() && m_Index.Lines[i].Kind == Detail::ELineKind::Blank)
                ++i;

            offset = m_Index.Lines[i - 1].Next;
        }
    }

    result = WriteKeyLines(indent, keys, lines);
    if (!result.IsOk())
        return result;

    // In the root, new key lines before a header or its comment are kept apart from it by a blank line
    if (table == 0 && last == NoIndex && m_Style.BlankLineBeforeTable)
    {
        std::size_t const next = LineAt(offset);
        if (next != NoIndex && m_Index.Lines[next].Kind != Detail::ELineKind::Blank)
            lines += m_LineBreak;
    }

    InsertLines(offset, std::move(lines));

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::NewKeys(std::size_t table, TASWTomlValue const& keys)
{
    Detail::TIndexNode const& holder = m_Index.Nodes[table];
    m_Style.MaxDepth = Room(holder.Depth);
    TASWTomlResult result;

    if (holder.Kind == ENodeKind::InlineTable)
    {
        // Entries at the end of the inline table
        TContainerPlan& plan = m_Containers[table];
        for (TASWTomlKeyValue const& item : *keys.AsTable())
        {
            std::string entry;
            result = Detail::WriteKey(item.Key, entry);
            if (result.IsOk())
            {
                entry += " = ";
                result = Detail::WriteValue(item.Value, m_Style, entry);
            }

            if (!result.IsOk())
                return result;

            plan.Appended.push_back(std::move(entry));
        }

        return result;
    }

    if (holder.Kind == ENodeKind::DottedTable)
    {
        // Dotted keys after the last one through this table, their key parts as that one's
        Detail::TIndexNode const& lastValue = m_Index.Nodes[LastValue(table)];

        if (lastValue.Line == NoIndex)
        {
            // In an inline table: new entries at the end of the inline table the dotted keys are in
            std::size_t container = holder.Parent;
            while (m_Index.Nodes[container].Kind == ENodeKind::DottedTable)
                container = m_Index.Nodes[container].Parent;

            std::size_t const keyBegin = KeyStart(m_Text, lastValue.Item.Begin);
            std::string const prefix(TextOf(TTextRange{ keyBegin, NameIn(table, lastValue.Item).End }));
            TContainerPlan& plan = m_Containers[container];
            for (TASWTomlKeyValue const& item : *keys.AsTable())
            {
                std::string entry = prefix + ".";
                result = Detail::WriteKey(item.Key, entry);
                if (result.IsOk())
                {
                    entry += " = ";
                    result = Detail::WriteValue(item.Value, m_Style, entry);
                }

                if (!result.IsOk())
                    return result;

                plan.Appended.push_back(std::move(entry));
            }

            return result;
        }

        Detail::TIndexLine const& line = m_Index.Lines[lastValue.Line];
        std::string const indent(Indentation(lastValue.Line));
        std::size_t const keyBegin = line.Text.Begin + indent.size();
        std::string const prefix(TextOf(TTextRange{ keyBegin, NameIn(table, line.Text).End }));
        std::string lines;
        for (TASWTomlKeyValue const& item : *keys.AsTable())
        {
            std::string key = prefix + ".";
            result = Detail::WriteKey(item.Key, key);
            if (!result.IsOk())
                return result;

            if (item.Value.IsTable())
                result = Detail::WriteDottedKeys(indent, key, item.Value, m_Style, lines);
            else
                result = Detail::WriteKeyValueLine(indent, key, item.Value, m_Style, lines);

            if (!result.IsOk())
                return result;
        }

        InsertLines(line.Next, std::move(lines));

        return result;
    }

    // The root, a header table, an element or an implicit table: tables and arrays of tables as headers, the others
    // as key lines
    TASWTomlValue lineKeys;
    TASWTomlValue blockKeys;
    for (TASWTomlKeyValue const& item : *keys.AsTable())
    {
        if (item.Value.IsTable() || IsArrayOfTables(item.Value))
            blockKeys.Set(item.Key, item.Value);
        else
            lineKeys.Set(item.Key, item.Value);
    }

    if (!lineKeys.AsTable()->empty())
        result = NewKeyLines(table, lineKeys);

    if (result.IsOk() && !blockKeys.AsTable()->empty())
        result = NewBlocks(table, blockKeys);

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::PathFailure(ETomlCode code, TASWTomlPath const& path, std::string const& reason) const
{
    return Failure(code, "Can't " + m_Verb + " " + MessagePath(path) + ": " + reason);
}
//---------------------------------------------------------------------------
std::string TEditor::PathText(std::size_t table) const
{
    if (table == 0)
        return std::string();

    // An element is named by its array of tables, in its own [[...]] header
    Detail::TIndexNode const& node = m_Index.Nodes[table];
    std::size_t const named = node.Kind == ENodeKind::ElementTable ? node.Parent : table;

    for (TTextRange const& name : m_Index.Nodes[named].Names)
    {
        std::size_t const line = LineOf(name.Begin);
        if (line == NoIndex || m_Index.Lines[line].Kind != Detail::ELineKind::Header)
            continue;

        if (node.Kind == ENodeKind::ElementTable && line != node.Line)
            continue;

        // From the header's first key part to the end of this one
        std::size_t begin = m_Index.Lines[line].Text.Begin;
        while (Detail::IsBlank(m_Text[begin]) || m_Text[begin] == '[')
            ++begin;

        return std::string(TextOf(TTextRange{ begin, name.End }));
    }

    return std::string();
}
//---------------------------------------------------------------------------
void TEditor::Remove(std::size_t node)
{
    RemoveNode(node);
}
//---------------------------------------------------------------------------
void TEditor::RemoveItems(std::size_t container, TIndexVector const& items)
{
    Detail::TIndexNode const& holder = m_Index.Nodes[container];
    TIndexVector const all = ContainerItems(container);
    TFlagVector removed(all.size(), false);
    for (std::size_t i = 0; i < all.size(); ++i)
        removed[i] = std::find(items.begin(), items.end(), all[i]) != items.end();

    if (items.size() == all.size())
    {
        // All of them: an empty array or inline table
        Erase(holder.Value.Begin + 1, holder.Close.End - 1);
        return;
    }

    for (std::size_t first = 0; first < all.size(); ++first)
    {
        if (!removed[first])
            continue;

        // A run of them, from 'first' to 'last'
        std::size_t last = first;
        while (last + 1 < all.size() && removed[last + 1])
            ++last;

        Detail::TIndexNode const& firstItem = m_Index.Nodes[all[first]];
        Detail::TIndexNode const& lastItem = m_Index.Nodes[all[last]];
        bool const atEnd = last + 1 == all.size();
        bool const lastComma = lastItem.Item.End > lastItem.Value.End;
        std::size_t const begin = ItemStart(all[first]);
        std::size_t const next = atEnd ? holder.Close.End - 1 : ItemStart(all[last + 1]);
        std::size_t const lineFeed = TextOf(TTextRange{ firstItem.Item.Begin, begin }).rfind('\n');
        std::size_t const nextLineFeed = TextOf(TTextRange{ lastItem.Item.End, next }).find('\n');
        std::size_t const comma = first > 0 ? m_Index.Nodes[all[first - 1]].Item.End - 1 : 0;

        if (lineFeed != std::string_view::npos)
        {
            // The run starts a line: that line goes with the comment lines directly above it, and the comma before
            // it when it ends the array or inline table without a comma of its own (which would be a trailing comma)
            std::size_t const lineStart = firstItem.Item.Begin + lineFeed + 1;
            std::size_t const head = CommentHead(firstItem.Item.Begin, lineStart);
            if (atEnd && !lastComma && first > 0)
                Erase(comma, comma + 1);

            if (nextLineFeed != std::string_view::npos)
            {
                // Through the end of the line it ends on, its comment included
                Erase(head, lastItem.Item.End + nextLineFeed + 1);
            }
            else if (atEnd)
            {
                Erase(head, lastItem.Item.End);
            }
            else
            {
                // The next one is on the same line: it keeps the indentation
                if (head < lineStart)
                    Erase(head, lineStart);

                Erase(begin, next);
            }
        }
        else if (first == 0)
        {
            // The first ones, with the comma after them and the blanks before the next one
            Erase(begin, next);
        }
        else if (atEnd)
        {
            // The last ones, with the comma before them; a trailing comma stays
            Erase(comma, lastComma ? lastItem.Item.End - 1 : lastItem.Item.End);
        }
        else
        {
            // Ones in the middle, with the blanks before them and the comma after them
            Erase(firstItem.Item.Begin, lastItem.Item.End);
        }

        first = last;
    }
}
//---------------------------------------------------------------------------
void TEditor::RemoveLines(TFlagVector const& lines)
{
    std::size_t const count = m_Index.Lines.size();
    std::size_t kept = 0; // The lines before this one are removed or kept already

    for (std::size_t first = 0; first < count; ++first)
    {
        if (!lines[first])
            continue;

        // A run of them, from 'first' to before 'end'
        std::size_t end = first;
        while (end < count && lines[end])
            ++end;

        // New lines inside the run go at its start, which gives the same text; with new lines at either end, the
        // blank lines next to it and the end of the text stay as they are
        std::size_t const runBegin = m_Index.Lines[first].Text.Begin;
        std::size_t const runEnd = end < count ? m_Index.Lines[end].Text.Begin : m_Text.size();
        bool touched = false;
        for (TSplice& splice : m_Splices)
        {
            if (splice.Begin != splice.End || splice.Begin < runBegin || splice.Begin > runEnd)
                continue;

            if (splice.Begin < runEnd)
                splice.Begin = splice.End = runBegin;

            touched = true;
        }

        // When a blank line would be left on each side (or one before the end of the text), the one before goes too;
        // at the start of the text, the blank line after
        std::size_t begin = first;
        bool const blankAfter = end < count && m_Index.Lines[end].Kind == Detail::ELineKind::Blank;
        bool const blankBefore = begin > kept && m_Index.Lines[begin - 1].Kind == Detail::ELineKind::Blank;
        if (!touched && blankBefore && (blankAfter || end == count))
            --begin;
        else if (!touched && begin == 0 && blankAfter)
            ++end;

        std::size_t from = m_Index.Lines[begin].Text.Begin;
        std::size_t const to = end < count ? m_Index.Lines[end].Text.Begin : m_Text.size();

        // A text that doesn't end with a line break keeps that: the line before loses its line break instead
        if (!touched && to == m_Text.size() && !m_Text.empty() && m_Text.back() != '\n' && begin > kept)
            from = m_Index.Lines[begin - 1].Text.End;

        Erase(from, to);
        kept = end;
        first = end - 1;
    }
}
//---------------------------------------------------------------------------
void TEditor::RemoveNode(std::size_t node)
{
    std::size_t const container = Container(node);
    if (container != NoIndex)
        AddValues(node, m_Containers[container].Removed);
    else
        MarkSubtree(node, m_RemovedLines);
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::Rename(TASWTomlPath const& path, std::string_view newKey)
{
    m_Verb = "rename";
    if (!path.IsValid())
        return Failure(ETomlCode::InvalidArgument, path.GetError());

    if (path.IsRoot())
        return PathFailure(ETomlCode::InvalidArgument, path, "it has no name.");

    TASWTomlPath::TPartVector const& parts = path.GetParts();
    if (parts.back().IsIndex())
        return PathFailure(ETomlCode::InvalidArgument, path, "an element of an array has no name.");

    std::size_t node = 0;
    std::size_t missing = 0;
    TASWTomlResult result = Locate(path, node, missing);
    if (!result.IsOk())
        return result;

    if (missing < parts.size())
        return PathFailure(ETomlCode::NotFound, path, MessagePath(Prefix(path, missing + 1)) + " doesn't exist.");

    Detail::TIndexNode const& renamed = m_Index.Nodes[node];
    if (renamed.Key == newKey)
        return result;

    std::string bare;
    result = Detail::WriteKey(newKey, bare);
    if (!result.IsOk())
        return result;

    if (m_Index.Nodes[renamed.Parent].Keys.find(newKey) != m_Index.Nodes[renamed.Parent].Keys.end())
    {
        return PathFailure(ETomlCode::AlreadyExists, path, MessagePath(Prefix(path, parts.size() - 1)) +
            " already has a key " + TASWTomlPath({ TASWTomlPathPart(newKey) }).ToString() + ".");
    }

    // A quoted part that was literal stays literal when it can
    std::string literal;
    if (bare.front() == '"')
        result = Detail::WriteString(newKey, literal, EStringStyle::Literal);

    for (TTextRange const& name : renamed.Names)
    {
        bool const wasLiteral = m_Text[name.Begin] == '\'';
        m_Splices.push_back(TSplice{ name.Begin, name.End, wasLiteral && !literal.empty() ? literal : bare });
    }

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::Replace(std::size_t node, TASWTomlValue const& value)
{
    // A scalar in the old one's form where it can
    Detail::TIndexNode const& replaced = m_Index.Nodes[node];
    m_Style.MaxDepth = Room(replaced.Depth - 1);
    std::string text;
    TASWTomlResult result;
    if (replaced.Kind == ENodeKind::Scalar)
        result = Detail::WriteValueLike(value, TextOf(replaced.Value), m_Style, text);
    else
        result = Detail::WriteValue(value, m_Style, text);

    if (result.IsOk())
        m_Splices.push_back(TSplice{ replaced.Value.Begin, replaced.Value.End, std::move(text) });

    return result;
}
//---------------------------------------------------------------------------
std::size_t TEditor::Room(std::size_t depth) const noexcept
{
    return depth < m_MaxDepth ? m_MaxDepth - depth : 0;
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::SetComment(TASWTomlPath const& path, std::string_view text)
{
    m_Verb = "set the comment of";
    if (!path.IsValid())
        return Failure(ETomlCode::InvalidArgument, path.GetError());

    // The comment lines there: the file comment's, or those above the line that defines the node at 'path'
    std::size_t line = 0;
    std::size_t begin = 0;
    std::size_t end = 0;
    TASWTomlResult result;
    if (path.IsRoot())
    {
        end = FileCommentLines(m_Index);
    }
    else
    {
        result = LocateLine(path, line);
        if (!result.IsOk())
            return result;

        begin = CommentStart(m_Index, line);
        end = line;
    }

    TStringVector const lines = CommentLines(text);
    for (std::string const& comment : lines)
    {
        result = CheckCommentText(path, comment);
        if (!result.IsOk())
            return result;
    }

    // A text that reads as the comment there changes nothing, however its lines are written
    bool same = lines.size() == end - begin;
    for (std::size_t i = 0; same && i < lines.size(); ++i)
        same = lines[i] == CommentText(TextOf(m_Index.Lines[begin + i].Text));

    if (same)
        return result;

    // Each line with the indentation of the line it is above
    std::string const indent(path.IsRoot() ? std::string_view() : Indentation(line));
    std::string written;
    for (std::string const& comment : lines)
    {
        written += indent;
        written += comment.empty() ? std::string("#") : m_CommentPrefix + comment;
        written += m_LineBreak;
    }

    std::size_t const count = m_Index.Lines.size();
    std::size_t const from = begin < count ? m_Index.Lines[begin].Text.Begin : m_Text.size();
    std::size_t to = end < count ? m_Index.Lines[end].Text.Begin : m_Text.size();

    // The file comment has a blank line under it: a new one gets one unless one follows already, and a removed one
    // takes it along, unless the comment lines after it would then be the file comment
    bool const blankFollows = end < count && m_Index.Lines[end].Kind == Detail::ELineKind::Blank;
    if (path.IsRoot() && lines.empty() && blankFollows)
    {
        std::size_t next = end + 1;
        while (next < count && m_Index.Lines[next].Kind == Detail::ELineKind::Comment)
            ++next;

        bool const nextIsFileComment = next > end + 1 &&
            (next == count || m_Index.Lines[next].Kind == Detail::ELineKind::Blank);
        if (!nextIsFileComment)
            to = m_Index.Lines[end].Next;
    }
    else if (path.IsRoot() && begin == end && !blankFollows)
    {
        written += m_LineBreak;
    }

    // A text that doesn't end with a line break keeps that
    if (to == m_Text.size() && !written.empty() && !m_Text.empty() && m_Text.back() != '\n')
        written.resize(written.size() - m_LineBreak.size());

    m_Splices.push_back(TSplice{ from, to, std::move(written) });

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::SetExisting(std::size_t node, TASWTomlPath const& path, TASWTomlValue const& value)
{
    TASWTomlValue const& old = *FindValue(m_Root, path);
    Detail::TIndexNode const& existing = m_Index.Nodes[node];
    if (existing.Kind != ENodeKind::ElementTable || value.IsTable())
        return MergeValue(node, old, value);

    // An array of tables holds only tables: the array is set to the one with the new element instead, which is
    // written as an array value
    TASWTomlPath const arrayPath = Prefix(path, path.GetParts().size() - 1);
    TASWTomlValue const& oldArray = *FindValue(m_Root, arrayPath);
    TASWTomlValue array = oldArray;
    (*array.AsArray())[path.GetParts().back().GetIndex()] = value;

    return MergeValue(existing.Parent, oldArray, array);
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::SetTrailingComment(TASWTomlPath const& path, std::string_view text)
{
    m_Verb = "set the trailing comment of";
    if (!path.IsValid())
        return Failure(ETomlCode::InvalidArgument, path.GetError());

    std::size_t line = 0;
    TASWTomlResult result = LocateLine(path, line);
    if (result.IsOk())
        result = CheckCommentText(path, text);

    if (!result.IsOk())
        return result;

    // A text that reads as the comment there changes nothing
    std::string_view const comment = Trimmed(text);
    TTextRange const trailing = m_Index.Lines[line].Trailing;
    std::size_t const hash = TextOf(trailing).find('#');
    bool const hasComment = hash != std::string_view::npos;
    if ((!hasComment && comment.empty()) || (hasComment && comment == CommentText(TextOf(trailing))))
        return result;

    // None removes the comment with the blanks before it; else it replaces the one there, after the blanks before it,
    // or goes at the end of the line after two spaces
    if (comment.empty())
        Erase(trailing.Begin, trailing.End);
    else if (hasComment)
        m_Splices.push_back(TSplice{ trailing.Begin + hash, trailing.End, m_CommentPrefix + std::string(comment) });
    else
        m_Splices.push_back(TSplice{ trailing.Begin, trailing.End, "  " + m_CommentPrefix + std::string(comment) });

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::SetValue(TASWTomlPath const& path, TASWTomlValue const& value)
{
    m_Verb = "set";
    if (!path.IsValid())
        return Failure(ETomlCode::InvalidArgument, path.GetError());

    return Walk(path, value);
}
//---------------------------------------------------------------------------
void TEditor::SpliceItems(std::size_t container, TContainerPlan const& plan)
{
    TIndexVector const items = ContainerItems(container);
    TFlagVector removed(items.size(), false);
    std::size_t lastKept = NoIndex; // In 'items'
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        removed[i] = std::find(plan.Removed.begin(), plan.Removed.end(), items[i]) != plan.Removed.end();
        if (!removed[i])
            lastKept = i;
    }

    // New ones at the end take the places of the removed ones after the last one that stays, so the layout stays
    std::size_t used = 0;
    for (std::size_t i = lastKept == NoIndex ? 0 : lastKept + 1; i < items.size() && used < plan.Appended.size(); ++i)
    {
        m_Splices.push_back(TSplice{ ItemStart(items[i]), m_Index.Nodes[items[i]].Value.End, plan.Appended[used] });
        removed[i] = false;
        lastKept = i;
        ++used;
    }

    TIndexVector gone;
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (removed[i])
            gone.push_back(items[i]);
    }

    if (!gone.empty())
        RemoveItems(container, gone);

    if (used < plan.Appended.size())
    {
        TStringVector const rest(plan.Appended.begin() + static_cast<std::ptrdiff_t>(used), plan.Appended.end());
        AppendItems(container, lastKept == NoIndex ? NoIndex : items[lastKept], rest);
    }

    for (TItemInsert const& insert : plan.Inserted)
        InsertItem(container, insert.Before, insert.Text);
}
//---------------------------------------------------------------------------
std::size_t TEditor::SubtreeFirstHeader(std::size_t table) const
{
    Detail::TIndexNode const& node = m_Index.Nodes[table];
    std::size_t first = node.Block != NoIndex ? node.Line : NoIndex;

    for (std::size_t const child : node.Children)
    {
        ENodeKind const kind = m_Index.Nodes[child].Kind;
        if (kind == ENodeKind::Scalar || kind == ENodeKind::Array || kind == ENodeKind::InlineTable)
            continue;

        std::size_t const line = SubtreeFirstHeader(child);
        if (line != NoIndex && (first == NoIndex || line < first))
            first = line;
    }

    return first;
}
//---------------------------------------------------------------------------
std::size_t TEditor::SubtreeLastBlock(std::size_t table) const
{
    Detail::TIndexNode const& node = m_Index.Nodes[table];
    std::size_t last = node.Block;

    for (std::size_t const child : node.Children)
    {
        ENodeKind const kind = m_Index.Nodes[child].Kind;
        if (kind == ENodeKind::Scalar || kind == ENodeKind::Array || kind == ENodeKind::InlineTable)
            continue;

        std::size_t const block = SubtreeLastBlock(child);
        if (block != NoIndex && (last == NoIndex || block > last))
            last = block;
    }

    return last;
}
//---------------------------------------------------------------------------
std::string_view TEditor::TextOf(TTextRange range) const
{
    return std::string_view(m_Text).substr(range.Begin, range.End - range.Begin);
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::Walk(TASWTomlPath const& path, TASWTomlValue const& value)
{
    TASWTomlPath::TPartVector const& parts = path.GetParts();
    if (parts.empty() && !value.IsTable())
        return PathFailure(ETomlCode::InvalidArgument, path, "it can only be set to a table.");

    // Through the parts that exist, to the first missing key, if any
    std::size_t node = 0;
    std::size_t missing = 0;
    TASWTomlResult const result = Locate(path, node, missing);
    if (!result.IsOk())
        return result;

    if (missing == parts.size())
        return SetExisting(node, path, value);

    for (std::size_t i = missing + 1; i < parts.size(); ++i)
    {
        if (parts[i].IsIndex())
        {
            return PathFailure(ETomlCode::NotFound, path, MessagePath(Prefix(path, missing + 1)) +
                " doesn't exist, so it has no elements.");
        }
    }

    TASWTomlValue keys;
    keys.Set(parts[missing].GetKey(), Wrapped(parts, missing + 1, value));

    return NewKeys(node, keys);
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::WriteKeyLines(std::string_view indent, TASWTomlValue const& keys, std::string& text) const
{
    TASWTomlResult result;
    for (TASWTomlKeyValue const& item : *keys.AsTable())
    {
        std::string key;
        result = Detail::WriteKey(item.Key, key);
        if (result.IsOk())
            result = Detail::WriteKeyValueLine(indent, key, item.Value, m_Style, text);

        if (!result.IsOk())
            return result;
    }

    return result;
}
//---------------------------------------------------------------------------

} // namespace


/////////////////////////////////////////////////////////////////////////////
// TASWTomlDocument
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlDocument::TASWTomlDocument()
    : m_Index(EmptyIndex())
{
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlDocument::Append(TASWTomlPath const& path, TASWTomlValue const& value)
{
    return Append(path, value, m_Options.Values);
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlDocument::Append(TASWTomlPath const& path, TASWTomlValue const& value,
    TASWTomlValueOptions const& values)
{
    TASWTomlResult result = values.Validate();
    if (!result.IsOk())
        return result;

    TASWTomlValue const* const old = FindValue(m_Root, path);
    std::size_t const oldSize = old != nullptr && old->IsArray() ? old->AsArray()->size() : 0;

    std::string const body = BodyText();
    TEditor editor(body, m_Index, m_Root, m_Options, values, GetLineBreak());
    result = editor.Append(path, value);
    if (!result.IsOk())
        return result;

    editor.Finish();

    Detail::TDocModel model;
    Detail::TDocIndex index;
    TASWTomlValue root;
    result = ReadEdited(editor.EditedText(), model, index, root);
    if (!result.IsOk())
        return result;

    // The array must have the value at its end now, and one element more
    TASWTomlValue const* const written = FindValue(root, path);
    TASWTomlValue::TValueVector const* const array = written != nullptr ? written->AsArray() : nullptr;
    if (array == nullptr || array->size() != oldSize + 1 || !Detail::IdenticalValues(array->back(), value))
    {
        return Failure(ETomlCode::InvalidValue, "Can't append to " + MessagePath(path) +
            ": the text written for the value doesn't read back as it.");
    }

    Commit(model, index, root);

    return result;
}
//---------------------------------------------------------------------------
std::string TASWTomlDocument::BodyText() const
{
    std::string text;
    for (Detail::TDocBlock const& block : m_Model.Blocks)
    {
        if (block.HasHeader)
        {
            Detail::TDocHeader const& header = block.Header;
            text += header.Indent;
            text += header.IsArray ? "[[" : "[";
            AppendKey(header.Key, text);
            text += header.IsArray ? "]]" : "]";
            text += header.Text;
            text += Detail::LineBreakText(header.LineBreak);
        }

        for (Detail::TDocEntry const& entry : block.Entries)
        {
            if (entry.Kind == Detail::EEntryKind::KeyValue)
                AppendKeyValue(entry.KeyValue, text);

            text += entry.Text;
            text += Detail::LineBreakText(entry.LineBreak);
        }
    }

    return text;
}
//---------------------------------------------------------------------------
void TASWTomlDocument::ClearModified() noexcept
{
    m_Modified = false;
}
//---------------------------------------------------------------------------
void TASWTomlDocument::Commit(Detail::TDocModel& model, Detail::TDocIndex& index, TASWTomlValue& root) noexcept
{
    m_Model = std::move(model);
    m_Index = std::move(index);
    m_Root = std::move(root);
    m_Modified = true;
}
//---------------------------------------------------------------------------
TASWTomlValue const* TASWTomlDocument::Find(TASWTomlPath const& path) const noexcept
{
    return FindValue(m_Root, path);
}
//---------------------------------------------------------------------------
std::string TASWTomlDocument::GetComment(TASWTomlPath const& path) const
{
    return CommentOf(m_Model, m_Index, path);
}
//---------------------------------------------------------------------------
ELineBreak TASWTomlDocument::GetLineBreak() const noexcept
{
    if (m_Model.LineBreak == ELineBreak::None)
        return Detail::NativeLineBreak();

    return m_Model.LineBreak;
}
//---------------------------------------------------------------------------
TASWTomlOptions const& TASWTomlDocument::GetOptions() const noexcept
{
    return m_Options;
}
//---------------------------------------------------------------------------
TASWTomlValue const& TASWTomlDocument::GetRoot() const noexcept
{
    return m_Root;
}
//---------------------------------------------------------------------------
std::string TASWTomlDocument::GetTrailingComment(TASWTomlPath const& path) const
{
    return TrailingCommentOf(m_Model, m_Index, path);
}
//---------------------------------------------------------------------------
bool TASWTomlDocument::HasBom() const noexcept
{
    return m_Model.HasBom;
}
//---------------------------------------------------------------------------
bool TASWTomlDocument::IsModified() const noexcept
{
    return m_Modified;
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlDocument::Parse(std::string_view text, TASWTomlOptions const& options)
{
    TASWTomlResult result = options.Validate();
    if (!result.IsOk())
        return result;

    // The options are copied and the text read into a new model, index and root first, so nothing changes on a
    // failure; moving them in can't fail
    TASWTomlOptions newOptions = options;
    Detail::TDocModel model;
    Detail::TDocIndex index;
    TASWTomlValue root;

    result = ReadText(text, options, model, index, root);
    if (!result.IsOk())
        return result;

    m_Model = std::move(model);
    m_Index = std::move(index);
    m_Options = std::move(newOptions);
    m_Root = std::move(root);
    m_Modified = false;

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlDocument::ReadEdited(std::string const& body, Detail::TDocModel& model,
    Detail::TDocIndex& index, TASWTomlValue& root) const
{
    std::string text = m_Model.HasBom ? std::string(Utf8Bom) : std::string();
    text += body;

    TASWTomlResult result = ReadText(text, m_Options, model, index, root);
    if (!result.IsOk())
    {
        result.Message = "The edit can't be made: " + result.Message;
        result.Line = 0;
        result.Column = 0;
    }

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlDocument::ReadText(std::string_view text, TASWTomlOptions const& options,
    Detail::TDocModel& model, Detail::TDocIndex& index, TASWTomlValue& root)
{
    Detail::TDocModel newModel;
    TASWTomlResult result = Detail::ParseDocument(text, options.Reading, newModel);
    if (!result.IsOk())
        return result;

    // The offsets of the index and its errors count from after the byte order mark, as the parser's do
    std::string_view const body = text.substr(newModel.HasBom ? Utf8Bom.size() : 0);
    result = Detail::BuildIndex(newModel, body, options.Reading.MaxNestingDepth, index, root);
    if (result.IsOk())
        model = std::move(newModel);

    return result;
}
//---------------------------------------------------------------------------
bool TASWTomlDocument::Remove(TASWTomlPath const& path)
{
    std::size_t const node = Detail::FindNode(m_Index, path);
    if (node == NoIndex || node == 0)
        return false;

    std::string const body = BodyText();
    TEditor editor(body, m_Index, m_Root, m_Options, m_Options.Values, GetLineBreak());
    editor.Remove(node);
    editor.Finish();

    Detail::TDocModel model;
    Detail::TDocIndex index;
    TASWTomlValue root;
    if (!ReadEdited(editor.EditedText(), model, index, root).IsOk())
        return false;

    // The text must read as the values without the one at 'path'
    TASWTomlValue expected = m_Root;
    RemoveValue(expected, m_Index, path);
    if (!Detail::IdenticalValues(root, expected))
        return false;

    Commit(model, index, root);

    return true;
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlDocument::Rename(TASWTomlPath const& path, std::string_view newKey)
{
    std::string const body = BodyText();
    TEditor editor(body, m_Index, m_Root, m_Options, m_Options.Values, GetLineBreak());
    TASWTomlResult result = editor.Rename(path, newKey);
    if (result.IsOk())
        editor.Finish();

    if (!result.IsOk() || editor.IsEmpty())
        return result;

    Detail::TDocModel model;
    Detail::TDocIndex index;
    TASWTomlValue root;
    result = ReadEdited(editor.EditedText(), model, index, root);
    if (!result.IsOk())
        return result;

    // The text must read as the values with the key under its new name
    TASWTomlValue expected = m_Root;
    std::string const& oldKey = path.GetParts().back().GetKey();
    for (TASWTomlKeyValue& item : *FindValue(expected, Prefix(path, path.GetParts().size() - 1))->AsTable())
    {
        if (item.Key == oldKey)
            item.Key = newKey;
    }

    if (!Detail::IdenticalValues(root, expected))
    {
        return Failure(ETomlCode::InvalidValue, "Can't rename " + MessagePath(path) +
            ": the text written for the new name doesn't read back as it.");
    }

    Commit(model, index, root);

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlDocument::Reset(TASWTomlOptions const& options)
{
    TASWTomlResult result = options.Validate();
    if (!result.IsOk())
        return result;

    // Made first, as they allocate; moving them in can't fail
    TASWTomlOptions newOptions = options;
    Detail::TDocModel model;
    Detail::TDocIndex index = EmptyIndex();

    m_Model = std::move(model);
    m_Index = std::move(index);
    m_Options = std::move(newOptions);
    m_Root = TASWTomlValue();
    m_Modified = false;

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlDocument::SetComment(TASWTomlPath const& path, std::string_view text)
{
    std::string const body = BodyText();
    TEditor editor(body, m_Index, m_Root, m_Options, m_Options.Values, GetLineBreak());
    TASWTomlResult result = editor.SetComment(path, text);
    if (result.IsOk())
        editor.Finish();

    if (!result.IsOk() || editor.IsEmpty())
        return result;

    Detail::TDocModel model;
    Detail::TDocIndex index;
    TASWTomlValue root;
    result = ReadEdited(editor.EditedText(), model, index, root);
    if (!result.IsOk())
        return result;

    // The text must read as the same values, with the comment at 'path'
    if (!Detail::IdenticalValues(root, m_Root) || CommentOf(model, index, path) != Joined(CommentLines(text)))
    {
        return Failure(ETomlCode::InvalidValue, "Can't set the comment of " + MessagePath(path) +
            ": the text written for it doesn't read back as it.");
    }

    Commit(model, index, root);

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlDocument::SetOptions(TASWTomlOptions const& options)
{
    TASWTomlResult result = options.Validate();
    if (!result.IsOk())
        return result;

    // Copied first, as 'options' may be m_Options
    TASWTomlOptions newOptions = options;
    std::string text = m_Model.HasBom ? std::string(Utf8Bom) : std::string();
    text += BodyText();

    Detail::TDocModel model;
    Detail::TDocIndex index;
    TASWTomlValue root;
    result = ReadText(text, newOptions, model, index, root);
    if (!result.IsOk())
        return result;

    m_Model = std::move(model);
    m_Index = std::move(index);
    m_Options = std::move(newOptions);
    m_Root = std::move(root);

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlDocument::SetTrailingComment(TASWTomlPath const& path, std::string_view text)
{
    std::string const body = BodyText();
    TEditor editor(body, m_Index, m_Root, m_Options, m_Options.Values, GetLineBreak());
    TASWTomlResult result = editor.SetTrailingComment(path, text);
    if (result.IsOk())
        editor.Finish();

    if (!result.IsOk() || editor.IsEmpty())
        return result;

    Detail::TDocModel model;
    Detail::TDocIndex index;
    TASWTomlValue root;
    result = ReadEdited(editor.EditedText(), model, index, root);
    if (!result.IsOk())
        return result;

    // The text must read as the same values, with the comment at the end of the line of 'path'
    if (!Detail::IdenticalValues(root, m_Root) || TrailingCommentOf(model, index, path) != Trimmed(text))
    {
        return Failure(ETomlCode::InvalidValue, "Can't set the trailing comment of " + MessagePath(path) +
            ": the text written for it doesn't read back as it.");
    }

    Commit(model, index, root);

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlDocument::SetValue(TASWTomlPath const& path, TASWTomlValue const& value)
{
    return SetValue(path, value, m_Options.Values);
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlDocument::SetValue(TASWTomlPath const& path, TASWTomlValue const& value,
    TASWTomlValueOptions const& values)
{
    TASWTomlResult result = values.Validate();
    if (!result.IsOk())
        return result;

    std::string const body = BodyText();
    TEditor editor(body, m_Index, m_Root, m_Options, values, GetLineBreak());
    result = editor.SetValue(path, value);
    if (result.IsOk())
        editor.Finish();

    if (!result.IsOk() || editor.IsEmpty())
        return result;

    std::string const edited = editor.EditedText();
    if (edited == body)
        return result;

    Detail::TDocModel model;
    Detail::TDocIndex index;
    TASWTomlValue root;
    result = ReadEdited(edited, model, index, root);
    if (!result.IsOk())
        return result;

    // What was written must read back as the value
    TASWTomlValue const* const written = FindValue(root, path);
    if (written == nullptr || !Detail::IdenticalValues(*written, value))
    {
        return Failure(ETomlCode::InvalidValue, "Can't set " + MessagePath(path) +
            ": the text written for the value doesn't read back as it.");
    }

    Commit(model, index, root);

    return result;
}
//---------------------------------------------------------------------------
std::string TASWTomlDocument::ToText() const
{
    TASWTomlWritingOptions const& writing = m_Options.Writing;

    std::string text;
    if (writing.Bom == EBomMode::Always || (writing.Bom == EBomMode::Auto && m_Model.HasBom))
        text += Utf8Bom;

    text += BodyText();

    switch (writing.LineEnding)
    {
        case ELineEnding::Auto:
            break;

        case ELineEnding::Native:
            text = ConvertLineBreaks(text, Detail::NativeLineBreak());
            break;

        case ELineEnding::LF:
            text = ConvertLineBreaks(text, ELineBreak::LF);
            break;

        case ELineEnding::CRLF:
            text = ConvertLineBreaks(text, ELineBreak::CRLF);
            break;
    }

    return text;
}
//---------------------------------------------------------------------------

} // namespace ASWToml
