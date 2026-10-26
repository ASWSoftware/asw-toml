/* **************************************************************************
ASWToml_Base.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "ASWToml_Base.h"
//---------------------------------------------------------------------------
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
//---------------------------------------------------------------------------
#include "ASWToml_File.h"
#include "ASWToml_Settings.h"
#include "ASWToml_Version.h"
//---------------------------------------------------------------------------

// A ThreadSanitizer build, whose runtime has the annotation the locks' destructors use (declared here as in
// <sanitizer/tsan_interface.h>)
#if defined(__SANITIZE_THREAD__)
#  define ASWTOML_THREAD_SANITIZER 1
#elif defined(__has_feature)
#  if __has_feature(thread_sanitizer)
#    define ASWTOML_THREAD_SANITIZER 1
#  endif
#endif

#if defined(ASWTOML_THREAD_SANITIZER)
extern "C" void __tsan_mutex_destroy(void* addr, unsigned flags);
#endif
//---------------------------------------------------------------------------

namespace ASWToml
{

namespace
{

// The message of Load()'s and SaveAs()'s InvalidArgument for an empty file path
constexpr char const* EmptyFilePathText = "The file path is empty.";

TASWTomlResult Failure(ETomlCode code, char const* message);
TASWTomlMessage MissingFileWarning(TASWTomlResult const& result);
void SetFailureFilePath(std::string_view filePath, TASWTomlResult& result) noexcept;
void SetFilePaths(std::string const& filePath, TASWTomlResult& result, TMessageVector& messages);

//---------------------------------------------------------------------------
/*
    Failure

    A failed result with 'code' and 'message'.
*/
TASWTomlResult Failure(ETomlCode code, char const* message)
{
    TASWTomlResult result;
    result.Code = code;
    result.Message = message;

    return result;
}
//---------------------------------------------------------------------------
/*
    MissingFileWarning

    The Warning message Load() reports for 'result', its FileNotFound result (the file it emptied the document for
    doesn't exist), with the result's code, text, file path and system error.
*/
TASWTomlMessage MissingFileWarning(TASWTomlResult const& result)
{
    TASWTomlMessage message;
    message.Severity = ESeverity::Warning;
    message.Code = result.Code;
    message.Text = result.Message;
    message.FilePath = result.FilePath;
    message.SystemError = result.SystemError;

    return message;
}
//---------------------------------------------------------------------------
/*
    SetFailureFilePath

    Sets the file path of 'result', the result of an exception Load() or SaveAs() caught (which has none), to
    'filePath', the file it was given, unless the text can't be made (std::bad_alloc: the result is complete without
    it), so its report doesn't take the object's file path, which is another file.
*/
void SetFailureFilePath(std::string_view filePath, TASWTomlResult& result) noexcept
{
    try
    {
        result.FilePath = filePath;
    }
    catch (...)
    {
        // The file path couldn't be made (std::bad_alloc)
    }
}
//---------------------------------------------------------------------------
/*
    SetFilePaths

    Sets the file path of 'result', when it failed, and of each of 'messages' to 'filePath', the file they are about,
    where they have none, so their reports don't take the object's file path, which may be another file by then.
*/
void SetFilePaths(std::string const& filePath, TASWTomlResult& result, TMessageVector& messages)
{
    if (!result.IsOk() && result.FilePath.empty())
        result.FilePath = filePath;

    for (TASWTomlMessage& message : messages)
    {
        if (message.FilePath.empty())
            message.FilePath = filePath;
    }
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

namespace Detail
{

/////////////////////////////////////////////////////////////////////////////
// TMutex
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TMutex::~TMutex()
{
#if defined(ASWTOML_THREAD_SANITIZER) && defined(__GLIBCXX__)
    // What the pthread_mutex_destroy() that libstdc++ leaves out would tell it (libc++ calls it)
    __tsan_mutex_destroy(m_Mutex.native_handle(), 0);
#endif
}
//---------------------------------------------------------------------------
void TMutex::lock()
{
    m_Mutex.lock();
}
//---------------------------------------------------------------------------
void TMutex::unlock() noexcept
{
    m_Mutex.unlock();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TSharedMutex
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TSharedMutex::~TSharedMutex()
{
#if defined(ASWTOML_THREAD_SANITIZER) && defined(_GLIBCXX_USE_PTHREAD_RWLOCK_T)
    // What the pthread_rwlock_destroy() that libstdc++ leaves out would tell it. Only libstdc++'s std::shared_mutex
    // on a pthread rwlock (_GLIBCXX_USE_PTHREAD_RWLOCK_T) has a native_handle(); libc++ builds its std::shared_mutex
    // on a std::mutex and condition variables, which it destroys.
    __tsan_mutex_destroy(m_Mutex.native_handle(), 0);
#endif
}
//---------------------------------------------------------------------------
void TSharedMutex::lock()
{
    m_Mutex.lock();
}
//---------------------------------------------------------------------------
void TSharedMutex::lock_shared()
{
    m_Mutex.lock_shared();
}
//---------------------------------------------------------------------------
void TSharedMutex::unlock() noexcept
{
    m_Mutex.unlock();
}
//---------------------------------------------------------------------------
void TSharedMutex::unlock_shared() noexcept
{
    m_Mutex.unlock_shared();
}
//---------------------------------------------------------------------------

} // namespace Detail

//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWBaseToml
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
void TASWBaseToml::ApplySettings(TASWTomlDocument& document, std::string_view filePath, TMessageVector& messages) const
{
    // An object without settings loads as one that has none
    if (m_Settings.empty() && m_Tables.empty())
        return;

    // The settings first, so a table a setting added gets its comment too
    Detail::TSettingsApply apply(document);
    for (Detail::TSettingBase const* setting : m_Settings)
        setting->Apply(apply);

    for (TASWTomlTable const* table : m_Tables)
    {
        if (Detail::IsSettingsTablePath(table->GetPath()))
            apply.AddComment(table->GetPath(), table->GetComment(), true);
    }

    TMessageVector applied;
    apply.Write(applied);
    for (TASWTomlMessage& message : applied)
    {
        if (message.FilePath.empty())
            message.FilePath = filePath;
    }

    // Room is made first, so the messages are then moved in without throwing
    messages.reserve(messages.size() + applied.size());
    std::move(applied.begin(), applied.end(), std::back_inserter(messages));
}
//---------------------------------------------------------------------------
bool TASWBaseToml::Contains(TASWTomlPath const& path) const
{
    std::shared_lock<Detail::TSharedMutex> lock(m_Mutex);

    return m_Document.Find(path) != nullptr;
}
//---------------------------------------------------------------------------
TASWTomlResult TASWBaseToml::DocumentAppend(TASWTomlPath const& path, TASWTomlValue const& value,
    TASWTomlValueOptions const& values)
{
    return Edit(static_cast<TValueMethod>(&TASWTomlDocument::Append), path, value, values);
}
//---------------------------------------------------------------------------
bool TASWBaseToml::DocumentFind(TASWTomlPath const& path, TASWTomlValue& value) const
{
    std::shared_lock<Detail::TSharedMutex> lock(m_Mutex);
    TASWTomlValue const* const found = m_Document.Find(path);
    if (found == nullptr)
        return false;

    value = *found;

    return true;
}
//---------------------------------------------------------------------------
TASWTomlResult TASWBaseToml::DocumentSetValue(TASWTomlPath const& path, TASWTomlValue const& value,
    TASWTomlValueOptions const& values)
{
    return Edit(static_cast<TValueMethod>(&TASWTomlDocument::SetValue), path, value, values);
}
//---------------------------------------------------------------------------
template <typename TMethod, typename ... TArgs>
TASWTomlResult TASWBaseToml::Edit(TMethod method, TASWTomlPath const& path, TArgs const& ... args)
{
    TASWTomlResult result;

    try
    {
        std::lock_guard<Detail::TSharedMutex> lock(m_Mutex);
        result = (m_Document.*method)(path, args ...);
        if (result.IsOk())
            ++m_EditCount;
    }
    catch (...)
    {
        result = CaughtException(path);
    }

    if (!result.IsOk())
        ReportFailure(result);

    return result;
}
//---------------------------------------------------------------------------
std::string TASWBaseToml::GetComment(TASWTomlPath const& path) const
{
    std::shared_lock<Detail::TSharedMutex> lock(m_Mutex);

    return m_Document.GetComment(path);
}
//---------------------------------------------------------------------------
TASWTomlDocument TASWBaseToml::GetDocument() const
{
    std::shared_lock<Detail::TSharedMutex> lock(m_Mutex);

    return m_Document;
}
//---------------------------------------------------------------------------
std::string TASWBaseToml::GetFilePath() const
{
    std::shared_lock<Detail::TSharedMutex> lock(m_Mutex);

    return m_FilePath;
}
//---------------------------------------------------------------------------
TStringVector TASWBaseToml::GetKeyNames(TASWTomlPath const& path) const
{
    TStringVector names;
    std::shared_lock<Detail::TSharedMutex> lock(m_Mutex);
    TASWTomlValue const* const found = m_Document.Find(path);
    if (found == nullptr || !found->IsTable())
        return names;

    TASWTomlValue::TKeyValueVector const& table = *found->AsTable();
    names.reserve(table.size());
    for (TASWTomlKeyValue const& keyValue : table)
        names.push_back(keyValue.Key);

    return names;
}
//---------------------------------------------------------------------------
TASWTomlOptions TASWBaseToml::GetOptions() const
{
    std::shared_lock<Detail::TSharedMutex> lock(m_Mutex);

    return m_Document.GetOptions();
}
//---------------------------------------------------------------------------
std::string TASWBaseToml::GetTrailingComment(TASWTomlPath const& path) const
{
    std::shared_lock<Detail::TSharedMutex> lock(m_Mutex);

    return m_Document.GetTrailingComment(path);
}
//---------------------------------------------------------------------------
TASWTomlValueOptions TASWBaseToml::GetValueOptions() const
{
    std::shared_lock<Detail::TSharedMutex> lock(m_Mutex);

    return m_Document.GetOptions().Values;
}
//---------------------------------------------------------------------------
std::string_view TASWBaseToml::GetVersionStr() const noexcept
{
    return Version;
}
//---------------------------------------------------------------------------
bool TASWBaseToml::IsModified() const
{
    std::shared_lock<Detail::TSharedMutex> lock(m_Mutex);

    return m_Document.IsModified();
}
//---------------------------------------------------------------------------
TASWTomlResult TASWBaseToml::Load(std::string_view filePath)
{
    TMessageVector messages;
    TASWTomlResult result;
    bool missing = false;

    try
    {
        result = LoadFile(filePath, messages, missing);
    }
    catch (...)
    {
        result = CaughtException();
        SetFailureFilePath(filePath, result);
    }

    ReportAll(messages);

    // A missing file is in 'messages', as a Warning
    if (!result.IsOk() && !missing)
        ReportFailure(result);

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TASWBaseToml::LoadFile(std::string_view filePath, TMessageVector& messages, bool& missing)
{
    if (filePath.empty())
        return Failure(ETomlCode::InvalidArgument, EmptyFilePathText);

    std::string path(filePath);
    std::lock_guard<Detail::TMutex> fileLock(m_FileMutex);
    TASWTomlOptions options;
    {
        std::shared_lock<Detail::TSharedMutex> lock(m_Mutex);
        options = m_Document.GetOptions();
    }

    // Read and parsed without the lock, as LoadText() parses; an edit made meanwhile is replaced by the file
    std::string text;
    TASWTomlDocument document;
    TASWTomlResult result = ReadFile(path, options.Reading.MaxFileSize, text);
    bool const notFound = result.Code == ETomlCode::FileNotFound;
    if (result.IsOk())
        result = document.Parse(text, options);
    else if (notFound)
        static_cast<void>(document.Reset(options)); // The options are the document's, so they are valid

    SetFilePaths(path, result, messages);
    if (!result.IsOk() && !notFound)
        return result;

    // An application's first run has no file: the document is emptied and gets the path, so Save() creates the file
    if (notFound)
        messages.push_back(MissingFileWarning(result));

    // The settings are applied to the document read (or emptied), and their messages are about the file too
    ApplySettings(document, path, messages);

    {
        std::lock_guard<Detail::TSharedMutex> lock(m_Mutex);
        m_Document = std::move(document);
        m_FilePath = std::move(path);
    }

    missing = notFound;

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TASWBaseToml::LoadText(std::string_view text)
{
    TMessageVector messages;
    TASWTomlResult result;

    try
    {
        std::lock_guard<Detail::TMutex> fileLock(m_FileMutex);
        TASWTomlOptions options;
        {
            std::shared_lock<Detail::TSharedMutex> lock(m_Mutex);
            options = m_Document.GetOptions();
        }

        // Parsed without the lock, so lookups and edits go on meanwhile (an edit made meanwhile is replaced by the
        // text); the options can't change, since SetOptions() waits for the file lock
        TASWTomlDocument document;
        result = document.Parse(text, options);
        if (result.IsOk())
        {
            // The settings are applied to the document read, before anyone sees it
            ApplySettings(document, std::string_view(), messages);

            std::lock_guard<Detail::TSharedMutex> lock(m_Mutex);
            m_Document = std::move(document);
        }
    }
    catch (...)
    {
        result = CaughtException();
    }

    ReportAll(messages);

    if (!result.IsOk())
        ReportFailure(result);

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TASWBaseToml::ReadFile(std::string const& filePath, std::size_t maxSize, std::string& contents)
{
    return Detail::ReadWholeFile(Utf8ToPath(filePath), maxSize, contents);
}
//---------------------------------------------------------------------------
void TASWBaseToml::Register(Detail::TSettingBase const& setting)
{
    std::lock_guard<Detail::TMutex> fileLock(m_FileMutex);
    m_Settings.push_back(&setting);
}
//---------------------------------------------------------------------------
void TASWBaseToml::Register(TASWTomlTable const& table)
{
    std::lock_guard<Detail::TMutex> fileLock(m_FileMutex);
    m_Tables.push_back(&table);
}
//---------------------------------------------------------------------------
bool TASWBaseToml::Remove(TASWTomlPath const& path)
{
    std::lock_guard<Detail::TSharedMutex> lock(m_Mutex);
    bool const removed = m_Document.Remove(path);
    if (removed)
        ++m_EditCount;

    return removed;
}
//---------------------------------------------------------------------------
TASWTomlResult TASWBaseToml::Rename(TASWTomlPath const& path, std::string_view newKey)
{
    return Edit(&TASWTomlDocument::Rename, path, newKey);
}
//---------------------------------------------------------------------------
void TASWBaseToml::Report(TASWTomlMessage const& message) const noexcept
{
    try
    {
        // The callback is called through a copy of the pointer, so SetLogCallback() may replace it meanwhile
        TLogCallbackPtr callback;
        std::string filePath;
        {
            std::shared_lock<Detail::TSharedMutex> lock(m_Mutex);
            callback = m_LogCallback;
            if (message.FilePath.empty())
                filePath = m_FilePath;
        }

        if (callback == nullptr)
            return;

        if (filePath.empty())
        {
            (*callback)(message);
        }
        else
        {
            TASWTomlMessage withPath = message;
            withPath.FilePath = std::move(filePath);
            (*callback)(withPath);
        }
    }
    catch (...)
    {
        // The callback threw, or the message couldn't be made (std::bad_alloc): reporting never throws
    }
}
//---------------------------------------------------------------------------
void TASWBaseToml::ReportAll(TMessageVector const& messages) const noexcept
{
    for (TASWTomlMessage const& message : messages)
        Report(message);
}
//---------------------------------------------------------------------------
TASWTomlResult TASWBaseToml::Save()
{
    return SaveTo(nullptr);
}
//---------------------------------------------------------------------------
TASWTomlResult TASWBaseToml::SaveAs(std::string_view filePath)
{
    return SaveTo(&filePath);
}
//---------------------------------------------------------------------------
TASWTomlResult TASWBaseToml::SaveFile(std::string_view const* filePath, TMessageVector& messages)
{
    if (filePath != nullptr && filePath->empty())
        return Failure(ETomlCode::InvalidArgument, EmptyFilePathText);

    std::lock_guard<Detail::TMutex> fileLock(m_FileMutex);
    std::string path;
    std::string text;
    TASWTomlSavingOptions options;
    unsigned long long editCount = 0;
    {
        std::shared_lock<Detail::TSharedMutex> lock(m_Mutex);
        path = filePath != nullptr ? std::string(*filePath) : m_FilePath;
        if (path.empty())
            return Failure(ETomlCode::NoFilePath, "There is no file path to save to; Load() or SaveAs() sets one.");

        text = m_Document.ToText();
        options = m_Document.GetOptions().Saving;
        editCount = m_EditCount;
    }

    // Written without the lock, so lookups and edits go on meanwhile
    TASWTomlResult result = WriteFile(path, text, options, messages);
    SetFilePaths(path, result, messages);
    if (!result.IsOk())
        return result;

    std::lock_guard<Detail::TSharedMutex> lock(m_Mutex);

    // An edit made while the file was being written isn't in the file
    if (m_EditCount == editCount)
        m_Document.ClearModified();

    if (filePath != nullptr)
        m_FilePath = std::move(path);

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TASWBaseToml::SaveTo(std::string_view const* filePath)
{
    TMessageVector messages;
    TASWTomlResult result;

    try
    {
        result = SaveFile(filePath, messages);
    }
    catch (...)
    {
        result = CaughtException();
        if (filePath != nullptr)
            SetFailureFilePath(*filePath, result);
    }

    ReportAll(messages);

    if (!result.IsOk())
        ReportFailure(result);

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TASWBaseToml::SetComment(TASWTomlPath const& path, std::string_view text)
{
    return Edit(&TASWTomlDocument::SetComment, path, text);
}
//---------------------------------------------------------------------------
void TASWBaseToml::SetLogCallback(TASWTomlLogCallback callback)
{
    TLogCallbackPtr newCallback;
    if (callback)
        newCallback = std::make_shared<TASWTomlLogCallback const>(std::move(callback));

    {
        std::lock_guard<Detail::TSharedMutex> lock(m_Mutex);
        m_LogCallback.swap(newCallback);
    }

    // The old callback is destroyed here, with no lock held (unless a report is still calling it)
}
//---------------------------------------------------------------------------
TASWTomlResult TASWBaseToml::SetOptions(TASWTomlOptions const& options)
{
    TASWTomlResult result;

    try
    {
        // Read again under the lock, so an edit made meanwhile isn't lost
        std::lock_guard<Detail::TMutex> fileLock(m_FileMutex);
        std::lock_guard<Detail::TSharedMutex> lock(m_Mutex);
        result = m_Document.SetOptions(options);
    }
    catch (...)
    {
        result = CaughtException();
    }

    if (!result.IsOk())
        ReportFailure(result);

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TASWBaseToml::SetSettingValue(TASWTomlPath const& path, TASWTomlValue const& value,
    TASWTomlValueOptions const& values, std::string_view comment, Detail::TValueOverlay const* overlay)
{
    TASWTomlResult result;

    try
    {
        std::lock_guard<Detail::TSharedMutex> lock(m_Mutex);
        TASWTomlValue const* const old = m_Document.Find(path);
        if (old != nullptr && overlay != nullptr)
        {
            // The document's merge pairs the elements of arrays as the overlay did
            Detail::TPartnersMap partners;
            TASWTomlValue const overlaid = overlay->Overlay(*old, value, partners);
            result = m_Document.SetValue(path, overlaid, values, partners);
        }
        else if (comment.empty() || old != nullptr)
        {
            result = m_Document.SetValue(path, value, values);
        }
        else
        {
            // The key and its comment are added to a copy, which replaces the document only when both succeed; a key
            // without a line of its own (InvalidArgument: in an inline table) can't have a comment, and is kept
            TASWTomlDocument changed = m_Document;
            result = changed.SetValue(path, value, values);
            if (result.IsOk())
            {
                TASWTomlResult const commented = changed.SetComment(path, comment);
                if (!commented.IsOk() && commented.Code != ETomlCode::InvalidArgument)
                    result = commented;
            }

            if (result.IsOk())
                m_Document = std::move(changed);
        }

        if (result.IsOk())
            ++m_EditCount;
    }
    catch (...)
    {
        result = CaughtException(path);
    }

    if (!result.IsOk())
        ReportFailure(result);

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult TASWBaseToml::SetTrailingComment(TASWTomlPath const& path, std::string_view text)
{
    return Edit(&TASWTomlDocument::SetTrailingComment, path, text);
}
//---------------------------------------------------------------------------
std::string TASWBaseToml::ToText() const
{
    std::shared_lock<Detail::TSharedMutex> lock(m_Mutex);

    return m_Document.ToText();
}
//---------------------------------------------------------------------------
void TASWBaseToml::Unregister(Detail::TSettingBase const& setting)
{
    std::lock_guard<Detail::TMutex> fileLock(m_FileMutex);
    m_Settings.erase(std::remove(m_Settings.begin(), m_Settings.end(), &setting), m_Settings.end());
}
//---------------------------------------------------------------------------
void TASWBaseToml::Unregister(TASWTomlTable const& table)
{
    std::lock_guard<Detail::TMutex> fileLock(m_FileMutex);
    m_Tables.erase(std::remove(m_Tables.begin(), m_Tables.end(), &table), m_Tables.end());
}
//---------------------------------------------------------------------------
TASWTomlResult TASWBaseToml::WriteFile(std::string const& filePath, std::string_view contents,
    TASWTomlSavingOptions const& options, TMessageVector& warnings)
{
    return Detail::WriteWholeFile(Utf8ToPath(filePath), contents, options, warnings);
}
//---------------------------------------------------------------------------

} // namespace ASWToml
