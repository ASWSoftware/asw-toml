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
#include <exception>
#include <string>
#include <string_view>
#include <utility>
//---------------------------------------------------------------------------

namespace ASWToml
{

namespace Detail
{

//---------------------------------------------------------------------------
TASWTomlResult PathFailure(TASWTomlResult result, TASWTomlPath const& path)
{
    std::string place = path.ToString();
    if (place.empty())
        return result;

    if (!result.Path.empty())
    {
        if (result.Path.front() != '[')
            place += '.';

        place += result.Path;
    }

    result.Path = std::move(place);

    return result;
}
//---------------------------------------------------------------------------

} // namespace Detail

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
