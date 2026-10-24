r"""Generates TomlTest_Corpus.cpp and TomlTest_Corpus.h from a copy of toml-test.

Usage: python generate_corpus.py <toml-test folder>

<toml-test folder> is the extracted source of a toml-test release (https://github.com/toml-lang/toml-test), the folder
that holds tests/files-toml-1.1.0. The files that list names (TOML 1.1) are written as C++ string literals, so the
unit tests need no file paths at run time, and each valid file's expected values (its .json file) are flattened into
lines the tests compare with what the document reads. The output goes next to this script. Python 3, standard library
only.

The expected values are one line per value, path TAB type TAB value, each ending with a line feed:
- The path is empty for the root table, then for each key ."key" and for each element of an array [index], e.g.
  ."fruits"[0]."name". A key is escaped as text (below).
- A table is "table" and its number of keys, an array "array" and its number of elements, so an empty table or array
  and a missing key are all checked.
- A string is "string" and its text, escaped: a backslash, a double quote, a tab, a line feed and a carriage return as
  \\, \", \t, \n and \r, every other control character (U+0000 to U+001F and U+007F) as \xHH (upper-case hex digits);
  every other byte of its UTF-8 as it is.
- An integer is "integer" and its decimal value; a float "float" and Python's repr() of it (e.g. 1000000.0, -0.0, inf,
  nan), which the tests compare as numbers; a boolean "bool" and true or false.
- A date or time is "datetime" (an offset date-time), "datetime-local", "date-local" or "time-local", and toml-test's
  text of it (RFC 3339), which the tests read with the library's own reader and compare as values.
"""

import json
import os
import sys

# Only toml-test's types; the tests rely on these names
SCALAR_TYPES = ('string', 'integer', 'float', 'bool', 'datetime', 'datetime-local', 'date-local', 'time-local')

# The longest literal written: ISO C++ only requires compilers to support 4095 characters (GCC's -Wpedantic warns
# beyond), and MSVC takes at most 16380 bytes per piece and 65535 in all
MAX_LITERAL_SIZE = 4095

# How many characters of a literal one line of the generated source holds at most, escapes included, so a line stays
# within 120 columns with its indentation, quotes, comma and suffix
MAX_PIECE_SIZE = 100


def escape_value(data):
    """'data' (bytes) escaped as the expected values' text (see the module's comment), as bytes."""
    out = bytearray()
    for byte in data:
        if byte == 0x5C:
            out += b'\\\\'
        elif byte == 0x22:
            out += b'\\"'
        elif byte == 0x09:
            out += b'\\t'
        elif byte == 0x0A:
            out += b'\\n'
        elif byte == 0x0D:
            out += b'\\r'
        elif byte < 0x20 or byte == 0x7F:
            out += b'\\x%02X' % byte
        else:
            out.append(byte)
    return bytes(out)


def flatten(path, value, lines):
    """Adds the lines of 'value' (toml-test's JSON form) at 'path' (bytes) to 'lines'."""
    if isinstance(value, dict) and set(value) == {'type', 'value'} and isinstance(value['value'], str):
        kind = value['type']
        text = value['value']
        if kind not in SCALAR_TYPES:
            raise ValueError('unknown type %r at %r' % (kind, path))
        if kind == 'string':
            text = escape_value(text.encode('utf-8')).decode('utf-8')
        elif kind == 'integer':
            text = str(int(text))
        elif kind == 'float':
            text = repr(float(text))
        elif kind == 'bool' and text not in ('true', 'false'):
            raise ValueError('a boolean %r at %r' % (text, path))
        lines.append(path + b'\t' + kind.encode('ascii') + b'\t' + text.encode('utf-8'))
    elif isinstance(value, dict):
        lines.append(path + b'\ttable\t' + str(len(value)).encode('ascii'))
        for key, item in value.items():
            flatten(path + b'."' + escape_value(key.encode('utf-8')) + b'"', item, lines)
    elif isinstance(value, list):
        lines.append(path + b'\tarray\t' + str(len(value)).encode('ascii'))
        for index, item in enumerate(value):
            flatten(path + b'[' + str(index).encode('ascii') + b']', item, lines)
    else:
        raise ValueError('unexpected JSON %r at %r' % (value, path))


