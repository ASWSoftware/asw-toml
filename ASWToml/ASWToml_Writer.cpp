/* **************************************************************************
ASWToml_Writer.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "ASWToml_Writer.h"
//---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWToml_Scalars.h"
#include "ASWToml_Utils.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

namespace Detail
{

namespace
{

void AppendLineBreak(TWriteStyle const& style, std::string& text);
bool HasLineBreakFirst(std::string_view oldText) noexcept;
bool IsArrayOfTables(TASWTomlValue const& value) noexcept;
TASWTomlResult NotATable();
TASWTomlResult TooDeep();
TASWTomlResult WriteBlocks(std::string_view path, bool isElement, bool blankLineFirst, TASWTomlValue const& table,
    TWriteStyle const& style, std::size_t depth, std::size_t start, std::string& text);
TASWTomlResult WriteDotted(std::string_view indent, std::string_view prefix, TASWTomlValue const& table,
    TWriteStyle const& style, std::size_t depth, std::string& text);
TASWTomlResult WriteInline(TASWTomlValue const& value, TWriteStyle const& style, std::size_t depth, std::string& text);
TASWTomlResult WriteScalar(TASWTomlValue const& value, TASWTomlValueOptions const& options, std::string& text);

//---------------------------------------------------------------------------
/*
    AppendLineBreak

    Appends the line break of 'style' to 'text' (LF when it is None)
*/
void AppendLineBreak(TWriteStyle const& style, std::string& text)
{
    text += style.LineBreak == ELineBreak::None ? "\n" : LineBreakText(style.LineBreak);
}
//---------------------------------------------------------------------------
/*
    HasLineBreakFirst

    Whether 'oldText', a multi-line string, has a line break right after its opening delimiter
*/
bool HasLineBreakFirst(std::string_view oldText) noexcept
{
    std::string_view const rest = oldText.substr(3);

    return rest.starts_with('\n') || rest.starts_with("\r\n");
}
//---------------------------------------------------------------------------
/*
    IsArrayOfTables

    Whether 'value' is written as an array of tables where a header can go: an array that isn't empty and holds only
    tables
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
    NotATable

    The InvalidValue failure of a writer of tables given another value
*/
TASWTomlResult NotATable()
{
    TASWTomlResult result;
    result.Code = ETomlCode::InvalidValue;
    result.Message = "The value can't be written as a table: it isn't one.";

    return result;
}
//---------------------------------------------------------------------------
/*
    TooDeep

    The NestingTooDeep failure of a value nested deeper than TWriteStyle::MaxDepth
*/
TASWTomlResult TooDeep()
{
    TASWTomlResult result;
    result.Code = ETomlCode::NestingTooDeep;
    result.Message = "The value can't be written there: its tables and arrays would be nested deeper than "
        "Reading.MaxNestingDepth allows.";

    return result;
}
//---------------------------------------------------------------------------
/*
    WriteBlocks

    WriteTableBlocks() for 'table' inside 'depth' levels of the text being written, which started at offset 'start'
    of 'text'
*/
TASWTomlResult WriteBlocks(std::string_view path, bool isElement, bool blankLineFirst, TASWTomlValue const& table,
    TWriteStyle const& style, std::size_t depth, std::size_t start, std::string& text)
{
    TASWTomlValue::TKeyValueVector const* const entries = table.AsTable();
    if (entries == nullptr)
        return NotATable();

    // The root table is no level, as Reading.MaxNestingDepth counts
    if (!path.empty())
        ++depth;

    if (depth > style.MaxDepth)
        return TooDeep();

    bool hasLines = false;
    for (TASWTomlKeyValue const& entry : *entries)
    {
        if (!entry.Value.IsTable() && !IsArrayOfTables(entry.Value))
            hasLines = true;
    }

    if (!path.empty() && (isElement || hasLines || entries->empty()))
    {
        if (style.BlankLineBeforeTable && (blankLineFirst || text.size() > start))
            AppendLineBreak(style, text);

        text += isElement ? "[[" : "[";
        text += path;
        text += isElement ? "]]" : "]";
        AppendLineBreak(style, text);
    }

    TASWTomlResult result;
    for (TASWTomlKeyValue const& entry : *entries)
    {
        if (entry.Value.IsTable() || IsArrayOfTables(entry.Value))
            continue;

        result = WriteKey(entry.Key, text);
        if (result.IsOk())
        {
            text += " = ";
            result = WriteInline(entry.Value, style, depth, text);
        }

        if (!result.IsOk())
            return result;

        AppendLineBreak(style, text);
    }

    for (TASWTomlKeyValue const& entry : *entries)
    {
        bool const isArrayOfTables = IsArrayOfTables(entry.Value);
        if (!entry.Value.IsTable() && !isArrayOfTables)
            continue;

        std::string subPath(path);
        if (!subPath.empty())
            subPath += '.';

        result = WriteKey(entry.Key, subPath);
        if (!result.IsOk())
            return result;

        if (!isArrayOfTables)
        {
            result = WriteBlocks(subPath, false, blankLineFirst, entry.Value, style, depth, start, text);
        }
        else
        {
            // The array is one level, and each element one more
            for (TASWTomlValue const& element : *entry.Value.AsArray())
            {
                result = WriteBlocks(subPath, true, blankLineFirst, element, style, depth + 1, start, text);
                if (!result.IsOk())
                    break;
            }
        }

        if (!result.IsOk())
            return result;
    }

    return result;
}
//---------------------------------------------------------------------------
/*
    WriteDotted

    WriteDottedKeys() for 'table' inside 'depth' levels of the text being written
*/
TASWTomlResult WriteDotted(std::string_view indent, std::string_view prefix, TASWTomlValue const& table,
    TWriteStyle const& style, std::size_t depth, std::string& text)
{
    TASWTomlValue::TKeyValueVector const* const entries = table.AsTable();
    if (entries == nullptr)
        return NotATable();

    if (depth + 1 > style.MaxDepth)
        return TooDeep();

    if (entries->empty())
    {
        text += indent;
        text += prefix;
        text += " = {}";
        AppendLineBreak(style, text);

        return TASWTomlResult();
    }

    TASWTomlResult result;
    for (TASWTomlKeyValue const& entry : *entries)
    {
        std::string key(prefix);
        key += '.';
        result = WriteKey(entry.Key, key);
        if (!result.IsOk())
            return result;

        if (entry.Value.IsTable())
        {
            result = WriteDotted(indent, key, entry.Value, style, depth + 1, text);
        }
        else
        {
            text += indent;
            text += key;
            text += " = ";
            result = WriteInline(entry.Value, style, depth + 1, text);
            AppendLineBreak(style, text);
        }

        if (!result.IsOk())
            return result;
    }

    return result;
}
//---------------------------------------------------------------------------
/*
    WriteInline

    WriteValue() for 'value' inside 'depth' levels of the text being written
*/
TASWTomlResult WriteInline(TASWTomlValue const& value, TWriteStyle const& style, std::size_t depth, std::string& text)
{
    TASWTomlValue::TValueVector const* const array = value.AsArray();
    TASWTomlValue::TKeyValueVector const* const table = value.AsTable();
    if (array == nullptr && table == nullptr)
        return WriteScalar(value, style.Values, text);

    if (depth + 1 > style.MaxDepth)
        return TooDeep();

    TASWTomlResult result;
    if (array != nullptr)
    {
        text += '[';
        for (std::size_t i = 0; i < array->size() && result.IsOk(); ++i)
        {
            if (i > 0)
                text += ", ";

            result = WriteInline((*array)[i], style, depth + 1, text);
        }

        text += ']';

        return result;
    }

    if (table->empty())
    {
        text += "{}";

        return result;
    }

    text += "{ ";
    for (std::size_t i = 0; i < table->size() && result.IsOk(); ++i)
    {
        if (i > 0)
            text += ", ";

        result = WriteKey((*table)[i].Key, text);
        if (result.IsOk())
        {
            text += " = ";
            result = WriteInline((*table)[i].Value, style, depth + 1, text);
        }
    }

    text += " }";

    return result;
}
//---------------------------------------------------------------------------
/*
    WriteScalar

    Appends 'value', a scalar, to 'text' as the scalar writers write it, a string and an integer in the form 'options'
    asks for
*/
TASWTomlResult WriteScalar(TASWTomlValue const& value, TASWTomlValueOptions const& options, std::string& text)
{
    switch (value.GetType())
    {
        case ETomlType::String:
            return WriteString(*value.AsString(), text, options.StringStyle);

        case ETomlType::Integer:
            WriteInteger(*value.AsInteger(), text, options.IntegerBase);
            break;

        case ETomlType::Float:
            WriteFloat(*value.AsFloat(), text);
            break;

        case ETomlType::Boolean:
            WriteBoolean(*value.AsBoolean(), text);
            break;

        case ETomlType::OffsetDateTime:
            return WriteOffsetDateTime(*value.AsOffsetDateTime(), text);

        case ETomlType::LocalDateTime:
            return WriteLocalDateTime(*value.AsLocalDateTime(), text);

        case ETomlType::LocalDate:
            return WriteLocalDate(*value.AsLocalDate(), text);

        case ETomlType::LocalTime:
            return WriteLocalTime(*value.AsLocalTime(), text);

        case ETomlType::Array:
        case ETomlType::Table:
            break;
    }

    return TASWTomlResult();
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
TASWTomlResult WriteDottedKeys(std::string_view indent, std::string_view prefix, TASWTomlValue const& table,
    TWriteStyle const& style, std::string& text)
{
    return WriteDotted(indent, prefix, table, style, 0, text);
}
//---------------------------------------------------------------------------
TASWTomlResult WriteKeyValueLine(std::string_view indent, std::string_view key, TASWTomlValue const& value,
    TWriteStyle const& style, std::string& text)
{
    text += indent;
    text += key;
    text += " = ";

    TASWTomlResult result = WriteInline(value, style, 0, text);
    if (result.IsOk())
        AppendLineBreak(style, text);

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult WriteTableBlocks(std::string_view path, bool isElement, bool blankLineFirst, TASWTomlValue const& table,
    TWriteStyle const& style, std::string& text)
{
    return WriteBlocks(path, isElement, blankLineFirst, table, style, 0, text.size(), text);
}
//---------------------------------------------------------------------------
TASWTomlResult WriteValue(TASWTomlValue const& value, TWriteStyle const& style, std::string& text)
{
    return WriteInline(value, style, 0, text);
}
//---------------------------------------------------------------------------
TASWTomlResult WriteValueLike(TASWTomlValue const& value, std::string_view oldText, TWriteStyle const& style,
    std::string& text)
{
    std::string const* const string = value.AsString();
    if (string != nullptr && style.Values.StringStyle == EStringStyle::Auto)
    {
        if (oldText.starts_with("\"\"\"") || oldText.starts_with("'''"))
        {
            return WriteMultiLineString(*string, text, oldText.front() == '\'', style.LineBreak,
                HasLineBreakFirst(oldText));
        }

        if (oldText.starts_with('"'))
            return WriteString(*string, text, EStringStyle::Basic);

        if (oldText.starts_with('\''))
            return WriteString(*string, text, EStringStyle::Literal);
    }

    std::int64_t const* const integer = value.AsInteger();
    if (integer != nullptr && *integer >= 0 && style.Values.IntegerBase == EIntegerBase::Auto)
    {
        if (oldText.starts_with("0x"))
        {
            WriteInteger(*integer, text, EIntegerBase::Hexadecimal);

            return TASWTomlResult();
        }

        if (oldText.starts_with("0o"))
        {
            WriteInteger(*integer, text, EIntegerBase::Octal);

            return TASWTomlResult();
        }

        if (oldText.starts_with("0b"))
        {
            WriteInteger(*integer, text, EIntegerBase::Binary);

            return TASWTomlResult();
        }
    }

    return WriteInline(value, style, 0, text);
}
//---------------------------------------------------------------------------

} // namespace Detail

} // namespace ASWToml
