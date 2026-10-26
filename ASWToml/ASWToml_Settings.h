/* **************************************************************************
ASWToml_Settings.h
Author: Anthony S. West - ASW Software

A TOML file reader/writer library.

The settings binding: TASWTomlTable, a table of an application's settings,
and TASWTomlSetting<T>, one setting declared once with its table, key,
type, default value, comment and options (TASWTomlSettingOptions<T>: a
range, a validator, its own value options). A setting reads and writes its
value in the document of the TOML object that owns its table
(TASWBaseToml), so it is thread-safe as that object is, and the object's
Load() and LoadText() apply the settings to the document they read.

Requires C++ 20 or higher.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

#pragma once

#ifndef ASWToml_SettingsH
#define ASWToml_SettingsH
//---------------------------------------------------------------------------
#include <concepts>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWToml_Base.h"
#include "ASWToml_Converters.h"
#include "ASWToml_Document.h"
#include "ASWToml_Options.h"
#include "ASWToml_Path.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

class TASWTomlTable;

//---------------------------------------------------------------------------

// Helpers of the settings' templates
namespace Detail
{

// False for every T, for a static_assert that fails only when its template is used
template <typename T>
constexpr bool AlwaysFalse = false;

// The type operator< gives for two values of T (see LessComparable)
template <typename T>
using TLessResult = decltype(std::declval<T const&>() < std::declval<T const&>());

// Whether operator< compares two values of T, as a setting's range does (see TASWTomlSettingOptions::Min). A standard
// container compares only when its elements do, so a std::vector of a struct without operator< doesn't.
template <typename T>
concept LessComparable = std::convertible_to<TLessResult<T>, bool>;


/////////////////////////////////////////////////////////////////////////////
// TNoRangeEnd
//
// The range end of a setting whose type operator< doesn't compare (see TRangeEnd): it holds nothing, and giving it a
// value is a compile error that says why
/////////////////////////////////////////////////////////////////////////////
struct TNoRangeEnd
{
    TNoRangeEnd() = default;

    template <typename U>
    TNoRangeEnd(U const&)
    {
        static_assert(AlwaysFalse<U>, "A setting's range needs a type that operator< compares.");
    }
};

// The type of an end of the range of a setting of type T: std::optional<T> when operator< compares T, else TNoRangeEnd
template <typename T>
using TRangeEnd = std::conditional_t<LessComparable<T>, std::optional<T>, TNoRangeEnd>;

// The ValueOutOfRange result of 'value', outside a setting's range from 'min' to 'max' (nullptr for an end the range
// doesn't have, or that can't be written): "The value 9 is out of the setting's range: it must be from 0 to 4."
TASWTomlResult OutOfSettingRange(TASWTomlValue const& value, TASWTomlValue const* min, TASWTomlValue const* max);

// Whether 'path' can be the path of a table of settings: a valid path of key parts only (the root path too)
bool IsSettingsTablePath(TASWTomlPath const& path) noexcept;

// The InvalidValue result of 'value', which a setting's validator refused with 'problem': "The value "" isn't valid:
// it can't be empty." (a '.' is added unless 'problem' ends with one)
TASWTomlResult RefusedValue(TASWTomlValue const& value, std::string_view problem);


/////////////////////////////////////////////////////////////////////////////
// TSettingChange
//
// A value that applying a setting gives its key (see TSettingsApply): its default, for a key that is missing or whose
// value isn't valid
/////////////////////////////////////////////////////////////////////////////
struct TSettingChange
{
    TASWTomlPath Path; // The setting's path
    std::string Key; // The setting's key in its table
    TASWTomlValue Value;
    std::optional<TASWTomlValueOptions> Values; // The setting's own value options; none for the document's

    // The index of the change's message in TSettingsApply's messages: KeyAdded for a key that is added, or a message
    // with the code Ok, which is dropped, for a value that is replaced; replaced by an Error when the write fails
    std::size_t Message = 0;
};

// The changes of one table, in the order of their settings
using TSettingChangeVector = std::vector<TSettingChange>;


/////////////////////////////////////////////////////////////////////////////
// TSettingComment
//
// A comment that applying the settings writes, as the comment policy says, once the values are written (see
// TSettingsApply): a setting's, or a table's (the file comment for the root table)
/////////////////////////////////////////////////////////////////////////////
struct TSettingComment
{
    TASWTomlPath Path;
    std::string Text;
    bool IsTable = false; // Whether it is a table's: written only when there is a table at Path
};

// The comments to write, in order
using TSettingCommentVector = std::vector<TSettingComment>;


/////////////////////////////////////////////////////////////////////////////
// TTableChanges
//
// The changes of the settings of one table (see TSettingsApply)
/////////////////////////////////////////////////////////////////////////////
struct TTableChanges
{
    TASWTomlPath Path;
    TSettingChangeVector Changes;
};

// The tables that have changes, in the order of their first setting
using TTableChangesVector = std::vector<TTableChanges>;


/////////////////////////////////////////////////////////////////////////////
// TSettingsApply
//
// Applies the settings of a TOML object to a document that Load() or LoadText() read, before anyone sees it (see
// TASWBaseToml::ApplySettings()): each setting renames an earlier name of its missing key at once (Rename()), and adds
// the value its key gets (AddValue()), its messages (AddMessage()) and its comment (AddComment()), and so does each
// table its comment; Write() then writes the values with one edit per table (a setting with value options of its own
// with one of its own), and all the comments with one more, as Writing.CommentPolicy says. Not copyable.
/////////////////////////////////////////////////////////////////////////////
class TSettingsApply
{
private:
    TASWTomlDocument& m_Document;
    TTableChangesVector m_Tables;
    TSettingCommentVector m_Comments;
    TMessageVector m_Messages; // In order; one with the code Ok is dropped (see TSettingChange::Message)

    // Writes 'm_Comments' that the policy asks for, in one edit, or when that fails, one at a time: one that has no
    // line of its own (InvalidArgument: e.g. a key in an inline table) is skipped, and any other failure is an Error
    void WriteComments();

    // Writes the changes of 'table': those without value options of their own merged into the table at its path in one
    // edit (a value there that isn't a table is first removed, with a Warning), or, when that fails, one at a time;
    // then the others, one at a time. A change that fails replaces its message with an Error.
    void WriteTable(TTableChanges const& table);

public:
    // Applies settings to 'document'
    explicit TSettingsApply(TASWTomlDocument& document);
    TSettingsApply(TSettingsApply const&) = delete;
    TSettingsApply& operator=(TSettingsApply const&) = delete;

    // Adds 'text' as the comment of 'path', a setting's or (with 'isTable') a table's; an empty 'text' adds nothing
    void AddComment(TASWTomlPath const& path, std::string const& text, bool isTable);

    // Adds 'message', which Write() gives in order with the others
    void AddMessage(TASWTomlMessage message);

    // Adds 'value' as the value of the key 'key' at 'path' in the table at 'tablePath', written with 'values' (the
    // setting's own value options; nullptr for the document's), and 'added' (when the key is missing: its KeyAdded
    // message; nullptr for a value that is replaced)
    void AddValue(TASWTomlPath const& tablePath, TASWTomlPath const& path, std::string const& key, TASWTomlValue value,
        TASWTomlValueOptions const* values, TASWTomlMessage const* added);

    // The document the settings are applied to, as it was read (Write() changes it)
    TASWTomlDocument const& GetDocument() const noexcept;

    // Renames the key at 'path' to 'newKey' in the document at once, before the values are written (see
    // TASWTomlDocument::Rename()), and returns the rename's result
    TASWTomlResult Rename(TASWTomlPath const& path, std::string_view newKey);

    // Writes the values, then the comments, to the document, and adds the messages to 'messages', in order. May throw
    // std::bad_alloc, and then the document may be partly changed: the caller drops it.
    void Write(TMessageVector& messages);
};


/////////////////////////////////////////////////////////////////////////////
// TSettingBase
//
// The part of TASWTomlSetting<T> that doesn't depend on T: the owner, the setting's path (its table's and the key),
// its key and its comment, and what the setting asks of its owner. Neither copyable nor movable.
/////////////////////////////////////////////////////////////////////////////
class TSettingBase
{
private:
    friend class ASWToml::TASWBaseToml;

private:
    TASWBaseToml* m_Owner;
    TASWTomlPath m_TablePath;
    TASWTomlPath m_Path; // The table's path and the key
    std::string m_Key;
    std::string m_Comment;

    // Why the setting's path can't be used (its table's path isn't valid or has an index, or the key isn't valid
    // UTF-8); empty when it can
    std::string m_PathError;

private:
    // Applies the setting to the document of 'apply', which Load() or LoadText() read (see TASWTomlSetting)
    virtual void Apply(TSettingsApply& apply) const = 0;

protected:
    // A setting of key 'key' in table 'table', with comment 'comment'. The derived class registers it with its owner
    // (Register()) once it is complete, and unregisters it (Unregister()) in its destructor.
    TSettingBase(TASWTomlTable const& table, std::string_view key, std::string_view comment);
    virtual ~TSettingBase() = default;

    // Adds 'result', a failure of applying the setting, to 'apply' as an Error
    void AddFailure(TSettingsApply& apply, TASWTomlResult const& result) const;

    // Adds to 'apply' the Error that the default value can't be used, as it failed with 'result' (it can't be written,
    // or the range or the validator refuses it)
    void AddInvalidDefault(TSettingsApply& apply, TASWTomlResult const& result) const;

    // Adds the setting's comment to 'apply' (none when it has none)
    void ApplyComment(TSettingsApply& apply) const;

    // Adds to 'apply' the default value 'defaultValue' (written with 'values', the setting's own value options, or
    // nullptr for the document's) as the key's value, and the setting's comment. With 'problem' (the value there isn't
    // valid), adds the Warning of DefaultUsedMessage(); without it (the key is missing), the Info that the key is added
    // (ETomlCode::KeyAdded).
    void ApplyDefault(TSettingsApply& apply, TASWTomlValue const& defaultValue, TASWTomlValueOptions const* values,
        TASWTomlResult const* problem) const;

    // For a setting whose key is missing in the document of 'apply': renames the first of 'oldKeys' (earlier names of
    // the key) that its table has to the key (TASWTomlDocument::Rename(): its line or header, value and comment stay
    // where they are), adding the Info that it was renamed (ETomlCode::KeyRenamed). Returns false when that fails
    // (added as an Error), true otherwise, also when the table has none of them.
    bool ApplyOldKeys(TSettingsApply& apply, TStringVector const& oldKeys) const;

    // The UnexpectedException result of Set() for the exception being handled (call it in a catch block), with the
    // setting's path (see IASWToml's CaughtException())
    TASWTomlResult CaughtSetFailure() const noexcept;

    // Whether the owner's document has a value at the setting's path; if it has, 'value' is set to a copy of it. A
    // path that can't be used is reported as an Error (InvalidArgument), and gives false.
    bool FindValue(TASWTomlValue& value) const;

    // The Warning that the default value is used, as the value at the setting's path failed with 'result' (its
    // converter's, range's or validator's failure, with a Path inside the value): the result's code, its text followed
    // by " The default value <defaultValue> is used." (or " The default value is used." when 'defaultValue' is nullptr:
    // it can't be written), and the setting's path joined with the result's
    TASWTomlMessage DefaultUsedMessage(TASWTomlResult const& result, TASWTomlValue const* defaultValue) const;

    TASWBaseToml& GetOwner() const noexcept;

    // Ok, or the InvalidArgument result of a setting whose path can't be used, with the reason
    TASWTomlResult PathProblem() const;

    // Adds the setting at the end of the ones bound to its owner
    void Register() const;

    // Reports DefaultUsedMessage() (Get() gives the default value). A message that can't be made (std::bad_alloc) is
    // dropped.
    void ReportDefaultUsed(TASWTomlResult const& result, TASWTomlValue const* defaultValue) const noexcept;

    // Reports 'result', a failure of Set(), as an Error message (see IASWToml::ReportFailure())
    void ReportFailure(TASWTomlResult const& result) const noexcept;

    // 'result', a failure of the setting's value, with the setting's path in front of its Path (see
    // Detail::PathFailure())
    TASWTomlResult SetFailure(TASWTomlResult result) const;

    // Removes the setting from the ones bound to its owner, waiting for a Load() that is applying it
    void Unregister() const;

    // Sets the value at the setting's path in the owner's document to 'value', written with the value options
    // 'values', as IASWToml's SetValue() does (reporting a failure); a key that it adds gets the setting's comment
    // in the same edit (see TASWBaseToml)
    TASWTomlResult WriteValue(TASWTomlValue const& value, TASWTomlValueOptions const& values);

public:
    TSettingBase(TSettingBase const&) = delete;
    TSettingBase& operator=(TSettingBase const&) = delete;

    // The comment the setting's key gets when Set() adds it
    std::string const& GetComment() const noexcept;

    // The setting's key in its table (one key part, unquoted)
    std::string const& GetKey() const noexcept;

    // The setting's path: its table's path and its key
    TASWTomlPath const& GetPath() const noexcept;

    // Whether the owner's document has a value at the setting's path
    bool IsPresent() const;
};

} // namespace Detail


/////////////////////////////////////////////////////////////////////////////
// TASWTomlTable
//
// A table of an application's settings: the TOML object it belongs to (its owner), its path from the root table (key
// parts only, e.g. "logging", "logging.files" or { "hosts", "example.com" }; the root path "" for the keys before the
// first header) and its comment. Its settings (TASWTomlSetting) take it in their constructors, so it is declared
// before them, and it must not outlive its owner; both hold when the table and its settings are members of the
// application's settings class, which derives from TASWBaseToml. It is bound to its owner from its constructor to its
// destructor: after the owner's Load() or LoadText() has applied the settings, its comment, if it has one, is set as
// the options' Writing.CommentPolicy says, when the table is in the document (applying adds no table for its comment
// alone, but a table a missing setting adds gets it) and has a line of its own for it (not one only dotted keys or
// longer headers make, nor an inline table):
//
//     class TAppSettings : public ASWToml::TASWBaseToml
//     {
//     public:
//         ASWToml::TASWTomlTable Logging{ *this, "logging" };
//         ASWToml::TASWTomlSetting<unsigned> Level{ Logging, "level", 2, "From 0 (errors only) to 4 (everything).",
//             { .Min = 0, .Max = 4 } };
//     };
//
// The root table's comment is the file comment. Neither copyable nor movable.
/////////////////////////////////////////////////////////////////////////////
class TASWTomlTable
{
private:
    TASWBaseToml* m_Owner;
    TASWTomlPath m_Path;
    std::string m_Comment;

public:
    // Table 'path' of 'owner', with comment 'comment' ("" for none), bound to 'owner'
    TASWTomlTable(TASWBaseToml& owner, TASWTomlPath const& path, std::string_view comment = std::string_view());
    TASWTomlTable(TASWTomlTable const&) = delete;
    TASWTomlTable& operator=(TASWTomlTable const&) = delete;
    ~TASWTomlTable();

    std::string const& GetComment() const noexcept;
    TASWBaseToml& GetOwner() const noexcept;
    TASWTomlPath const& GetPath() const noexcept;
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlSettingOptions
//
// The options of a TASWTomlSetting<T>: the range its values must be in, a function that checks them, whether it is
// optional, the earlier names of its key, and the value options it writes them with. Given with designated
// initializers, in the order of the members, as the last argument of the setting's constructor, e.g.
//
//     { .Min = 0, .Max = 4 }
//     { .Validator = NotEmpty, .Values = hexadecimal }
//
// New options are added at the end.
/////////////////////////////////////////////////////////////////////////////
template <typename T>
struct TASWTomlSettingOptions
{
    // A function that checks a value: it returns "" when the value is valid, else what is wrong with it, as the end of
    // a sentence (e.g. "it can't be empty"), which the message about the value ends with. It is given only the value,
    // and must not call the TOML object.
    using TValidator = std::function<std::string (T const& value)>;

    // The smallest and the largest value the setting takes, compared with operator<: a value less than Min or greater
    // than Max is out of range. None by default. Only for a T that operator< compares (numbers, enums, strings, a
    // struct with operator<=>, ...): for any other, giving one is a compile error.
    Detail::TRangeEnd<T> Min = {};
    Detail::TRangeEnd<T> Max = {};

    // The function that checks the setting's values (see TValidator); none by default
    TValidator Validator = {};

    // Whether a missing key is left missing when a document is loaded, instead of being added with the default value
    // (e.g. a hidden setting a user may add); it is written only when Set() writes it
    bool Optional = false;

    // Earlier names of the setting's key, in its own table, for a key renamed in a later version of the application
    // (e.g. { .OldKeys = { "log_days" } }): when the key is missing, Load() renames the first of them that the table
    // has to it; when the key is there, they are left alone
    TStringVector OldKeys = {};

    // The value options the setting writes its values with, instead of its owner's (e.g. an integer in hexadecimal);
    // valid ones (see TASWTomlValueOptions::Validate())
    std::optional<TASWTomlValueOptions> Values = {};
};


/////////////////////////////////////////////////////////////////////////////
// TASWTomlSetting
//
// One setting of an application: a key of a table (TASWTomlTable), with its type T (any type TASWTomlConverter
// converts: a number, a string, an enum, a date, a std::vector, a struct with ASWTomlFields(), ...), its default
// value, its comment and its options (TASWTomlSettingOptions). It holds no value of its own: Get() reads the value
// from the owner's document and Set() writes it there, so a setting always agrees with the document (with Load(), and
// with a raw SetValue() of the same key), and any thread may call them, as the owner's operations. A value is valid
// when it converts, is in the range and the validator accepts it; Get() gives the default value for one that isn't.
// A setting is bound to its owner from its constructor to its destructor, in the order settings are declared. When
// the owner's Load() or LoadText() has read a document, before anyone sees it, each setting bound to it is applied to
// it, in that order: a missing key whose earlier name (see TASWTomlSettingOptions::OldKeys) its table has is renamed
// from it, keeping its line or header, value and comment, reported as an Info message (ETomlCode::KeyRenamed), and is
// then applied as a key that was there; any other missing key is added with the default value and the setting's
// comment, reported as an Info message (ETomlCode::KeyAdded), unless the setting is optional (see
// TASWTomlSettingOptions::Optional); a value that isn't valid is replaced by the default value, reported as a Warning
// (as Get() reports it), and a valid one is left as it is; the key's comment is set to the setting's (if it has one)
// as the options' Writing.CommentPolicy says. A table of settings that the document holds as another kind of value is
// replaced by a table of its settings' default values, with a Warning. So the document then holds every setting with
// a valid value, and the owner's IsModified() says whether that changed it (whether it needs saving). A default value
// that can't be written, or that the range or the validator refuses, is reported as an Error, and the key is left as
// it is; so is an earlier name that can't be renamed. The earlier names are renamed at once, and the values are
// written with one edit per table (a setting with value options of its own with an edit of its own), and the comments
// with one more. The converter and the validator are called with the owner's file lock held, so they must not call
// its Load(), LoadText(), Save(), SaveAs() or SetOptions(). SetOptions() doesn't apply the settings. A setting must
// not outlive its table's owner (see TASWTomlTable). Neither copyable nor movable.
/////////////////////////////////////////////////////////////////////////////
template <TomlConvertible T>
class TASWTomlSetting : public Detail::TSettingBase
{
private:
    T m_Default;
    TASWTomlSettingOptions<T> m_Options;

private:
    void Apply(Detail::TSettingsApply& apply) const override
    {
        TASWTomlResult result = PathProblem();
        if (!result.IsOk())
        {
            AddFailure(apply, result);
            return;
        }

        // The setting's own value options, or the document's (the owner's, which it was read with)
        TASWTomlDocument const& document = apply.GetDocument();
        TASWTomlValueOptions const* const values = m_Options.Values.has_value() ? &*m_Options.Values : nullptr;
        TASWTomlValueOptions const options = values != nullptr ? *values : document.GetOptions().Values;
        TASWTomlValue defaultValue;
        result = TASWTomlConverter<T>::ToValue(m_Default, defaultValue, options);
        if (result.IsOk())
            result = Check(m_Default, defaultValue, options);

        if (!result.IsOk())
        {
            AddInvalidDefault(apply, result);
            return;
        }

        TASWTomlValue const* found = document.Find(GetPath());
        if (found == nullptr)
        {
            if (!ApplyOldKeys(apply, m_Options.OldKeys))
                return;

            found = document.Find(GetPath());
        }

        if (found == nullptr)
        {
            if (!m_Options.Optional)
                ApplyDefault(apply, defaultValue, values, nullptr);

            return;
        }

        T value = m_Default;
        result = TASWTomlConverter<T>::FromValue(*found, value, options);
        if (result.IsOk())
            result = Check(value, *found, options);

        if (result.IsOk())
            ApplyComment(apply);
        else
            ApplyDefault(apply, defaultValue, values, &result);
    }

    // Ok when 'value' (written as 'written') is in the range and the validator accepts it, else the failure
    // (ValueOutOfRange or InvalidValue), with a message that shows 'written'; the range's ends are written with
    // 'options'
    TASWTomlResult Check(T const& value, TASWTomlValue const& written, TASWTomlValueOptions const& options) const
    {
        if constexpr (Detail::LessComparable<T>)
        {
            bool const belowMin = m_Options.Min.has_value() && value < *m_Options.Min;
            bool const aboveMax = m_Options.Max.has_value() && *m_Options.Max < value;
            if (belowMin || aboveMax)
            {
                // An end that can't be written (an enum value with no name) isn't named
                TASWTomlValue min;
                TASWTomlValue max;
                bool const hasMin = m_Options.Min.has_value() &&
                    TASWTomlConverter<T>::ToValue(*m_Options.Min, min, options).IsOk();
                bool const hasMax = m_Options.Max.has_value() &&
                    TASWTomlConverter<T>::ToValue(*m_Options.Max, max, options).IsOk();

                return Detail::OutOfSettingRange(written, hasMin ? &min : nullptr, hasMax ? &max : nullptr);
            }
        }

        if (m_Options.Validator)
        {
            std::string const problem = m_Options.Validator(value);
            if (!problem.empty())
                return Detail::RefusedValue(written, problem);
        }

        return TASWTomlResult();
    }

    // The value options the setting converts and writes with: its own, or else its owner's
    TASWTomlValueOptions ValueOptions() const
    {
        return m_Options.Values.has_value() ? *m_Options.Values : GetOwner().GetValueOptions();
    }

public:
    // Setting 'key' (one key part, unquoted) of table 'table', whose value is 'defaultValue' while the key is missing
    // or its value isn't valid, and whose key is written with comment 'comment' ("" for none). The default value is
    // expected to be valid.
    TASWTomlSetting(TASWTomlTable const& table, std::string_view key, T const& defaultValue,
        std::string_view comment = std::string_view(), TASWTomlSettingOptions<T> const& options = {})
        : TSettingBase(table, key, comment),
          m_Default(defaultValue),
          m_Options(options)
    {
        Register();
    }

    ~TASWTomlSetting() override
    {
        Unregister();
    }

    // The value at the setting's path in the owner's document, converted with TASWTomlConverter<T> and the value
    // options (the setting's own, or else the owner's), or the default value when there is none or it isn't valid
    // (it doesn't convert, is out of range, or the validator refuses it). A value that isn't valid is reported as a
    // Warning message with the failure's code and text, the default value, and the path of the place that failed
    // (e.g. servers[1].port); a setting whose path can't be used (see GetPath()) as an Error (InvalidArgument). May
    // throw what the converter, the validator or the owner's operations throw (e.g. std::bad_alloc).
    T Get() const
    {
        TASWTomlValue found;
        if (!FindValue(found))
            return m_Default;

        TASWTomlValueOptions const options = ValueOptions();
        T value = m_Default;
        TASWTomlResult result = TASWTomlConverter<T>::FromValue(found, value, options);
        if (result.IsOk())
            result = Check(value, found, options);

        if (result.IsOk())
            return value;

        TASWTomlValue written;
        bool const hasDefault = TASWTomlConverter<T>::ToValue(m_Default, written, options).IsOk();
        ReportDefaultUsed(result, hasDefault ? &written : nullptr);

        return m_Default;
    }

    T const& GetDefault() const noexcept
    {
        return m_Default;
    }

    TASWTomlSettingOptions<T> const& GetOptions() const noexcept
    {
        return m_Options;
    }

    // Set(GetDefault())
    TASWTomlResult Reset()
    {
        return Set(m_Default);
    }

    // Sets the value at the setting's path in the owner's document to 'value', converted with TASWTomlConverter<T>
    // and the value options (the setting's own, or else the owner's, which the document also writes it with), as
    // IASWToml's SetValue() sets it: a missing key is added, with the setting's comment above it when it has a line
    // of its own, in the same edit, and a missing table too. Fails, and then nothing changes, with ValueOutOfRange for
    // a value out of the range, InvalidValue for one that the validator refuses or that can't be converted (e.g. an
    // enum value with no name), each with the setting's path in Path, InvalidArgument for a setting whose path can't
    // be used, UnexpectedException for an exception (e.g. one the validator throws), or as IASWToml's SetValue()
    // does. A failure is reported as an Error message.
    TASWTomlResult Set(T const& value)
    {
        TASWTomlResult result;
        try
        {
            result = PathProblem();
            if (result.IsOk())
            {
                TASWTomlValueOptions const options = ValueOptions();
                TASWTomlValue written;
                result = TASWTomlConverter<T>::ToValue(value, written, options);
                if (result.IsOk())
                    result = Check(value, written, options);

                // WriteValue() reports its own failure
                if (result.IsOk())
                    return WriteValue(written, options);

                result = SetFailure(std::move(result));
            }
        }
        catch (...)
        {
            result = CaughtSetFailure();
        }

        ReportFailure(result);

        return result;
    }
};

} // namespace ASWToml

//---------------------------------------------------------------------------
#endif // #ifndef ASWToml_SettingsH
