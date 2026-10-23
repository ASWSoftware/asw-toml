/* **************************************************************************
ASWToml_Scalars.h
Author: Anthony S. West - ASW Software

A TOML file reader/writer library.

Internal: reading and writing the TOML text of scalar values (strings, keys,
integers, floats, booleans, and dates and times), which the parser and the
edits use. A reader starts at a position in a larger text and stops at the
end of its token, so an error gives the exact place; a writer appends to a
std::string.

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
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWToml_Options.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------

namespace ASWToml
{

//---------------------------------------------------------------------------
// Internals of the library. Not part of the public interface.
//---------------------------------------------------------------------------
namespace Detail
{

// Reads the date or time that starts at 'position' in 'text': an offset date-time (1979-05-27T07:32:00Z or
// 1979-05-27 07:32:00.5-07:00), a local date-time (1979-05-27T07:32:00), a local date (1979-05-27) or a local time
// (07:32:00), with the date and time separated by T, t or a space, the offset Z, z or +HH:MM or -HH:MM, the seconds
// optional (:00 then), and a fraction of a second after them kept to nanoseconds (more digits are cut off). Second 60
// (a leap second) reads at any time. A space after a date starts a time only when digits and ':' follow it, so in
// "1979-05-27 # comment" the date ends before the space. What follows the value is the caller's to check, unless it
// is a character a date or time can hold (A-Z, a-z, 0-9, '_', '.', '+', '-' or ':'), which fails.
// On success, sets 'value' to a TASWTomlOffsetDateTime, TASWTomlLocalDateTime, std::chrono::year_month_day or
// TASWTomlLocalTime, and 'position' to the offset after it. On failure, leaves 'value' as it was, sets 'position' to
// the offset of the error (the start of a field out of range, e.g. the day of 2023-02-29, or the first character
// that doesn't fit the grammar), and returns ParseFailed with the Line and Column of that offset in 'text'. A year is
// 0000 to 9999 (four digits), a day must be in its month, an hour is 00 to 23, a minute 00 to 59, a second 00 to 60,
// and an offset -23:59 to +23:59. Throws only std::bad_alloc.
TASWTomlResult ReadDateTime(std::string_view text, std::size_t& position, TASWTomlValue& value);

// Reads the number that starts at 'position' in 'text': the longest run of the characters a number can have (A-Z,
// a-z, 0-9, '_', '.', '+' and '-'), which must be a TOML integer (decimal, or hexadecimal, octal or binary after 0x,
// 0o or 0b, with no sign) or float (with a fraction, an exponent or both, or inf or nan, each with an optional sign),
// with '_' only between digits. What follows the run is the caller's to check.
// On success, sets 'value' to the integer (std::int64_t) or float (double) and 'position' to the offset after the
// run. A float smaller than the smallest double reads as 0.0 (or -0.0), as it rounds to it; -nan reads as a NaN with
// its sign bit set. On failure, leaves 'value' as it was and returns, with the Line and Column of 'position' in 'text':
// - ParseFailed, with 'position' set to the offset of the first character that doesn't fit the grammar, e.g. the
//   second 0 of 00, or the end of 1.
// - ValueOutOfRange, with 'position' unchanged, for an integer outside the 64-bit range or a float outside a double's
//   (about plus or minus 1.8e308).
// Throws only std::bad_alloc.
TASWTomlResult ReadNumber(std::string_view text, std::size_t& position, TASWTomlValue& value);

// Reads the scalar value that starts at 'position' in 'text', its kind chosen by its first characters: a string after
// " or ' (ReadString()), a date or time when digits and then '-' or ':' come first (ReadDateTime()), a boolean for
// true or false, and else a number (ReadNumber()), inf and nan included. Other words, such as True or an unquoted
// string, fail with ParseFailed at 'position', and so does text that can't start a scalar (an array or an inline
// table is the caller's to read). On success, sets 'value' to the value and 'position' to the offset after it; on
// failure, leaves 'value' as it was and returns what the reader of that kind returns. Throws only std::bad_alloc.
TASWTomlResult ReadScalar(std::string_view text, std::size_t& position, TASWTomlValue& value);

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

// Appends 'value' to 'text' as a TOML boolean: true or false. Throws only std::bad_alloc.
void WriteBoolean(bool value, std::string& text);

// Appends 'value' to 'text' as a TOML float: the shortest text that reads back as the same double (std::to_chars),
// with ".0" added when it has neither a '.' nor an exponent, e.g. 1.0, 0.1, -0.0, 1e+16 or 5e-324; inf, -inf, nan,
// or -nan for a NaN with its sign bit set. Throws only std::bad_alloc.
void WriteFloat(double value, std::string& text);

// Appends 'value' to 'text' as a TOML integer in the base 'base' asks for: 0x with upper-case digits, 0o or 0b, e.g.
// 0xFF; decimal for Auto, Decimal, a value that isn't an EIntegerBase, or a negative number (TOML's other bases have
// no sign). Throws only std::bad_alloc.
void WriteInteger(std::int64_t value, std::string& text, EIntegerBase base);

// Appends 'key' to 'text' as one part of a key: a bare key when it can be (not empty, and only A-Z, a-z, 0-9, '-' and
// '_'), else a basic string as WriteString() writes it, e.g. port, "host name", "a.b" or "". Fails with InvalidValue,
// appending nothing, when 'key' isn't valid UTF-8. Throws only std::bad_alloc ('text' may then hold part of the key).
TASWTomlResult WriteKey(std::string_view key, std::string& text);

// Appends 'value' to 'text' as a TOML local date, e.g. 1979-05-27. Fails with InvalidValue, appending nothing, when
// it isn't a real date with a year from 0 to 9999. Throws only std::bad_alloc.
TASWTomlResult WriteLocalDate(std::chrono::year_month_day const& value, std::string& text);

// Appends 'value' to 'text' as a TOML local date-time, e.g. 1979-05-27T07:32:00 (see WriteLocalTime() for the time).
// Fails with InvalidValue, appending nothing, when the date or the time isn't valid (see
// TASWTomlLocalDateTime::IsValid()). Throws only std::bad_alloc.
TASWTomlResult WriteLocalDateTime(TASWTomlLocalDateTime const& value, std::string& text);

// Appends 'value' to 'text' as a TOML local time: HH:MM:SS, with the seconds always, and the fraction of a second
// only when it isn't 0, with as few digits as it needs, e.g. 07:32:00, 07:32:00.5 or 07:32:00.000001. Fails with
// InvalidValue, appending nothing, when a field is outside its range (see TASWTomlLocalTime::IsValid()). Throws only
// std::bad_alloc.
TASWTomlResult WriteLocalTime(TASWTomlLocalTime const& value, std::string& text);

// Appends 'value' to 'text' as a TOML offset date-time: the date and time as WriteLocalDateTime() writes them, then Z
// for offset 0, else +HH:MM or -HH:MM, e.g. 1979-05-27T07:32:00Z or 1979-05-27T00:32:00-07:00. Fails with
// InvalidValue, appending nothing, when the date, the time or the offset isn't valid (see
// TASWTomlOffsetDateTime::IsValid()). Throws only std::bad_alloc.
TASWTomlResult WriteOffsetDateTime(TASWTomlOffsetDateTime const& value, std::string& text);

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
