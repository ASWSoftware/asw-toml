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
#include <tuple>
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
// TPartRepair
//
// A part of a setting's value that bringing the value up to date by parts (see RepairValue()) replaced by its default,
// or a key it added with its default
/////////////////////////////////////////////////////////////////////////////
struct TPartRepair
{
    // For a part replaced: why (the converter's failure), with the part's path from the setting's value in Path (e.g.
    // [1].port, or none for the whole value); for a key added: Ok, with the key's path in Path
    TASWTomlResult Problem;

    TASWTomlValue Default; // The part's default value, as it is written
    bool Added = false; // Whether it is a key added
};

// The parts replaced and the keys added, in the order they were found
using TPartRepairVector = std::vector<TPartRepair>;


/////////////////////////////////////////////////////////////////////////////
// TValueOverlay
//
// Works out the value a setting's Set() writes over the value the document has (see TASWBaseToml::SetSettingValue()
// and OverlayValue()). Implemented by TSettingOverlay<T>.
/////////////////////////////////////////////////////////////////////////////
class TValueOverlay
{
public:
    virtual ~TValueOverlay() = default;

    // The value to set where the document has 'old' (one of its values), for the new value 'value', adding to
    // 'partners' how the elements of the arrays of 'old' pair with the new ones (see TASWTomlDocument::SetValue()).
    // Throws only std::bad_alloc.
    virtual TASWTomlValue Overlay(TASWTomlValue const& old, TASWTomlValue const& value,
        TPartnersMap& partners) const = 0;
};

//---------------------------------------------------------------------------

// Adds 'key' to 'table' with 'written', the default value of a field the table lacks, and that it was added to
// 'repairs'
void AddPartKey(TASWTomlValue& table, std::string_view key, TASWTomlValue written, TPartRepairVector& repairs);

// Puts "[index]" in front of the Path of each of 'repairs' from 'first' on (the repairs of an element), as
// ElementFailure() does
void ElementRepairs(TPartRepairVector& repairs, std::size_t first, std::size_t index);

// Puts 'key' in front of the Path of each of 'repairs' from 'first' on (the repairs of a key's value), as KeyFailure()
// does
void KeyRepairs(TPartRepairVector& repairs, std::size_t first, std::string_view key);

// Replaces 'value', a part that failed with 'problem', by 'written', its default value, and adds that to 'repairs'
void ReplacePart(TASWTomlValue& value, TASWTomlResult problem, TASWTomlValue written, TPartRepairVector& repairs);

// The kind of parts a T's value is brought up to date by (TArrayParts, TStructParts or TTableParts), which its
// converter names; none for a type that is one part
template <typename T>
using TPartsKind = typename TASWTomlConverter<T>::TByParts;

// Whether a T's value is brought up to date by parts (see RepairValue()), and whether by parts of the kind TKind
template <typename T>
concept ByParts = requires { typename TPartsKind<T>; };
template <typename T, typename TKind>
concept WalkedBy = ByParts<T> && std::same_as<TPartsKind<T>, TKind>;

// Whether 'old' and 'value' (a value of 'in' as TASWTomlConverter<T> writes it with 'options') are the same as far as
// T knows them: the same, but for the keys of 'old' that no field of a struct names and the optional fields
// (OptionalField()) that 'old' lacks, at any depth. Throws only std::bad_alloc.
template <typename T>
bool KnownEqual(T const& in, TASWTomlValue const& old, TASWTomlValue const& value,
    TASWTomlValueOptions const& options);

// The value a setting's Set() writes where the document has 'old', for 'value' (the value of 'in' as
// TASWTomlConverter<T> writes it with 'options'): 'value', with what 'old' has that T doesn't know kept, walked by the
// type as RepairValue() walks it. A struct (walked by parts): 'old' with each field's key set from 'value' (each
// overlaid in turn), a key 'value' lacks (a std::nullopt member) removed, and the keys no field names kept where they
// are. A std::vector: the elements of 'value', each paired with an element of 'old' as the document's merge pairs
// them (see Partners()), but by what the element type knows of them (see KnownEqual()), and each overlaid on its
// partner; a new one as it is. A map: the keys of 'value', each value overlaid on the value of the same key in 'old';
// the keys only 'old' has go. Anything else, or a value of another TOML type than 'value' in 'old': 'value'. Adds the
// pairs of each array's elements to 'partners', by the address of the array in 'old', so the document's merge pairs
// them so too (see TASWTomlDocument::SetValue()). Runs only the library's code and the field lists (ASWTomlFields()),
// not a converter, so it may run under the owner's lock. Throws only std::bad_alloc.
template <typename T>
TASWTomlValue OverlayValue(T const& in, TASWTomlValue const& old, TASWTomlValue const& value,
    TASWTomlValueOptions const& options, TPartnersMap& partners);

