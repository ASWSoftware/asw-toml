/* **************************************************************************
ASWToml_Json.cpp
Author: Anthony S. West - ASW Software

A TOML file reader/writer library.

Requires C++ 20 or higher.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "ASWToml_Json.h"
//---------------------------------------------------------------------------
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
//---------------------------------------------------------------------------
#include "ASWToml_Converters.h"
#include "ASWToml_Options.h"
#include "ASWToml_Scalars.h"
#include "ASWToml_Utils.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

namespace
{

/////////////////////////////////////////////////////////////////////////////
// TJsonWriter
//
// Writes a value as JSON text in the form of its options (see ToJson()), into a text of its own, so a failure leaves
// the caller's as it was. A failure is kept with the Path of the value that failed, which each table and array adds
// its part to on the way back up. The recursive calls (Write(), WriteArray() and WriteTable()) keep their frames small
// (the value, the depth and a position), with the failures' messages made in helpers, so a level takes little stack.
/////////////////////////////////////////////////////////////////////////////
class TJsonWriter
{
private:
    TASWTomlJsonOptions const& m_Options;
    std::string m_Json;
    TASWTomlResult m_Failure;

    bool Fail(ETomlCode code, std::string message);
    bool FailAtElement(std::size_t index);
    bool FailAtKey(std::string_view key);
    bool FailInvalidKey();
    bool FailTooDeep();
    bool WriteArray(TASWTomlValue::TValueVector const& array, std::size_t depth);
    void WriteLineBreak(std::size_t depth);
    bool WriteScalar(TASWTomlValue const& value);
    bool WriteTable(TASWTomlValue::TKeyValueVector const& table, std::size_t depth);
    void WriteTag(char const* type);

public:
    explicit TJsonWriter(TASWTomlJsonOptions const& options);

    // The failure of the last Write() that failed
    TASWTomlResult& GetFailure() noexcept;

    // The text written
    std::string& GetJson() noexcept;

    // Appends 'value' to the text, at 'depth' levels of the value ToJson() writes (0 for that value, 1 for a value in
    // it, ...), which is also how far it is indented; false (with the failure kept) when it can't be written
    bool Write(TASWTomlValue const& value, std::size_t depth);
};

//---------------------------------------------------------------------------

void AppendString(std::string_view value, std::string& json);
TASWTomlResult InvalidOptions(std::string message);
char const* TaggedType(ETomlType type) noexcept;
TASWTomlResult TomlText(TASWTomlValue const& value, std::string& text);

//---------------------------------------------------------------------------
/*
    AppendString

    Appends 'value', valid UTF-8, to 'json' as a JSON string: '"' and '\' escaped, \b, \t, \n, \f and \r for those
    control characters, \u00XX (upper-case hex) for the other ones from U+0000 to U+001F, and the rest as it is
*/
void AppendString(std::string_view value, std::string& json)
{
    char const* const hex = "0123456789ABCDEF";

    json += '"';

    for (char const c : value)
    {
        unsigned char const byte = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\')
        {
            json += '\\';
            json += c;
        }
        else if (c == '\b')
        {
            json += "\\b";
        }
        else if (c == '\t')
        {
            json += "\\t";
        }
        else if (c == '\n')
        {
            json += "\\n";
        }
        else if (c == '\f')
        {
            json += "\\f";
        }
        else if (c == '\r')
        {
            json += "\\r";
        }
        else if (byte < 0x20)
        {
            json += "\\u00";
            json += hex[byte >> 4];
            json += hex[byte & 0x0F];
        }
        else
        {
            json += c;
        }
    }

    json += '"';
}
//---------------------------------------------------------------------------
/*
    InvalidOptions

    An InvalidOptions result with 'message'
*/
TASWTomlResult InvalidOptions(std::string message)
{
    TASWTomlResult result;
    result.Code = ETomlCode::InvalidOptions;
    result.Message = std::move(message);

    return result;
}
//---------------------------------------------------------------------------
/*
    TaggedType

    toml-test's name of a scalar type, e.g. datetime-local; "" for an array or a table
*/
char const* TaggedType(ETomlType type) noexcept
{
    switch (type)
    {
        case ETomlType::String:
            return "string";

        case ETomlType::Integer:
            return "integer";

        case ETomlType::Float:
            return "float";

        case ETomlType::Boolean:
            return "bool";

        case ETomlType::OffsetDateTime:
            return "datetime";

        case ETomlType::LocalDateTime:
            return "datetime-local";

        case ETomlType::LocalDate:
            return "date-local";

        case ETomlType::LocalTime:
            return "time-local";

        case ETomlType::Array:
        case ETomlType::Table:
            break;
    }

    return "";
}
//---------------------------------------------------------------------------
/*
    TomlText

    Appends 'value', a scalar that isn't a string, to 'text' as TOML writes it: an integer in decimal, a float (inf,
    -inf, nan and -nan included), true or false, or a date or time in RFC 3339; InvalidValue, appending nothing, for a
    date or time that isn't valid
*/
TASWTomlResult TomlText(TASWTomlValue const& value, std::string& text)
{
    if (std::int64_t const* const integer = value.AsInteger())
        Detail::WriteInteger(*integer, text, EIntegerBase::Decimal);
    else if (double const* const number = value.AsFloat())
        Detail::WriteFloat(*number, text);
    else if (bool const* const flag = value.AsBoolean())
        Detail::WriteBoolean(*flag, text);
    else if (TASWTomlOffsetDateTime const* const offsetDateTime = value.AsOffsetDateTime())
        return Detail::WriteOffsetDateTime(*offsetDateTime, text);
    else if (TASWTomlLocalDateTime const* const localDateTime = value.AsLocalDateTime())
        return Detail::WriteLocalDateTime(*localDateTime, text);
    else if (std::chrono::year_month_day const* const date = value.AsLocalDate())
        return Detail::WriteLocalDate(*date, text);
    else if (TASWTomlLocalTime const* const time = value.AsLocalTime())
        return Detail::WriteLocalTime(*time, text);

    return TASWTomlResult();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TJsonWriter
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TJsonWriter::TJsonWriter(TASWTomlJsonOptions const& options)
    : m_Options(options)
{
}
//---------------------------------------------------------------------------
bool TJsonWriter::Fail(ETomlCode code, std::string message)
{
    m_Failure.Code = code;
    m_Failure.Message = std::move(message);

    return false;
}
//---------------------------------------------------------------------------
bool TJsonWriter::FailAtElement(std::size_t index)
{
    m_Failure = Detail::ElementFailure(std::move(m_Failure), index);

    return false;
}
//---------------------------------------------------------------------------
bool TJsonWriter::FailAtKey(std::string_view key)
{
    m_Failure = Detail::KeyFailure(std::move(m_Failure), key);

    return false;
}
//---------------------------------------------------------------------------
bool TJsonWriter::FailInvalidKey()
{
    return Fail(ETomlCode::InvalidValue, "The key can't be written: it isn't valid UTF-8.");
}
//---------------------------------------------------------------------------
bool TJsonWriter::FailTooDeep()
{
    return Fail(ETomlCode::NestingTooDeep, "The value can't be written as JSON: it is inside more than " +
        std::to_string(TASWTomlReadingOptions::MaxNestingDepthLimit) + " tables and arrays, deeper than "
        "Reading.MaxNestingDepth lets a document's values be.");
}
//---------------------------------------------------------------------------
TASWTomlResult& TJsonWriter::GetFailure() noexcept
{
    return m_Failure;
}
//---------------------------------------------------------------------------
std::string& TJsonWriter::GetJson() noexcept
{
    return m_Json;
}
//---------------------------------------------------------------------------
bool TJsonWriter::Write(TASWTomlValue const& value, std::size_t depth)
{
    // 'depth' counts the value ToJson() writes as a level, and the limit doesn't (as Reading.MaxNestingDepth doesn't
    // count the root table): a value inside 128 tables and arrays of it is at depth 129
    if (depth > TASWTomlReadingOptions::MaxNestingDepthLimit + 1)
        return FailTooDeep();

    if (TASWTomlValue::TKeyValueVector const* const table = value.AsTable())
        return WriteTable(*table, depth);

    if (TASWTomlValue::TValueVector const* const array = value.AsArray())
        return WriteArray(*array, depth);

    return WriteScalar(value);
}
//---------------------------------------------------------------------------
bool TJsonWriter::WriteArray(TASWTomlValue::TValueVector const& array, std::size_t depth)
{
    m_Json += '[';

    for (std::size_t i = 0; i < array.size(); ++i)
    {
        if (i != 0)
            m_Json += ',';

        WriteLineBreak(depth + 1);
        if (!Write(array[i], depth + 1))
            return FailAtElement(i);
    }

    if (!array.empty())
        WriteLineBreak(depth);

    m_Json += ']';

    return true;
}
//---------------------------------------------------------------------------
void TJsonWriter::WriteLineBreak(std::size_t depth)
{
    if (m_Options.Indent == 0)
        return;

    m_Json += '\n';
    m_Json.append(depth * m_Options.Indent, ' ');
}
//---------------------------------------------------------------------------
bool TJsonWriter::WriteScalar(TASWTomlValue const& value)
{
    bool const tagged = m_Options.Style == EJsonStyle::Tagged;

    if (std::string const* const string = value.AsString())
    {
        if (!Detail::IsValidUtf8(*string))
            return Fail(ETomlCode::InvalidValue, "The string can't be written: it isn't valid UTF-8.");

        if (tagged)
            WriteTag("string");

        AppendString(*string, m_Json);
        if (tagged)
            m_Json += '}';

        return true;
    }

    // Every other scalar's text has only letters, digits and "+-.:", which a JSON string holds as they are
    std::string text;
    TASWTomlResult result = TomlText(value, text);
    if (!result.IsOk())
        return Fail(result.Code, std::move(result.Message));

    if (tagged)
    {
        WriteTag(TaggedType(value.GetType()));
        m_Json += '"';
        m_Json += text;
        m_Json += "\"}";

        return true;
    }

    double const* const number = value.AsFloat();
    if (number != nullptr && !std::isfinite(*number))
    {
        if (m_Options.NonFinite == EJsonNonFinite::Null)
        {
            m_Json += "null";
        }
        else if (m_Options.NonFinite == EJsonNonFinite::String)
        {
            m_Json += '"';
            m_Json += text;
            m_Json += '"';
        }
        else
        {
            return Fail(ETomlCode::InvalidValue, "The float " + text + " can't be written: JSON has no inf or nan "
                "(see TASWTomlJsonOptions::NonFinite).");
        }

        return true;
    }

    // A date or time is a string; an integer, a float and a boolean are written as they are
    bool const quoted = number == nullptr && !value.IsInteger() && !value.IsBoolean();
    if (quoted)
        m_Json += '"';

    m_Json += text;
    if (quoted)
        m_Json += '"';

    return true;
}
//---------------------------------------------------------------------------
bool TJsonWriter::WriteTable(TASWTomlValue::TKeyValueVector const& table, std::size_t depth)
{
    m_Json += '{';

    for (std::size_t i = 0; i < table.size(); ++i)
    {
        TASWTomlKeyValue const& entry = table[i];
        if (i != 0)
            m_Json += ',';

        WriteLineBreak(depth + 1);
        bool written = false;
        if (Detail::IsValidUtf8(entry.Key))
        {
            AppendString(entry.Key, m_Json);
            m_Json += m_Options.Indent == 0 ? ":" : ": ";
            written = Write(entry.Value, depth + 1);
        }
        else
        {
            written = FailInvalidKey();
        }

        if (!written)
            return FailAtKey(entry.Key);
    }

    if (!table.empty())
        WriteLineBreak(depth);

    m_Json += '}';

    return true;
}
//---------------------------------------------------------------------------
void TJsonWriter::WriteTag(char const* type)
{
    m_Json += m_Options.Indent == 0 ? "{\"type\":\"" : "{\"type\": \"";
    m_Json += type;
    m_Json += m_Options.Indent == 0 ? "\",\"value\":" : "\", \"value\": ";
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
TASWTomlResult ToJson(TASWTomlValue const& value, std::string& json, TASWTomlJsonOptions const& options)
{
    TASWTomlResult result = options.Validate();
    if (!result.IsOk())
        return result;

    TJsonWriter writer(options);
    if (!writer.Write(value, 0))
        return std::move(writer.GetFailure());

    json = std::move(writer.GetJson());

    return result;
}
//---------------------------------------------------------------------------
char const* ToString(EJsonNonFinite nonFinite) noexcept
{
    switch (nonFinite)
    {
        case EJsonNonFinite::Fail:
            return "Fail";

        case EJsonNonFinite::Null:
            return "Null";

        case EJsonNonFinite::String:
            return "String";
    }

    return "Unknown";
}
//---------------------------------------------------------------------------
char const* ToString(EJsonStyle style) noexcept
{
    switch (style)
    {
        case EJsonStyle::Plain:
            return "Plain";

        case EJsonStyle::Tagged:
            return "Tagged";
    }

    return "Unknown";
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlJsonOptions
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlJsonOptions::Validate() const
{
    // The enumerations, in case one was cast from a number
    if (std::string_view(ToString(Style)) == "Unknown")
        return InvalidOptions("Style isn't one of the EJsonStyle values.");

    if (std::string_view(ToString(NonFinite)) == "Unknown")
        return InvalidOptions("NonFinite isn't one of the EJsonNonFinite values.");

    if (Indent > MaxIndent)
    {
        return InvalidOptions("Indent is " + std::to_string(Indent) + "; it can be at most " +
            std::to_string(MaxIndent) + ".");
    }

    return TASWTomlResult();
}
//---------------------------------------------------------------------------

} // namespace ASWToml
