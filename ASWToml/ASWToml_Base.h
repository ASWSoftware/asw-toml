/* **************************************************************************
ASWToml_Base.h
Author: Anthony S. West - ASW Software

A TOML file reader/writer library.

TASWBaseToml, the thread-safe TOML object that implements IASWToml: a
TASWTomlDocument, its file path, the locks and the log callback. An
application's settings class derives from it. The locks it uses.

Requires C++ 20 or higher.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

#pragma once

#ifndef ASWToml_BaseH
#define ASWToml_BaseH
//---------------------------------------------------------------------------
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWToml_Document.h"
#include "ASWToml_Interface.h"
#include "ASWToml_Options.h"
#include "ASWToml_Path.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

// The internals of TASWBaseToml
namespace Detail
{

/////////////////////////////////////////////////////////////////////////////
// TMutex
//
// A std::mutex, locked as one is (std::lock_guard, std::unique_lock). In a ThreadSanitizer build with libstdc++ its
// destructor tells ThreadSanitizer that the mutex is gone: libstdc++'s std::mutex never destroys its pthread mutex,
// so ThreadSanitizer would take a later mutex at the same address (e.g. another object's, on a reused stack) for this
// one, and report lock-order inversions between unrelated objects. Neither copyable nor movable.
/////////////////////////////////////////////////////////////////////////////
class TMutex
{
private:
    std::mutex m_Mutex;

public:
    TMutex() = default;
    TMutex(TMutex const&) = delete;
    TMutex& operator=(TMutex const&) = delete;
    ~TMutex();

    // The names std::lock_guard and std::unique_lock call
    void lock();
    void unlock() noexcept;
};


/////////////////////////////////////////////////////////////////////////////
// TSharedMutex
//
// A std::shared_mutex, locked as one is (std::lock_guard and std::unique_lock for writing, std::shared_lock for
// reading). In a ThreadSanitizer build its destructor tells ThreadSanitizer that the mutex is gone, as TMutex's does
// (libstdc++'s std::shared_mutex on a pthread rwlock never destroys it either). Neither copyable nor movable.
/////////////////////////////////////////////////////////////////////////////
class TSharedMutex
{
private:
    std::shared_mutex m_Mutex;

public:
    TSharedMutex() = default;
    TSharedMutex(TSharedMutex const&) = delete;
    TSharedMutex& operator=(TSharedMutex const&) = delete;
    ~TSharedMutex();

    // The names std::lock_guard, std::unique_lock and std::shared_lock call
    void lock();
    void lock_shared();
    void unlock() noexcept;
    void unlock_shared() noexcept;
};

} // namespace Detail


/////////////////////////////////////////////////////////////////////////////
// TASWBaseToml
//
// The thread-safe TOML object: a TASWTomlDocument (empty, with the default options, until a text or a file is
// loaded), its file path and the log callback, with IASWToml's operations, which any thread may call at any time. Each
// operation is complete in itself: a lookup sees the document before or after an edit or a load, never during one.
// Lookups share a reader/writer lock; edits take it alone. LoadText() parses the text without that lock, so lookups
// and edits go on meanwhile; a second lock keeps loads and SetOptions() from overlapping. Failures are reported after
// the locks are released (see TASWTomlLogCallback). An application's settings class derives from it. Neither copyable
// nor movable.
/////////////////////////////////////////////////////////////////////////////
class TASWBaseToml : public IASWToml
{
private:
    // The log callback, shared with the reports that are calling it, so SetLogCallback() may replace it meanwhile
    using TLogCallbackPtr = std::shared_ptr<TASWTomlLogCallback const>;

    // TASWTomlDocument::SetValue() and Append() with value options, which DocumentSetValue() and DocumentAppend() edit
    // with
    using TValueMethod = TASWTomlResult (TASWTomlDocument::*)(TASWTomlPath const&, TASWTomlValue const&,
        TASWTomlValueOptions const&);

private:
    mutable Detail::TSharedMutex m_Mutex; // Guards the members after it
    TASWTomlDocument m_Document;
    std::string m_FilePath; // UTF-8
    TLogCallbackPtr m_LogCallback; // nullptr when there is none
    unsigned long long m_EditCount = 0; // Counts the edits that succeeded, so a save can tell whether one was made

    // Held for the whole of LoadText() and SetOptions(), so they never overlap; taken before m_Mutex, never after it
    Detail::TMutex m_FileMutex;

private:
    // Calls 'method', an edit of TASWTomlDocument, with 'path' and 'args' on the document under the lock, and counts
    // it when it succeeds; an exception becomes CaughtException(path). Reports a failed result.
    template <typename TMethod, typename ... TArgs>
    TASWTomlResult Edit(TMethod method, TASWTomlPath const& path, TArgs const& ... args);

protected:
    TASWTomlResult DocumentAppend(TASWTomlPath const& path, TASWTomlValue const& value,
        TASWTomlValueOptions const& values) override;
    bool DocumentFind(TASWTomlPath const& path, TASWTomlValue& value) const override;
    TASWTomlResult DocumentSetValue(TASWTomlPath const& path, TASWTomlValue const& value,
        TASWTomlValueOptions const& values) override;

    // Calls the log callback with 'message' (FilePath set to the file path when it is empty), unless there is none.
    // An exception the callback throws is dropped.
    void Report(TASWTomlMessage const& message) const noexcept override;

public:
    // An empty document with the default options, no file path and no log callback
    TASWBaseToml() = default;

    bool Contains(TASWTomlPath const& path) const override;
    std::string GetComment(TASWTomlPath const& path) const override;
    TASWTomlDocument GetDocument() const override;
    std::string GetFilePath() const override;
    TStringVector GetKeyNames(TASWTomlPath const& path) const override;
    TASWTomlOptions GetOptions() const override;
    std::string GetTrailingComment(TASWTomlPath const& path) const override;
    TASWTomlValueOptions GetValueOptions() const override;
    std::string_view GetVersionStr() const noexcept override;
    bool IsModified() const override;
    TASWTomlResult LoadText(std::string_view text) override;
    bool Remove(TASWTomlPath const& path) override;
    TASWTomlResult Rename(TASWTomlPath const& path, std::string_view newKey) override;
    TASWTomlResult SetComment(TASWTomlPath const& path, std::string_view text) override;
    void SetLogCallback(TASWTomlLogCallback callback) override;
    TASWTomlResult SetOptions(TASWTomlOptions const& options) override;
    TASWTomlResult SetTrailingComment(TASWTomlPath const& path, std::string_view text) override;
    std::string ToText() const override;
};

} // namespace ASWToml

//---------------------------------------------------------------------------
#endif // #ifndef ASWToml_BaseH
