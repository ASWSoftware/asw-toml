/* **************************************************************************
ASWToml_Scalars.h
Author: Anthony S. West - ASW Software

A TOML file reader/writer library.

Internal: reading and writing the TOML text of scalar values (strings and
keys so far), which the parser and the edits use. A reader starts at a
position in a larger text and stops at the end of its token, so an error
gives the exact place; a writer appends to a std::string.

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

#ifndef ASWToml_ScalarsH
#define ASWToml_ScalarsH
//---------------------------------------------------------------------------
#include <cstddef>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
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

// Reads the string whose opening quote is at 'position' in 'text': a basic string ("..."), a multi-line basic string
// ("""..."""), a literal string ('...') or a multi-line literal string ('''...'''), with every escape of TOML 1.1
// (\xHH and \e included). In a multi-line string, a line break right after the opening delimiter is left out, and
// every other one reads as "\n", whether the text has LF or CRLF.
// On success, sets 'value' to the string (UTF-8) and 'position' to the offset after the closing delimiter. On failure,
// leaves 'value' as it was, sets 'position' to the offset of the error, and returns ParseFailed (InvalidUtf8 for bytes
// that aren't valid UTF-8) with the Line and Column of that offset in 'text': no quote at 'position', an escape TOML
// doesn't have or that isn't a Unicode scalar value, a control character the string can't hold, a carriage return
// without a line feed, or a string without its closing delimiter (reported at the opening one). Throws only
// std::bad_alloc.
TASWTomlResult ReadString(std::string_view text, std::size_t& position, std::string& value);

// Appends 'key' to 'text' as one part of a key: a bare key when it can be (not empty, and only A-Z, a-z, 0-9, '-' and
// '_'), else a basic string as WriteString() writes it, e.g. port, "host name", "a.b" or "". Fails with InvalidValue,
// appending nothing, when 'key' isn't valid UTF-8. Throws only std::bad_alloc ('text' may then hold part of the key).
TASWTomlResult WriteKey(std::string_view key, std::string& text);

// Appends 'value' to 'text' as a string in the form 'style' asks for (see EStringStyle): a literal string ('...')
// when the style wants one and one can hold the text (no ' and no control character but a tab), else a basic string
// ("...") with \", \\, \b, \t, \n, \f and \r escaped, the other control characters as \u00XX (e.g. \u001B), and the
// rest as it is. Only escapes that TOML 1.0 reads are written. Fails with InvalidValue, appending nothing, when 'value'
// isn't valid UTF-8. Throws only std::bad_alloc ('text' may then hold part of the string).
TASWTomlResult WriteString(std::string_view value, std::string& text, EStringStyle style);

} // namespace Detail

} // namespace ASWToml

//---------------------------------------------------------------------------
#endif // #ifndef ASWToml_ScalarsH
