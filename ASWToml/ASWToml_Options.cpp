/* **************************************************************************
ASWToml_Options.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "ASWToml_Options.h"
//---------------------------------------------------------------------------
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
//---------------------------------------------------------------------------
#include "ASWToml_Utils.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

namespace
{

bool HasLineBreakOrNul(std::string_view text) noexcept;
TASWTomlResult Invalid(std::string message);

//---------------------------------------------------------------------------
/*
    HasLineBreakOrNul

    Whether 'text' has a line break or a NUL, which no file name may have
*/
bool HasLineBreakOrNul(std::string_view text) noexcept
{
    return text.find_first_of(std::string_view("\r\n\0", 3)) != std::string_view::npos;
}
//---------------------------------------------------------------------------
/*
    Invalid

    An InvalidOptions result with 'message'
*/
TASWTomlResult Invalid(std::string message)
{
    TASWTomlResult result;
    result.Code = ETomlCode::InvalidOptions;
    result.Message = std::move(message);

    return result;
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
char const* ToString(EBomMode bom) noexcept
{
    switch (bom)
    {
        case EBomMode::Auto:
            return "Auto";

        case EBomMode::Always:
            return "Always";

        case EBomMode::Never:
            return "Never";
    }

    return "Unknown";
}
//---------------------------------------------------------------------------
char const* ToString(EIntegerBase integerBase) noexcept
{
    switch (integerBase)
    {
        case EIntegerBase::Auto:
            return "Auto";

        case EIntegerBase::Decimal:
            return "Decimal";

        case EIntegerBase::Hexadecimal:
            return "Hexadecimal";

        case EIntegerBase::Octal:
            return "Octal";

        case EIntegerBase::Binary:
            return "Binary";
    }

    return "Unknown";
}
//---------------------------------------------------------------------------
char const* ToString(ELineEnding lineEnding) noexcept
{
    switch (lineEnding)
    {
        case ELineEnding::Auto:
            return "Auto";

        case ELineEnding::Native:
            return "Native";

        case ELineEnding::LF:
            return "LF";

        case ELineEnding::CRLF:
            return "CRLF";
    }

    return "Unknown";
}
//---------------------------------------------------------------------------
char const* ToString(EStringStyle stringStyle) noexcept
{
    switch (stringStyle)
    {
        case EStringStyle::Auto:
            return "Auto";

        case EStringStyle::Basic:
            return "Basic";

        case EStringStyle::Literal:
            return "Literal";
    }

    return "Unknown";
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlReadingOptions
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlReadingOptions::Validate() const
{
    if (MaxFileSize == 0)
        return Invalid("Reading.MaxFileSize is 0; no file could be loaded.");

    if (MaxNestingDepth == 0)
        return Invalid("Reading.MaxNestingDepth is 0; no table, array or inline table could be read.");

    if (MaxNestingDepth > MaxNestingDepthLimit)
    {
        return Invalid("Reading.MaxNestingDepth is " + std::to_string(MaxNestingDepth) + "; it can be at most " +
            std::to_string(MaxNestingDepthLimit) + ", so a deep value can't overflow the stack.");
    }

    return TASWTomlResult();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlWritingOptions
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlWritingOptions::Validate() const
{
    // CommentPrefix: '#' and blanks, so a comment line written with it reads back as the text after it
    if (CommentPrefix.empty() || CommentPrefix[0] != '#')
    {
        return Invalid("Writing.CommentPrefix " + Detail::MessageText(CommentPrefix) +
            " must start with '#', the only comment character in TOML.");
    }

    for (std::size_t i = 1; i < CommentPrefix.size(); ++i)
    {
        if (!Detail::IsBlank(CommentPrefix[i]))
        {
            return Invalid("Writing.CommentPrefix " + Detail::MessageText(CommentPrefix) + " may hold only spaces "
                "and tabs after its '#', so a comment reads back as the text written after it.");
        }
    }

    // The enumerations, in case one was cast from a number
    if (std::string_view(ToString(LineEnding)) == "Unknown")
        return Invalid("Writing.LineEnding isn't one of the ELineEnding values.");

    if (std::string_view(ToString(Bom)) == "Unknown")
        return Invalid("Writing.Bom isn't one of the EBomMode values.");

    return TASWTomlResult();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlSavingOptions
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlSavingOptions::Validate() const
{
    if (Backup)
    {
        if (BackupSuffix.empty())
            return Invalid("Saving.BackupSuffix is empty, so the backup would replace the file itself.");

        if (HasLineBreakOrNul(BackupSuffix) || BackupSuffix.find_first_of("/\\:") != std::string::npos)
        {
            return Invalid("Saving.BackupSuffix \"" + BackupSuffix +
                "\" can't hold '/', '\\', ':', line breaks or NUL.");
        }
    }

    return TASWTomlResult();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlValueOptions
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlValueOptions::Validate() const
{
    // The enumerations, in case one was cast from a number
    if (std::string_view(ToString(StringStyle)) == "Unknown")
        return Invalid("Values.StringStyle isn't one of the EStringStyle values.");

    if (std::string_view(ToString(IntegerBase)) == "Unknown")
        return Invalid("Values.IntegerBase isn't one of the EIntegerBase values.");

    return TASWTomlResult();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TASWTomlOptions
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTomlResult TASWTomlOptions::Validate() const
{
    TASWTomlResult result = Reading.Validate();
    if (!result.IsOk())
        return result;

    result = Writing.Validate();
    if (!result.IsOk())
        return result;

    result = Saving.Validate();
    if (!result.IsOk())
        return result;

    return Values.Validate();
}
//---------------------------------------------------------------------------

} // namespace ASWToml
