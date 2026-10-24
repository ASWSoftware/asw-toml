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
#include <cstddef>
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

using Detail::ENodeKind;
using Detail::NoIndex;
using Detail::TTextRange;


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

// The changes of an edit, in the order of their places in the text
using TSpliceVector = std::vector<TSplice>;


/////////////////////////////////////////////////////////////////////////////
// TEditor
//
// Works out how an edit changes a document's text, from its index: where new text goes and what it is, in the forms
// TASWTomlDocument::SetValue() and Append() describe. It doesn't change the text: the document makes the changes and
// reads the text again.
/////////////////////////////////////////////////////////////////////////////
class TEditor
{
private:
    std::string const& m_Text; // The document's text without a byte order mark
    Detail::TDocIndex const& m_Index; // Its index
    TASWTomlValue const& m_Root; // Its values
    std::size_t m_MaxDepth; // Reading.MaxNestingDepth
    Detail::TWriteStyle m_Style; // How new text is written; MaxDepth is set for each piece
    std::string m_LineBreak; // The text of the line break new lines get
    std::string m_Verb; // What the edit does to a path, for messages: "set" or "append to"
    TSpliceVector m_Splices;

    // Adds 'item' (a value, or a "key = value" entry) at the end of the array or inline table 'container', after its
    // last element or entry 'last' (NoIndex when it has none), in their layout: on the same line after the last one,
    // or, when they are one per line, on a line of its own after the last one's line
    void AppendItem(std::size_t container, std::size_t last, std::string const& item);

    // Adds 'value', a table, to the array of tables 'array' as a new element: a [[...]] header after the last element
    // and its sub-tables
    TASWTomlResult AppendTable(std::size_t array, TASWTomlValue const& value);

    // Whether a new header at 'offset' gets a blank line before it: when Writing.BlankLineBeforeNewTable is on and
    // the line before 'offset' isn't blank (none is at the start of the text)
    bool BlankLineBefore(std::size_t offset) const;

    // The offset after the last key line or header of 'block', before the blank and comment lines that end it; 0 for
    // an empty block
    std::size_t BlockEnd(std::size_t block) const;

    // The first of the comment lines directly above 'line'; 'line' itself when there are none
    std::size_t CommentStart(std::size_t line) const;

    // Adds a change of the text: 'text' inserted at 'offset'
    void Insert(std::size_t offset, std::string text);

    // Adds 'block', headers and their lines (with the blank line before them that BlankLineBefore() asks for), at
    // 'offset', with a blank line after it when it goes before a line that isn't blank and
    // Writing.BlankLineBeforeNewTable is on
    void InsertBlock(std::size_t offset, std::string block);

    // Adds 'lines', which end with a line break, at 'offset', the start of a line or the end of the text (where a
    // line break comes first when the last line has none)
    void InsertLines(std::size_t offset, std::string lines);

    // The last key line of 'block'; NoIndex when it has none
    std::size_t LastKeyLine(std::size_t block) const;

    // The last value (a scalar, array or inline table) whose key reaches 'table' through dotted keys only: the last
    // key line under a dotted-key table, or the last entry of an inline table; NoIndex when there is none
    std::size_t LastValue(std::size_t table) const;

    // The line that starts at 'offset'; NoIndex when none does
    std::size_t LineAt(std::size_t offset) const;

    // The line that ends at 'offset', its line break included; NoIndex when none does
    std::size_t LineBefore(std::size_t offset) const;

    // The line the byte at 'offset' is on; NoIndex when it is past the end
    std::size_t LineOf(std::size_t offset) const;

    // The name of 'node' (one of its Names) inside 'range'; an empty range when it has none there
    TTextRange NameIn(std::size_t node, TTextRange range) const;

    // Adds 'value', a table or an array of tables, as the table or array of tables 'path' (its key parts as written)
    // under 'table' (the root, a header table, an element or an implicit table): its headers after the last block of
    // 'table' and of the tables under it, or at the end of the text for the root
    TASWTomlResult NewBlocks(std::size_t table, std::string const& path, TASWTomlValue const& value);

