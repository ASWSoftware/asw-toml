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
#include <cstddef>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWToml_Document.h"
#include "ASWToml_Edits.h"
#include "ASWToml_Interface.h"
#include "ASWToml_Options.h"
#include "ASWToml_Path.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

class TASWTomlTable;

// The internals of TASWBaseToml
namespace Detail
{

class TSettingBase;

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
// Lookups share a reader/writer lock; edits take it alone. Load() reads and parses the file (and LoadText() the text)
// without that lock, and Save() writes the file without it, so lookups and edits go on meanwhile; a second lock keeps
// loads, saves and SetOptions() from overlapping. Failures and warnings are reported after the locks are released (see
// TASWTomlLogCallback). An application's settings class derives from it, and declares its settings (TASWTomlTable,
// TASWTomlSetting<T>, in ASWToml_Settings.h), which Load() and LoadText() apply to the document they read before
// swapping it in. Neither copyable nor movable.
/////////////////////////////////////////////////////////////////////////////
class TASWBaseToml : public IASWToml
{
private:
    friend class Detail::TSettingBase;
    friend class TASWTomlTable;

    // The log callback, shared with the reports that are calling it, so SetLogCallback() may replace it meanwhile
    using TLogCallbackPtr = std::shared_ptr<TASWTomlLogCallback const>;

    // TASWTomlDocument::SetValue() and Append() with value options, which DocumentSetValue() and DocumentAppend() edit
    // with
    using TValueMethod = TASWTomlResult (TASWTomlDocument::*)(TASWTomlPath const&, TASWTomlValue const&,
        TASWTomlValueOptions const&);

    // The settings and the tables bound to the object, in the order they were declared
    using TSettingVector = std::vector<Detail::TSettingBase const*>;
    using TTableVector = std::vector<TASWTomlTable const*>;

private:
    mutable Detail::TSharedMutex m_Mutex; // Guards the members after it
    TASWTomlDocument m_Document;
    std::string m_FilePath; // UTF-8
    TLogCallbackPtr m_LogCallback; // nullptr when there is none
    unsigned long long m_EditCount = 0; // Counts the edits that succeeded, so a save can tell whether one was made

    // Held for the whole of Load(), LoadText(), Save(), SaveAs() and SetOptions(), so they never overlap; taken before
    // m_Mutex, never after it. Guards the members after it.
    Detail::TMutex m_FileMutex;
    TSettingVector m_Settings;
    TTableVector m_Tables;

private:
    // Applies the settings and then the tables' comments to 'document', a document Load() or LoadText() read, before
    // anyone else can see it (see TASWTomlSetting and TASWTomlTable), and then adds what they report to 'messages',
    // with 'filePath' (the file read, or "") as the file path of each. Called with the file lock held. May throw
    // (std::bad_alloc, or what a converter or a validator throws), and then adds nothing to 'messages', so a load that
    // fails doesn't report changes it didn't make.
    void ApplySettings(TASWTomlDocument& document, std::string_view filePath, TMessageVector& messages) const;

    // Calls 'method', an edit of TASWTomlDocument, with 'path' and 'args' on the document under the lock, and counts
    // it when it succeeds; an exception becomes CaughtException(path). Reports a failed result.
    template <typename TMethod, typename ... TArgs>
    TASWTomlResult Edit(TMethod method, TASWTomlPath const& path, TArgs const& ... args);

    // Load()'s work, which may throw: adds the warnings to 'messages', and sets 'missing' when the file doesn't exist
    // and the document was emptied
    TASWTomlResult LoadFile(std::string_view filePath, TMessageVector& messages, bool& missing);

    // Adds 'setting' or 'table' at the end of the ones bound to the object, under the file lock: a setting's or a
    // table's constructor calls it, once it is complete
    void Register(Detail::TSettingBase const& setting);
    void Register(TASWTomlTable const& table);

    // Reports each of 'messages', in order (with no lock held)
    void ReportAll(TMessageVector const& messages) const noexcept;

    // The work of Save() (when 'filePath' is nullptr) and SaveAs(*filePath), which may throw: adds the warnings to
    // 'messages'
    TASWTomlResult SaveFile(std::string_view const* filePath, TMessageVector& messages);

