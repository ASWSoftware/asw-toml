/* **************************************************************************
ASWToml_Parser.h
Author: Anthony S. West - ASW Software

A TOML file reader/writer library.

Internal: reading a TOML text into the document's model (see
ASWToml_Document.h), by recursive descent, with the scalar readers of
ASWToml_Scalars.h for the values.

Requires C++ 20 or higher.

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

#pragma once

#ifndef ASWToml_ParserH
#define ASWToml_ParserH
//---------------------------------------------------------------------------
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWToml_Document.h"
#include "ASWToml_Options.h"
#include "ASWToml_Types.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

//---------------------------------------------------------------------------
// Internals of the library. Not part of the public interface.
//---------------------------------------------------------------------------
namespace Detail
{

// Reads 'text', a whole TOML document, into 'model', replacing what it held, as TASWTomlDocument::Parse() describes,
// with the limits of 'options', which must be valid. Checks, in this order: a UTF-16 or UTF-32 byte order mark
// (UnsupportedEncoding), the whole text after a byte order mark for UTF-8 (InvalidUtf8 at the first byte that isn't
// valid), then the syntax, failing on the first error (ParseFailed, ValueOutOfRange or NestingTooDeep). An error in
// the text has the Line and Column of its offset in the text after the byte order mark. On failure, 'model' is left
// as it was. Throws only std::bad_alloc, and then 'model' is left as it was too.
TASWTomlResult ParseDocument(std::string_view text, TASWTomlReadingOptions const& options, TDocModel& model);

} // namespace Detail

} // namespace ASWToml

//---------------------------------------------------------------------------
#endif // #ifndef ASWToml_ParserH