def literal_char(byte):
    """'byte' as it is written in an ASCII C++ string literal: printable ASCII as it is, except a backslash, a double
    quote and a question mark (so no trigraph can form), which are escaped; a tab, line feed and carriage return as
    \\t, \\n and \\r; every other byte as a three-digit octal escape, which never takes a following digit with it (but
    MSVC warns C4125 about a digit after one, so literal() splits the literal there)."""
    if byte == 0x5C:
        return '\\\\'
    if byte == 0x22:
        return '\\"'
    if byte == 0x3F:
        return '\\?'
    if byte == 0x09:
        return '\\t'
    if byte == 0x0A:
        return '\\n'
    if byte == 0x0D:
        return '\\r'
    if 0x20 <= byte < 0x7F:
        return chr(byte)
    return '\\%03o' % byte


def literal(data, indent):
    """'data' (bytes) as a std::string_view literal: one quoted piece per line of the data (a long line split), each
    on its own source line at 'indent', the last with the sv suffix. A digit after an octal escape on the same line
    starts a new literal there (" "), as MSVC warns C4125 "decimal digit terminates octal escape sequence" otherwise."""
    if len(data) > MAX_LITERAL_SIZE:
        raise ValueError('a literal of %d bytes, more than %d' % (len(data), MAX_LITERAL_SIZE))
    pieces = [[]]
    for byte in data:
        char = literal_char(byte)
        if 0x30 <= byte <= 0x39 and pieces[-1] and len(pieces[-1][-1]) == 4 and pieces[-1][-1][0] == '\\':
            char = '" "' + char
        if sum(len(c) for c in pieces[-1]) + len(char) > MAX_PIECE_SIZE:
            char = literal_char(byte)  # A new line starts a new literal anyway
            pieces.append([])
        pieces[-1].append(char)
        if byte == 0x0A:
            pieces.append([])
    if len(pieces) > 1 and not pieces[-1]:
        pieces.pop()
    lines = [indent + '"' + ''.join(piece) + '"' for piece in pieces]
    lines[-1] += 'sv'
    return lines


def file_entry(name, text, expected):
    """The lines of one TTomlTestFile initializer."""
    lines = ['    {']
    lines += [line + ',' if i == 0 else line for i, line in enumerate(literal(name.encode('ascii'), '        '))]
    text_lines = literal(text, '        ')
    text_lines[-1] += ','
    lines += text_lines
    expected_lines = literal(expected, '        ')
    expected_lines[-1] += ','
    lines += expected_lines
    lines.append('    },')
    return lines


HEADER = '''/* **************************************************************************
TomlTest_Corpus.h
Generated by generate_corpus.py from toml-test {version}; don't edit it, run
the script again (see README.md in this folder).

The files of toml-test's TOML 1.1 list, for the unit tests: each file's
text, and for a valid file its expected values. The files are from toml-test
(https://github.com/toml-lang/toml-test), under the MIT license: see
LICENSE-toml-test in this folder.
************************************************************************** */

#pragma once

#ifndef TomlTest_CorpusH
#define TomlTest_CorpusH
//---------------------------------------------------------------------------
#include <span>
#include <string_view>
//---------------------------------------------------------------------------

namespace ASWUnitTests
{{

/////////////////////////////////////////////////////////////////////////////
// TTomlTestFile
//
// A file of toml-test's TOML 1.1 list
/////////////////////////////////////////////////////////////////////////////
struct TTomlTestFile
{{
    std::string_view Name; // Its path in toml-test's tests folder, e.g. valid/array/array.toml
    std::string_view Text; // Its bytes

    // A valid file's values, one line each: path TAB type TAB value (see generate_corpus.py); empty for an invalid file
    std::string_view Expected;
}};

//---------------------------------------------------------------------------

// The invalid files ({invalid_count}), which a parser must refuse
std::span<TTomlTestFile const> TomlTestInvalidFiles() noexcept;

// The valid files ({valid_count}), with their values
std::span<TTomlTestFile const> TomlTestValidFiles() noexcept;

// The version of toml-test the files are from, e.g. 2.2.0
std::string_view TomlTestVersion() noexcept;

}} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef TomlTest_CorpusH
'''

