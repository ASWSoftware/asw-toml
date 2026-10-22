/* **************************************************************************
ASWToml_Types.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "ASWToml_Types.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

//---------------------------------------------------------------------------
char const* ToString(ETomlCode code) noexcept
{
    switch (code)
    {
        case ETomlCode::Ok:
            return "Ok";

        case ETomlCode::InvalidArgument:
            return "InvalidArgument";

        case ETomlCode::InvalidOptions:
            return "InvalidOptions";

        case ETomlCode::NoFilePath:
            return "NoFilePath";

        case ETomlCode::FileNotFound:
            return "FileNotFound";

        case ETomlCode::AccessDenied:
            return "AccessDenied";

        case ETomlCode::FileTooLarge:
            return "FileTooLarge";

        case ETomlCode::OpenFailed:
            return "OpenFailed";

        case ETomlCode::ReadFailed:
            return "ReadFailed";

        case ETomlCode::UnsupportedEncoding:
            return "UnsupportedEncoding";

        case ETomlCode::InvalidUtf8:
            return "InvalidUtf8";

        case ETomlCode::ParseFailed:
            return "ParseFailed";

        case ETomlCode::DuplicateKey:
            return "DuplicateKey";

        case ETomlCode::NestingTooDeep:
            return "NestingTooDeep";

        case ETomlCode::CreateFolderFailed:
            return "CreateFolderFailed";

        case ETomlCode::WriteFailed:
            return "WriteFailed";

        case ETomlCode::ReplaceFailed:
            return "ReplaceFailed";

        case ETomlCode::UnexpectedException:
            return "UnexpectedException";

        case ETomlCode::NotFound:
            return "NotFound";

        case ETomlCode::AlreadyExists:
            return "AlreadyExists";

        case ETomlCode::TypeMismatch:
            return "TypeMismatch";

        case ETomlCode::InvalidValue:
            return "InvalidValue";

        case ETomlCode::ValueOutOfRange:
            return "ValueOutOfRange";

        case ETomlCode::BackupFailed:
            return "BackupFailed";

        case ETomlCode::TempFileCleanupFailed:
            return "TempFileCleanupFailed";

        case ETomlCode::KeyAdded:
            return "KeyAdded";

        case ETomlCode::KeyRenamed:
            return "KeyRenamed";
    }

    return "Unknown";
}
//---------------------------------------------------------------------------
char const* ToString(ELineBreak lineBreak) noexcept
{
    switch (lineBreak)
    {
        case ELineBreak::None:
            return "None";

        case ELineBreak::LF:
            return "LF";

        case ELineBreak::CRLF:
            return "CRLF";
    }

    return "Unknown";
}
//---------------------------------------------------------------------------
char const* ToString(ESeverity severity) noexcept
{
    switch (severity)
    {
        case ESeverity::Info:
            return "Info";

        case ESeverity::Warning:
            return "Warning";

        case ESeverity::Error:
            return "Error";
    }

    return "Unknown";
}
//---------------------------------------------------------------------------
std::string ToString(TASWTomlMessage const& message)
{
    std::string text = ToString(message.Severity);
    text += " ";
    text += ToString(message.Code);
    text += ": ";
    text += message.Text;

    // The parts that are set, e.g. (file "app.toml", line 4, column 1)
    std::string details;
    auto const addDetail = [&details](std::string const& detail)
        {
            details += details.empty() ? " (" : ", ";
            details += detail;
        };

    if (!message.FilePath.empty())
        addDetail("file \"" + message.FilePath + "\"");

    if (message.Line > 0)
        addDetail("line " + std::to_string(message.Line));

    if (message.Column > 0)
        addDetail("column " + std::to_string(message.Column));

    if (!message.Path.empty())
        addDetail("path " + message.Path);

    if (message.SystemError != 0)
        addDetail("system error " + std::to_string(message.SystemError));

    if (!details.empty())
        text += details + ")";

    return text;
}
//---------------------------------------------------------------------------

} // namespace ASWToml
