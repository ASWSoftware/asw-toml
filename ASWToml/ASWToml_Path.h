/* **************************************************************************
ASWToml_Path.h
Author: Anthony S. West - ASW Software

A TOML file reader/writer library.

TASWTomlPath: the place of a table, a key or an element of an array in a
TOML document, from the root table, made from a string in TOML's dotted-key
syntax (server."host name".port, products[0].name) or from a list of parts
({ "products", 0, "name" }).

Requires C++ 20 or higher.

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

#pragma once

#ifndef ASWToml_PathH
#define ASWToml_PathH
//---------------------------------------------------------------------------
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
//---------------------------------------------------------------------------

namespace ASWToml
{

/////////////////////////////////////////////////////////////////////////////
// TASWTomlPathPart
//
// One part of a TASWTomlPath: a key (one key part, unquoted UTF-8, e.g. host name for "host name") or an index (of an
// element of an array, from 0). Made implicitly from a string or from a number of any integer type but bool and the
// character types, so a list such as { "products", 0, "name" } makes a path. A key that isn't valid UTF-8, and an
// index from a negative number (or one larger than a std::size_t can hold), make a part that isn't valid, which a path
// doesn't take.
/////////////////////////////////////////////////////////////////////////////
class TASWTomlPathPart
{
private:
    std::string m_Key;
    std::size_t m_Index = 0;
    bool m_IsIndex = false;
    bool m_IsValid = true;

public:
    // A key; a null pointer is the empty key ("" in TOML)
    TASWTomlPathPart(char const* key);
    TASWTomlPathPart(std::string key);
    TASWTomlPathPart(std::string_view key);

    // An index
    template <std::integral T>
    requires(!std::same_as<T, bool> && !std::same_as<T, char> && !std::same_as<T, wchar_t> &&
        !std::same_as<T, char8_t> && !std::same_as<T, char16_t> && !std::same_as<T, char32_t>)
    TASWTomlPathPart(T index)
        : m_Index(std::in_range<std::size_t>(index) ? static_cast<std::size_t>(index) : 0),
          m_IsIndex(true),
          m_IsValid(std::in_range<std::size_t>(index))
    {
    }

    bool operator==(TASWTomlPathPart const& other) const = default;

    // The index; 0 for a key, and for an index that isn't valid
    std::size_t GetIndex() const noexcept;

    // The key; empty for an index
    std::string const& GetKey() const noexcept;

    // Whether the part is an index (made from a number)
    bool IsIndex() const noexcept;

    // Whether the part is a key (made from a string)
    bool IsKey() const noexcept;

    // Whether the part can be in a path: a key that is valid UTF-8, or an index from 0 to the largest std::size_t
    bool IsValid() const noexcept;
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlPath
//
// The place of a table, a key or an element of an array in a TOML document: a list of parts, each a key or an index
// (see TASWTomlPathPart), starting from the root table. Made implicitly from a string in TOML's dotted-key syntax,
// with [n] after a key for an element of an array, e.g. server."host name".port or products[0].name, or from a list
// of parts, e.g. { "server", "host name", "port" } or { "products", 0, "name" }. The empty string and the empty list
// are the root table. A braced list is always a list of parts, even of one string: { "a.b" } (and
// TASWTomlPath({ "a.b" })) is the key a.b, while "a.b" is the key b of the table a.
// A path that can't be made (a string that isn't a valid path, a part that isn't valid, or an index as the first
// part, since the root is a table) isn't valid: IsValid() is false, GetError() says why, and an operation given it
// fails with InvalidArgument and that reason. Nothing here throws except std::bad_alloc.
/////////////////////////////////////////////////////////////////////////////
class TASWTomlPath
{
public:
    // The parts of a path, from the root
    using TPartVector = std::vector<TASWTomlPathPart>;

private:
    TPartVector m_Parts;
    std::string m_Error; // Why the path isn't valid; empty when it is

    void Fail(std::string error);
    void Parse(std::string_view text);

public:
    // The root table
    TASWTomlPath();

    // The path a string names, in TOML's dotted-key syntax: keys (bare, or basic or literal strings on one line)
    // separated by dots, each followed by any number of indexes [n] (n in decimal, without leading zeros), e.g.
    // server."host name".port, products[0].name or matrix[1][2]. Blanks (spaces and tabs) may come before and after
    // each key, dot, bracket and number, as around a key in TOML. The empty string (and a null char pointer) is the
    // root table. Any type that converts to std::string_view is taken (a string literal, char const*, std::string,
    // std::string_view); a template, so a braced list never selects it (see the class comment).
    template <typename T>
    requires(!std::is_null_pointer_v<T> && std::convertible_to<T const&, std::string_view>)
    TASWTomlPath(T const& text)
    {
        if constexpr (std::is_pointer_v<T>)
        {
            if (text == nullptr)
                return;
        }

        Parse(text);
    }

    // The path of 'parts', in order; an empty list is the root table
    TASWTomlPath(std::initializer_list<TASWTomlPathPart> parts);

    // Whether both have the same parts (or both aren't valid, for the same reason)
    bool operator==(TASWTomlPath const& other) const = default;

    // Adds 'part' at the end of the path: a key of the table the path names, or an index of its array. A path that
    // isn't valid stays as it is. A part that isn't valid, or an index added to the root table, makes the path not
    // valid. Returns this path, so calls can be chained: path.Append("products").Append(0).Append("name").
    TASWTomlPath& Append(TASWTomlPathPart part);

    // Why the path isn't valid, as a complete sentence, e.g. The path "a..b" isn't valid at column 3. A key must be a
    // bare key (A-Z, a-z, 0-9, '-' and '_') or a quoted string. Empty when the path is valid.
    std::string const& GetError() const noexcept;

    // The parts, from the root; empty for the root table and for a path that isn't valid
    TPartVector const& GetParts() const noexcept;

    // Whether the path is valid and names the root table (it has no parts)
    bool IsRoot() const noexcept;

    // Whether the path is valid (see GetError())
    bool IsValid() const noexcept;

    // The path in TOML's dotted-key syntax, as messages show it: each key bare when it can be (not empty, and only
    // A-Z, a-z, 0-9, '-' and '_'), else a basic string, and [n] for an index, e.g. server."host name".port or
    // products[0].name. Empty for the root table and for a path that isn't valid. A valid path made from the text is
    // equal to this one.
    std::string ToString() const;
};

} // namespace ASWToml

//---------------------------------------------------------------------------
#endif // #ifndef ASWToml_PathH
