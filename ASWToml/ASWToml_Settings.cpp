/* **************************************************************************
ASWToml_Settings.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "ASWToml_Settings.h"
//---------------------------------------------------------------------------
#include <string>
#include <string_view>
#include <utility>
//---------------------------------------------------------------------------
#include "ASWToml_Interface.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

namespace Detail
{

//---------------------------------------------------------------------------
TASWTomlResult OutOfSettingRange(TASWTomlValue const& value, TASWTomlValue const* min, TASWTomlValue const* max)
{
    std::string problem = "is out of the setting's range";
    if (min != nullptr && max != nullptr)
        problem += ": it must be from " + ValueText(*min) + " to " + ValueText(*max);
    else if (min != nullptr)
        problem += ": it must be at least " + ValueText(*min);
    else if (max != nullptr)
        problem += ": it must be at most " + ValueText(*max);

    problem += ".";

    return ValueFailure(ETomlCode::ValueOutOfRange, value, problem);
}
//---------------------------------------------------------------------------
TASWTomlResult RefusedValue(TASWTomlValue const& value, std::string_view problem)
{
    std::string sentence = "isn't valid: ";
    sentence += problem;
    if (sentence.back() != '.')
        sentence += ".";

    return ValueFailure(ETomlCode::InvalidValue, value, sentence);
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TSettingBase
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TSettingBase::TSettingBase(TASWTomlTable const& table, std::string_view key, std::string_view comment)
    : m_Owner(&table.GetOwner()),
      m_Path(table.GetPath()),
      m_Key(key),
      m_Comment(comment)
{
    m_Path.Append(m_Key);
    if (!table.GetPath().IsValid())
    {
        m_PathError = "The path of the setting's table isn't valid: " + table.GetPath().GetError();
        return;
    }

    if (!m_Path.IsValid())
    {
        m_PathError = "The setting's key isn't valid: " + m_Path.GetError();
        return;
    }

    for (TASWTomlPathPart const& part : table.GetPath().GetParts())
    {
        if (part.IsIndex())
        {
            m_PathError = "The path of the setting's table, " + table.GetPath().ToString() +
                ", has an index: a table of settings is named by keys only.";
            break;
        }
    }
}
//---------------------------------------------------------------------------
TASWTomlResult TSettingBase::CaughtSetFailure() const noexcept
{
    return TASWBaseToml::CaughtException(m_Path);
}
//---------------------------------------------------------------------------
bool TSettingBase::FindValue(TASWTomlValue& value) const
{
    TASWTomlResult const problem = PathProblem();
    if (!problem.IsOk())
    {
        m_Owner->ReportFailure(problem);
        return false;
    }

    return m_Owner->DocumentFind(m_Path, value);
}
//---------------------------------------------------------------------------
std::string const& TSettingBase::GetComment() const noexcept
{
    return m_Comment;
}
//---------------------------------------------------------------------------
std::string const& TSettingBase::GetKey() const noexcept
{
    return m_Key;
}
//---------------------------------------------------------------------------
TASWBaseToml& TSettingBase::GetOwner() const noexcept
{
    return *m_Owner;
}
//---------------------------------------------------------------------------
TASWTomlPath const& TSettingBase::GetPath() const noexcept
{
    return m_Path;
}
//---------------------------------------------------------------------------
bool TSettingBase::IsPresent() const
{
    return m_Owner->Contains(m_Path);
}
//---------------------------------------------------------------------------
TASWTomlResult TSettingBase::PathProblem() const
{
    TASWTomlResult result;
    if (m_PathError.empty())
        return result;

    result.Code = ETomlCode::InvalidArgument;
    result.Message = m_PathError;
    result.Path = m_Path.ToString();

    return result;
}
//---------------------------------------------------------------------------
void TSettingBase::ReportDefaultUsed(TASWTomlResult const& result, TASWTomlValue const* defaultValue) const noexcept
{
    try
    {
        TASWTomlMessage message;
        message.Severity = ESeverity::Warning;
        message.Code = result.Code;
        message.Text = result.Message;
        if (defaultValue != nullptr)
            message.Text += " The default value " + ValueText(*defaultValue) + " is used.";
        else
            message.Text += " The default value is used.";

        message.Path = PathFailure(result, m_Path).Path;
        m_Owner->Report(message);
    }
    catch (...)
    {
        // The message couldn't be made (std::bad_alloc); Report() itself doesn't throw
    }
}
//---------------------------------------------------------------------------
void TSettingBase::ReportFailure(TASWTomlResult const& result) const noexcept
{
    m_Owner->ReportFailure(result);
}
//---------------------------------------------------------------------------
TASWTomlResult TSettingBase::SetFailure(TASWTomlResult result) const
{
    return PathFailure(std::move(result), m_Path);
}
//---------------------------------------------------------------------------
TASWTomlResult TSettingBase::WriteValue(TASWTomlValue const& value, TASWTomlValueOptions const& values)
{
    return m_Owner->SetSettingValue(m_Path, value, values, m_Comment);
}
//---------------------------------------------------------------------------

} // namespace Detail

//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlTable
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlTable::TASWTomlTable(TASWBaseToml& owner, TASWTomlPath const& path, std::string_view comment)
    : m_Owner(&owner),
      m_Path(path),
      m_Comment(comment)
{
}
//---------------------------------------------------------------------------
std::string const& TASWTomlTable::GetComment() const noexcept
{
    return m_Comment;
}
//---------------------------------------------------------------------------
TASWBaseToml& TASWTomlTable::GetOwner() const noexcept
{
    return *m_Owner;
}
//---------------------------------------------------------------------------
TASWTomlPath const& TASWTomlTable::GetPath() const noexcept
{
    return m_Path;
}
//---------------------------------------------------------------------------

} // namespace ASWToml