    // Adds 'key' (unquoted), which 'table' doesn't have, with 'value', in the form and place 'table' allows
    TASWTomlResult NewKey(std::size_t table, std::string const& key, TASWTomlValue const& value);

    // Adds the key/value line of 'key' (as written) and 'value' to 'table' (the root, a header table, an element or
    // an implicit table, which then gets a header of its own)
    TASWTomlResult NewKeyLine(std::size_t table, std::string const& key, TASWTomlValue const& value);

    // The failure 'code' of the edit of 'path', because of 'reason'
    TASWTomlResult PathFailure(ETomlCode code, TASWTomlPath const& path, std::string const& reason) const;

    // The key parts, as written, of a header that names 'table' (a header table, an element or an implicit table),
    // e.g. a."b c"; empty for the root
    std::string PathText(std::size_t table) const;

    // How many levels of tables and arrays may be nested in a value inside 'depth' levels
    std::size_t Room(std::size_t depth) const noexcept;

    // Sets 'node', which exists at 'path', to 'value'
    TASWTomlResult SetExisting(std::size_t node, TASWTomlPath const& path, TASWTomlValue const& value);

    // The first header line of 'table' or of a table under it; NoIndex when there is none
    std::size_t SubtreeFirstHeader(std::size_t table) const;

    // The last block of 'table' or of a table under it; NoIndex when there is none
    std::size_t SubtreeLastBlock(std::size_t table) const;

    // The text in 'range'
    std::string_view TextOf(TTextRange range) const;

    // Sets the value at 'path' to 'value', as SetValue() does
    TASWTomlResult Walk(TASWTomlPath const& path, TASWTomlValue const& value);

public:
    // An editor of 'text' (the document's text without a byte order mark), whose index is 'index' and values 'root',
    // with the document's 'options', the value options 'values' and the line break 'lineBreak' for new lines
    TEditor(std::string const& text, Detail::TDocIndex const& index, TASWTomlValue const& root,
        TASWTomlOptions const& options, TASWTomlValueOptions const& values, ELineBreak lineBreak);

    // The changes of TASWTomlDocument::Append()
    TASWTomlResult Append(TASWTomlPath const& path, TASWTomlValue const& value);

    // The text with the changes made
    std::string EditedText() const;

    // Whether the edit changes nothing
    bool IsEmpty() const noexcept;

    // The changes of TASWTomlDocument::SetValue()
    TASWTomlResult SetValue(TASWTomlPath const& path, TASWTomlValue const& value);
};


