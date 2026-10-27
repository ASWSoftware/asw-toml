/* **************************************************************************
ASWToml_Index.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "ASWToml_Index.h"
//---------------------------------------------------------------------------
#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWToml_Document.h"
#include "ASWToml_Utils.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

namespace Detail
{

namespace
{

// FindValue(): a table of at most this many keys is searched key by key, which is faster than its map of keys
constexpr std::size_t LinearSearchMax = 16;

// The offsets in a text of the parts of a key
using TOffsetVector = std::vector<std::size_t>;

// For each node, its value in the model when it is a scalar (else nullptr), while an index is built
using TValuePointerVector = std::vector<TDocValue const*>;


/////////////////////////////////////////////////////////////////////////////
// TTableReader
//
// Reads the tables, keys and values of a model into an index, applying TOML's table rules: a key or table is defined
// once, so a header can't define a table a header or dotted keys defined, dotted keys can't add to a table a header
// defined, nothing can be added to an inline table or an array value, and [[a]] can only add to an array of tables;
// and no table or array is nested deeper than Reading.MaxNestingDepth. Then makes the decoded document from the
// tree. Walks the model in the order ToText() writes it, counting the offset of each piece in the text, so the index
// has where each piece is and an error has the place of what it is about.
/////////////////////////////////////////////////////////////////////////////
class TTableReader
{
private:
    std::string_view m_Text; // The model's text, without a byte order mark
    std::size_t m_MaxDepth; // Reading.MaxNestingDepth
    TDocIndex m_Index; // The index being built
    TValuePointerVector m_Values; // For each node of m_Index
    std::size_t m_Offset = 0; // The offset in m_Text of the next piece of the model
    TASWTomlResult m_Error; // The error that stopped the reading

    // Adds the line that starts at 'begin' and whose value or header ended at m_Offset, 'rest' (blanks and a comment
    // after it; the whole line for a blank or comment line) and 'lineBreak' following, and moves m_Offset after it
    void AddLine(ELineKind kind, std::size_t block, std::size_t entry, std::size_t node, std::size_t begin,
        std::string const& rest, ELineBreak lineBreak);

    // Adds the key part at 'offset', 'size' bytes long, to the names of 'node'
    void AddName(std::size_t node, std::size_t offset, std::size_t size);

    // Adds a node of 'kind' for 'key' (empty for an element of an array) to the table or array 'parent', defined at
    // 'offset'; its index
    std::size_t AddNode(std::size_t parent, ENodeKind kind, std::string_view key, std::size_t offset);

    // Sets m_Error to DuplicateKey at 'offset', the first key part of a definition of what 'node' already is; false
    bool AlreadyDefined(std::size_t node, std::size_t offset);

    // The node of 'key' in the table 'table'; NoIndex if it has no such key
    std::size_t Find(std::size_t table, std::string_view key) const;

    // The line of the byte at 'offset' in m_Text
    std::size_t LineOf(std::size_t offset) const noexcept;

    // The path of 'node', as messages show it
    std::string PathOf(std::size_t node) const;

    // Reads 'header' up to its closing bracket, setting 'table' to the table its keys go into: the table it defines,
    // or the new element of an array of tables. False (and m_Error set) for a table defined twice or nested too deep.
    bool ReadHeader(TDocHeader const& header, std::size_t& table);

    // Reads 'keyValue' into the table 'table', setting 'node' to the node of its key's last part. False (and m_Error
    // set) for a key defined twice or nested too deep.
    bool ReadKeyValue(TDocKeyValue const& keyValue, std::size_t table, std::size_t& node);

    // Reads 'value', the value of 'key' (empty for an element) in the table or array 'parent', its key part at
    // 'offset' (or the value, for an element) and written at 'name' (empty for an element), setting 'node' to its
    // node. False (and m_Error set) for a key defined twice or nested too deep.
    bool ReadValue(TDocValue const& value, std::size_t parent, std::string_view key, std::size_t offset,
        TTextRange name, std::size_t& node);

    // Skips 'key', adding the offset of each of its parts to 'offsets'
    void SkipKey(TDocKey const& key, TOffsetVector& offsets);

    // Sets m_Error to NestingTooDeep at 'offset' for 'what' (e.g. "array"), which is 'depth' deep; false
    bool TooDeep(std::string const& what, std::size_t depth, std::size_t offset);

    // Sets 'value' to the decoded value of 'node', recursing as deep as the node's tables and arrays are nested
    void ToValue(std::size_t node, TASWTomlValue& value) const;

public:
    TTableReader(std::string_view text, std::size_t maxDepth);

    // Reads the tables of 'model', whose text (without a byte order mark) is the one given to the constructor, and on
    // success sets 'index' to its index and 'root' to the decoded document; on failure, DuplicateKey or
    // NestingTooDeep, they are left as they were. Call it once.
    TASWTomlResult Read(TDocModel const& model, TDocIndex& index, TASWTomlValue& root);
};

//---------------------------------------------------------------------------

bool IsNamedTable(ENodeKind kind) noexcept;
std::size_t NodeOfParts(TDocIndex const& index, TASWTomlPath::TPartVector const& parts, std::size_t count) noexcept;

//---------------------------------------------------------------------------
/*
    IsNamedTable

    Whether 'kind' is a table that a header may name a part of: one a header or dotted keys defined, or one made by a
    longer header (not an inline table, which is closed, nor an array of tables, whose last element is meant)
*/
bool IsNamedTable(ENodeKind kind) noexcept
{
    return kind == ENodeKind::ImplicitTable || kind == ENodeKind::HeaderTable || kind == ENodeKind::DottedTable;
}
//---------------------------------------------------------------------------
/*
    NodeOfParts

    The node of 'index' that the first 'count' of 'parts' name, which FindValue() has found a value for, so each is
    there: each key part through its table's map of keys, each index through its array's elements
*/
std::size_t NodeOfParts(TDocIndex const& index, TASWTomlPath::TPartVector const& parts, std::size_t count) noexcept
{
    std::size_t node = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        TIndexNode const& holder = index.Nodes[node];
        node = parts[i].IsKey() ? holder.Keys.find(parts[i].GetKey())->second : holder.Children[parts[i].GetIndex()];
    }

    return node;
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TTableReader
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTableReader::TTableReader(std::string_view text, std::size_t maxDepth)
    : m_Text(text),
      m_MaxDepth(maxDepth)
{
}
//---------------------------------------------------------------------------
void TTableReader::AddLine(ELineKind kind, std::size_t block, std::size_t entry, std::size_t node, std::size_t begin,
    std::string const& rest, ELineBreak lineBreak)
{
    TIndexLine& line = m_Index.Lines.emplace_back();
    line.Kind = kind;
    line.Block = block;
    line.Entry = entry;
    line.Node = node;

    std::size_t const restBegin = m_Offset;
    m_Offset += rest.size();
    line.Text = TTextRange{ begin, m_Offset };
    if (kind == ELineKind::KeyValue || kind == ELineKind::Header)
        line.Trailing = TTextRange{ restBegin, m_Offset };
    else
        line.Trailing = TTextRange{ m_Offset, m_Offset };

    m_Offset += LineBreakText(lineBreak).size();
    line.Next = m_Offset;
}
//---------------------------------------------------------------------------
void TTableReader::AddName(std::size_t node, std::size_t offset, std::size_t size)
{
    m_Index.Nodes[node].Names.push_back(TTextRange{ offset, offset + size });
}
//---------------------------------------------------------------------------
std::size_t TTableReader::AddNode(std::size_t parent, ENodeKind kind, std::string_view key, std::size_t offset)
{
    std::size_t const index = m_Index.Nodes.size();
    TIndexNode& node = m_Index.Nodes.emplace_back();
    node.Kind = kind;
    node.Key = key;
    node.Parent = parent;
    node.Depth = m_Index.Nodes[parent].Depth + 1;
    node.Offset = offset;
    m_Values.push_back(nullptr);

    TIndexNode& holder = m_Index.Nodes[parent];
    if (holder.Kind != ENodeKind::Array && holder.Kind != ENodeKind::ArrayOfTables)
        holder.Keys.emplace(key, index);

    m_Index.Nodes[index].Position = holder.Children.size();
    holder.Children.push_back(index);

    return index;
}
//---------------------------------------------------------------------------
bool TTableReader::AlreadyDefined(std::size_t node, std::size_t offset)
{
    ENodeKind const kind = m_Index.Nodes[node].Kind;
    char const* what = "key";
    if (kind == ENodeKind::ArrayOfTables)
        what = "array of tables";
    else if (IsNamedTable(kind))
        what = "table";

    m_Error.Code = ETomlCode::DuplicateKey;
    m_Error.Message = std::string("The ") + what + " " + PathOf(node) + " is already defined on line " +
        std::to_string(LineOf(m_Index.Nodes[node].Offset)) + ".";
    SetLineAndColumn(m_Error, m_Text, offset);

    return false;
}
//---------------------------------------------------------------------------
std::size_t TTableReader::Find(std::size_t table, std::string_view key) const
{
    TNodeIndexMap const& keys = m_Index.Nodes[table].Keys;
    TNodeIndexMap::const_iterator const found = keys.find(key);

    return found == keys.end() ? NoIndex : found->second;
}
//---------------------------------------------------------------------------
std::size_t TTableReader::LineOf(std::size_t offset) const noexcept
{
    TASWTomlResult place;
    SetLineAndColumn(place, m_Text, offset);

    return place.Line;
}
//---------------------------------------------------------------------------
std::string TTableReader::PathOf(std::size_t node) const
{
    // The nodes from 'node' up to the root's child
    TIndexVector chain;
    for (std::size_t index = node; index != 0; index = m_Index.Nodes[index].Parent)
        chain.push_back(index);

    TASWTomlPath path;
    for (TIndexVector::const_reverse_iterator it = chain.rbegin(); it != chain.rend(); ++it)
    {
        TIndexNode const& part = m_Index.Nodes[*it];
        TIndexNode const& holder = m_Index.Nodes[part.Parent];
        if (holder.Kind == ENodeKind::Array || holder.Kind == ENodeKind::ArrayOfTables)
        {
            TIndexVector const& elements = holder.Children;
            path.Append(static_cast<std::size_t>(std::find(elements.begin(), elements.end(), *it) - elements.begin()));
        }
        else
        {
            path.Append(part.Key);
        }
    }

    return path.ToString();
}
//---------------------------------------------------------------------------
TASWTomlResult TTableReader::Read(TDocModel const& model, TDocIndex& index, TASWTomlValue& root)
{
    m_Index.Nodes.emplace_back();
    m_Values.push_back(nullptr);
    std::size_t table = 0;

    for (std::size_t b = 0; b < model.Blocks.size(); ++b)
    {
        TDocBlock const& block = model.Blocks[b];
        m_Index.BlockLines.push_back(m_Index.Lines.size());

        if (block.HasHeader)
        {
            std::size_t const begin = m_Offset;
            if (!ReadHeader(block.Header, table))
                return m_Error;

            m_Index.Nodes[table].Line = m_Index.Lines.size();
            m_Index.Nodes[table].Block = b;
            AddLine(ELineKind::Header, b, NoIndex, table, begin, block.Header.Text, block.Header.LineBreak);
        }

        for (std::size_t e = 0; e < block.Entries.size(); ++e)
        {
            TDocEntry const& entry = block.Entries[e];
            std::size_t const begin = m_Offset;
            std::size_t node = NoIndex;
            ELineKind kind = entry.Kind == EEntryKind::Comment ? ELineKind::Comment : ELineKind::Blank;

            if (entry.Kind == EEntryKind::KeyValue)
            {
                if (!ReadKeyValue(entry.KeyValue, table, node))
                    return m_Error;

                kind = ELineKind::KeyValue;
                m_Index.Nodes[node].Line = m_Index.Lines.size();
            }

            AddLine(kind, b, e, node, begin, entry.Text, entry.LineBreak);
        }
    }

    TASWTomlValue decoded;
    ToValue(0, decoded);
    index = std::move(m_Index);
    root = std::move(decoded);

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
bool TTableReader::ReadHeader(TDocHeader const& header, std::size_t& table)
{
    std::size_t const brackets = header.IsArray ? 2 : 1;
    TOffsetVector offsets;
    m_Offset += header.Indent.size() + brackets;
    SkipKey(header.Key, offsets);
    m_Offset += brackets;

    // Each part but the last names a table, made when there is none yet; of an array of tables, its last element
    TDocKey::TPartVector const& parts = header.Key.Parts;
    std::size_t const last = parts.size() - 1;
    std::size_t node = 0;

    for (std::size_t i = 0; i < last; ++i)
    {
        std::size_t child = Find(node, parts[i].Name);
        if (child == NoIndex)
        {
            std::size_t const depth = m_Index.Nodes[node].Depth + 1;
            if (depth > m_MaxDepth)
                return TooDeep("table " + MessageText(parts[i].Name), depth, offsets[i]);

            child = AddNode(node, ENodeKind::ImplicitTable, parts[i].Name, offsets[i]);
        }
        else if (!IsNamedTable(m_Index.Nodes[child].Kind) && m_Index.Nodes[child].Kind != ENodeKind::ArrayOfTables)
        {
            return AlreadyDefined(child, offsets[0]);
        }

        AddName(child, offsets[i], parts[i].Text.size());
        if (m_Index.Nodes[child].Kind == ENodeKind::ArrayOfTables)
            child = m_Index.Nodes[child].Children.back();

        node = child;
    }

    // The last part names the table the header defines, which may have been made by a longer header before, or an
    // array of tables, new or not, which gets a new element
    std::string const& name = parts[last].Name;
    std::size_t child = Find(node, name);
    std::size_t const levels = header.IsArray ? 2 : 1;
    if (child == NoIndex && m_Index.Nodes[node].Depth + levels > m_MaxDepth)
        return TooDeep("table " + MessageText(name), m_Index.Nodes[node].Depth + levels, offsets[last]);

    if (!header.IsArray)
    {
        if (child == NoIndex)
        {
            child = AddNode(node, ENodeKind::HeaderTable, name, offsets[last]);
        }
        else if (m_Index.Nodes[child].Kind == ENodeKind::ImplicitTable)
        {
            m_Index.Nodes[child].Kind = ENodeKind::HeaderTable;
            m_Index.Nodes[child].Offset = offsets[last];
        }
        else
        {
            return AlreadyDefined(child, offsets[0]);
        }

        AddName(child, offsets[last], parts[last].Text.size());
        table = child;

        return true;
    }

    if (child == NoIndex)
        child = AddNode(node, ENodeKind::ArrayOfTables, name, offsets[last]);
    else if (m_Index.Nodes[child].Kind != ENodeKind::ArrayOfTables)
        return AlreadyDefined(child, offsets[0]);

    AddName(child, offsets[last], parts[last].Text.size());
    table = AddNode(child, ENodeKind::ElementTable, std::string_view(), offsets[last]);

    return true;
}
//---------------------------------------------------------------------------
bool TTableReader::ReadKeyValue(TDocKeyValue const& keyValue, std::size_t table, std::size_t& node)
{
    TOffsetVector offsets;
    SkipKey(keyValue.Key, offsets);
    m_Offset += 1 + keyValue.BeforeValue.size();

    // Each part but the last names a table: one dotted keys made (or make now), or one only a longer header made,
    // which they define now. Only the keys of the table they were in can reach the tables dotted keys made: that table
    // is the root, a header's table, an element of an array of tables or an inline table, and the keys of any other
    // table can't pass it.
    TDocKey::TPartVector const& parts = keyValue.Key.Parts;
    std::size_t const last = parts.size() - 1;
    std::size_t holder = table;

    for (std::size_t i = 0; i < last; ++i)
    {
        std::size_t child = Find(holder, parts[i].Name);
        if (child == NoIndex)
        {
            std::size_t const depth = m_Index.Nodes[holder].Depth + 1;
            if (depth > m_MaxDepth)
                return TooDeep("table " + MessageText(parts[i].Name), depth, offsets[i]);

            child = AddNode(holder, ENodeKind::DottedTable, parts[i].Name, offsets[i]);
        }
        else if (m_Index.Nodes[child].Kind == ENodeKind::ImplicitTable)
        {
            m_Index.Nodes[child].Kind = ENodeKind::DottedTable;
            m_Index.Nodes[child].Offset = offsets[i];
        }
        else if (m_Index.Nodes[child].Kind != ENodeKind::DottedTable)
        {
            return AlreadyDefined(child, offsets[0]);
        }

        AddName(child, offsets[i], parts[i].Text.size());
        holder = child;
    }

    std::size_t const existing = Find(holder, parts[last].Name);
    if (existing != NoIndex)
        return AlreadyDefined(existing, offsets[0]);

    TTextRange const name{ offsets[last], offsets[last] + parts[last].Text.size() };

    return ReadValue(keyValue.Value, holder, parts[last].Name, offsets[last], name, node);
}
//---------------------------------------------------------------------------
bool TTableReader::ReadValue(TDocValue const& value, std::size_t parent, std::string_view key, std::size_t offset,
    TTextRange name, std::size_t& node)
{
    std::size_t const begin = m_Offset;

    if (value.Kind == EValueKind::Scalar)
    {
        node = AddNode(parent, ENodeKind::Scalar, key, offset);
        m_Values[node] = &value;
        m_Offset += value.Text.size();
        m_Index.Nodes[node].Value = TTextRange{ begin, m_Offset };
        if (name.End > name.Begin)
            m_Index.Nodes[node].Names.push_back(name);

        return true;
    }

    bool const isArray = value.Kind == EValueKind::Array;
    std::size_t const depth = m_Index.Nodes[parent].Depth + 1;
    if (depth > m_MaxDepth)
        return TooDeep(isArray ? "array" : "inline table", depth, m_Offset);

    node = AddNode(parent, isArray ? ENodeKind::Array : ENodeKind::InlineTable, key, offset);
    if (name.End > name.Begin)
        m_Index.Nodes[node].Names.push_back(name);

    ++m_Offset;

    if (isArray)
    {
        for (TDocElement const& element : value.Elements)
        {
            std::size_t const itemBegin = m_Offset;
            std::size_t child = NoIndex;
            m_Offset += element.Before.size();
            if (!ReadValue(element.Value, node, std::string_view(), m_Offset, TTextRange(), child))
                return false;

            m_Offset += element.After.size() + (element.Comma ? 1 : 0);
            m_Index.Nodes[child].Item = TTextRange{ itemBegin, m_Offset };
        }
    }
    else
    {
        for (TDocInlineEntry const& entry : value.Entries)
        {
            std::size_t const itemBegin = m_Offset;
            std::size_t child = NoIndex;
            m_Offset += entry.Before.size();
            if (!ReadKeyValue(entry.KeyValue, node, child))
                return false;

            m_Offset += entry.After.size() + (entry.Comma ? 1 : 0);
            m_Index.Nodes[child].Item = TTextRange{ itemBegin, m_Offset };
        }
    }

    std::size_t const closeBegin = m_Offset;
    m_Offset += value.Close.size() + 1;
    m_Index.Nodes[node].Close = TTextRange{ closeBegin, m_Offset };
    m_Index.Nodes[node].Value = TTextRange{ begin, m_Offset };

    return true;
}
//---------------------------------------------------------------------------
void TTableReader::SkipKey(TDocKey const& key, TOffsetVector& offsets)
{
    for (std::size_t i = 0; i < key.Parts.size(); ++i)
    {
        TDocKeyPart const& part = key.Parts[i];
        if (i > 0)
            ++m_Offset;

        m_Offset += part.Before.size();
        offsets.push_back(m_Offset);
        m_Offset += part.Text.size() + part.After.size();
    }
}
//---------------------------------------------------------------------------
bool TTableReader::TooDeep(std::string const& what, std::size_t depth, std::size_t offset)
{
    m_Error.Code = ETomlCode::NestingTooDeep;
    m_Error.Message = "The " + what + " is nested " + std::to_string(depth) +
        " deep, deeper than Reading.MaxNestingDepth (" + std::to_string(m_MaxDepth) + ") allows.";
    SetLineAndColumn(m_Error, m_Text, offset);

    return false;
}
//---------------------------------------------------------------------------
void TTableReader::ToValue(std::size_t node, TASWTomlValue& value) const
{
    TIndexNode const& source = m_Index.Nodes[node];
    switch (source.Kind)
    {
        case ENodeKind::Scalar:
            value = m_Values[node]->Scalar;
            break;

        case ENodeKind::Array:
        case ENodeKind::ArrayOfTables:
        {
            value = TASWTomlValue(TASWTomlValue::TValueVector(source.Children.size()));
            TASWTomlValue::TValueVector& array = *value.AsArray();
            for (std::size_t i = 0; i < array.size(); ++i)
                ToValue(source.Children[i], array[i]);

            break;
        }

        case ENodeKind::InlineTable:
        case ENodeKind::ImplicitTable:
        case ENodeKind::HeaderTable:
        case ENodeKind::DottedTable:
        case ENodeKind::ElementTable:
        {
            value = TASWTomlValue(TASWTomlValue::TKeyValueVector(source.Children.size()));
            TASWTomlValue::TKeyValueVector& table = *value.AsTable();
            for (std::size_t i = 0; i < table.size(); ++i)
            {
                table[i].Key = m_Index.Nodes[source.Children[i]].Key;
                ToValue(source.Children[i], table[i].Value);
            }

            break;
        }
    }
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
TASWTomlResult BuildIndex(TDocModel const& model, std::string_view text, std::size_t maxDepth, TDocIndex& index,
    TASWTomlValue& root)
{
    return TTableReader(text, maxDepth).Read(model, index, root);
}
//---------------------------------------------------------------------------
std::size_t FindNode(TDocIndex const& index, TASWTomlPath const& path) noexcept
{
    if (!path.IsValid() || index.Nodes.empty())
        return NoIndex;

    std::size_t node = 0;
    for (TASWTomlPathPart const& part : path.GetParts())
    {
        TIndexNode const& holder = index.Nodes[node];
        if (part.IsKey())
        {
            TNodeIndexMap::const_iterator const found = holder.Keys.find(part.GetKey());
            if (found == holder.Keys.end())
                return NoIndex;

            node = found->second;
        }
        else
        {
            bool const isArray = holder.Kind == ENodeKind::Array || holder.Kind == ENodeKind::ArrayOfTables;
            if (!isArray || part.GetIndex() >= holder.Children.size())
                return NoIndex;

            node = holder.Children[part.GetIndex()];
        }
    }

    return node;
}
//---------------------------------------------------------------------------
TASWTomlValue const* FindValue(TDocIndex const& index, TASWTomlValue const& root, TASWTomlPath const& path) noexcept
{
    if (!path.IsValid())
        return nullptr;

    // The values are walked as TASWTomlValue::Find() would; only a large table's key is found through the index, in
    // the map of keys of the table's node, and a node's Position is its place in its parent's value (ToValue() builds
    // the values in the order of Children)
    TASWTomlPath::TPartVector const& parts = path.GetParts();
    TASWTomlValue const* value = &root;
    for (std::size_t i = 0; i < parts.size(); ++i)
    {
        if (parts[i].IsKey())
        {
            TASWTomlValue::TKeyValueVector const* const table = value->AsTable();
            if (table == nullptr || table->size() <= LinearSearchMax)
            {
                value = value->Find(parts[i].GetKey());
            }
            else
            {
                TNodeIndexMap const& keys = index.Nodes[NodeOfParts(index, parts, i)].Keys;
                TNodeIndexMap::const_iterator const found = keys.find(parts[i].GetKey());
                value = found != keys.end() ? &(*table)[index.Nodes[found->second].Position].Value : nullptr;
            }

            if (value == nullptr)
                return nullptr;
        }
        else
        {
            TASWTomlValue::TValueVector const* const array = value->AsArray();
            if (array == nullptr || parts[i].GetIndex() >= array->size())
                return nullptr;

            value = &(*array)[parts[i].GetIndex()];
        }
    }

    return value;
}
//---------------------------------------------------------------------------

} // namespace Detail

} // namespace ASWToml
