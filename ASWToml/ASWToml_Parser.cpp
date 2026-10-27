/* **************************************************************************
ASWToml_Parser.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "ASWToml_Parser.h"
//---------------------------------------------------------------------------
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
//---------------------------------------------------------------------------
#include "ASWToml_Scalars.h"
#include "ASWToml_Utils.h"
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

namespace Detail
{

namespace
{

/////////////////////////////////////////////////////////////////////////////
// TParser
//
// Reads a TOML text (valid UTF-8, without a byte order mark) into blocks, one expression at a time (a line, or the
// lines of a value that spans several), by recursive descent: an array or an inline table reads the values in it,
// after checking how deep it is. Each Parse...() method starts at m_Position and leaves it after what it read; on
// failure, the result has the Line and Column of the error.
/////////////////////////////////////////////////////////////////////////////
class TParser
{
private:
    std::string_view m_Text;
    std::size_t m_MaxDepth; // Reading.MaxNestingDepth
    std::size_t m_Position = 0;
    std::size_t m_TableDepth = 0; // How deep the table of the current header is: 0 for the root table

    // An error with 'code' and 'message' at 'offset'
    TASWTomlResult Error(ETomlCode code, std::string message, std::size_t offset) const;

    // The size of the line break at m_Position: 1 for "\n", 2 for "\r\n", 0 for anything else
    std::size_t LineBreakSize() const noexcept;

    // The array at m_Position ('['), which is 'depth' deep
    TASWTomlResult ParseArray(std::size_t depth, TDocValue& value);

    // The comment at m_Position ('#'), added to 'text', up to the line break or the end of the text
    TASWTomlResult ParseComment(std::string& text);

    // The header at m_Position ('['), with the rest of its line
    TASWTomlResult ParseHeader(TDocHeader& header);

    // The inline table at m_Position ('{'), which is 'depth' deep
    TASWTomlResult ParseInlineTable(std::size_t depth, TDocValue& value);

    // The key at m_Position, with the blanks before it, in a table 'depth' deep. Each part but the last is a table
    // one deeper than the one before it; the last one is one ('lastPartLevels' 1, a header), two (2, the header of an
    // element of an array of tables: the array and the table) or no levels (0, the key of a value) deeper.
    TASWTomlResult ParseKey(std::size_t depth, std::size_t lastPartLevels, TDocKey& key);

    // The key and value at m_Position, with the blanks before the key, in a table 'depth' deep
    TASWTomlResult ParseKeyValue(std::size_t depth, TDocKeyValue& keyValue);

    // The rest of the line at m_Position: blanks and a comment, added to 'text', then the line break (or the end of
    // the text), skipped and set in 'lineBreak'. Fails when something else comes first, saying that 'what' must be
    // followed by a comment or the end of the line.
    TASWTomlResult ParseLineEnd(char const* what, std::string& text, ELineBreak& lineBreak);

    // The blanks, comments and line breaks at m_Position, added to 'trivia'
    TASWTomlResult ParseTrivia(std::string& trivia);

    // The value at m_Position, in a table or array 'depth' deep
    TASWTomlResult ParseValue(std::size_t depth, TDocValue& value);

    // The blanks at m_Position, which are skipped
    std::string ReadBlanks();

    // The NestingTooDeep error of 'what' (e.g. "array"), at 'offset', which is 'depth' deep
    TASWTomlResult TooDeep(std::string const& what, std::size_t depth, std::size_t offset) const;

public:
    TParser(std::string_view text, std::size_t maxDepth);

    // Reads the whole text, adding its entries and blocks to 'blocks', which holds the (empty) block without a header
    TASWTomlResult Parse(TDocModel::TBlockVector& blocks);
};

//---------------------------------------------------------------------------

ELineBreak MostCommonLineBreak(std::string_view text) noexcept;

//---------------------------------------------------------------------------
/*
    MostCommonLineBreak

    The line break 'text' has most often (the one found first in a tie); None when it has none
*/
ELineBreak MostCommonLineBreak(std::string_view text) noexcept
{
    std::size_t lfCount = 0;
    std::size_t crlfCount = 0;
    ELineBreak first = ELineBreak::None;

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != '\n')
            continue;

        bool const crlf = i > 0 && text[i - 1] == '\r';
        if (crlf)
            ++crlfCount;
        else
            ++lfCount;

        if (first == ELineBreak::None)
            first = crlf ? ELineBreak::CRLF : ELineBreak::LF;
    }

    if (lfCount > crlfCount)
        return ELineBreak::LF;

    if (crlfCount > lfCount)
        return ELineBreak::CRLF;

    return first;
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TParser
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TParser::TParser(std::string_view text, std::size_t maxDepth)
    : m_Text(text),
      m_MaxDepth(maxDepth)
{
}
//---------------------------------------------------------------------------
TASWTomlResult TParser::Error(ETomlCode code, std::string message, std::size_t offset) const
{
    TASWTomlResult result;
    result.Code = code;
    result.Message = std::move(message);
    SetLineAndColumn(result, m_Text, offset);

    return result;
}
//---------------------------------------------------------------------------
std::size_t TParser::LineBreakSize() const noexcept
{
    if (m_Position < m_Text.size() && m_Text[m_Position] == '\n')
        return 1;

    if (m_Position + 1 < m_Text.size() && m_Text[m_Position] == '\r' && m_Text[m_Position + 1] == '\n')
        return 2;

    return 0;
}
//---------------------------------------------------------------------------
TASWTomlResult TParser::Parse(TDocModel::TBlockVector& blocks)
{
    while (m_Position < m_Text.size())
    {
        std::size_t const lineStart = m_Position;
        std::string indent = ReadBlanks();
        char const first = m_Position < m_Text.size() ? m_Text[m_Position] : '\n';

        if (first == '[')
        {
            TDocBlock& block = blocks.emplace_back();
            block.HasHeader = true;
            block.Header.Indent = std::move(indent);

            TASWTomlResult result = ParseHeader(block.Header);
            if (!result.IsOk())
                return result;

            continue;
        }

        TDocEntry& entry = blocks.back().Entries.emplace_back();
        if (first == '#' || first == '\n' || first == '\r')
        {
            // A blank or comment line, which ParseLineEnd() reads whole (failing only for a control character in the
            // comment or a carriage return without a line feed)
            entry.Kind = first == '#' ? EEntryKind::Comment : EEntryKind::Blank;
            entry.Text = std::move(indent);

            TASWTomlResult result = ParseLineEnd("A comment", entry.Text, entry.LineBreak);
            if (!result.IsOk())
                return result;

            continue;
        }

        // A key and its value, the key's first part with the indentation before it
        entry.Kind = EEntryKind::KeyValue;
        m_Position = lineStart;

        TASWTomlResult result = ParseKeyValue(m_TableDepth, entry.KeyValue);
        if (result.IsOk())
            result = ParseLineEnd("A value", entry.Text, entry.LineBreak);

        if (!result.IsOk())
            return result;
    }

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult TParser::ParseArray(std::size_t depth, TDocValue& value)
{
    std::size_t const open = m_Position;
    if (depth > m_MaxDepth)
        return TooDeep("array", depth, open);

    value.Kind = EValueKind::Array;
    ++m_Position;

    while (true)
    {
        std::string trivia;
        TASWTomlResult result = ParseTrivia(trivia);
        if (!result.IsOk())
            return result;

        if (m_Position >= m_Text.size())
            return Error(ETomlCode::ParseFailed, "The array has no closing ']'.", open);

        if (m_Text[m_Position] == ']')
        {
            value.Close = std::move(trivia);
            ++m_Position;

            return result;
        }

        if (m_Text[m_Position] == ',')
            return Error(ETomlCode::ParseFailed, "A comma in an array must follow a value.", m_Position);

        TDocElement& element = value.Elements.emplace_back();
        element.Before = std::move(trivia);

        result = ParseValue(depth, element.Value);
        if (!result.IsOk())
            return result;

        std::string after;
        result = ParseTrivia(after);
        if (!result.IsOk())
            return result;

        if (m_Position >= m_Text.size())
            return Error(ETomlCode::ParseFailed, "The array has no closing ']'.", open);

        if (m_Text[m_Position] == ',')
        {
            element.After = std::move(after);
            element.Comma = true;
            ++m_Position;
            continue;
        }

        if (m_Text[m_Position] == ']')
        {
            value.Close = std::move(after);
            ++m_Position;

            return result;
        }

        return Error(ETomlCode::ParseFailed, "The values of an array must be separated by commas.", m_Position);
    }
}
//---------------------------------------------------------------------------
TASWTomlResult TParser::ParseComment(std::string& text)
{
    std::size_t const start = m_Position;

    while (m_Position < m_Text.size() && m_Text[m_Position] != '\n' && m_Text[m_Position] != '\r')
    {
        unsigned char const byte = static_cast<unsigned char>(m_Text[m_Position]);
        if ((byte < 0x20 && byte != '\t') || byte == 0x7F)
        {
            char const* const hex = "0123456789ABCDEF";
            std::string message = "A comment can't hold the control character U+00";
            message += hex[byte >> 4];
            message += hex[byte & 0x0F];
            message += '.';

            return Error(ETomlCode::ParseFailed, std::move(message), m_Position);
        }

        ++m_Position;
    }

    text += m_Text.substr(start, m_Position - start);

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult TParser::ParseHeader(TDocHeader& header)
{
    header.IsArray = m_Text.substr(m_Position, 2) == "[[";
    m_Position += header.IsArray ? 2 : 1;

    TASWTomlResult result = ParseKey(0, header.IsArray ? 2 : 1, header.Key);
    if (!result.IsOk())
        return result;

    std::string_view const close = header.IsArray ? "]]" : "]";
    if (m_Text.substr(m_Position, close.size()) != close)
    {
        if (header.IsArray)
            return Error(ETomlCode::ParseFailed, "The header of an array of tables must end with ']]'.", m_Position);

        return Error(ETomlCode::ParseFailed, "A table header must end with ']'.", m_Position);
    }

    m_Position += close.size();
    m_TableDepth = header.Key.Parts.size() - 1 + (header.IsArray ? 2 : 1);

    return ParseLineEnd("A table header", header.Text, header.LineBreak);
}
//---------------------------------------------------------------------------
TASWTomlResult TParser::ParseInlineTable(std::size_t depth, TDocValue& value)
{
    std::size_t const open = m_Position;
    if (depth > m_MaxDepth)
        return TooDeep("inline table", depth, open);

    value.Kind = EValueKind::InlineTable;
    ++m_Position;

    while (true)
    {
        std::string trivia;
        TASWTomlResult result = ParseTrivia(trivia);
        if (!result.IsOk())
            return result;

        if (m_Position >= m_Text.size())
            return Error(ETomlCode::ParseFailed, "The inline table has no closing '}'.", open);

        if (m_Text[m_Position] == '}')
        {
            value.Close = std::move(trivia);
            ++m_Position;

            return result;
        }

        if (m_Text[m_Position] == ',')
        {
            return Error(ETomlCode::ParseFailed, "A comma in an inline table must follow a key and its value.",
                m_Position);
        }

        TDocInlineEntry& entry = value.Entries.emplace_back();
        entry.Before = std::move(trivia);

        result = ParseKeyValue(depth, entry.KeyValue);
        if (!result.IsOk())
            return result;

        std::string after;
        result = ParseTrivia(after);
        if (!result.IsOk())
            return result;

        if (m_Position >= m_Text.size())
            return Error(ETomlCode::ParseFailed, "The inline table has no closing '}'.", open);

        if (m_Text[m_Position] == ',')
        {
            entry.After = std::move(after);
            entry.Comma = true;
            ++m_Position;
            continue;
        }

        if (m_Text[m_Position] == '}')
        {
            value.Close = std::move(after);
            ++m_Position;

            return result;
        }

        return Error(ETomlCode::ParseFailed, "The keys of an inline table must be separated by commas.",
            m_Position);
    }
}
//---------------------------------------------------------------------------
TASWTomlResult TParser::ParseKey(std::size_t depth, std::size_t lastPartLevels, TDocKey& key)
{
    while (true)
    {
        TDocKeyPart& part = key.Parts.emplace_back();
        part.Before = ReadBlanks();

        std::size_t const start = m_Position;
        TASWTomlResult result = ReadKey(m_Text, m_Position, part.Name);
        if (!result.IsOk())
            return result;

        part.Text = m_Text.substr(start, m_Position - start);
        part.After = ReadBlanks();

        // How deep the part's table is (the first part's is one deeper than the table the key is in)
        std::size_t const partDepth = depth + key.Parts.size();
        if (m_Position < m_Text.size() && m_Text[m_Position] == '.')
        {
            if (partDepth > m_MaxDepth)
                return TooDeep("table " + MessageText(part.Name), partDepth, start);

            ++m_Position;
            continue;
        }

        if (lastPartLevels > 0 && partDepth - 1 + lastPartLevels > m_MaxDepth)
            return TooDeep("table " + MessageText(part.Name), partDepth - 1 + lastPartLevels, start);

        return result;
    }
}
//---------------------------------------------------------------------------
TASWTomlResult TParser::ParseKeyValue(std::size_t depth, TDocKeyValue& keyValue)
{
    TASWTomlResult result = ParseKey(depth, 0, keyValue.Key);
    if (!result.IsOk())
        return result;

    if (m_Position >= m_Text.size() || m_Text[m_Position] != '=')
    {
        return Error(ETomlCode::ParseFailed, "A key must be followed by '=' and a value, or by '.' and another key.",
            m_Position);
    }

    ++m_Position;
    keyValue.BeforeValue = ReadBlanks();

    return ParseValue(depth + keyValue.Key.Parts.size() - 1, keyValue.Value);
}
//---------------------------------------------------------------------------
TASWTomlResult TParser::ParseLineEnd(char const* what, std::string& text, ELineBreak& lineBreak)
{
    text += ReadBlanks();
    if (m_Position < m_Text.size() && m_Text[m_Position] == '#')
    {
        TASWTomlResult result = ParseComment(text);
        if (!result.IsOk())
            return result;
    }

    if (m_Position >= m_Text.size())
    {
        lineBreak = ELineBreak::None;
        return TASWTomlResult();
    }

    std::size_t const size = LineBreakSize();
    if (size != 0)
    {
        lineBreak = size == 1 ? ELineBreak::LF : ELineBreak::CRLF;
        m_Position += size;

        return TASWTomlResult();
    }

    if (m_Text[m_Position] == '\r')
        return Error(ETomlCode::ParseFailed, "A carriage return must be followed by a line feed.", m_Position);

    return Error(ETomlCode::ParseFailed, std::string(what) + " must be followed by a comment or the end of the line.",
        m_Position);
}
//---------------------------------------------------------------------------
TASWTomlResult TParser::ParseTrivia(std::string& trivia)
{
    while (true)
    {
        trivia += ReadBlanks();
        if (m_Position < m_Text.size() && m_Text[m_Position] == '#')
        {
            TASWTomlResult result = ParseComment(trivia);
            if (!result.IsOk())
                return result;
        }

        std::size_t const size = LineBreakSize();
        if (size == 0)
            break;

        trivia += m_Text.substr(m_Position, size);
        m_Position += size;
    }

    if (m_Position < m_Text.size() && m_Text[m_Position] == '\r')
        return Error(ETomlCode::ParseFailed, "A carriage return must be followed by a line feed.", m_Position);

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult TParser::ParseValue(std::size_t depth, TDocValue& value)
{
    char const first = m_Position < m_Text.size() ? m_Text[m_Position] : '\0';

    if (first == '[')
        return ParseArray(depth + 1, value);

    if (first == '{')
        return ParseInlineTable(depth + 1, value);

    std::size_t const start = m_Position;
    TASWTomlResult result = ReadScalar(m_Text, m_Position, value.Scalar);
    if (result.IsOk())
        value.Text = m_Text.substr(start, m_Position - start);

    return result;
}
//---------------------------------------------------------------------------
std::string TParser::ReadBlanks()
{
    std::size_t const start = m_Position;
    while (m_Position < m_Text.size() && IsBlank(m_Text[m_Position]))
        ++m_Position;

    return std::string(m_Text.substr(start, m_Position - start));
}
//---------------------------------------------------------------------------
TASWTomlResult TParser::TooDeep(std::string const& what, std::size_t depth, std::size_t offset) const
{
    return Error(ETomlCode::NestingTooDeep, "The " + what + " is nested " + std::to_string(depth) +
        " deep, deeper than Reading.MaxNestingDepth (" + std::to_string(m_MaxDepth) + ") allows.", offset);
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
TASWTomlResult ParseDocument(std::string_view text, TASWTomlReadingOptions const& options, TDocModel& model)
{
    TASWTomlResult result;

    EBom const bom = DetectBom(text);
    if (bom != EBom::None && bom != EBom::Utf8)
    {
        result.Code = ETomlCode::UnsupportedEncoding;
        result.Message = "The text starts with a UTF-16 or UTF-32 byte order mark; only UTF-8 text can be read.";

        return result;
    }

    std::string_view const body = text.substr(BomSize(bom));
    std::size_t const invalid = FindInvalidUtf8(body);
    if (invalid != std::string_view::npos)
    {
        result.Code = ETomlCode::InvalidUtf8;
        result.Message = "The text holds bytes that aren't valid UTF-8.";
        SetLineAndColumn(result, body, invalid);

        return result;
    }

    // Parsed into a new model, which replaces 'model' only when the whole text is read
    TDocModel parsed;
    parsed.HasBom = bom == EBom::Utf8;
    parsed.LineBreak = MostCommonLineBreak(body);

    TParser parser(body, options.MaxNestingDepth);
    result = parser.Parse(parsed.Blocks);
    if (!result.IsOk())
        return result;

    model = std::move(parsed);

    return result;
}
//---------------------------------------------------------------------------

} // namespace Detail

} // namespace ASWToml