void AppendKey(Detail::TDocKey const& key, std::string& text);
void AppendKeyValue(Detail::TDocKeyValue const& keyValue, std::string& text);
void AppendValue(Detail::TDocValue const& value, std::string& text);
std::string ConvertLineBreaks(std::string const& text, ELineBreak lineBreak);
Detail::TDocIndex EmptyIndex();
TASWTomlResult Failure(ETomlCode code, std::string message);
TASWTomlValue const* FindValue(TASWTomlValue const& root, TASWTomlPath const& path) noexcept;
bool IsArrayOfTables(TASWTomlValue const& value) noexcept;
std::size_t KeyStart(std::string_view text, std::size_t offset) noexcept;
std::string Layout(std::string_view before);
std::string MessagePath(TASWTomlPath const& path);
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
      m_LineBreak(Detail::LineBreakText(lineBreak))
{
    m_Style.Values = values;
    m_Style.LineBreak = lineBreak;
    m_Style.BlankLineBeforeTable = options.Writing.BlankLineBeforeNewTable;
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
        AppendItem(node, array.Children.empty() ? NoIndex : array.Children.back(), item);

    return result;
}
//---------------------------------------------------------------------------
void TEditor::AppendItem(std::size_t container, std::size_t last, std::string const& item)
{
    Detail::TIndexNode const& holder = m_Index.Nodes[container];
    bool const isArray = holder.Kind == ENodeKind::Array;

    if (last == NoIndex)
    {
        // An empty one: the item replaces the blanks inside the brackets, or goes first before what else is there
        TTextRange const inside{ holder.Close.Begin, holder.Close.End - 1 };
        bool blanksOnly = true;
        for (char const c : TextOf(inside))
        {
            if (!Detail::IsBlank(c))
                blanksOnly = false;
        }

        if (blanksOnly)
            m_Splices.push_back(TSplice{ inside.Begin, inside.End, isArray ? item : " " + item + " " });
        else
            Insert(inside.Begin, isArray ? item : " " + item);

        return;
    }

    Detail::TIndexNode const& previous = m_Index.Nodes[last];
    std::string_view const previousItem = TextOf(previous.Item);
    bool const hasComma = !previousItem.empty() && previousItem.back() == ',';
    std::size_t const start = isArray ? previous.Value.Begin : KeyStart(m_Text, previous.Item.Begin);
    std::string layout = Layout(TextOf(TTextRange{ previous.Item.Begin, start }));

    if (layout.find('\n') == std::string::npos)
    {
        // On one line: right after the last one (the first one's blanks are the brackets', so one blank)
        if (previous.Item.Begin == holder.Value.Begin + 1)
            layout = " ";

        if (hasComma)
            Insert(previous.Item.End, layout + item + ",");
        else
            Insert(previous.Item.End, "," + layout + item);

        return;
    }

    // One per line: on a line of its own after the last one's line, so a comment at its end stays there
    std::string_view const close = TextOf(holder.Close);
    std::size_t const lineBreak = close.find_first_of("\r\n");
    std::size_t const offset = holder.Close.Begin + (lineBreak == std::string_view::npos ? 0 : lineBreak);
    if (!hasComma)
        Insert(previous.Item.End, ",");

    Insert(offset, layout + item + (hasComma ? "," : ""));
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
    if (!m_Style.BlankLineBeforeTable || offset == 0)
        return false;

    std::size_t const line = LineBefore(offset);

    return line == NoIndex || m_Index.Lines[line].Kind != Detail::ELineKind::Blank;
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
std::size_t TEditor::CommentStart(std::size_t line) const
{
    while (line > 0 && m_Index.Lines[line - 1].Kind == Detail::ELineKind::Comment)
        --line;

    return line;
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
std::size_t TEditor::LastKeyLine(std::size_t block) const
{
    std::size_t const begin = m_Index.BlockLines[block];
    std::size_t end = block + 1 < m_Index.BlockLines.size() ? m_Index.BlockLines[block + 1] : m_Index.Lines.size();

    for (; end > begin; --end)
    {
        if (m_Index.Lines[end - 1].Kind == Detail::ELineKind::KeyValue)
            return end - 1;
    }

    return NoIndex;
}
//---------------------------------------------------------------------------
std::size_t TEditor::LastValue(std::size_t table) const
{
    std::size_t last = NoIndex;
    std::size_t lastOffset = 0;

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

        Detail::TIndexNode const& node = m_Index.Nodes[value];
        std::size_t const offset = node.Line != NoIndex ? m_Index.Lines[node.Line].Text.Begin : node.Item.Begin;
        if (last == NoIndex || offset > lastOffset)
        {
            last = value;
            lastOffset = offset;
        }
    }

    return last;
}
//---------------------------------------------------------------------------
std::size_t TEditor::LineAt(std::size_t offset) const
{
    for (std::size_t i = 0; i < m_Index.Lines.size(); ++i)
    {
        if (m_Index.Lines[i].Text.Begin == offset)
            return i;
    }

    return NoIndex;
}
//---------------------------------------------------------------------------
std::size_t TEditor::LineBefore(std::size_t offset) const
{
    for (std::size_t i = 0; i < m_Index.Lines.size(); ++i)
    {
        if (m_Index.Lines[i].Next == offset)
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
TASWTomlResult TEditor::NewBlocks(std::size_t table, std::string const& path, TASWTomlValue const& value)
{
    Detail::TIndexNode const& holder = m_Index.Nodes[table];
    std::size_t offset = m_Text.size();
    if (table != 0)
    {
        std::size_t const last = SubtreeLastBlock(table);
        if (last != NoIndex)
            offset = BlockEnd(last);
    }

    std::string block;
    TASWTomlResult result;
    if (IsArrayOfTables(value))
    {
        // Each element is inside the array, one level more
        m_Style.MaxDepth = Room(holder.Depth + 1);
        for (TASWTomlValue const& element : *value.AsArray())
        {
            result = Detail::WriteTableBlocks(path, true, !block.empty() || BlankLineBefore(offset), element, m_Style,
                block);
            if (!result.IsOk())
                return result;
        }
    }
    else
    {
        m_Style.MaxDepth = Room(holder.Depth);
        result = Detail::WriteTableBlocks(path, false, BlankLineBefore(offset), value, m_Style, block);
        if (!result.IsOk())
            return result;
    }

    InsertBlock(offset, std::move(block));

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::NewKey(std::size_t table, std::string const& key, TASWTomlValue const& value)
{
    Detail::TIndexNode const& holder = m_Index.Nodes[table];
    std::string keyText;
    TASWTomlResult result = Detail::WriteKey(key, keyText);
    if (!result.IsOk())
        return result;

    m_Style.MaxDepth = Room(holder.Depth);

    if (holder.Kind == ENodeKind::InlineTable)
    {
        std::string entry = keyText + " = ";
        result = Detail::WriteValue(value, m_Style, entry);
        if (result.IsOk())
            AppendItem(table, LastValue(table), entry);

        return result;
    }

    if (holder.Kind == ENodeKind::DottedTable)
    {
        // A dotted key after the last one through this table, its key parts as that one's
        std::size_t const last = LastValue(table);
        Detail::TIndexNode const& lastValue = m_Index.Nodes[last];

        if (lastValue.Line == NoIndex)
        {
            // In an inline table: a new entry of the inline table the dotted keys are in
            std::size_t container = holder.Parent;
            while (m_Index.Nodes[container].Kind == ENodeKind::DottedTable)
                container = m_Index.Nodes[container].Parent;

            std::size_t const keyBegin = KeyStart(m_Text, lastValue.Item.Begin);
            std::string entry(TextOf(TTextRange{ keyBegin, NameIn(table, lastValue.Item).End }));
            entry += "." + keyText + " = ";
            result = Detail::WriteValue(value, m_Style, entry);
            if (result.IsOk())
                AppendItem(container, LastValue(container), entry);

            return result;
        }

        Detail::TIndexLine const& line = m_Index.Lines[lastValue.Line];
        std::size_t keyBegin = line.Text.Begin;
        while (Detail::IsBlank(m_Text[keyBegin]))
            ++keyBegin;

        std::string const indent(TextOf(TTextRange{ line.Text.Begin, keyBegin }));
        std::string prefix(TextOf(TTextRange{ keyBegin, NameIn(table, line.Text).End }));
        prefix += "." + keyText;

        std::string lines;
        if (value.IsTable())
            result = Detail::WriteDottedKeys(indent, prefix, value, m_Style, lines);
        else
            result = Detail::WriteKeyValueLine(indent, prefix, value, m_Style, lines);

        if (result.IsOk())
            InsertLines(line.Next, std::move(lines));

        return result;
    }

    // The root, a header table, an element or an implicit table: a table as headers, anything else as a key line
    if (!value.IsTable() && !IsArrayOfTables(value))
        return NewKeyLine(table, keyText, value);

    std::string path = PathText(table);
    if (!path.empty())
        path += '.';

    path += keyText;

    return NewBlocks(table, path, value);
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::NewKeyLine(std::size_t table, std::string const& key, TASWTomlValue const& value)
{
    Detail::TIndexNode const& holder = m_Index.Nodes[table];
    m_Style.MaxDepth = Room(holder.Depth);
    TASWTomlResult result;

    if (holder.Kind == ENodeKind::ImplicitTable)
    {
        // The table gets its own header with the key, before the first header of its sub-tables and the comment
        // lines above it
        std::size_t const offset = m_Index.Lines[CommentStart(SubtreeFirstHeader(table))].Text.Begin;
        std::string block = BlankLineBefore(offset) ? m_LineBreak : std::string();
        block += "[" + PathText(table) + "]" + m_LineBreak;
        result = Detail::WriteKeyValueLine("", key, value, m_Style, block);
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
        Detail::TIndexLine const& line = m_Index.Lines[last];
        std::size_t keyBegin = line.Text.Begin;
        while (Detail::IsBlank(m_Text[keyBegin]))
            ++keyBegin;

        indent = TextOf(TTextRange{ line.Text.Begin, keyBegin });
        offset = line.Next;
    }
    else if (table != 0)
    {
        // Right after the header
        offset = m_Index.Lines[holder.Line].Next;
    }
    else
    {
        // The root without keys: after the file comment (the comment lines at the start, when a blank line follows)
        // and the blank lines under it, or at the start of the text
        std::size_t const begin = m_Index.BlockLines[0];
        std::size_t const end = m_Index.BlockLines.size() > 1 ? m_Index.BlockLines[1] : m_Index.Lines.size();
        std::size_t i = begin;
        while (i < end && m_Index.Lines[i].Kind == Detail::ELineKind::Comment)
            ++i;

        if (i > begin && i < end && m_Index.Lines[i].Kind == Detail::ELineKind::Blank)
        {
            while (i < end && m_Index.Lines[i].Kind == Detail::ELineKind::Blank)
                ++i;

            offset = m_Index.Lines[i - 1].Next;
        }
        else if (i > begin && i == m_Index.Lines.size())
        {
            // A file comment that ends the text gets the blank line under it first
            offset = m_Text.size();
            lines = m_LineBreak;
        }
    }

    result = Detail::WriteKeyValueLine(indent, key, value, m_Style, lines);
    if (!result.IsOk())
        return result;

    // In the root, a new key line before a header or its comment is kept apart from it by a blank line
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
std::size_t TEditor::Room(std::size_t depth) const noexcept
{
    return depth < m_MaxDepth ? m_MaxDepth - depth : 0;
}
//---------------------------------------------------------------------------
TASWTomlResult TEditor::SetExisting(std::size_t node, TASWTomlPath const& path, TASWTomlValue const& value)
{
    TASWTomlValue const* const old = FindValue(m_Root, path);
    if (old != nullptr && Detail::IdenticalValues(*old, value))
        return TASWTomlResult();

    Detail::TIndexNode const& existing = m_Index.Nodes[node];
    if (existing.Kind != ENodeKind::Scalar && existing.Kind != ENodeKind::Array &&
        existing.Kind != ENodeKind::InlineTable)
    {
        return PathFailure(ETomlCode::InvalidArgument, path,
            std::string(existing.Kind == ENodeKind::ArrayOfTables ? "an array of tables" : "a table") +
            " can't be set as a whole yet, only its keys and elements.");
    }

    // A value in place of the old one: a scalar in the old one's form where it can
    m_Style.MaxDepth = Room(existing.Depth - 1);
    std::string text;
    TASWTomlResult result;
    if (existing.Kind == ENodeKind::Scalar)
        result = Detail::WriteValueLike(value, TextOf(existing.Value), m_Style, text);
    else
        result = Detail::WriteValue(value, m_Style, text);

    if (result.IsOk())
        m_Splices.push_back(TSplice{ existing.Value.Begin, existing.Value.End, std::move(text) });

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
    TASWTomlPath walked;

    for (std::size_t i = 0; i < parts.size(); ++i)
    {
        Detail::TIndexNode const& holder = m_Index.Nodes[node];
        bool const isArray = holder.Kind == ENodeKind::Array || holder.Kind == ENodeKind::ArrayOfTables;
        TASWTomlPathPart const& part = parts[i];

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
        }
        else
        {
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
                for (std::size_t j = i + 1; j < parts.size(); ++j)
                {
                    if (parts[j].IsIndex())
                    {
                        walked.Append(part);
                        return PathFailure(ETomlCode::NotFound, path, MessagePath(walked) +
                            " doesn't exist, so it has no elements.");
                    }
                }

                return NewKey(node, part.GetKey(), Wrapped(parts, i + 1, value));
            }

            node = found->second;
        }

        walked.Append(part);
    }

    return SetExisting(node, path, value);
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
