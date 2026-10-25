/* **************************************************************************
ASWToml_Converters.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "ASWToml_Converters.h"
//---------------------------------------------------------------------------
#include <string>
#include <string_view>
#include <utility>
//---------------------------------------------------------------------------
#include "ASWToml_File.h"
#include "ASWToml_Scalars.h"
#include "ASWToml_Utils.h"
#include "ASWToml_Writer.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

namespace
{

TASWTomlResult Failure(ETomlCode code, std::string message);
std::string PrefixedPath(std::string part, std::string const& path);
std::string ValidUtf8(std::string_view text);

//---------------------------------------------------------------------------
/*
    Failure

    A failed result with 'code' and 'message'
*/
TASWTomlResult Failure(ETomlCode code, std::string message)
{
    TASWTomlResult result;
    result.Code = code;
    result.Message = std::move(message);

    return result;
}
//---------------------------------------------------------------------------
/*
    PrefixedPath

    'path' (a path relative to a part of a value, maybe empty) after 'part' (the part's key or [index]): 'part' alone,
    'part' followed by 'path' when that starts with an index, else 'part', '.' and 'path'
*/
std::string PrefixedPath(std::string part, std::string const& path)
{
    if (!path.empty())
    {
        if (path.front() != '[')
            part += '.';

        part += path;
    }

    return part;
}
//---------------------------------------------------------------------------
/*
    ValidUtf8

    'text' with each byte that doesn't belong to a valid UTF-8 sequence as '?'
*/
std::string ValidUtf8(std::string_view text)
{
    std::string valid;
    std::size_t invalid = Detail::FindInvalidUtf8(text);
    while (invalid != std::string_view::npos)
    {
        valid += text.substr(0, invalid);
        valid += '?';
        text.remove_prefix(invalid + 1);
        invalid = Detail::FindInvalidUtf8(text);
    }

    valid += text;

    return valid;
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
TASWTomlResult Detail::ElementFailure(TASWTomlResult result, std::size_t index)
{
    result.Path = PrefixedPath("[" + std::to_string(index) + "]", result.Path);

    return result;
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::KeyFailure(TASWTomlResult result, std::string_view key)
{
    // A key that isn't valid UTF-8 can't be written as TOML (WriteKey() then appends nothing), so its bytes that
    // aren't are shown as '?'
    std::string part;
    if (!WriteKey(key, part).IsOk())
        static_cast<void>(WriteKey(ValidUtf8(key), part));

    result.Path = PrefixedPath(std::move(part), result.Path);

    return result;
}
//---------------------------------------------------------------------------
char const* Detail::TypeName(TASWTomlValue const& value) noexcept
{
    switch (value.GetType())
    {
        case ETomlType::String:
            return "a string";

        case ETomlType::Integer:
            return "an integer";

        case ETomlType::Float:
            return "a float";

        case ETomlType::Boolean:
            return "a boolean";

        case ETomlType::OffsetDateTime:
            return "an offset date-time";

        case ETomlType::LocalDateTime:
            return "a local date-time";

        case ETomlType::LocalDate:
            return "a local date";

        case ETomlType::LocalTime:
            return "a local time";

        case ETomlType::Array:
            return "an array";

        case ETomlType::Table:
            return "a table";
    }

    return "a value";
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::TypeMismatch(TASWTomlValue const& value, std::string_view wanted)
{
    std::string problem = "is ";
    problem += TypeName(value);
    problem += ", not ";
    problem += wanted;
    problem += '.';

    return ValueFailure(ETomlCode::TypeMismatch, value, problem);
}
//---------------------------------------------------------------------------
TASWTomlResult Detail::ValueFailure(ETomlCode code, TASWTomlValue const& value, std::string_view problem)
{
    std::string message = "The value ";
    message += ValueText(value);
    message += ' ';
    message += problem;

    return Failure(code, std::move(message));
}
//---------------------------------------------------------------------------
std::string Detail::ValueText(TASWTomlValue const& value)
{
    // Strings as basic strings, which escape every control character; nested as deep as a document's values may be
    TWriteStyle style;
    style.Values.StringStyle = EStringStyle::Basic;
    style.MaxDepth = TASWTomlReadingOptions::MaxNestingDepthLimit;

    std::string text;
    if (!WriteValue(value, style, text).IsOk())
        return std::string("of type ") + ToString(value.GetType());

    if (text.size() > MaxValueTextSize)
    {
        // Cut before a UTF-8 sequence, never inside one
        std::size_t cut = MaxValueTextSize;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
            --cut;

        text.resize(cut);
        text += "...";
    }

    return text;
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<TASWTomlValue>
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<TASWTomlValue>::FromValue(TASWTomlValue const& value, TASWTomlValue& out,
    TASWTomlValueOptions const& /*options*/)
{
    // Copied first, so 'out' is unchanged if the copy throws
    TASWTomlValue copy = value;
    out = std::move(copy);

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<TASWTomlValue>::ToValue(TASWTomlValue const& in, TASWTomlValue& value,
    TASWTomlValueOptions const& /*options*/)
{
    TASWTomlValue copy = in;
    value = std::move(copy);

    return TASWTomlResult();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<bool>
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<bool>::FromValue(TASWTomlValue const& value, bool& out,
    TASWTomlValueOptions const& /*options*/)
{
    bool const* const flag = value.AsBoolean();
    if (flag == nullptr)
        return Detail::TypeMismatch(value, "a boolean");

    out = *flag;

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<bool>::ToValue(bool const& in, TASWTomlValue& value,
    TASWTomlValueOptions const& /*options*/)
{
    value = TASWTomlValue(in);

    return TASWTomlResult();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<std::filesystem::path>
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<std::filesystem::path>::FromValue(TASWTomlValue const& value,
    std::filesystem::path& out, TASWTomlValueOptions const& /*options*/)
{
    std::string const* const text = value.AsString();
    if (text == nullptr)
        return Detail::TypeMismatch(value, "a string");

    // Windows would make each invalid sequence U+FFFD, and a POSIX system would take the bytes as they are
    if (!Detail::IsValidUtf8(*text))
        return Detail::ValueFailure(ETomlCode::InvalidValue, value, "isn't a path: it isn't valid UTF-8.");

    out = Utf8ToPath(*text);

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<std::filesystem::path>::ToValue(std::filesystem::path const& in,
    TASWTomlValue& value, TASWTomlValueOptions const& /*options*/)
{
    std::string text = PathToUtf8(in);
    if (!Detail::IsValidUtf8(text) || Utf8ToPath(text) != in)
    {
        return Failure(ETomlCode::InvalidValue,
            "The path can't be written: it isn't valid Unicode, so it wouldn't read back as the same path.");
    }

    value = TASWTomlValue(std::move(text));

    return TASWTomlResult();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<std::string>
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<std::string>::FromValue(TASWTomlValue const& value, std::string& out,
    TASWTomlValueOptions const& /*options*/)
{
    std::string const* const text = value.AsString();
    if (text == nullptr)
        return Detail::TypeMismatch(value, "a string");

    out = *text;

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<std::string>::ToValue(std::string const& in, TASWTomlValue& value,
    TASWTomlValueOptions const& /*options*/)
{
    std::size_t const invalid = Detail::FindInvalidUtf8(in);
    if (invalid != std::string_view::npos)
    {
        return Failure(ETomlCode::InvalidValue, "The string can't be written: it isn't valid UTF-8 (at byte " +
            std::to_string(invalid) + "), which TOML needs.");
    }

    value = TASWTomlValue(in);

    return TASWTomlResult();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlConverter<std::u8string>
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<std::u8string>::FromValue(TASWTomlValue const& value, std::u8string& out,
    TASWTomlValueOptions const& /*options*/)
{
    std::string const* const text = value.AsString();
    if (text == nullptr)
        return Detail::TypeMismatch(value, "a string");

    out.assign(text->begin(), text->end());

    return TASWTomlResult();
}
//---------------------------------------------------------------------------
TASWTomlResult TASWTomlConverter<std::u8string>::ToValue(std::u8string const& in, TASWTomlValue& value,
    TASWTomlValueOptions const& options)
{
    return TASWTomlConverter<std::string>::ToValue(std::string(in.begin(), in.end()), value, options);
}
//---------------------------------------------------------------------------

} // namespace ASWToml