    // Save() (when 'filePath' is nullptr) or SaveAs(*filePath): SaveFile() with an exception caught, and its warnings
    // and failure reported
    TASWTomlResult SaveTo(std::string_view const* filePath);

    // DocumentSetValue() for a setting (see TASWTomlSetting::Set()): with 'overlay' (nullptr for none), a value that is
    // there is set to the overlay of it and 'value' instead, worked out under the same lock; when it adds the key and
    // 'comment' isn't empty, also sets the key's comment to 'comment' in the same edit, so a failure of either changes
    // nothing; a key that has no line of its own for a comment (in an inline table) is added without it
    TASWTomlResult SetSettingValue(TASWTomlPath const& path, TASWTomlValue const& value,
        TASWTomlValueOptions const& values, std::string_view comment, Detail::TValueOverlay const* overlay);

    // Removes 'setting' or 'table' from the ones bound to the object, under the file lock (so after a Load() that is
    // applying it): a setting's or a table's destructor calls it
    void Unregister(Detail::TSettingBase const& setting);
    void Unregister(TASWTomlTable const& table);

protected:
    TASWTomlResult DocumentAppend(TASWTomlPath const& path, TASWTomlValue const& value,
        TASWTomlValueOptions const& values) override;
    bool DocumentFind(TASWTomlPath const& path, TASWTomlValue& value) const override;
    TASWTomlResult DocumentSetValue(TASWTomlPath const& path, TASWTomlValue const& value,
        TASWTomlValueOptions const& values) override;

    // Reads the whole file 'filePath' (UTF-8) into 'contents', failing with FileTooLarge if it has more than 'maxSize'
    // bytes, as Detail::ReadWholeFile() does (the default). Load() calls it with the file lock held but not the
    // document's lock, so it may call the object's lookups and edits, but not Load(), LoadText(), Save(), SaveAs() or
    // SetOptions(). A derived class may override it, e.g. for a test to make a failure the file system can't give
    // reliably. A FileNotFound result is a missing file (see Load()). May throw; Load() catches it.
    virtual TASWTomlResult ReadFile(std::string const& filePath, std::size_t maxSize, std::string& contents);

    // Calls the log callback with 'message' (FilePath set to the file path when it is empty), unless there is none.
    // An exception the callback throws is dropped.
    void Report(TASWTomlMessage const& message) const noexcept override;

    // Writes 'contents' to the file 'filePath' (UTF-8) as the saving options 'options' say, adding warnings (e.g.
    // BackupFailed) to 'warnings', as Detail::WriteWholeFile() does (the default). Save() and SaveAs() call it as
    // Load() calls ReadFile(). May throw; Save() and SaveAs() catch it.
    virtual TASWTomlResult WriteFile(std::string const& filePath, std::string_view contents,
        TASWTomlSavingOptions const& options, TMessageVector& warnings);

public:
    // An empty document with the default options, no file path and no log callback
    TASWBaseToml() = default;

    // Makes the edits as TASWTomlDocument::Apply() does (all of them or none) under the lock, so other threads see all
    // of them or none, as one edit (an edit Save() tells apart, see IASWToml::Save()); a failure is reported once. The
    // edits of the object's settings are made as their Set() would make them (see TASWTomlSetting::Set() given edits);
    // another object's fail with InvalidArgument. An exception (out of memory) is UnexpectedException without a Path:
    // no one edit ran out of it.
    TASWTomlResult Apply(TASWTomlEdits const& edits) override;
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
    TASWTomlResult Load(std::string_view filePath) override;
    TASWTomlResult LoadText(std::string_view text) override;
    bool Remove(TASWTomlPath const& path) override;
    TASWTomlResult Rename(TASWTomlPath const& path, std::string_view newKey) override;
    TASWTomlResult Save() override;
    TASWTomlResult SaveAs(std::string_view filePath) override;
    TASWTomlResult SetComment(TASWTomlPath const& path, std::string_view text) override;
    void SetLogCallback(TASWTomlLogCallback callback) override;
    TASWTomlResult SetOptions(TASWTomlOptions const& options) override;
    TASWTomlResult SetTrailingComment(TASWTomlPath const& path, std::string_view text) override;
    std::string ToText() const override;
};

} // namespace ASWToml

//---------------------------------------------------------------------------
#endif // #ifndef ASWToml_BaseH
