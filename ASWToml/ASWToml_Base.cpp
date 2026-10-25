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
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
//---------------------------------------------------------------------------
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
TASWTomlResult TASWBaseToml::LoadText(std::string_view text)
{
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
            std::lock_guard<Detail::TSharedMutex> lock(m_Mutex);
            m_Document = std::move(document);
        }
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

} // namespace ASWToml
