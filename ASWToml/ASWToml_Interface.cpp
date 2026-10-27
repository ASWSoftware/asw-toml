/* **************************************************************************
ASWToml_Interface.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "ASWToml_Interface.h"
//---------------------------------------------------------------------------
#include <cstddef>
#include <exception>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------

namespace ASWToml
{

namespace
{

//---------------------------------------------------------------------------

TASWTomlResult CheckRemoval(TASWTomlPath const& path);

//---------------------------------------------------------------------------
/*
    CheckRemoval

    Ok when a removal of a batch can be made at 'path'; InvalidArgument when 'path' isn't valid or is the root, as
    TASWTomlDocument::Apply() fails (without a Path: neither has one)
*/
TASWTomlResult CheckRemoval(TASWTomlPath const& path)
{
    TASWTomlResult result;
    if (path.IsValid() && !path.IsRoot())
        return result;

    result.Code = ETomlCode::InvalidArgument;
    result.Message = path.IsValid() ? std::string("Can't remove the root table.") : path.GetError();

    return result;
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// IASWToml
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult IASWToml::Append(TASWTomlPath const& path, char const* value)
{
    return WriteText(path, value != nullptr ? std::string_view(value) : std::string_view(), &IASWToml::DocumentAppend,
        nullptr);
}
//---------------------------------------------------------------------------
TASWTomlResult IASWToml::Append(TASWTomlPath const& path, char const* value, TASWTomlValueOptions const& options)
{
    return WriteText(path, value != nullptr ? std::string_view(value) : std::string_view(), &IASWToml::DocumentAppend,
        &options);
}
//---------------------------------------------------------------------------
TASWTomlResult IASWToml::Append(TASWTomlPath const& path, std::string_view value)
{
    return WriteText(path, value, &IASWToml::DocumentAppend, nullptr);
}
//---------------------------------------------------------------------------
TASWTomlResult IASWToml::Append(TASWTomlPath const& path, std::string_view value, TASWTomlValueOptions const& options)
{
    return WriteText(path, value, &IASWToml::DocumentAppend, &options);
}
//---------------------------------------------------------------------------
TASWTomlResult IASWToml::Apply(TASWTomlEdits const& edits)
{
    TASWTomlResult result = edits.GetResult();
    if (!result.IsOk())
    {
        ReportFailure(result);
        return result;
    }

    // The edit being made, whose path an exception is given
    std::size_t current = 0;
    try
    {
        TASWTomlEdits::TEditVector const& all = edits.GetEdits();
        for (; result.IsOk() && current < all.size(); ++current)
            result = ApplyEdit(all[current]);
    }
    catch (...)
    {
        result = CaughtException(edits.GetEdits()[current].Path);
        ReportFailure(result);
    }

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult IASWToml::ApplyEdit(TASWTomlEdit const& edit)
{
    // A setting's edit is made only by its owner, a TASWBaseToml, whose Apply() doesn't come here
    if (edit.Setting != nullptr)
    {
        TASWTomlResult const result = Detail::SettingEditFailure(edit.Path);
        ReportFailure(result);

        return result;
    }

    switch (edit.Kind)
    {
        case EEditKind::Append:
            return DocumentAppend(edit.Path, edit.Value, edit.Values.has_value() ? *edit.Values : GetValueOptions());

        case EEditKind::Remove:
            break;

        case EEditKind::Rename:
            return Rename(edit.Path, edit.Text);

        case EEditKind::SetComment:
            return SetComment(edit.Path, edit.Text);

        case EEditKind::SetTrailingComment:
            return SetTrailingComment(edit.Path, edit.Text);

        case EEditKind::SetValue:
            return DocumentSetValue(edit.Path, edit.Value, edit.Values.has_value() ? *edit.Values : GetValueOptions());
    }

    // A removal. Remove() returns false for nothing to remove; in a batch, that changes nothing and isn't a failure.
    TASWTomlResult const result = CheckRemoval(edit.Path);
    if (result.IsOk())
        static_cast<void>(Remove(edit.Path));
    else
        ReportFailure(result);

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult IASWToml::CaughtException() noexcept
{
    TASWTomlResult result;
    result.Code = ETomlCode::UnexpectedException;

    try
    {
        try
        {
            throw;
        }
        catch (std::exception const& e)
        {
            result.Message = std::string("An unexpected exception was thrown: ") + e.what();
        }
        catch (...)
        {
            result.Message = "An exception that isn't a std::exception was thrown.";
        }
    }
    catch (...)
    {
        // The message couldn't be made (std::bad_alloc); the code says what happened
    }

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult IASWToml::CaughtException(TASWTomlPath const& path) noexcept
{
    TASWTomlResult result = CaughtException();

    try
    {
        result.Path = path.ToString();
    }
    catch (...)
    {
        // The path couldn't be made (std::bad_alloc); the result is complete without it
    }

    return result;
}
//---------------------------------------------------------------------------
std::string IASWToml::GetValue(TASWTomlPath const& path, char const* defaultValue) const
{
    return GetValue(path, defaultValue != nullptr ? std::string_view(defaultValue) : std::string_view());
}
//---------------------------------------------------------------------------
std::string IASWToml::GetValue(TASWTomlPath const& path, char const* defaultValue,
    TASWTomlValueOptions const& options) const
{
    return GetValue(path, defaultValue != nullptr ? std::string_view(defaultValue) : std::string_view(), options);
}
//---------------------------------------------------------------------------
std::string IASWToml::GetValue(TASWTomlPath const& path, std::string_view defaultValue) const
{
    return GetOrDefault(path, std::string(defaultValue), nullptr);
}
//---------------------------------------------------------------------------
std::string IASWToml::GetValue(TASWTomlPath const& path, std::string_view defaultValue,
    TASWTomlValueOptions const& options) const
{
    return GetOrDefault(path, std::string(defaultValue), &options);
}
//---------------------------------------------------------------------------
TASWTomlResult IASWToml::InvalidPath(TASWTomlPath const& path)
{
    TASWTomlResult result;
    result.Code = ETomlCode::InvalidArgument;
    result.Message = path.GetError();

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult IASWToml::NoValueAt(TASWTomlPath const& path)
{
    TASWTomlResult result;
    result.Code = ETomlCode::NotFound;
    result.Path = path.ToString();
    result.Message = "There is no value at " + result.Path + ".";

    return result;
}
//---------------------------------------------------------------------------
void IASWToml::ReportFailure(TASWTomlResult const& result) const noexcept
{
    try
    {
        TASWTomlMessage message;
        message.Severity = ESeverity::Error;
        message.Code = result.Code;
        message.Text = result.Message;
        message.FilePath = result.FilePath;
        message.Line = result.Line;
        message.Column = result.Column;
        message.Path = result.Path;
        message.SystemError = result.SystemError;
        Report(message);
    }
    catch (...)
    {
        // The message couldn't be made (std::bad_alloc); Report() itself doesn't throw
    }
}
//---------------------------------------------------------------------------
void IASWToml::ReportValueWarning(TASWTomlResult const& result) const noexcept
{
    try
    {
        TASWTomlMessage message;
        message.Severity = ESeverity::Warning;
        message.Code = result.Code;
        message.Text = result.Message;
        message.Path = result.Path;
        Report(message);
    }
    catch (...)
    {
        // The message couldn't be made (std::bad_alloc); Report() itself doesn't throw
    }
}
//---------------------------------------------------------------------------
TASWTomlResult IASWToml::SetValue(TASWTomlPath const& path, char const* value)
{
    return WriteText(path, value != nullptr ? std::string_view(value) : std::string_view(),
        &IASWToml::DocumentSetValue, nullptr);
}
//---------------------------------------------------------------------------
TASWTomlResult IASWToml::SetValue(TASWTomlPath const& path, char const* value, TASWTomlValueOptions const& options)
{
    return WriteText(path, value != nullptr ? std::string_view(value) : std::string_view(),
        &IASWToml::DocumentSetValue, &options);
}
//---------------------------------------------------------------------------
TASWTomlResult IASWToml::SetValue(TASWTomlPath const& path, std::string_view value)
{
    return WriteText(path, value, &IASWToml::DocumentSetValue, nullptr);
}
//---------------------------------------------------------------------------
TASWTomlResult IASWToml::SetValue(TASWTomlPath const& path, std::string_view value,
    TASWTomlValueOptions const& options)
{
    return WriteText(path, value, &IASWToml::DocumentSetValue, &options);
}
//---------------------------------------------------------------------------
TASWTomlResult IASWToml::WriteText(TASWTomlPath const& path, std::string_view text, TStoreMethod store,
    TASWTomlValueOptions const* options)
{
    // As a std::string, so its converter checks it as it checks any other string
    std::string string;
    try
    {
        string = text;
    }
    catch (...)
    {
        TASWTomlResult const result = CaughtException(path);
        ReportFailure(result);

        return result;
    }

    return WriteValue(path, string, store, options);
}
//---------------------------------------------------------------------------

} // namespace ASWToml
