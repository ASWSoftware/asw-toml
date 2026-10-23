/* **************************************************************************
ASWToml_Document.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "ASWToml_Document.h"
//---------------------------------------------------------------------------
#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWToml_Parser.h"
#include "ASWToml_Utils.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

namespace
{

// The UTF-8 byte order mark
constexpr std::string_view Utf8Bom = "\xEF\xBB\xBF";

// The index of no node, e.g. of a key a table doesn't have
constexpr std::size_t NoNode = static_cast<std::size_t>(-1);

// What a node of the tree of tables (see TTableReader) is, which says what may be added to it
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

// The indexes of nodes, e.g. the keys of a table in order
using TNodeIndexVector = std::vector<std::size_t>;

// The keys of a table and the indexes of their nodes
using TNodeIndexMap = std::map<std::string, std::size_t, std::less<> >;

// The offsets in a text of the parts of a key
using TOffsetVector = std::vector<std::size_t>;


/////////////////////////////////////////////////////////////////////////////
// TNode
//
// A table, array or value in the tree of tables TTableReader builds
/////////////////////////////////////////////////////////////////////////////
struct TNode
{
    ENodeKind Kind = ENodeKind::HeaderTable;
    std::string Key; // Its key in the table holding it; empty for an element of an array and for the root
    std::size_t Parent = 0; // The index of the table or array holding it (the root's is its own)
    std::size_t Depth = 0; // How many tables and arrays it is inside, itself included (the root not counted)

    // Where its first definition is in the text, for messages: the key part that made it, in a key or header
    std::size_t Offset = 0;

    Detail::TDocValue const* Value = nullptr; // Scalar: its value in the model
    TNodeIndexVector Children; // A table's keys in the order they were first defined, or an array's elements
    TNodeIndexMap Keys; // A table's keys
};


/////////////////////////////////////////////////////////////////////////////
// TTableReader
//
// Reads the tables, keys and values of a model into a tree of nodes, applying TOML's table rules: a key or table is
// defined once, so a header can't define a table a header or dotted keys defined, dotted keys can't add to a table a
// header defined, nothing can be added to an inline table or an array value, and [[a]] can only add to an array of
// tables; and no table or array is nested deeper than Reading.MaxNestingDepth. Then makes the decoded document from
// the tree. Walks the model in the order ToText() writes it, counting the offset of each piece in the text, so an
// error has the place of what it is about.
/////////////////////////////////////////////////////////////////////////////
class TTableReader
{
private:
    // The nodes of the tree
    using TNodeVector = std::vector<TNode>;

    std::string_view m_Text; // The model's text, without a byte order mark
    std::size_t m_MaxDepth; // Reading.MaxNestingDepth
    TNodeVector m_Nodes; // The root table first
    std::size_t m_Offset = 0; // The offset in m_Text of the next piece of the model
    TASWTomlResult m_Error; // The error that stopped the reading

    // Adds a node of 'kind' for 'key' (empty for an element of an array) to the table or array 'parent', defined at
    // 'offset'; its index
    std::size_t AddNode(std::size_t parent, ENodeKind kind, std::string_view key, std::size_t offset);

    // Sets m_Error to DuplicateKey at 'offset', the first key part of a definition of what 'node' already is; false
    bool AlreadyDefined(std::size_t node, std::size_t offset);

    // The node of 'key' in the table 'table'; NoNode if it has no such key
    std::size_t Find(std::size_t table, std::string_view key) const;

    // The line of the byte at 'offset' in m_Text
    std::size_t LineOf(std::size_t offset) const noexcept;

    // The path of 'node', as messages show it
    std::string PathOf(std::size_t node) const;

    // Reads 'header', setting 'table' to the table its keys go into: the table it defines, or the new element of an
    // array of tables. False (and m_Error set) for a table defined twice or nested too deep.
    bool ReadHeader(Detail::TDocHeader const& header, std::size_t& table);

    // Reads 'keyValue' into the table 'table'. False (and m_Error set) for a key defined twice or nested too deep.
    bool ReadKeyValue(Detail::TDocKeyValue const& keyValue, std::size_t table);

    // Reads 'value', the value of 'key' (empty for an element) in the table or array 'parent', its key part at 'offset'
    // (or the value, for an element). False (and m_Error set) for a key defined twice or nested too deep.
    bool ReadValue(Detail::TDocValue const& value, std::size_t parent, std::string_view key, std::size_t offset);

    // Skips 'key', adding the offset of each of its parts to 'offsets'
    void SkipKey(Detail::TDocKey const& key, TOffsetVector& offsets);

    // Sets m_Error to NestingTooDeep at 'offset' for 'what' (e.g. "array"), which is 'depth' deep; false
    bool TooDeep(std::string const& what, std::size_t depth, std::size_t offset);

    // Sets 'value' to the decoded value of 'node', recursing as deep as the node's tables and arrays are nested
    void ToValue(std::size_t node, TASWTomlValue& value) const;

public:
    TTableReader(std::string_view text, std::size_t maxDepth);

    // Reads the tables of 'model', whose text (without a byte order mark) is the one given to the constructor, and on
    // success sets 'root' to the decoded document; on failure, DuplicateKey or NestingTooDeep, 'root' is left as it was.
    // Call it once.
    TASWTomlResult Read(Detail::TDocModel const& model, TASWTomlValue& root);
};


void AppendKey(Detail::TDocKey const& key, std::string& text);
void AppendKeyValue(Detail::TDocKeyValue const& keyValue, std::string& text);
void AppendValue(Detail::TDocValue const& value, std::string& text);
std::string ConvertLineBreaks(std::string const& text, ELineBreak lineBreak);
bool IsNamedTable(ENodeKind kind) noexcept;

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
    IsNamedTable

    Whether 'kind' is a table that a header may name a part of: one a header or dotted keys defined, or one made by a
    longer header (not an inline table, which is closed, nor an array of tables, whose last element is meant)
*/
bool IsNamedTable(ENodeKind kind) noexcept
{
    return kind == ENodeKind::ImplicitTable || kind == ENodeKind::HeaderTable || kind == ENodeKind::DottedTable;
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
std::size_t TTableReader::AddNode(std::size_t parent, ENodeKind kind, std::string_view key, std::size_t offset)
{
    std::size_t const index = m_Nodes.size();
    TNode& node = m_Nodes.emplace_back();
    node.Kind = kind;
    node.Key = key;
    node.Parent = parent;
    node.Depth = m_Nodes[parent].Depth + 1;
    node.Offset = offset;

    TNode& holder = m_Nodes[parent];
    if (holder.Kind != ENodeKind::Array && holder.Kind != ENodeKind::ArrayOfTables)
        holder.Keys.emplace(key, index);

    holder.Children.push_back(index);

    return index;
}
//---------------------------------------------------------------------------
bool TTableReader::AlreadyDefined(std::size_t node, std::size_t offset)
{
    ENodeKind const kind = m_Nodes[node].Kind;
    char const* what = "key";
    if (kind == ENodeKind::ArrayOfTables)
        what = "array of tables";
    else if (IsNamedTable(kind))
        what = "table";

    m_Error.Code = ETomlCode::DuplicateKey;
    m_Error.Message = std::string("The ") + what + " " + PathOf(node) + " is already defined on line " +
        std::to_string(LineOf(m_Nodes[node].Offset)) + ".";
    Detail::SetLineAndColumn(m_Error, m_Text, offset);

    return false;
}
//---------------------------------------------------------------------------
std::size_t TTableReader::Find(std::size_t table, std::string_view key) const
{
    TNodeIndexMap const& keys = m_Nodes[table].Keys;
    TNodeIndexMap::const_iterator const found = keys.find(key);

    return found == keys.end() ? NoNode : found->second;
}
//---------------------------------------------------------------------------
std::size_t TTableReader::LineOf(std::size_t offset) const noexcept
{
    TASWTomlResult place;
    Detail::SetLineAndColumn(place, m_Text, offset);

    return place.Line;
}
//---------------------------------------------------------------------------
std::string TTableReader::PathOf(std::size_t node) const
{
    // The nodes from 'node' up to the root's child
    TNodeIndexVector chain;
    for (std::size_t index = node; index != 0; index = m_Nodes[index].Parent)
        chain.push_back(index);

    TASWTomlPath path;
    for (TNodeIndexVector::const_reverse_iterator it = chain.rbegin(); it != chain.rend(); ++it)
    {
        TNode const& part = m_Nodes[*it];
        TNode const& holder = m_Nodes[part.Parent];
        if (holder.Kind == ENodeKind::Array || holder.Kind == ENodeKind::ArrayOfTables)
        {
            TNodeIndexVector const& elements = holder.Children;
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
TASWTomlResult TTableReader::Read(Detail::TDocModel const& model, TASWTomlValue& root)
{
    m_Nodes.emplace_back();
    std::size_t table = 0;

    for (Detail::TDocBlock const& block : model.Blocks)
    {
        if (block.HasHeader && !ReadHeader(block.Header, table))
            return m_Error;

        for (Detail::TDocEntry const& entry : block.Entries)
        {
            if (entry.Kind == Detail::EEntryKind::KeyValue && !ReadKeyValue(entry.KeyValue, table))
                return m_Error;

            m_Offset += entry.Text.size() + Detail::LineBreakText(entry.LineBreak).size();
        }
    }

    TASWTomlValue decoded;
    ToValue(0, decoded);
    root = std::move(decoded);

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
bool TTableReader::ReadHeader(Detail::TDocHeader const& header, std::size_t& table)
{
    std::size_t const brackets = header.IsArray ? 2 : 1;
    TOffsetVector offsets;
    m_Offset += header.Indent.size() + brackets;
    SkipKey(header.Key, offsets);
    m_Offset += brackets + header.Text.size() + Detail::LineBreakText(header.LineBreak).size();

    // Each part but the last names a table, made when there is none yet; of an array of tables, its last element
    Detail::TDocKey::TPartVector const& parts = header.Key.Parts;
    std::size_t const last = parts.size() - 1;
    std::size_t node = 0;

    for (std::size_t i = 0; i < last; ++i)
    {
        std::size_t child = Find(node, parts[i].Name);
        if (child == NoNode)
        {
            std::size_t const depth = m_Nodes[node].Depth + 1;
            if (depth > m_MaxDepth)
                return TooDeep("table " + Detail::MessageText(parts[i].Name), depth, offsets[i]);

            child = AddNode(node, ENodeKind::ImplicitTable, parts[i].Name, offsets[i]);
        }
        else if (m_Nodes[child].Kind == ENodeKind::ArrayOfTables)
        {
            child = m_Nodes[child].Children.back();
        }
        else if (!IsNamedTable(m_Nodes[child].Kind))
        {
            return AlreadyDefined(child, offsets[0]);
        }

        node = child;
    }

    // The last part names the table the header defines, which may have been made by a longer header before, or an
    // array of tables, new or not, which gets a new element
    std::string const& name = parts[last].Name;
    std::size_t child = Find(node, name);
    std::size_t const levels = header.IsArray ? 2 : 1;
    if (child == NoNode && m_Nodes[node].Depth + levels > m_MaxDepth)
        return TooDeep("table " + Detail::MessageText(name), m_Nodes[node].Depth + levels, offsets[last]);

    if (!header.IsArray)
    {
        if (child == NoNode)
        {
            child = AddNode(node, ENodeKind::HeaderTable, name, offsets[last]);
        }
        else if (m_Nodes[child].Kind == ENodeKind::ImplicitTable)
        {
            m_Nodes[child].Kind = ENodeKind::HeaderTable;
            m_Nodes[child].Offset = offsets[last];
        }
        else
        {
            return AlreadyDefined(child, offsets[0]);
        }

        table = child;

        return true;
    }

    if (child == NoNode)
        child = AddNode(node, ENodeKind::ArrayOfTables, name, offsets[last]);
    else if (m_Nodes[child].Kind != ENodeKind::ArrayOfTables)
        return AlreadyDefined(child, offsets[0]);

    table = AddNode(child, ENodeKind::ElementTable, std::string_view(), offsets[last]);

    return true;
}
//---------------------------------------------------------------------------
bool TTableReader::ReadKeyValue(Detail::TDocKeyValue const& keyValue, std::size_t table)
{
    TOffsetVector offsets;
    SkipKey(keyValue.Key, offsets);
    m_Offset += 1 + keyValue.BeforeValue.size();

    // Each part but the last names a table: one dotted keys made (or make now), or one only a longer header made,
    // which they define now. Only the keys of the table they were in can reach the tables dotted keys made: that table
    // is the root, a header's table, an element of an array of tables or an inline table, and the keys of any other
    // table can't pass it.
    Detail::TDocKey::TPartVector const& parts = keyValue.Key.Parts;
    std::size_t const last = parts.size() - 1;
    std::size_t node = table;

    for (std::size_t i = 0; i < last; ++i)
    {
        std::size_t child = Find(node, parts[i].Name);
        if (child == NoNode)
        {
            std::size_t const depth = m_Nodes[node].Depth + 1;
            if (depth > m_MaxDepth)
                return TooDeep("table " + Detail::MessageText(parts[i].Name), depth, offsets[i]);

            child = AddNode(node, ENodeKind::DottedTable, parts[i].Name, offsets[i]);
        }
        else if (m_Nodes[child].Kind == ENodeKind::ImplicitTable)
        {
            m_Nodes[child].Kind = ENodeKind::DottedTable;
            m_Nodes[child].Offset = offsets[i];
        }
        else if (m_Nodes[child].Kind != ENodeKind::DottedTable)
        {
            return AlreadyDefined(child, offsets[0]);
        }

        node = child;
    }

    std::size_t const existing = Find(node, parts[last].Name);
    if (existing != NoNode)
        return AlreadyDefined(existing, offsets[0]);

    return ReadValue(keyValue.Value, node, parts[last].Name, offsets[last]);
}
//---------------------------------------------------------------------------
bool TTableReader::ReadValue(Detail::TDocValue const& value, std::size_t parent, std::string_view key,
    std::size_t offset)
{
    if (value.Kind == Detail::EValueKind::Scalar)
    {
        std::size_t const node = AddNode(parent, ENodeKind::Scalar, key, offset);
        m_Nodes[node].Value = &value;
        m_Offset += value.Text.size();

        return true;
    }

    bool const isArray = value.Kind == Detail::EValueKind::Array;
    std::size_t const depth = m_Nodes[parent].Depth + 1;
    if (depth > m_MaxDepth)
        return TooDeep(isArray ? "array" : "inline table", depth, m_Offset);

    std::size_t const node = AddNode(parent, isArray ? ENodeKind::Array : ENodeKind::InlineTable, key, offset);
    ++m_Offset;

    if (isArray)
    {
        for (Detail::TDocElement const& element : value.Elements)
        {
            m_Offset += element.Before.size();
            if (!ReadValue(element.Value, node, std::string_view(), m_Offset))
                return false;

            m_Offset += element.After.size() + (element.Comma ? 1 : 0);
        }
    }
    else
    {
        for (Detail::TDocInlineEntry const& entry : value.Entries)
        {
            m_Offset += entry.Before.size();
            if (!ReadKeyValue(entry.KeyValue, node))
                return false;

            m_Offset += entry.After.size() + (entry.Comma ? 1 : 0);
        }
    }

    m_Offset += value.Close.size() + 1;

    return true;
}
//---------------------------------------------------------------------------
void TTableReader::SkipKey(Detail::TDocKey const& key, TOffsetVector& offsets)
{
    for (std::size_t i = 0; i < key.Parts.size(); ++i)
    {
        Detail::TDocKeyPart const& part = key.Parts[i];
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
    Detail::SetLineAndColumn(m_Error, m_Text, offset);

    return false;
}
//---------------------------------------------------------------------------
void TTableReader::ToValue(std::size_t node, TASWTomlValue& value) const
{
    TNode const& source = m_Nodes[node];
    switch (source.Kind)
    {
        case ENodeKind::Scalar:
            value = source.Value->Scalar;
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
                table[i].Key = m_Nodes[source.Children[i]].Key;
                ToValue(source.Children[i], table[i].Value);
            }

            break;
        }
    }
}
//---------------------------------------------------------------------------

} // namespace


/////////////////////////////////////////////////////////////////////////////
// TASWTomlDocument
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlDocument::TASWTomlDocument()
{
}
//---------------------------------------------------------------------------
TASWTomlValue const* TASWTomlDocument::Find(TASWTomlPath const& path) const noexcept
{
    if (!path.IsValid())
        return nullptr;

    TASWTomlValue const* value = &m_Root;
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
TASWTomlResult TASWTomlDocument::Parse(std::string_view text, TASWTomlOptions const& options)
{
    TASWTomlResult result = options.Validate();
    if (!result.IsOk())
        return result;

    // The options are copied and the text read into a new model and root first, so nothing changes on a failure;
    // moving them in can't fail
    TASWTomlOptions newOptions = options;
    Detail::TDocModel model;
    TASWTomlValue root;

    result = Detail::ParseDocument(text, options.Reading, model);
    if (result.IsOk())
    {
        // The offsets of the table reader's errors count from after the byte order mark, as the parser's do
        std::string_view const body = text.substr(model.HasBom ? Utf8Bom.size() : 0);
        result = TTableReader(body, options.Reading.MaxNestingDepth).Read(model, root);
    }

    if (!result.IsOk())
        return result;

    m_Model = std::move(model);
    m_Options = std::move(newOptions);
    m_Root = std::move(root);

    return result;
}
//---------------------------------------------------------------------------
std::string TASWTomlDocument::ToText() const
{
    TASWTomlWritingOptions const& writing = m_Options.Writing;

    std::string text;
    if (writing.Bom == EBomMode::Always || (writing.Bom == EBomMode::Auto && m_Model.HasBom))
        text += Utf8Bom;

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