// Reads 'value', a setting's value or a part of one, into 'out', which holds the part's default value, bringing it up
// to date by parts as the C++ type says, with 'options': each part that is missing or isn't valid gets its default
// value, in 'out' and in 'value', and is added to 'repairs' (with its path from 'value'):
// - A struct with fields (see TASWTomlConverter<structs with fields>): each field the table lacks gets the member's
//   value in 'out' as its key's value (added to 'repairs'), unless it is optional (OptionalField(), or a
//   std::optional member, which is then std::nullopt); each key that is there is brought up to date as its member's
//   type; the keys no field names stay.
// - A std::vector: each element, as a new element whose default is a value-initialized one.
// - A std::map or std::unordered_map: each key's value, as a new value whose default is a value-initialized one; a
//   key that doesn't read, or reads as one the map has, makes the whole map one part.
// - Anything else (a type whose converter doesn't name TByParts), and a value of another TOML type than a table or
//   array where one is wanted: one part, replaced by the default value when its converter doesn't read it.
// Returns Ok, or when a part's default value can't be written (e.g. an enum value without a name), the failure of
// that part ('value', 'out' and 'repairs' then hold part of the work: the caller uses the setting's default value).
template <typename T>
TASWTomlResult RepairValue(TASWTomlValue& value, T& out, TASWTomlValueOptions const& options,
    TPartRepairVector& repairs);


/////////////////////////////////////////////////////////////////////////////
// TKnownMatch
//
// The elements of an old array and a new one that are the same for a setting's overlay (see OverlayValue()): the same
// as far as the vector T's element type knows them (see KnownEqual())
/////////////////////////////////////////////////////////////////////////////
template <typename T>
class TKnownMatch : public TElementMatch
{
private:
    T const& m_In; // The vector the new elements were written from
    TASWTomlValue::TValueVector const& m_Old;
    TASWTomlValue::TValueVector const& m_Values;
    TASWTomlValueOptions const& m_Options;

public:
    // The elements of 'old' and 'values', which 'in' was written as with 'options'; all must outlive it
    TKnownMatch(T const& in, TASWTomlValue::TValueVector const& old, TASWTomlValue::TValueVector const& values,
        TASWTomlValueOptions const& options) noexcept
        : m_In(in),
          m_Old(old),
          m_Values(values),
          m_Options(options)
    {
    }

    bool IsSame(std::size_t oldIndex, std::size_t index) const override
    {
        return KnownEqual<typename T::value_type>(m_In[index], m_Old[oldIndex], m_Values[index], m_Options);
    }
};


/////////////////////////////////////////////////////////////////////////////
// TSettingOverlay
//
// The overlay of a value of T (see OverlayValue()), for TASWTomlSetting<T>::Set()
/////////////////////////////////////////////////////////////////////////////
template <typename T>
class TSettingOverlay final : public TValueOverlay
{
private:
    T const& m_In;
    TASWTomlValueOptions const& m_Options;

public:
    // The overlay of the value 'in', written with 'options'; both must outlive it
    TSettingOverlay(T const& in, TASWTomlValueOptions const& options) noexcept
        : m_In(in),
          m_Options(options)
    {
    }

    TASWTomlValue Overlay(TASWTomlValue const& old, TASWTomlValue const& value, TPartnersMap& partners) const override
    {
        return OverlayValue<T>(m_In, old, value, m_Options, partners);
    }
};

//---------------------------------------------------------------------------

// Adds the key 'key' to 'table' with 'out' (the default value of a field the table lacks) written with 'options', and
// that to 'repairs'; or when 'out' can't be written, returns the failure that the table lacks the key
template <typename T>
TASWTomlResult AddDefaultKey(TASWTomlValue& table, std::string_view key, T const& out,
    TASWTomlValueOptions const& options, TPartRepairVector& repairs)
{
    TASWTomlValue written;
    if (!TASWTomlConverter<T>::ToValue(out, written, options).IsOk())
        return KeyFailure(MissingKey(key), key);

    AddPartKey(table, key, std::move(written), repairs);

    return TASWTomlResult();
}

