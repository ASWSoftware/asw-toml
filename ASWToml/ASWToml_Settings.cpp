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
#include "ASWToml_Document.h"
#include "ASWToml_Interface.h"
#include "ASWToml_Options.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

namespace
{

TASWTomlMessage ResultMessage(ESeverity severity, TASWTomlResult const& result);

//---------------------------------------------------------------------------
/*
    ResultMessage

    A message with 'severity', and the code, text and path of 'result'
*/
TASWTomlMessage ResultMessage(ESeverity severity, TASWTomlResult const& result)
{
    TASWTomlMessage message;
    message.Severity = severity;
    message.Code = result.Code;
    message.Text = result.Message;
    message.Path = result.Path;

    return message;
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

namespace Detail
{

//---------------------------------------------------------------------------
bool IsSettingsTablePath(TASWTomlPath const& path) noexcept
{
    if (!path.IsValid())
        return false;

    for (TASWTomlPathPart const& part : path.GetParts())
    {
        if (part.IsIndex())
            return false;
    }

    return true;
}
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
// TSettingsApply
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TSettingsApply::TSettingsApply(TASWTomlDocument& document)
    : m_Document(document)
{
}
//---------------------------------------------------------------------------
void TSettingsApply::AddComment(TASWTomlPath const& path, std::string const& text, bool isTable)
{
    if (!text.empty())
        m_Comments.push_back(TSettingComment{ path, text, isTable });
}
//---------------------------------------------------------------------------
void TSettingsApply::AddMessage(TASWTomlMessage message)
{
    m_Messages.push_back(std::move(message));
}
//---------------------------------------------------------------------------
void TSettingsApply::AddValue(TASWTomlPath const& tablePath, TASWTomlPath const& path, std::string const& key,
    TASWTomlValue value, TASWTomlValueOptions const* values, TASWTomlMessage const* added)
{
    TTableChanges* table = nullptr;
    for (TTableChanges& changes : m_Tables)
    {
        if (changes.Path == tablePath)
        {
            table = &changes;
            break;
        }
    }

    if (table == nullptr)
    {
        m_Tables.push_back(TTableChanges{ tablePath, TSettingChangeVector() });
        table = &m_Tables.back();
    }

    TSettingChange change;
    change.Path = path;
    change.Key = key;
    change.Value = std::move(value);
    if (values != nullptr)
        change.Values = *values;

    // A message with the code Ok holds the place of the Error of a write that fails
    change.Message = m_Messages.size();
    m_Messages.push_back(added != nullptr ? *added : TASWTomlMessage());
    table->Changes.push_back(std::move(change));
}
//---------------------------------------------------------------------------
TASWTomlDocument const& TSettingsApply::GetDocument() const noexcept
{
    return m_Document;
}
//---------------------------------------------------------------------------
void TSettingsApply::Write(TMessageVector& messages)
{
    for (TTableChanges const& table : m_Tables)
        WriteTable(table);

    WriteComments();
    for (TASWTomlMessage& message : m_Messages)
    {
        if (message.Code != ETomlCode::Ok)
            messages.push_back(std::move(message));
    }
}
//---------------------------------------------------------------------------
void TSettingsApply::WriteComments()
{
    // Each comment of a key or table that is there (a table only as a table), and with FileWins, only where there is
    // none
    bool const fileWins = m_Document.GetOptions().Writing.CommentPolicy == ECommentPolicy::FileWins;
    TASWTomlDocument::TPathCommentVector comments;
    for (TSettingComment const& comment : m_Comments)
    {
        TASWTomlValue const* const found = m_Document.Find(comment.Path);
        bool const there = found != nullptr && (!comment.IsTable || found->IsTable());
        if (there && (!fileWins || m_Document.GetComment(comment.Path).empty()))
            comments.push_back(TASWTomlPathComment{ comment.Path, comment.Text });
    }

    if (comments.empty() || m_Document.SetComments(comments).IsOk())
        return;

    // One at a time, so one that can't be set doesn't keep the others from being set
    for (TASWTomlPathComment const& comment : comments)
    {
        TASWTomlResult const result = m_Document.SetComment(comment.Path, comment.Text);
        if (!result.IsOk() && result.Code != ETomlCode::InvalidArgument)
            m_Messages.push_back(ResultMessage(ESeverity::Error, result));
    }
}
//---------------------------------------------------------------------------
void TSettingsApply::WriteTable(TTableChanges const& table)
{
    // The table as it is, with the changes merged in; a value there that isn't a table can't hold the settings, so it
    // is replaced by a table of their values
    TASWTomlValue merged;
    TASWTomlValue const* const found = m_Document.Find(table.Path);
    if (found != nullptr && found->IsTable())
    {
        merged = *found;
    }
    else if (found != nullptr)
    {
        TASWTomlResult replaced = TypeMismatch(*found, "a table");
        replaced.Message += " It is replaced by a table of its settings' values.";
        replaced.Path = table.Path.ToString();
        m_Messages.push_back(ResultMessage(ESeverity::Warning, replaced));
        static_cast<void>(m_Document.Remove(table.Path));
    }

    bool merging = false;
    for (TSettingChange const& change : table.Changes)
    {
        if (!change.Values.has_value())
        {
            merged.Set(change.Key, change.Value);
            merging = true;
        }
    }

    // When the merge fails, each change is written on its own, so each failure is its setting's
    bool const inOneEdit = merging && m_Document.SetValue(table.Path, merged).IsOk();
    for (TSettingChange const& change : table.Changes)
    {
        if (inOneEdit && !change.Values.has_value())
            continue;

        TASWTomlResult result;
        if (change.Values.has_value())
            result = m_Document.SetValue(change.Path, change.Value, *change.Values);
        else
            result = m_Document.SetValue(change.Path, change.Value);

        if (!result.IsOk())
            m_Messages[change.Message] = ResultMessage(ESeverity::Error, result);
    }
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TSettingBase
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TSettingBase::TSettingBase(TASWTomlTable const& table, std::string_view key, std::string_view comment)
    : m_Owner(&table.GetOwner()),
      m_TablePath(table.GetPath()),
      m_Path(table.GetPath()),
      m_Key(key),
      m_Comment(comment)
{
    m_Path.Append(m_Key);
    if (!m_TablePath.IsValid())
    {
        m_PathError = "The path of the setting's table isn't valid: " + m_TablePath.GetError();
    }
    else if (!m_Path.IsValid())
    {
        m_PathError = "The setting's key isn't valid: " + m_Path.GetError();
    }
    else if (!IsSettingsTablePath(m_TablePath))
    {
        m_PathError = "The path of the setting's table, " + m_TablePath.ToString() +
            ", has an index: a table of settings is named by keys only.";
    }
}
//---------------------------------------------------------------------------
void TSettingBase::AddFailure(TSettingsApply& apply, TASWTomlResult const& result) const
{
    apply.AddMessage(ResultMessage(ESeverity::Error, result));
}
//---------------------------------------------------------------------------
void TSettingBase::AddInvalidDefault(TSettingsApply& apply, TASWTomlResult const& result) const
{
    TASWTomlResult failure = PathFailure(result, m_Path);
    failure.Message = "The default value of the setting " + m_Path.ToString() + " can't be used: " + result.Message;
    AddFailure(apply, failure);
}
//---------------------------------------------------------------------------
void TSettingBase::ApplyComment(TSettingsApply& apply) const
{
    apply.AddComment(m_Path, m_Comment, false);
}
//---------------------------------------------------------------------------
void TSettingBase::ApplyDefault(TSettingsApply& apply, TASWTomlValue const& defaultValue,
    TASWTomlValueOptions const* values, TASWTomlResult const* problem) const
{
    TASWTomlMessage added;
    if (problem != nullptr)
    {
        apply.AddMessage(DefaultUsedMessage(*problem, &defaultValue));
    }
    else
    {
        added.Severity = ESeverity::Info;
        added.Code = ETomlCode::KeyAdded;
        added.Path = m_Path.ToString();
        added.Text = "The key " + added.Path + " is missing: it is added with its default value " +
            ValueText(defaultValue) + ".";
    }

    apply.AddValue(m_TablePath, m_Path, m_Key, defaultValue, values, problem == nullptr ? &added : nullptr);
    ApplyComment(apply);
}
//---------------------------------------------------------------------------
TASWTomlResult TSettingBase::CaughtSetFailure() const noexcept
{
    return TASWBaseToml::CaughtException(m_Path);
}
//---------------------------------------------------------------------------
TASWTomlMessage TSettingBase::DefaultUsedMessage(TASWTomlResult const& result, TASWTomlValue const* defaultValue) const
{
    TASWTomlMessage message = ResultMessage(ESeverity::Warning, PathFailure(result, m_Path));
    if (defaultValue != nullptr)
        message.Text += " The default value " + ValueText(*defaultValue) + " is used.";
    else
        message.Text += " The default value is used.";

    return message;
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
void TSettingBase::Register() const
{
    m_Owner->Register(*this);
}
//---------------------------------------------------------------------------
void TSettingBase::ReportDefaultUsed(TASWTomlResult const& result, TASWTomlValue const* defaultValue) const noexcept
{
    try
    {
        m_Owner->Report(DefaultUsedMessage(result, defaultValue));
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
void TSettingBase::Unregister() const
{
    m_Owner->Unregister(*this);
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
    m_Owner->Register(*this);
}
//---------------------------------------------------------------------------
TASWTomlTable::~TASWTomlTable()
{
    m_Owner->Unregister(*this);
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
