/* **************************************************************************
ASWToml_Interface.h
Author: Anthony S. West - ASW Software

A TOML file reader/writer library.

IASWToml, the interface of a TOML object (TASWBaseToml implements it): its
operations by TASWTomlPath, each thread-safe and complete in itself, and the
typed GetValue(), TryGetValue(), SetValue() and Append() built on them with
the value converters (TASWTomlConverter). TASWTomlLogCallback, the function a
TOML object reports its warnings and errors to.

Requires C++ 20 or higher.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

#pragma once

#ifndef ASWToml_InterfaceH
#define ASWToml_InterfaceH
//---------------------------------------------------------------------------
#include <functional>
#include <string>
#include <string_view>
#include <utility>
//---------------------------------------------------------------------------
#include "ASWToml_Converters.h"
#include "ASWToml_Document.h"
#include "ASWToml_Options.h"
#include "ASWToml_Path.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

// The function a TOML object reports its messages to (see IASWToml::SetLogCallback()): the warnings of saving, a value
// GetValue() couldn't convert, and each operation that failed. It is called on the thread of the operation, after it,
// with no lock of the object held, so it may call the object's methods (Load() and Save() too). An exception it throws
// is dropped.
using TASWTomlLogCallback = std::function<void (TASWTomlMessage const& message)>;

//---------------------------------------------------------------------------

// Helpers of IASWToml's templates
namespace Detail
{

// 'result' (a converter's failure for the value at 'path', with a Path relative to that value, see TASWTomlConverter)
// with 'path' put in front of its Path, so it is the place in the document: servers and [1].port make
// servers[1].port, server and port make server.port; the root path leaves it as it was, and an empty Path becomes
// 'path'
TASWTomlResult PathFailure(TASWTomlResult result, TASWTomlPath const& path);

} // namespace Detail


/////////////////////////////////////////////////////////////////////////////
// IASWToml
//
// The interface of a TOML object, which holds a TASWTomlDocument and the file it was loaded from: operations by
// TASWTomlPath that an implementation (TASWBaseToml) provides, each thread-safe and complete in itself, and the typed
// GetValue(), TryGetValue(), SetValue() and Append() built on them, which convert values with TASWTomlConverter<T>.
// Code that only reads and writes values can take an IASWToml&. The operations give out copies (strings, names, values,
// a copy of the document), never pointers into the document, which another thread may change.
// Every operation that fails with a TASWTomlResult reports it to the log callback as an Error message too (except
// TryGetValue(), whose failure is an answer for the caller), with the result's code, text, file path, line, column,
// path and system error. Operations that return a TASWTomlResult don't throw: an unexpected exception (std::bad_alloc,
// or one an application's converter throws) is returned as ETomlCode::UnexpectedException with nothing changed. The
// others may throw std::bad_alloc (and GetValue() what a converter throws), since they have no result to carry it.
// Neither copyable nor movable.
/////////////////////////////////////////////////////////////////////////////
class IASWToml
{
private:
    // DocumentSetValue() or DocumentAppend(), which WriteValue() stores a converted value with
    using TStoreMethod = TASWTomlResult (IASWToml::*)(TASWTomlPath const&, TASWTomlValue const&,
        TASWTomlValueOptions const&);

private:
    // GetValue() with 'options', or with GetValueOptions() when it is nullptr (only asked for when there is a value)
    template <TomlConvertible T>
    T GetOrDefault(TASWTomlPath const& path, T const& defaultValue, TASWTomlValueOptions const* options) const
    {
        if (!path.IsValid())
        {
            ReportFailure(InvalidPath(path));
            return defaultValue;
        }

        TASWTomlValue found;
        if (!DocumentFind(path, found))
            return defaultValue;

        // Read into a copy of the default, so a struct keeps the default's members that the table lacks
        T value = defaultValue;
        TASWTomlValueOptions const valueOptions = options != nullptr ? *options : GetValueOptions();
        TASWTomlResult const result = TASWTomlConverter<T>::FromValue(found, value, valueOptions);
        if (!result.IsOk())
        {
            ReportValueWarning(Detail::PathFailure(result, path));
            return defaultValue;
        }

        return value;
    }

    // The InvalidArgument result of a typed operation given 'path', which isn't valid: its GetError() as the message
    static TASWTomlResult InvalidPath(TASWTomlPath const& path);

    // The NotFound result of TryGetValue() for 'path', at which there is no value
    static TASWTomlResult NoValueAt(TASWTomlPath const& path);

    // TryGetValue() with 'options', or with GetValueOptions() when it is nullptr (only asked for when there is a value)
    template <TomlConvertible T>
    TASWTomlResult ReadValue(TASWTomlPath const& path, T& value, TASWTomlValueOptions const* options) const
    {
        try
        {
            if (!path.IsValid())
                return InvalidPath(path);

            TASWTomlValue found;
            if (!DocumentFind(path, found))
                return NoValueAt(path);

            TASWTomlValueOptions const valueOptions = options != nullptr ? *options : GetValueOptions();
            TASWTomlResult result = TASWTomlConverter<T>::FromValue(found, value, valueOptions);
            if (!result.IsOk())
                return Detail::PathFailure(std::move(result), path);

            return result;
        }
        catch (...)
        {
            return CaughtException(path);
        }
    }

    // Reports 'result', GetValue()'s failure to convert a value, as a Warning message with its code, text and path. A
    // message that can't be made (std::bad_alloc) is dropped.
    void ReportValueWarning(TASWTomlResult const& result) const noexcept;

    // SetValue() or Append() of the text 'text' as a string, by 'store', with 'options', or with GetValueOptions() when
    // it is nullptr
    TASWTomlResult WriteText(TASWTomlPath const& path, std::string_view text, TStoreMethod store,
        TASWTomlValueOptions const* options);

    // SetValue() or Append() of 'value', converted with TASWTomlConverter<T> and stored by 'store', with 'options', or
    // with GetValueOptions() when it is nullptr
    template <TomlConvertible T>
    TASWTomlResult WriteValue(TASWTomlPath const& path, T const& value, TStoreMethod store,
        TASWTomlValueOptions const* options)
    {
        TASWTomlResult result;
        try
        {
            if (path.IsValid())
            {
                TASWTomlValueOptions const valueOptions = options != nullptr ? *options : GetValueOptions();
                TASWTomlValue converted;
                result = TASWTomlConverter<T>::ToValue(value, converted, valueOptions);

                // The store reports its own failure
                if (result.IsOk())
                    return (this->*store)(path, converted, valueOptions);

                result = Detail::PathFailure(std::move(result), path);
            }
            else
            {
                result = InvalidPath(path);
            }
        }
        catch (...)
        {
            result = CaughtException(path);
        }

        ReportFailure(result);

        return result;
    }

protected:
    IASWToml() = default;

    // The UnexpectedException result of the exception being handled (call it in a catch block): its message is "An
    // unexpected exception was thrown: " and what() for a std::exception, else "An exception that isn't a
    // std::exception was thrown."; the second form also has the path of 'path' (see TASWTomlResult::Path). Without a
    // message (or a path) when it can't be made.
    static TASWTomlResult CaughtException() noexcept;
    static TASWTomlResult CaughtException(TASWTomlPath const& path) noexcept;

    // Appends 'value' to the array at 'path' with the value options 'values', as TASWTomlDocument::Append() does,
    // reporting a failure. The typed Append() is built on it.
    virtual TASWTomlResult DocumentAppend(TASWTomlPath const& path, TASWTomlValue const& value,
        TASWTomlValueOptions const& values) = 0;

    // Whether there is a value at 'path' (as TASWTomlDocument::Find() finds it); if there is, 'value' is set to a copy
    // of it. The typed GetValue() and TryGetValue() are built on it.
    virtual bool DocumentFind(TASWTomlPath const& path, TASWTomlValue& value) const = 0;

    // Sets the value at 'path' to 'value' with the value options 'values', as TASWTomlDocument::SetValue() does,
    // reporting a failure. The typed SetValue() is built on it.
    virtual TASWTomlResult DocumentSetValue(TASWTomlPath const& path, TASWTomlValue const& value,
        TASWTomlValueOptions const& values) = 0;

    // Sends 'message' to the log callback, with FilePath set to GetFilePath() when it is empty. Never called with a
    // lock of the object held.
    virtual void Report(TASWTomlMessage const& message) const noexcept = 0;

    // Reports 'result', which failed, as an Error message with its code, text, file path, line, column, path and system
    // error. A message that can't be made (std::bad_alloc) is dropped.
    void ReportFailure(TASWTomlResult const& result) const noexcept;

public:
    IASWToml(IASWToml const&) = delete;
    IASWToml& operator=(IASWToml const&) = delete;
    virtual ~IASWToml() = default;

    // Whether there is a value at 'path': a table (the root table for the root path), a key or an element of an array,
    // as TASWTomlDocument::Find() finds it; false for a path that isn't valid
    virtual bool Contains(TASWTomlPath const& path) const = 0;

    // The comment of the key, table or array of tables at 'path', as TASWTomlDocument::GetComment() gives it (the root
    // path's is the file comment)
    virtual std::string GetComment(TASWTomlPath const& path) const = 0;

    // A copy of the document, e.g. to go through its tables or write it with other options
    virtual TASWTomlDocument GetDocument() const = 0;

    // The file Load() read or SaveAs() wrote last, which Save() writes (UTF-8, as it was given); "" before either
    virtual std::string GetFilePath() const = 0;

    // The keys of the table at 'path' (the root table for the root path), in the order TASWTomlDocument::GetRoot()
    // gives them; empty when there is no table there
    virtual TStringVector GetKeyNames(TASWTomlPath const& path) const = 0;

    // The options the document is read, written and saved with, and values are written with (see SetOptions())
    virtual TASWTomlOptions GetOptions() const = 0;

    // The comment at the end of the line of the key, table or array of tables at 'path', as
    // TASWTomlDocument::GetTrailingComment() gives it
    virtual std::string GetTrailingComment(TASWTomlPath const& path) const = 0;

    // GetOptions().Values, without copying the other options: what GetValue(), TryGetValue(), SetValue() and Append()
    // convert and write values with, unless they are given their own
    virtual TASWTomlValueOptions GetValueOptions() const = 0;

    // The library's version (ASWToml::Version)
    virtual std::string_view GetVersionStr() const noexcept = 0;

    // Whether an edit changed the document's text since it was loaded or saved (see TASWTomlDocument::IsModified())
    virtual bool IsModified() const = 0;

    // Reads file 'filePath' (UTF-8) as LoadText() reads a text, and makes it the file path. Fails with InvalidArgument
    // for an empty path, the file errors (e.g. AccessDenied, FileTooLarge), and LoadText()'s (e.g. ParseFailed, with
    // the line and column of the error); then nothing changes, the file path included, so Save() doesn't write over a
    // file with an error unless it already was the file path (an earlier Load() or SaveAs() of it). A missing file
    // (FileNotFound) is the exception, since an application's first run has none: the document is emptied, as a new
    // one with the options, 'filePath' becomes the file path, so Save() creates the file, and the failure is reported
    // as a Warning, not an Error.
    virtual TASWTomlResult Load(std::string_view filePath) = 0;

    // Replaces the document with 'text', read as TASWTomlDocument::Parse() reads it, with the options. Keeps the file
    // path, and clears the modified flag. Fails as Parse() does (e.g. ParseFailed, with the line and column of the
    // error), and then nothing changes. LoadText("") empties the document.
    virtual TASWTomlResult LoadText(std::string_view text) = 0;

    // Removes the key, table, array of tables or element of an array at 'path', as TASWTomlDocument::Remove() does;
    // returns whether anything was removed. May throw std::bad_alloc, since false would say there was nothing to
    // remove.
    virtual bool Remove(TASWTomlPath const& path) = 0;

    // Renames the key, table or array of tables at 'path' to 'newKey', as TASWTomlDocument::Rename() does
    virtual TASWTomlResult Rename(TASWTomlPath const& path, std::string_view newKey) = 0;

    // Writes ToText() to the file path, as the options' Saving group says (atomically by default, so a failed save
    // leaves the file as it was), even when the document isn't modified; its warnings (e.g. BackupFailed) are
    // reported. Clears the modified flag, unless an edit succeeded while the file was being written. Fails with
    // NoFilePath when there is no file path (see SaveAs()), or the file errors (e.g. AccessDenied, WriteFailed); then
    // the document, the file path and the modified flag stay as they were.
    virtual TASWTomlResult Save() = 0;

    // As Save(), to file 'filePath' (UTF-8), which becomes the file path when it succeeds. Fails with InvalidArgument
    // for an empty path.
    virtual TASWTomlResult SaveAs(std::string_view filePath) = 0;

    // Sets the comment of the key, table or array of tables at 'path', as TASWTomlDocument::SetComment() does (the
    // root path's is the file comment)
    virtual TASWTomlResult SetComment(TASWTomlPath const& path, std::string_view text) = 0;

    // Sets the function messages are reported to (see TASWTomlLogCallback); an empty one turns reporting off (the
    // default). A report that started before it returns may still go to the function it replaced.
    virtual void SetLogCallback(TASWTomlLogCallback callback) = 0;

    // Gives the object 'options' and reads the document's text again with them, as TASWTomlDocument::SetOptions()
    // does: the text, the file path and the modified flag stay. Fails with InvalidOptions (see
    // TASWTomlOptions::Validate()), or with what the new reading options refuse in the text (e.g. NestingTooDeep for a
    // lower MaxNestingDepth); then nothing changes.
    virtual TASWTomlResult SetOptions(TASWTomlOptions const& options) = 0;

    // Sets the comment at the end of the line of the key, table or array of tables at 'path', as
    // TASWTomlDocument::SetTrailingComment() does
    virtual TASWTomlResult SetTrailingComment(TASWTomlPath const& path, std::string_view text) = 0;

    // The document as text, as TASWTomlDocument::ToText() writes it
    virtual std::string ToText() const = 0;

public: // Typed values, built on the operations above
    // Appends 'value', converted with TASWTomlConverter<T> and the value options (GetValueOptions(), or 'options'), to
    // the array at 'path', as TASWTomlDocument::Append() does: a struct (a table) is a new [[path]] element of an
    // array of tables. Fails as SetValue() does. A text value (a string literal, a char array or pointer, or a
    // std::string_view) is appended as a string (a nullptr value is "").
    template <TomlConvertible T>
    TASWTomlResult Append(TASWTomlPath const& path, T const& value)
    {
        return WriteValue(path, value, &IASWToml::DocumentAppend, nullptr);
    }

    template <TomlConvertible T>
    TASWTomlResult Append(TASWTomlPath const& path, T const& value, TASWTomlValueOptions const& options)
    {
        return WriteValue(path, value, &IASWToml::DocumentAppend, &options);
    }

    TASWTomlResult Append(TASWTomlPath const& path, char const* value);
    TASWTomlResult Append(TASWTomlPath const& path, char const* value, TASWTomlValueOptions const& options);
    TASWTomlResult Append(TASWTomlPath const& path, std::string_view value);
    TASWTomlResult Append(TASWTomlPath const& path, std::string_view value, TASWTomlValueOptions const& options);

    // The value at 'path', converted with TASWTomlConverter<T> and the value options (GetValueOptions(), or
    // 'options'), or 'defaultValue' when there is no value there or it doesn't convert. The value is read into a copy
    // of 'defaultValue', so a struct keeps the default's members that the table lacks. A value that doesn't convert is
    // reported as a Warning message with the converter's code and text and the path of the place in the document that
    // failed (e.g. servers[1].port); a path that isn't valid is reported as an Error (InvalidArgument). A text default
    // (a string literal, a char array or pointer, or a std::string_view) reads a string, as std::string (a nullptr
    // default is ""). May throw what the converter or the operations throw.
    template <TomlConvertible T>
    T GetValue(TASWTomlPath const& path, T const& defaultValue) const
    {
        return GetOrDefault(path, defaultValue, nullptr);
    }

    template <TomlConvertible T>
    T GetValue(TASWTomlPath const& path, T const& defaultValue, TASWTomlValueOptions const& options) const
    {
        return GetOrDefault(path, defaultValue, &options);
    }

    std::string GetValue(TASWTomlPath const& path, char const* defaultValue) const;
    std::string GetValue(TASWTomlPath const& path, char const* defaultValue, TASWTomlValueOptions const& options) const;
    std::string GetValue(TASWTomlPath const& path, std::string_view defaultValue) const;
    std::string GetValue(TASWTomlPath const& path, std::string_view defaultValue,
        TASWTomlValueOptions const& options) const;

    // Sets the value at 'path' to 'value', converted with TASWTomlConverter<T> and the value options
    // (GetValueOptions(), or 'options', which the document also writes it with), as TASWTomlDocument::SetValue() sets
    // it: a table or array that is there is merged with it. Fails with InvalidArgument for a path that isn't valid,
    // with the converter's code (e.g. InvalidValue for an enum value that has no name) and the path of the place in
    // the document that failed, or as TASWTomlDocument::SetValue() does. A text value (a string literal, a char array
    // or pointer, or a std::string_view) is set as a string (a nullptr value is "").
    template <TomlConvertible T>
    TASWTomlResult SetValue(TASWTomlPath const& path, T const& value)
    {
        return WriteValue(path, value, &IASWToml::DocumentSetValue, nullptr);
    }

    template <TomlConvertible T>
    TASWTomlResult SetValue(TASWTomlPath const& path, T const& value, TASWTomlValueOptions const& options)
    {
        return WriteValue(path, value, &IASWToml::DocumentSetValue, &options);
    }

    TASWTomlResult SetValue(TASWTomlPath const& path, char const* value);
    TASWTomlResult SetValue(TASWTomlPath const& path, char const* value, TASWTomlValueOptions const& options);
    TASWTomlResult SetValue(TASWTomlPath const& path, std::string_view value);
    TASWTomlResult SetValue(TASWTomlPath const& path, std::string_view value, TASWTomlValueOptions const& options);

    // Reads the value at 'path' into 'value', converted with TASWTomlConverter<T> and the value options
    // (GetValueOptions(), or 'options'). Fails with InvalidArgument for a path that isn't valid, NotFound when there is
    // no value at 'path', or with the converter's code (e.g. TypeMismatch, ValueOutOfRange) and the path of the place
    // in the document that failed (e.g. servers[1].port); then 'value' stays as it was. Not reported to the log
    // callback: the caller decides what a failure means.
    template <TomlConvertible T>
    TASWTomlResult TryGetValue(TASWTomlPath const& path, T& value) const
    {
        return ReadValue(path, value, nullptr);
    }

    template <TomlConvertible T>
    TASWTomlResult TryGetValue(TASWTomlPath const& path, T& value, TASWTomlValueOptions const& options) const
    {
        return ReadValue(path, value, &options);
    }
};

} // namespace ASWToml

//---------------------------------------------------------------------------
#endif // #ifndef ASWToml_InterfaceH