// Whether the key of 'field', a field of the struct T, has the same value in 'old' and 'value' as far as the field's
// type knows them (see KnownEqual()), or is in neither. An optional field (OptionalField()) that 'old' lacks doesn't
// make them differ, as a struct always writes it.
template <typename T, typename TStruct, typename TMember>
bool KnownFieldEqual(TASWTomlField<TStruct, TMember> const& field, T const& in, TASWTomlValue const& old,
    TASWTomlValue const& value, TASWTomlValueOptions const& options)
{
    TASWTomlValue const* const oldItem = old.Find(field.Key);
    TASWTomlValue const* const item = value.Find(field.Key);
    if (oldItem == nullptr && field.Optional)
        return true;

    if (oldItem == nullptr || item == nullptr)
        return oldItem == item;

    TMember const& member = in.*field.Member;
    if constexpr (TIsOptional<TMember>::value)
    {
        // 'value' has the key only when the member has a value
        if (!member)
            return IdenticalValues(*oldItem, *item);

        return KnownEqual<typename TIsOptional<TMember>::TItem>(*member, *oldItem, *item, options);
    }
    else
    {
        return KnownEqual<TMember>(member, *oldItem, *item, options);
    }
}

// Whether the fields of 'fields' from the index I on are the same in 'old' and 'value' (see KnownFieldEqual())
template <std::size_t I, typename T, typename TFields>
bool KnownFieldsEqual(TFields const& fields, T const& in, TASWTomlValue const& old, TASWTomlValue const& value,
    TASWTomlValueOptions const& options)
{
    if constexpr (I == std::tuple_size_v<TFields>)
    {
        return true;
    }
    else
    {
        if (!KnownFieldEqual(std::get<I>(fields), in, old, value, options))
            return false;

        return KnownFieldsEqual<I + 1>(fields, in, old, value, options);
    }
}

// Sets the key of 'field', a field of the struct T, in 'result' (a copy of 'old') as OverlayValue() says: from 'value',
// overlaid on the key's value in 'old', or removed when 'value' lacks it
template <typename T, typename TStruct, typename TMember>
void OverlayField(TASWTomlField<TStruct, TMember> const& field, T const& in, TASWTomlValue const& old,
    TASWTomlValue const& value, TASWTomlValueOptions const& options, TPartnersMap& partners, TASWTomlValue& result)
{
    TASWTomlValue const* const item = value.Find(field.Key);
    if (item == nullptr)
    {
        static_cast<void>(result.Remove(field.Key));
        return;
    }

    TASWTomlValue const* const oldItem = old.Find(field.Key);
    TMember const& member = in.*field.Member;
    TASWTomlValue overlaid;
    if (oldItem == nullptr)
    {
        overlaid = *item;
    }
    else if constexpr (TIsOptional<TMember>::value)
    {
        // 'value' has the key only when the member has a value
        if (member)
            overlaid = OverlayValue<typename TIsOptional<TMember>::TItem>(*member, *oldItem, *item, options,
                partners);
        else
            overlaid = *item;
    }
    else
    {
        overlaid = OverlayValue<TMember>(member, *oldItem, *item, options, partners);
    }

    static_cast<void>(result.Set(std::string(field.Key), std::move(overlaid)));
}

// Sets the fields of 'fields' from the index I on in 'result' (see OverlayField())
template <std::size_t I, typename T, typename TFields>
void OverlayFields(TFields const& fields, T const& in, TASWTomlValue const& old, TASWTomlValue const& value,
    TASWTomlValueOptions const& options, TPartnersMap& partners, TASWTomlValue& result)
{
    if constexpr (I < std::tuple_size_v<TFields>)
    {
        OverlayField(std::get<I>(fields), in, old, value, options, partners, result);
        OverlayFields<I + 1>(fields, in, old, value, options, partners, result);
    }
}

