/* **************************************************************************
UT_Helper_Json.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "UT_Helper_Json.h"
//---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
//---------------------------------------------------------------------------
#include "ASWToml_Scalars.h"
#include "ASWToml_Types.h"
#include "ASWToml_Utils.h"
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

using ASWToml::TASWTomlValue;

// The deepest the reader goes, far deeper than ToJson() writes, so a broken text can't overflow the stack
constexpr std::size_t MaxReadDepth = 200;


/////////////////////////////////////////////////////////////////////////////
// TJsonReader
//
// Reads a JSON text strictly (see ReadJson())
/////////////////////////////////////////////////////////////////////////////
class TJsonReader
{
private:
    std::string_view m_Text;
    std::size_t m_Offset = 0;
    std::string m_Problem;

    bool Fail(std::string const& what);
    bool ReadArray(TJsonValue& value, std::size_t depth);
    bool ReadDigits();
    bool ReadEscape(std::string& text);
    bool ReadHex(std::uint32_t& unit);
    bool ReadNumber(TJsonValue& value);
    bool ReadObject(TJsonValue& value, std::size_t depth);
    bool ReadString(std::string& text);
    bool ReadValue(TJsonValue& value, std::size_t depth);
    void SkipBlanks() noexcept;

public:
    explicit TJsonReader(std::string_view text);

    // What is wrong with the text; empty when it read
    std::string const& GetProblem() const noexcept;

    // Reads the whole text into 'value'; false when it isn't one JSON value
    bool Read(TJsonValue& value);
};

//---------------------------------------------------------------------------

void AppendUtf8(std::uint32_t codePoint, std::string& text);
bool IsTaggedScalar(TJsonValue const& json) noexcept;
std::string ScalarProblem(std::string const& type, std::string const& text, TASWTomlValue& value);
std::string TaggedProblem(TJsonValue const& json, std::string const& path, TASWTomlValue& value);

//---------------------------------------------------------------------------
/*
    AppendUtf8

    Appends the UTF-8 bytes of 'codePoint', a Unicode scalar value, to 'text'
*/
void AppendUtf8(std::uint32_t codePoint, std::string& text)
{
    if (codePoint < 0x80)
    {
        text += static_cast<char>(codePoint);
    }
    else if (codePoint < 0x800)
    {
        text += static_cast<char>(0xC0 | (codePoint >> 6));
        text += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
    else if (codePoint < 0x10000)
    {
        text += static_cast<char>(0xE0 | (codePoint >> 12));
        text += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        text += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
    else
    {
        text += static_cast<char>(0xF0 | (codePoint >> 18));
        text += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
        text += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        text += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
}
//---------------------------------------------------------------------------
/*
    IsTaggedScalar

    Whether 'json' is a tagged scalar: an object of exactly the members "type" and "value", both strings
*/
bool IsTaggedScalar(TJsonValue const& json) noexcept
{
    if (json.Kind != EJsonKind::Object || json.Members.size() != 2)
        return false;

    bool hasType = false;
    bool hasValue = false;
    for (TJsonMember const& member : json.Members)
    {
        if (member.Value.Kind != EJsonKind::String)
            return false;

        hasType = hasType || member.Key == "type";
        hasValue = hasValue || member.Key == "value";
    }

    return hasType && hasValue;
}
//---------------------------------------------------------------------------
/*
    ScalarProblem

    Sets 'value' to the scalar of toml-test's type 'type' written as 'text'; empty when it is one, else what is wrong
*/
std::string ScalarProblem(std::string const& type, std::string const& text, TASWTomlValue& value)
{
    if (type == "string")
    {
        value = text;
        return std::string();
    }

    if (type == "bool")
    {
        if (text != "true" && text != "false")
            return "the bool " + text + " isn't true or false";

        value = text == "true";
        return std::string();
    }

    std::size_t position = 0;
    TASWTomlValue read;
    ASWToml::TASWTomlResult result;
    ASWToml::ETomlType expected = ASWToml::ETomlType::Integer;
    if (type == "integer" || type == "float")
    {
        result = ASWToml::Detail::ReadNumber(text, position, read);
        if (type == "float")
            expected = ASWToml::ETomlType::Float;
    }
    else
    {
        result = ASWToml::Detail::ReadDateTime(text, position, read);
        if (type == "datetime")
            expected = ASWToml::ETomlType::OffsetDateTime;
        else if (type == "datetime-local")
            expected = ASWToml::ETomlType::LocalDateTime;
        else if (type == "date-local")
            expected = ASWToml::ETomlType::LocalDate;
        else if (type == "time-local")
            expected = ASWToml::ETomlType::LocalTime;
        else
            return "the type " + type + " isn't one of toml-test's";
    }

    if (!result.IsOk() || position != text.size() || read.GetType() != expected)
        return "the " + type + " " + text + " doesn't read as one";

    if (std::int64_t const* const integer = read.AsInteger())
    {
        if (std::to_string(*integer) != text)
            return "the integer " + text + " isn't in decimal as std::to_string() writes it";
    }

    value = std::move(read);

    return std::string();
}
//---------------------------------------------------------------------------
/*
    TaggedProblem

    TaggedToValue() for 'json', at 'path' (in the form .key and [n]) in the value it is read for
*/
std::string TaggedProblem(TJsonValue const& json, std::string const& path, TASWTomlValue& value)
{
    if (IsTaggedScalar(json))
    {
        std::string type;
        std::string text;
        for (TJsonMember const& member : json.Members)
            (member.Key == "type" ? type : text) = member.Value.Text;

        std::string const problem = ScalarProblem(type, text, value);

        return problem.empty() ? problem : "At " + path + ": " + problem + ".";
    }

    if (json.Kind == EJsonKind::Object)
    {
        TASWTomlValue table;
        for (TJsonMember const& member : json.Members)
        {
            TASWTomlValue element;
            std::string const problem = TaggedProblem(member.Value, path + "." + member.Key, element);
            if (!problem.empty())
                return problem;

            table.Set(member.Key, std::move(element));
        }

        value = std::move(table);
        return std::string();
    }

    if (json.Kind == EJsonKind::Array)
    {
        TASWTomlValue::TValueVector array;
        for (std::size_t i = 0; i < json.Elements.size(); ++i)
        {
            TASWTomlValue element;
            std::string const problem = TaggedProblem(json.Elements[i], path + "[" + std::to_string(i) + "]", element);
            if (!problem.empty())
                return problem;

            array.push_back(std::move(element));
        }

        value = std::move(array);
        return std::string();
    }

    return "At " + path + ": a " + (json.Kind == EJsonKind::String ? "string" : "number, boolean or null") +
        " isn't a tagged value.";
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TJsonReader
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TJsonReader::TJsonReader(std::string_view text)
    : m_Text(text)
{
}
//---------------------------------------------------------------------------
bool TJsonReader::Fail(std::string const& what)
{
    m_Problem = what + " at offset " + std::to_string(m_Offset) + ".";

    return false;
}
//---------------------------------------------------------------------------
std::string const& TJsonReader::GetProblem() const noexcept
{
    return m_Problem;
}
//---------------------------------------------------------------------------
bool TJsonReader::Read(TJsonValue& value)
{
    if (!ASWToml::Detail::IsValidUtf8(m_Text))
        return Fail("The text isn't valid UTF-8");

    SkipBlanks();
    if (!ReadValue(value, 0))
        return false;

    SkipBlanks();
    if (m_Offset != m_Text.size())
        return Fail("More than one value");

    return true;
}
//---------------------------------------------------------------------------
bool TJsonReader::ReadArray(TJsonValue& value, std::size_t depth)
{
    value.Kind = EJsonKind::Array;
    ++m_Offset;
    SkipBlanks();
    if (m_Offset < m_Text.size() && m_Text[m_Offset] == ']')
    {
        ++m_Offset;
        return true;
    }

    while (true)
    {
        value.Elements.emplace_back();
        if (!ReadValue(value.Elements.back(), depth + 1))
            return false;

        SkipBlanks();
        if (m_Offset < m_Text.size() && m_Text[m_Offset] == ',')
        {
            ++m_Offset;
            SkipBlanks();
            continue;
        }

        if (m_Offset < m_Text.size() && m_Text[m_Offset] == ']')
        {
            ++m_Offset;
            return true;
        }

        return Fail("No ',' or ']' after an element");
    }
}
//---------------------------------------------------------------------------
bool TJsonReader::ReadDigits()
{
    std::size_t const start = m_Offset;
    while (m_Offset < m_Text.size() && m_Text[m_Offset] >= '0' && m_Text[m_Offset] <= '9')
        ++m_Offset;

    return m_Offset != start;
}
//---------------------------------------------------------------------------
bool TJsonReader::ReadEscape(std::string& text)
{
    // At the backslash
    ++m_Offset;
    if (m_Offset >= m_Text.size())
        return Fail("An escape without its letter");

    char const letter = m_Text[m_Offset];
    char const* const letters = "\"\\/bfnrt";
    char const* const characters = "\"\\/\b\f\n\r\t";
    for (std::size_t i = 0; letters[i] != '\0'; ++i)
    {
        if (letter == letters[i])
        {
            text += characters[i];
            ++m_Offset;
            return true;
        }
    }

    if (letter != 'u')
        return Fail("An escape JSON doesn't have");

    ++m_Offset;
    std::uint32_t unit = 0;
    if (!ReadHex(unit))
        return false;

    if (unit >= 0xDC00 && unit <= 0xDFFF)
        return Fail("A low surrogate without a high one before it");

    if (unit >= 0xD800 && unit <= 0xDBFF)
    {
        std::uint32_t low = 0;
        if (m_Text.substr(m_Offset, 2) != "\\u")
            return Fail("A high surrogate without a low one after it");

        m_Offset += 2;
        if (!ReadHex(low))
            return false;

        if (low < 0xDC00 || low > 0xDFFF)
            return Fail("A high surrogate without a low one after it");

        unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
    }

    AppendUtf8(unit, text);

    return true;
}
//---------------------------------------------------------------------------
bool TJsonReader::ReadHex(std::uint32_t& unit)
{
    unit = 0;
    for (std::size_t i = 0; i < 4; ++i, ++m_Offset)
    {
        char const c = m_Offset < m_Text.size() ? m_Text[m_Offset] : '\0';
        unit <<= 4;
        if (c >= '0' && c <= '9')
            unit |= static_cast<std::uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            unit |= static_cast<std::uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            unit |= static_cast<std::uint32_t>(c - 'A' + 10);
        else
            return Fail("A \\u escape without four hex digits");
    }

    return true;
}
//---------------------------------------------------------------------------
bool TJsonReader::ReadNumber(TJsonValue& value)
{
    // RFC 8259: -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?
    std::size_t const start = m_Offset;
    if (m_Text[m_Offset] == '-')
        ++m_Offset;

    if (m_Offset < m_Text.size() && m_Text[m_Offset] == '0')
        ++m_Offset;
    else if (!ReadDigits())
        return Fail("A number without digits");

    if (m_Offset < m_Text.size() && m_Text[m_Offset] == '.')
    {
        ++m_Offset;
        if (!ReadDigits())
            return Fail("A number without digits after its '.'");
    }

    if (m_Offset < m_Text.size() && (m_Text[m_Offset] == 'e' || m_Text[m_Offset] == 'E'))
    {
        ++m_Offset;
        if (m_Offset < m_Text.size() && (m_Text[m_Offset] == '+' || m_Text[m_Offset] == '-'))
            ++m_Offset;

        if (!ReadDigits())
            return Fail("A number without digits in its exponent");
    }

    value.Kind = EJsonKind::Number;
    value.Text = std::string(m_Text.substr(start, m_Offset - start));

    return true;
}
//---------------------------------------------------------------------------
bool TJsonReader::ReadObject(TJsonValue& value, std::size_t depth)
{
    value.Kind = EJsonKind::Object;
    ++m_Offset;
    SkipBlanks();
    if (m_Offset < m_Text.size() && m_Text[m_Offset] == '}')
    {
        ++m_Offset;
        return true;
    }

    while (true)
    {
        if (m_Offset >= m_Text.size() || m_Text[m_Offset] != '"')
            return Fail("No key where a member starts");

        TJsonMember member;
        if (!ReadString(member.Key))
            return false;

        for (TJsonMember const& other : value.Members)
        {
            if (other.Key == member.Key)
                return Fail("A key the object has already");
        }

        SkipBlanks();
        if (m_Offset >= m_Text.size() || m_Text[m_Offset] != ':')
            return Fail("No ':' after a key");

        ++m_Offset;
        SkipBlanks();
        if (!ReadValue(member.Value, depth + 1))
            return false;

        value.Members.push_back(std::move(member));
        SkipBlanks();
        if (m_Offset < m_Text.size() && m_Text[m_Offset] == ',')
        {
            ++m_Offset;
            SkipBlanks();
            continue;
        }

        if (m_Offset < m_Text.size() && m_Text[m_Offset] == '}')
        {
            ++m_Offset;
            return true;
        }

        return Fail("No ',' or '}' after a member");
    }
}
//---------------------------------------------------------------------------
bool TJsonReader::ReadString(std::string& text)
{
    // At the opening quote
    ++m_Offset;
    while (m_Offset < m_Text.size())
    {
        char const c = m_Text[m_Offset];
        if (c == '"')
        {
            ++m_Offset;
            return true;
        }

        if (static_cast<unsigned char>(c) < 0x20)
            return Fail("A control character in a string");

        if (c == '\\')
        {
            if (!ReadEscape(text))
                return false;
        }
        else
        {
            text += c;
            ++m_Offset;
        }
    }

    return Fail("A string without its closing quote");
}
//---------------------------------------------------------------------------
bool TJsonReader::ReadValue(TJsonValue& value, std::size_t depth)
{
    if (depth > MaxReadDepth)
        return Fail("Values nested too deep to read");

    if (m_Offset >= m_Text.size())
        return Fail("No value");

    char const c = m_Text[m_Offset];
    if (c == '{')
        return ReadObject(value, depth);

    if (c == '[')
        return ReadArray(value, depth);

    if (c == '"')
    {
        value.Kind = EJsonKind::String;
        return ReadString(value.Text);
    }

    if (c == '-' || (c >= '0' && c <= '9'))
        return ReadNumber(value);

    char const* const words[] = { "true", "false", "null" };
    for (char const* const word : words)
    {
        std::string_view const literal(word);
        if (m_Text.substr(m_Offset, literal.size()) == literal)
        {
            m_Offset += literal.size();
            value.Kind = literal == "null" ? EJsonKind::Null : EJsonKind::Boolean;
            if (value.Kind == EJsonKind::Boolean)
                value.Text = std::string(literal);

            return true;
        }
    }

    return Fail("No value");
}
//---------------------------------------------------------------------------
void TJsonReader::SkipBlanks() noexcept
{
    while (m_Offset < m_Text.size())
    {
        char const c = m_Text[m_Offset];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            break;

        ++m_Offset;
    }
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
std::string ReadJson(std::string_view text, TJsonValue& value)
{
    value = TJsonValue();
    TJsonReader reader(text);
    if (!reader.Read(value))
        return reader.GetProblem();

    return std::string();
}
//---------------------------------------------------------------------------
std::string TaggedToValue(TJsonValue const& json, ASWToml::TASWTomlValue& value)
{
    return TaggedProblem(json, std::string(), value);
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests
