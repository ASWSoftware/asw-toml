/* **************************************************************************
ASWToml_Edits.cpp
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
#include "ASWToml_Edits.h"
//---------------------------------------------------------------------------
#include <string>
#include <string_view>
#include <utility>
//---------------------------------------------------------------------------

namespace ASWToml
{

/////////////////////////////////////////////////////////////////////////////
// TASWTomlEdits
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlEdits& TASWTomlEdits::AddText(EEditKind kind, TASWTomlPath const& path, std::string_view text)
{
    TASWTomlEdit edit;
    edit.Kind = kind;
    edit.Path = path;
    edit.Text = text;
    m_Edits.push_back(std::move(edit));

    return *this;
}
//---------------------------------------------------------------------------
void TASWTomlEdits::AddValue(EEditKind kind, TASWTomlPath const& path, TASWTomlResult converted, TASWTomlValue value,
    TASWTomlValueOptions const* options)
{
    if (!converted.IsOk())
    {
        if (m_Result.IsOk())
            m_Result = Detail::PathFailure(std::move(converted), path);

        return;
    }

    TASWTomlEdit edit;
    edit.Kind = kind;
    edit.Path = path;
    edit.Value = std::move(value);
    if (options != nullptr)
        edit.Values = *options;

    m_Edits.push_back(std::move(edit));
}
//---------------------------------------------------------------------------
TASWTomlEdits& TASWTomlEdits::Append(TASWTomlPath const& path, char const* value)
{
    return AddConverted(EEditKind::Append, path, std::string(value != nullptr ? value : ""), nullptr);
}
//---------------------------------------------------------------------------
TASWTomlEdits& TASWTomlEdits::Append(TASWTomlPath const& path, char const* value, TASWTomlValueOptions const& options)
{
    return AddConverted(EEditKind::Append, path, std::string(value != nullptr ? value : ""), &options);
}
//---------------------------------------------------------------------------
TASWTomlEdits& TASWTomlEdits::Append(TASWTomlPath const& path, std::string_view value)
{
    return AddConverted(EEditKind::Append, path, std::string(value), nullptr);
}
//---------------------------------------------------------------------------
TASWTomlEdits& TASWTomlEdits::Append(TASWTomlPath const& path, std::string_view value,
    TASWTomlValueOptions const& options)
{
    return AddConverted(EEditKind::Append, path, std::string(value), &options);
}
//---------------------------------------------------------------------------
void TASWTomlEdits::Clear() noexcept
{
    m_Edits.clear();
    m_Result = TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlEdits::TEditVector const& TASWTomlEdits::GetEdits() const noexcept
{
    return m_Edits;
}
//---------------------------------------------------------------------------
TASWTomlResult const& TASWTomlEdits::GetResult() const noexcept
{
    return m_Result;
}
//---------------------------------------------------------------------------
bool TASWTomlEdits::IsEmpty() const noexcept
{
    return m_Edits.empty();
}
//---------------------------------------------------------------------------
TASWTomlEdits& TASWTomlEdits::Remove(TASWTomlPath const& path)
{
    return AddText(EEditKind::Remove, path, std::string_view());
}
//---------------------------------------------------------------------------
TASWTomlEdits& TASWTomlEdits::Rename(TASWTomlPath const& path, std::string_view newKey)
{
    return AddText(EEditKind::Rename, path, newKey);
}
//---------------------------------------------------------------------------
TASWTomlEdits& TASWTomlEdits::SetComment(TASWTomlPath const& path, std::string_view text)
{
    return AddText(EEditKind::SetComment, path, text);
}
//---------------------------------------------------------------------------
TASWTomlEdits& TASWTomlEdits::SetTrailingComment(TASWTomlPath const& path, std::string_view text)
{
    return AddText(EEditKind::SetTrailingComment, path, text);
}
//---------------------------------------------------------------------------
TASWTomlEdits& TASWTomlEdits::SetValue(TASWTomlPath const& path, char const* value)
{
    return AddConverted(EEditKind::SetValue, path, std::string(value != nullptr ? value : ""), nullptr);
}
//---------------------------------------------------------------------------
TASWTomlEdits& TASWTomlEdits::SetValue(TASWTomlPath const& path, char const* value,
    TASWTomlValueOptions const& options)
{
    return AddConverted(EEditKind::SetValue, path, std::string(value != nullptr ? value : ""), &options);
}
//---------------------------------------------------------------------------
TASWTomlEdits& TASWTomlEdits::SetValue(TASWTomlPath const& path, std::string_view value)
{
    return AddConverted(EEditKind::SetValue, path, std::string(value), nullptr);
}
//---------------------------------------------------------------------------
TASWTomlEdits& TASWTomlEdits::SetValue(TASWTomlPath const& path, std::string_view value,
    TASWTomlValueOptions const& options)
{
    return AddConverted(EEditKind::SetValue, path, std::string(value), &options);
}
//---------------------------------------------------------------------------

} // namespace ASWToml