// Replaces 'value', a part that failed with 'problem', by its default value 'out' written with 'options', and adds
// that to 'repairs'; or when 'out' can't be written, returns 'problem'
template <typename T>
TASWTomlResult ReplaceByDefault(TASWTomlValue& value, T const& out, TASWTomlResult problem,
    TASWTomlValueOptions const& options, TPartRepairVector& repairs)
{
    TASWTomlValue written;
    if (!TASWTomlConverter<T>::ToValue(out, written, options).IsOk())
        return problem;

    ReplacePart(value, std::move(problem), std::move(written), repairs);

    return TASWTomlResult();
}

// RepairValue() of a std::vector
template <typename T>
TASWTomlResult RepairArray(TASWTomlValue& value, T& out, TASWTomlValueOptions const& options,
    TPartRepairVector& repairs)
{
    using E = typename T::value_type;

    TASWTomlValue::TValueVector* const array = value.AsArray();
    if (array == nullptr)
        return ReplaceByDefault(value, out, TypeMismatch(value, "an array"), options, repairs);

    T read(out.get_allocator());
    read.reserve(array->size());
    for (std::size_t i = 0; i < array->size(); ++i)
    {
        std::size_t const first = repairs.size();
        E element{};
        TASWTomlResult result = RepairValue<E>((*array)[i], element, options, repairs);
        ElementRepairs(repairs, first, i);
        if (!result.IsOk())
            return ElementFailure(std::move(result), i);

        read.push_back(std::move(element));
    }

    out = std::move(read);

    return TASWTomlResult();
}

// RepairValue() of the key of 'field', a field of the struct T, in 'table'
template <typename T, typename TStruct, typename TMember>
TASWTomlResult RepairField(TASWTomlField<TStruct, TMember> const& field, TASWTomlValue& table, T& out,
    TASWTomlValueOptions const& options, TPartRepairVector& repairs)
{
    TMember& member = out.*field.Member;
    TASWTomlValue* const value = table.Find(field.Key);
    std::size_t const first = repairs.size();
    TASWTomlResult result;
    if constexpr (TIsOptional<TMember>::value)
    {
        if (value == nullptr)
        {
            member.reset();
            return result;
        }

        // Read into the value it holds, as the struct converter reads it
        if (!member)
            member.emplace();

        result = RepairValue<typename TIsOptional<TMember>::TItem>(*value, *member, options, repairs);
    }
    else
    {
        if (value == nullptr)
        {
            if (field.Optional)
                return result;

            return AddDefaultKey(table, field.Key, member, options, repairs);
        }

        result = RepairValue<TMember>(*value, member, options, repairs);
    }

    KeyRepairs(repairs, first, field.Key);
    if (!result.IsOk())
        return KeyFailure(std::move(result), field.Key);

    return result;
}

// RepairValue() of the fields of 'fields' from the index I on, in 'table'
template <std::size_t I, typename T, typename TFields>
TASWTomlResult RepairFields(TFields const& fields, TASWTomlValue& table, T& out, TASWTomlValueOptions const& options,
    TPartRepairVector& repairs)
{
    if constexpr (I == std::tuple_size_v<TFields>)
    {
        return TASWTomlResult();
    }
    else
    {
        TASWTomlResult result = RepairField(std::get<I>(fields), table, out, options, repairs);
        if (!result.IsOk())
            return result;

        return RepairFields<I + 1>(fields, table, out, options, repairs);
    }
}

// RepairValue() of a struct with fields
template <typename T>
TASWTomlResult RepairStruct(TASWTomlValue& value, T& out, TASWTomlValueOptions const& options,
    TPartRepairVector& repairs)
{
    if (!value.IsTable())
        return ReplaceByDefault(value, out, TypeMismatch(value, "a table"), options, repairs);

    return RepairFields<0>(ASWTomlFields(out), value, out, options, repairs);
}

// RepairValue() of a std::map or std::unordered_map
template <typename T>
TASWTomlResult RepairTable(TASWTomlValue& value, T& out, TASWTomlValueOptions const& options,
    TPartRepairVector& repairs)
{
    using K = typename T::key_type;
    using M = typename T::mapped_type;

    TASWTomlValue::TKeyValueVector* const table = value.AsTable();
    if (table == nullptr)
        return ReplaceByDefault(value, out, TypeMismatch(value, "a table"), options, repairs);

    // A copy of the map without its keys, so it has the same comparator, hash and allocator
    T read = out;
    read.clear();
    std::size_t const before = repairs.size();
    TASWTomlResult keyProblem;
    for (TASWTomlKeyValue& entry : *table)
    {
        K key{};
        keyProblem = TKeyConverter<K>::FromKey(entry.Key, key, options);
        if (keyProblem.IsOk() && read.contains(key))
            keyProblem = RepeatedKey(entry.Key);

        if (!keyProblem.IsOk())
        {
            keyProblem = KeyFailure(std::move(keyProblem), entry.Key);
            break;
        }

        std::size_t const first = repairs.size();
        M item{};
        TASWTomlResult result = RepairValue<M>(entry.Value, item, options, repairs);
        KeyRepairs(repairs, first, entry.Key);
        if (!result.IsOk())
            return KeyFailure(std::move(result), entry.Key);

        read.emplace(std::move(key), std::move(item));
    }

    // A key that doesn't read makes the whole map one part
    if (!keyProblem.IsOk())
    {
        repairs.erase(repairs.begin() + static_cast<std::ptrdiff_t>(before), repairs.end());
        return ReplaceByDefault(value, out, std::move(keyProblem), options, repairs);
    }

    out = std::move(read);

    return TASWTomlResult();
}

// RepairValue() of a value that is one part
template <typename T>
TASWTomlResult RepairWhole(TASWTomlValue& value, T& out, TASWTomlValueOptions const& options,
    TPartRepairVector& repairs)
{
    // Read into a copy, so 'out' keeps the default value whatever the converter does before it fails
    T read = out;
    TASWTomlResult result = TASWTomlConverter<T>::FromValue(value, read, options);
    if (!result.IsOk())
        return ReplaceByDefault(value, out, std::move(result), options, repairs);

    out = std::move(read);

    return result;
}

//---------------------------------------------------------------------------
template <typename T>
bool KnownEqual(T const& in, TASWTomlValue const& old, TASWTomlValue const& value,
    TASWTomlValueOptions const& options)
{
    if constexpr (WalkedBy<T, TStructParts>)
    {
        if (!old.IsTable() || !value.IsTable())
            return IdenticalValues(old, value);

        return KnownFieldsEqual<0>(ASWTomlFields(in), in, old, value, options);
    }
    else if constexpr (WalkedBy<T, TArrayParts>)
    {
        TASWTomlValue::TValueVector const* const oldArray = old.AsArray();
        TASWTomlValue::TValueVector const* const array = value.AsArray();
        if (oldArray == nullptr || array == nullptr)
            return IdenticalValues(old, value);

        if (oldArray->size() != array->size() || array->size() != in.size())
            return false;

        for (std::size_t i = 0; i < array->size(); ++i)
        {
            if (!KnownEqual<typename T::value_type>(in[i], (*oldArray)[i], (*array)[i], options))
                return false;
        }

        return true;
    }
    else if constexpr (WalkedBy<T, TTableParts>)
    {
        TASWTomlValue::TKeyValueVector const* const oldTable = old.AsTable();
        TASWTomlValue::TKeyValueVector const* const table = value.AsTable();
        if (oldTable == nullptr || table == nullptr)
            return IdenticalValues(old, value);

        if (oldTable->size() != table->size())
            return false;

        // The same keys, as each of 'in' is in both
        for (auto const& item : in)
        {
            std::string key;
            if (!TKeyConverter<typename T::key_type>::ToKey(item.first, key, options).IsOk())
                return IdenticalValues(old, value);

            TASWTomlValue const* const oldItem = old.Find(key);
            TASWTomlValue const* const newItem = value.Find(key);
            if (oldItem == nullptr || newItem == nullptr)
                return false;

            if (!KnownEqual<typename T::mapped_type>(item.second, *oldItem, *newItem, options))
                return false;
        }

        return true;
    }
    else
    {
        return IdenticalValues(old, value);
    }
}
//---------------------------------------------------------------------------
template <typename T>
TASWTomlValue OverlayValue(T const& in, TASWTomlValue const& old, TASWTomlValue const& value,
    TASWTomlValueOptions const& options, TPartnersMap& partners)
{
    if constexpr (WalkedBy<T, TStructParts>)
    {
        if (!old.IsTable() || !value.IsTable())
            return value;

        TASWTomlValue result = old;
        OverlayFields<0>(ASWTomlFields(in), in, old, value, options, partners, result);

        return result;
    }
    else if constexpr (WalkedBy<T, TArrayParts>)
    {
        TASWTomlValue::TValueVector const* const oldArray = old.AsArray();
        TASWTomlValue::TValueVector const* const array = value.AsArray();
        if (oldArray == nullptr || array == nullptr || array->size() != in.size())
            return value;

        TIndexVector const& pairs = partners[&old] = Partners(oldArray->size(), array->size(),
            TKnownMatch<T>(in, *oldArray, *array, options));
        TASWTomlValue::TValueVector elements;
        elements.reserve(array->size());
        for (std::size_t j = 0; j < array->size(); ++j)
        {
            if (pairs[j] == NoIndex)
            {
                elements.push_back((*array)[j]);
            }
            else
            {
                TASWTomlValue const& oldElement = (*oldArray)[pairs[j]];
                elements.push_back(OverlayValue<typename T::value_type>(in[j], oldElement, (*array)[j], options,
                    partners));
            }
        }

        return TASWTomlValue(std::move(elements));
    }
    else if constexpr (WalkedBy<T, TTableParts>)
    {
        if (!old.IsTable() || !value.IsTable())
            return value;

        // The keys of 'value', each overlaid on the same key's value in 'old'
        TASWTomlValue result = value;
        for (auto const& item : in)
        {
            std::string key;
            if (!TKeyConverter<typename T::key_type>::ToKey(item.first, key, options).IsOk())
                continue;

            TASWTomlValue const* const oldItem = old.Find(key);
            TASWTomlValue* const newItem = result.Find(key);
            if (oldItem != nullptr && newItem != nullptr)
            {
                TASWTomlValue overlaid = OverlayValue<typename T::mapped_type>(item.second, *oldItem, *newItem, options,
                    partners);
                *newItem = std::move(overlaid);
            }
        }

        return result;
    }
    else
    {
        return value;
    }
}
//---------------------------------------------------------------------------
template <typename T>
TASWTomlResult RepairValue(TASWTomlValue& value, T& out, TASWTomlValueOptions const& options,
    TPartRepairVector& repairs)
{
    if constexpr (WalkedBy<T, TStructParts>)
        return RepairStruct(value, out, options, repairs);
    else if constexpr (WalkedBy<T, TArrayParts>)
        return RepairArray(value, out, options, repairs);
    else if constexpr (WalkedBy<T, TTableParts>)
        return RepairTable(value, out, options, repairs);
    else
        return RepairWhole(value, out, options, repairs);
}
//---------------------------------------------------------------------------


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

    // Adds to 'apply' the value 'repaired' (the key's value brought up to date by parts, see RepairValue(); written
    // with 'values', the setting's own value options, or nullptr for the document's) as the key's value, a message for
    // each of 'repairs' (for a part replaced, the Warning of DefaultUsedMessage(); for a key added, the Info that it is
    // added, ETomlCode::KeyAdded), and the setting's comment
    void ApplyRepairs(TSettingsApply& apply, TASWTomlValue const& repaired, TASWTomlValueOptions const* values,
        TPartRepairVector const& repairs) const;

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

    // Reports the Warning of DefaultUsedMessage() for each part of 'repairs' that was replaced (Get() gives the value
    // with its default), but not a key added (a missing part gives its default silently). A message that can't be made
    // (std::bad_alloc) is dropped.
    void ReportRepairs(TPartRepairVector const& repairs) const noexcept;

    // 'result', a failure of the setting's value, with the setting's path in front of its Path (see
    // Detail::PathFailure())
    TASWTomlResult SetFailure(TASWTomlResult result) const;

    // Removes the setting from the ones bound to its owner, waiting for a Load() that is applying it
    void Unregister() const;

    // Sets the value at the setting's path in the owner's document to 'value', written with the value options
    // 'values', as IASWToml's SetValue() does (reporting a failure), or with 'overlay' (nullptr for none) to its
    // overlay on the value there (see OverlayValue()); a key that it adds gets the setting's comment in the same edit
    // (see TASWBaseToml)
    TASWTomlResult WriteValue(TASWTomlValue const& value, TASWTomlValueOptions const& values,
        TValueOverlay const* overlay);

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
// A struct with fields, a std::vector or a map (of the library's converters) is taken by parts: its value is the
// document's with each part that is missing or isn't valid replaced by its default (a field's is the member of the
// default value for the setting's own struct, and of a value-initialized element or value for an element or a map's
// value; see Detail::RepairValue()), then checked whole by the range and the validator; so one port that isn't valid
// costs that port, not the user's other servers, and a field a later version of the struct has gets its default. A
// setting is bound to its owner from its constructor to its destructor, in the order settings are declared. When
// the owner's Load() or LoadText() has read a document, before anyone sees it, each setting bound to it is applied to
// it, in that order: a missing key whose earlier name (see TASWTomlSettingOptions::OldKeys) its table has is renamed
// from it, keeping its line or header, value and comment, reported as an Info message (ETomlCode::KeyRenamed), and is
// then applied as a key that was there; any other missing key is added with the default value and the setting's
// comment, reported as an Info message (ETomlCode::KeyAdded), unless the setting is optional (see
// TASWTomlSettingOptions::Optional); a value that isn't valid is replaced by the default value, reported as a Warning
// (as Get() reports it), and a valid one is left as it is (for a value taken by parts, each part that isn't valid is
// replaced by its default, with a Warning, and each missing field that isn't optional is added, with an Info,
// KeyAdded, keeping the rest as it is); the key's comment is set to the setting's (if it has one)
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

        // Brought up to date by parts: each part that is missing or isn't valid gets its default (see RepairValue()),
        // then the whole is checked
        T value = m_Default;
        TASWTomlValue repaired = *found;
        Detail::TPartRepairVector repairs;
        result = Detail::RepairValue<T>(repaired, value, options, repairs);
        if (result.IsOk())
            result = Check(value, repaired, options);

        if (!result.IsOk())
            ApplyDefault(apply, defaultValue, values, &result);
        else if (repairs.empty())
            ApplyComment(apply);
        else
            ApplyRepairs(apply, repaired, values, repairs);
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
    // (it doesn't convert, is out of range, or the validator refuses it). A value taken by parts (see TASWTomlSetting)
    // has each part that is missing or isn't valid replaced by its default, and is the default value only when the
    // range or the validator refuses the result, or a part's default can't be written. A value or part that isn't
    // valid is reported as a Warning message with the failure's code and text, the default value, and the path of the
    // place that failed (e.g. servers[1].port), and a missing part isn't reported; a setting whose path can't be used
    // (see GetPath()) as an Error (InvalidArgument). May throw what the converter, the validator or the owner's
    // operations throw (e.g. std::bad_alloc).
    T Get() const
    {
        TASWTomlValue found;
        if (!FindValue(found))
            return m_Default;

        // Brought up to date by parts, as Load() does it
        TASWTomlValueOptions const options = ValueOptions();
        T value = m_Default;
        Detail::TPartRepairVector repairs;
        TASWTomlResult result = Detail::RepairValue<T>(found, value, options, repairs);
        if (result.IsOk())
            result = Check(value, found, options);

        if (result.IsOk())
        {
            ReportRepairs(repairs);
            return value;
        }

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
    // of its own, in the same edit, and a missing table too. A value taken by parts (see TASWTomlSetting) is set over
    // the document's instead, in the same lock: the keys the struct's fields don't name stay, at any depth; the
    // elements of a vector are paired with the document's by what their type knows of them, so the block of an element
    // removed goes and the others keep their other keys and comments; a map's values by key (see
    // Detail::OverlayValue()). IASWToml's SetValue() sets a value as it is. Fails, and then nothing changes, with
    // ValueOutOfRange for a value out of the range, InvalidValue for one that the validator refuses or that can't be
    // converted (e.g. an enum value with no name), each with the setting's path in Path, InvalidArgument for a setting
    // whose path can't be used, UnexpectedException for an exception (e.g. one the validator throws), or as IASWToml's
    // SetValue() does. A failure is reported as an Error message.
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

                // WriteValue() reports its own failure. A value taken by parts is overlaid on the document's, so it
                // keeps what T doesn't know (see OverlayValue()).
                if (result.IsOk())
                {
                    Detail::TSettingOverlay<T> const overlay(value, options);
                    return WriteValue(written, options, Detail::ByParts<T> ? &overlay : nullptr);
                }

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