SOURCE_TOP = '''/* **************************************************************************
TomlTest_Corpus.cpp
Generated by generate_corpus.py from toml-test {version}; don't edit it, run
the script again (see README.md in this folder).

The files are from toml-test (https://github.com/toml-lang/toml-test), under
the MIT license: see LICENSE-toml-test in this folder.
************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "TomlTest_Corpus.h"
//---------------------------------------------------------------------------
#include <span>
#include <string_view>
//---------------------------------------------------------------------------

namespace ASWUnitTests
{{

namespace
{{

using namespace std::string_view_literals;
'''

SOURCE_BOTTOM = '''
}} // namespace

//---------------------------------------------------------------------------
std::span<TTomlTestFile const> TomlTestInvalidFiles() noexcept
{{
    return InvalidFiles;
}}
//---------------------------------------------------------------------------
std::span<TTomlTestFile const> TomlTestValidFiles() noexcept
{{
    return ValidFiles;
}}
//---------------------------------------------------------------------------
std::string_view TomlTestVersion() noexcept
{{
    return "{version}"sv;
}}
//---------------------------------------------------------------------------

}} // namespace ASWUnitTests
'''


def read_version(folder):
    """toml-test's version, from the first line of its CHANGELOG.md (e.g. v2.2.0 2026-03-30)."""
    first = open(os.path.join(folder, 'CHANGELOG.md'), encoding='utf-8').readline().split()
    if not first or not first[0].startswith('v'):
        raise ValueError('no version on the first line of CHANGELOG.md')
    return first[0][1:]


def main():
    if len(sys.argv) != 2:
        print(__doc__.split('\n\n')[1])
        return 1

    folder = sys.argv[1]
    tests = os.path.join(folder, 'tests')
    version = read_version(folder)
    names = [line.strip() for line in open(os.path.join(tests, 'files-toml-1.1.0'), encoding='utf-8')]
    names = sorted(name for name in names if name.endswith('.toml'))

    valid, invalid = [], []
    for name in names:
        text = open(os.path.join(tests, name), 'rb').read()
        if name.startswith('valid/'):
            data = json.load(open(os.path.join(tests, name[:-5] + '.json'), encoding='utf-8'))
            lines = []
            flatten(b'', data, lines)
            valid.append(file_entry(name, text, b''.join(line + b'\n' for line in lines)))
        elif name.startswith('invalid/'):
            invalid.append(file_entry(name, text, b''))
        else:
            raise ValueError('neither valid nor invalid: ' + name)

    out = os.path.dirname(os.path.abspath(__file__))
    source = [SOURCE_TOP.format(version=version).rstrip('\n'), '',
              '// The invalid files, in the order of their names', 'TTomlTestFile const InvalidFiles[] = {']
    for entry in invalid:
        source += entry
    source += ['};', '', '// The valid files and their values, in the order of their names',
               'TTomlTestFile const ValidFiles[] = {']
    for entry in valid:
        source += entry
    source += ['};']
    source.append(SOURCE_BOTTOM.format(version=version).rstrip('\n'))

    # Text mode: the platform's line breaks, as git checks text files out
    with open(os.path.join(out, 'TomlTest_Corpus.cpp'), 'w', encoding='ascii') as file:
        file.write('\n'.join(source) + '\n')
    with open(os.path.join(out, 'TomlTest_Corpus.h'), 'w', encoding='ascii') as file:
        file.write(HEADER.format(version=version, valid_count=len(valid), invalid_count=len(invalid)))

    print('toml-test %s: %d valid and %d invalid files' % (version, len(valid), len(invalid)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
