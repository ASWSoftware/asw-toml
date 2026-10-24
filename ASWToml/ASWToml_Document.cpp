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


void AppendKey(Detail::TDocKey const& key, std::string& text);
void AppendKeyValue(Detail::TDocKeyValue const& keyValue, std::string& text);
void AppendValue(Detail::TDocValue const& value, std::string& text);
std::string ConvertLineBreaks(std::string const& text, ELineBreak lineBreak);
Detail::TDocIndex EmptyIndex();

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
