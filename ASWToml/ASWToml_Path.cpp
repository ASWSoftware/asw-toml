/* **************************************************************************
ASWToml_Path.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "ASWToml_Path.h"
//---------------------------------------------------------------------------
#include <charconv>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
//---------------------------------------------------------------------------
#include "ASWToml_Scalars.h"
#include "ASWToml_Types.h"
#include "ASWToml_Utils.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

namespace
{

std::string PathTextError(std::string_view text, std::size_t offset, std::string_view reason);
std::size_t SkipBlanks(std::string_view text, std::size_t offset) noexcept;

//---------------------------------------------------------------------------
/*
    PathTextError

    The reason the path 'text' isn't valid: the error 'reason' (a complete sentence) at the byte 'offset', with the
    path quoted when it is valid UTF-8
*/
std::string PathTextError(std::string_view text, std::size_t offset, std::string_view reason)
{
    std::string error = "The path ";
    if (Detail::IsValidUtf8(text))
    {
        error += Detail::MessageText(text);
        error += ' ';
    }

    error += "isn't valid at column ";
    error += std::to_string(Detail::CountCodePoints(text.substr(0, offset)) + 1);
    error += ". ";
    error += reason;

    return error;
}
//---------------------------------------------------------------------------
/*
    SkipBlanks

    The offset of the first character at or after 'offset' in 'text' that isn't a space or a tab
*/
std::size_t SkipBlanks(std::string_view text, std::size_t offset) noexcept
{
    while (offset < text.size() && Detail::IsBlank(text[offset]))
        ++offset;

    return offset;
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWTomlPathPart
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlPathPart::TASWTomlPathPart(char const* key)
    : TASWTomlPathPart(key != nullptr ? std::string_view(key) : std::string_view())
{
}
//---------------------------------------------------------------------------
TASWTomlPathPart::TASWTomlPathPart(std::string key)
    : m_Key(std::move(key)),
      m_IsValid(Detail::IsValidUtf8(m_Key))
{
}
//---------------------------------------------------------------------------
TASWTomlPathPart::TASWTomlPathPart(std::string_view key)
    : TASWTomlPathPart(std::string(key))
{
}
//---------------------------------------------------------------------------
std::size_t TASWTomlPathPart::GetIndex() const noexcept
{
    return m_Index;
}
//---------------------------------------------------------------------------
std::string const& TASWTomlPathPart::GetKey() const noexcept
{
    return m_Key;
}
//---------------------------------------------------------------------------
bool TASWTomlPathPart::IsIndex() const noexcept
{
    return m_IsIndex;
}
//---------------------------------------------------------------------------
bool TASWTomlPathPart::IsKey() const noexcept
{
    return !m_IsIndex;
}
//---------------------------------------------------------------------------
bool TASWTomlPathPart::IsValid() const noexcept
{
    return m_IsValid;
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlPath
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlPath::TASWTomlPath()
{
}
//---------------------------------------------------------------------------
TASWTomlPath::TASWTomlPath(std::initializer_list<TASWTomlPathPart> parts)
{
    m_Parts.reserve(parts.size());

    for (TASWTomlPathPart const& part : parts)
        Append(part);
}
//---------------------------------------------------------------------------
TASWTomlPath& TASWTomlPath::Append(TASWTomlPathPart part)
{
    if (!IsValid())
        return *this;

    if (!part.IsValid())
    {
        std::string error = "Part " + std::to_string(m_Parts.size() + 1) + " of the path isn't valid: ";
        if (part.IsIndex())
            error += "an index can't be negative, or larger than a std::size_t can hold.";
        else
            error += "a key must be valid UTF-8.";

        Fail(std::move(error));
    }
    else if (part.IsIndex() && m_Parts.empty())
    {
        Fail("A path must start with a key, not an index: the root is a table.");
    }
    else
    {
        m_Parts.push_back(std::move(part));
    }

    return *this;
}
//---------------------------------------------------------------------------
void TASWTomlPath::Fail(std::string error)
{
    m_Parts.clear();
    m_Error = std::move(error);
}
//---------------------------------------------------------------------------
std::string const& TASWTomlPath::GetError() const noexcept
{
    return m_Error;
}
//---------------------------------------------------------------------------
TASWTomlPath::TPartVector const& TASWTomlPath::GetParts() const noexcept
{
    return m_Parts;
}
//---------------------------------------------------------------------------
bool TASWTomlPath::IsRoot() const noexcept
{
    return IsValid() && m_Parts.empty();
}
//---------------------------------------------------------------------------
bool TASWTomlPath::IsValid() const noexcept
{
    return m_Error.empty();
}
//---------------------------------------------------------------------------
void TASWTomlPath::Parse(std::string_view text)
{
    if (text.empty())
        return;

    std::size_t const invalid = Detail::FindInvalidUtf8(text);
    if (invalid != std::string_view::npos)
    {
        Fail(PathTextError(text, invalid, "It holds bytes that aren't valid UTF-8."));
        return;
    }

    std::size_t offset = SkipBlanks(text, 0);
    while (true)
    {
        std::string key;
        TASWTomlResult const result = Detail::ReadKey(text, offset, key);
        if (!result.IsOk())
        {
            Fail(PathTextError(text, offset, result.Message));
            return;
        }

        m_Parts.emplace_back(std::move(key));
        offset = SkipBlanks(text, offset);

        // The key's indexes
        while (offset < text.size() && text[offset] == '[')
        {
            offset = SkipBlanks(text, offset + 1);

            std::size_t const start = offset;
            while (offset < text.size() && text[offset] >= '0' && text[offset] <= '9')
                ++offset;

            std::string_view const digits = text.substr(start, offset - start);
            if (digits.empty() || (digits.size() > 1 && digits[0] == '0'))
            {
                Fail(PathTextError(text, start,
                    "An index must be a decimal number without leading zeros, e.g. [0] or [12]."));
                return;
            }

            std::size_t index = 0;
            if (std::from_chars(digits.data(), digits.data() + digits.size(), index).ec != std::errc())
            {
                Fail(PathTextError(text, start, "The index " + std::string(digits) + " is too large."));
                return;
            }

            offset = SkipBlanks(text, offset);
            if (offset >= text.size() || text[offset] != ']')
            {
                Fail(PathTextError(text, offset, "An index must end with ']'."));
                return;
            }

            m_Parts.emplace_back(index);
            offset = SkipBlanks(text, offset + 1);
        }

        if (offset >= text.size())
            return;

        if (text[offset] != '.')
        {
            Fail(PathTextError(text, offset, "After a key or an index, a path must go on with '.' or '[', or end."));
            return;
        }

        offset = SkipBlanks(text, offset + 1);
    }
}
//---------------------------------------------------------------------------
std::string TASWTomlPath::ToString() const
{
    std::string text;

    for (TASWTomlPathPart const& part : m_Parts)
    {
        if (part.IsIndex())
        {
            text += '[';
            text += std::to_string(part.GetIndex());
            text += ']';
            continue;
        }

        if (!text.empty())
            text += '.';

        // A key of a path is valid UTF-8, which is all WriteKey() checks
        static_cast<void>(Detail::WriteKey(part.GetKey(), text));
    }

    return text;
}
//---------------------------------------------------------------------------

} // namespace ASWToml
