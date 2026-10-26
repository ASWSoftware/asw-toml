/* **************************************************************************
ASWToml_Edits.h
Author: Anthony S. West - ASW Software

A TOML file reader/writer library.

TASWTomlEdits: a list of edits of a TOML document (set a value, append,
remove, rename, set a comment or a trailing comment), which
TASWTomlDocument::Apply() makes as one: all of them or none.

Requires C++ 20 or higher.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

#pragma once

#ifndef ASWToml_EditsH
#define ASWToml_EditsH
//---------------------------------------------------------------------------
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWToml_Converters.h"
#include "ASWToml_Options.h"
#include "ASWToml_Path.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

// What an edit of a TASWTomlEdits does: the TASWTomlDocument edit of the same name
enum class EEditKind : int
{
    Append = 0,
    Remove = 1,
    Rename = 2,
    SetComment = 3,
    SetTrailingComment = 4,
    SetValue = 5,
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlEdit
//
// One edit of a TASWTomlEdits: what it does, where, and with what
/////////////////////////////////////////////////////////////////////////////
struct TASWTomlEdit
{
    EEditKind Kind = EEditKind::SetValue;
    TASWTomlPath Path;
    TASWTomlValue Value; // Append and SetValue: the value appended or set
    std::string Text; // Rename: the new key; SetComment and SetTrailingComment: the comment

    // Append and SetValue: the value options the value is written with; none for the document's (its options' Values)
    std::optional<TASWTomlValueOptions> Values;
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlEdits
//
// Edits of a TOML document, in order, which TASWTomlDocument::Apply() makes as one: all of them, as if one after the
// other, or none. Each method adds one edit at the end and returns the edits, so calls can be chained, e.g.
// edits.SetValue("server.port", 8080).Remove("server.legacy"). Adding an edit checks nothing but a value's conversion:
// the edit's own checks (a path that isn't valid, a key that doesn't exist, ...) are made by Apply(), as the
// document's edit of the same name makes them. Copyable and movable.
/////////////////////////////////////////////////////////////////////////////
class TASWTomlEdits
{
public:
    // The edits, in order
    using TEditVector = std::vector<TASWTomlEdit>;

private:
    TEditVector m_Edits;
    TASWTomlResult m_Result; // The first conversion that failed (see GetResult())

    // Adds an edit of 'kind' (Append or SetValue) at 'path' of 'value', converted with TASWTomlConverter<T> and
    // 'options' (the default value options when it is nullptr)
    template <TomlConvertible T>
    TASWTomlEdits& AddConverted(EEditKind kind, TASWTomlPath const& path, T const& value,
        TASWTomlValueOptions const* options)
    {
        TASWTomlValue converted;
        TASWTomlResult result = TASWTomlConverter<T>::ToValue(value, converted,
            options != nullptr ? *options : TASWTomlValueOptions());
        AddValue(kind, path, std::move(result), std::move(converted), options);

        return *this;
    }

    // Adds an edit of 'kind' at 'path' with the text 'text'
    TASWTomlEdits& AddText(EEditKind kind, TASWTomlPath const& path, std::string_view text);

    // Adds an edit of 'kind' (Append or SetValue) at 'path' of 'value', written with 'options' (nullptr for the
    // document's), when 'converted', the result of converting it, is Ok; else keeps 'converted' with 'path' in front
    // of its Path as GetResult() when it is the first failure, and adds nothing
    void AddValue(EEditKind kind, TASWTomlPath const& path, TASWTomlResult converted, TASWTomlValue value,
        TASWTomlValueOptions const* options);

public:
    // No edits
    TASWTomlEdits() = default;

    // Adds an Append() of 'value' to the array at 'path' (see TASWTomlDocument::Append()), converted with
    // TASWTomlConverter<T> and the value options given ('options', else the default ones), and written with the
    // value options given, else the document's. A text value (a string literal, a char array or pointer, or a
    // std::string_view) is a string (a nullptr value is ""). A value that doesn't convert adds no edit: the first such
    // failure is kept (see GetResult()). May throw std::bad_alloc, and what a converter throws.
    template <TomlConvertible T>
    TASWTomlEdits& Append(TASWTomlPath const& path, T const& value)
    {
        return AddConverted(EEditKind::Append, path, value, nullptr);
    }

    template <TomlConvertible T>
    TASWTomlEdits& Append(TASWTomlPath const& path, T const& value, TASWTomlValueOptions const& options)
    {
        return AddConverted(EEditKind::Append, path, value, &options);
    }

    TASWTomlEdits& Append(TASWTomlPath const& path, char const* value);
    TASWTomlEdits& Append(TASWTomlPath const& path, char const* value, TASWTomlValueOptions const& options);
    TASWTomlEdits& Append(TASWTomlPath const& path, std::string_view value);
    TASWTomlEdits& Append(TASWTomlPath const& path, std::string_view value, TASWTomlValueOptions const& options);

    // Removes every edit, and the failure GetResult() gives
    void Clear() noexcept;

    // The edits, in the order they were added
    TEditVector const& GetEdits() const noexcept;

    // Ok, or the first failure to convert a value that SetValue() or Append() were given (the converter's code and
    // text, with the edit's path in front of its Path, e.g. servers[1].port), which Apply() returns without making any
    // edit
    TASWTomlResult const& GetResult() const noexcept;

    // Whether there are no edits
    bool IsEmpty() const noexcept;

    // Adds a removal of the key, table, array of tables or element of an array at 'path' (see
    // TASWTomlDocument::Remove()). Apply() fails with InvalidArgument when 'path' isn't valid or is the root, and
    // changes nothing for it when nothing is at 'path'. May throw std::bad_alloc.
    TASWTomlEdits& Remove(TASWTomlPath const& path);

    // Adds a Rename() of the key, table or array of tables at 'path' to 'newKey' (see TASWTomlDocument::Rename()).
    // May throw std::bad_alloc.
    TASWTomlEdits& Rename(TASWTomlPath const& path, std::string_view newKey);

    // Adds a SetComment() of the key, table or array of tables at 'path' (the file comment for the root path) to
    // 'text' (see TASWTomlDocument::SetComment()). May throw std::bad_alloc.
    TASWTomlEdits& SetComment(TASWTomlPath const& path, std::string_view text);

    // Adds a SetTrailingComment() of the key, table or array of tables at 'path' to 'text' (see
    // TASWTomlDocument::SetTrailingComment()). May throw std::bad_alloc.
    TASWTomlEdits& SetTrailingComment(TASWTomlPath const& path, std::string_view text);

    // Adds a SetValue() of the key, table or element of an array at 'path' to 'value' (see
    // TASWTomlDocument::SetValue()), converted and written as Append() says
    template <TomlConvertible T>
    TASWTomlEdits& SetValue(TASWTomlPath const& path, T const& value)
    {
        return AddConverted(EEditKind::SetValue, path, value, nullptr);
    }

    template <TomlConvertible T>
    TASWTomlEdits& SetValue(TASWTomlPath const& path, T const& value, TASWTomlValueOptions const& options)
    {
        return AddConverted(EEditKind::SetValue, path, value, &options);
    }

    TASWTomlEdits& SetValue(TASWTomlPath const& path, char const* value);
    TASWTomlEdits& SetValue(TASWTomlPath const& path, char const* value, TASWTomlValueOptions const& options);
    TASWTomlEdits& SetValue(TASWTomlPath const& path, std::string_view value);
    TASWTomlEdits& SetValue(TASWTomlPath const& path, std::string_view value, TASWTomlValueOptions const& options);
};

} // namespace ASWToml

//---------------------------------------------------------------------------
#endif // #ifndef ASWToml_EditsH
