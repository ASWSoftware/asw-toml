/* **************************************************************************
Test_ASWToml_Path.cpp
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
#include "Test_ASWToml_Path.h"
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <type_traits>
//---------------------------------------------------------------------------
#include "ASWToml_Path.h"
#include "ASWToml_Utils.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

using namespace std::string_literals;

using ASWToml::TASWTomlPath;
using ASWToml::TASWTomlPathPart;
using TPartVector = TASWTomlPath::TPartVector;

// A part is made from a string or from a number of an integer type, but not from bool or a character type
static_assert(std::is_convertible_v<char const*, TASWTomlPathPart>, "a part should be made from char const*");
static_assert(std::is_convertible_v<std::string, TASWTomlPathPart>, "a part should be made from std::string");
static_assert(std::is_convertible_v<std::string_view, TASWTomlPathPart>,
    "a part should be made from std::string_view");
static_assert(std::is_convertible_v<int, TASWTomlPathPart>, "a part should be made from int");
static_assert(std::is_convertible_v<long, TASWTomlPathPart>, "a part should be made from long");
static_assert(std::is_convertible_v<std::size_t, TASWTomlPathPart>, "a part should be made from std::size_t");
static_assert(std::is_convertible_v<std::int64_t, TASWTomlPathPart>, "a part should be made from std::int64_t");
static_assert(!std::is_constructible_v<TASWTomlPathPart, bool>, "a part shouldn't be made from bool");
static_assert(!std::is_constructible_v<TASWTomlPathPart, char>, "a part shouldn't be made from char");
static_assert(!std::is_constructible_v<TASWTomlPathPart, wchar_t>, "a part shouldn't be made from wchar_t");
static_assert(!std::is_constructible_v<TASWTomlPathPart, char8_t>, "a part shouldn't be made from char8_t");
static_assert(!std::is_constructible_v<TASWTomlPathPart, char16_t>, "a part shouldn't be made from char16_t");
static_assert(!std::is_constructible_v<TASWTomlPathPart, char32_t>, "a part shouldn't be made from char32_t");

// A path is made implicitly from a string
static_assert(std::is_convertible_v<char const*, TASWTomlPath>, "a path should be made from char const*");
static_assert(std::is_convertible_v<std::string, TASWTomlPath>, "a path should be made from std::string");
static_assert(std::is_convertible_v<std::string_view, TASWTomlPath>, "a path should be made from std::string_view");
static_assert(!std::is_constructible_v<TASWTomlPath, std::nullptr_t>, "a path shouldn't be made from nullptr");

// The fuzz test: the seed of the first random path, and how many
constexpr std::uint32_t FuzzFirstSeed = 20261023;
constexpr std::size_t FuzzPathCount = 5000;

// The reasons in the errors of paths made from strings
std::string const AfterPart = "After a key or an index, a path must go on with '.' or '[', or end.";
std::string const IndexDigits = "An index must be a decimal number without leading zeros, e.g. [0] or [12].";
std::string const IndexEnd = "An index must end with ']'.";
std::string const NotAKey = "A key must be a bare key (A-Z, a-z, 0-9, '-' and '_') or a quoted string.";

std::string PartsText(TPartVector const& parts);
std::string RandomKey(std::mt19937& generator);
TASWTomlPath RandomPath(std::mt19937& generator);

//---------------------------------------------------------------------------
/*
    PartsText

    'parts' as text for a message, e.g. key "products", [0], key "name"
*/
std::string PartsText(TPartVector const& parts)
{
    std::string text;

    for (TASWTomlPathPart const& part : parts)
    {
        if (!text.empty())
            text += ", ";

        if (!part.IsValid())
            text += "invalid ";

        if (part.IsIndex())
            text += "[" + std::to_string(part.GetIndex()) + "]";
        else
            text += "key \"" + part.GetKey() + "\"";
    }

    return text;
}
//---------------------------------------------------------------------------
/*
    RandomKey

    A random key of up to 4 pieces: what a bare key holds, and what makes a key quoted (blanks, dots, quotes,
    backslashes, brackets, control characters and UTF-8 characters); empty at times
*/
std::string RandomKey(std::mt19937& generator)
{
    std::string const pieces[] = {
        "a", "Z", "0", "9", "-", "_", " ", "\t", ".", "\"", "'", "\\", "[", "]", "#", "=", "\x7F", "\n",
        std::string(1, '\0'), "\xC3\xA9", "\xF0\x9F\x98\x80",
    };

    std::string key;
    std::size_t const count = generator() % 5;

    for (std::size_t i = 0; i < count; ++i)
        key += pieces[generator() % (sizeof(pieces) / sizeof(pieces[0]))];

    return key;
}
//---------------------------------------------------------------------------
/*
    RandomPath

    A random valid path: a key, then up to 5 more parts, each a key or an index (small, or any std::size_t)
*/
TASWTomlPath RandomPath(std::mt19937& generator)
{
    TASWTomlPath path = { RandomKey(generator) };
    std::size_t const count = generator() % 6;

    for (std::size_t i = 0; i < count; ++i)
    {
        if (generator() % 3 != 0)
        {
            path.Append(RandomKey(generator));
            continue;
        }

        std::size_t index = generator() % 12;
        if (generator() % 4 == 0)
        {
            // Two calls, in this order, so a seed gives the same index everywhere
            std::uint64_t const high = generator();
            std::uint64_t const low = generator();
            index = static_cast<std::size_t>((high << 32) | low);
        }

        path.Append(index);
    }

    return path;
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_Path
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_Path::TTest_ASWToml_Path()
    : inherited("ASWToml_Path_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_Path::~TTest_ASWToml_Path()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Path::CheckParts(TASWTomlPath const& path, TPartVector const& expected, std::string const& msg,
    std::source_location loc)
{
    CheckTrue(path.IsValid(), msg + ": the path should be valid: " + path.GetError(), loc);
    CheckEquals(""s, path.GetError(), msg + ": a valid path has no error", loc);
    CheckEquals(PartsText(expected), PartsText(path.GetParts()), msg + ": the parts", loc);
    CheckTrue(path.GetParts() == expected, msg + ": the parts should be equal", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Path::CheckRejects(std::string const& text, std::size_t column, std::string const& reason,
    std::string const& msg, std::source_location loc)
{
    TASWTomlPath const path = text;

    CheckFalse(path.IsValid(), msg + ": the path shouldn't be valid", loc);
    CheckFalse(path.IsRoot(), msg + ": a path that isn't valid isn't the root", loc);
    CheckTrue(path.GetParts().empty(), msg + ": a path that isn't valid has no parts", loc);
    CheckEquals(""s, path.ToString(), msg + ": a path that isn't valid is written as nothing", loc);
    CheckContains(path.GetError(), "The path " + ASWToml::Detail::MessageText(text) + " isn't valid at column " +
        std::to_string(column) + ". " + reason, msg + ": the error", loc);
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Path::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Path::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Path::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_Path::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Path, Append_AddsParts)
{
    // Arrange
    TASWTomlPath path;
    TASWTomlPath matrix = "matrix";
    TASWTomlPath root;
    TASWTomlPath badKey = "a";

    // Act
    TASWTomlPath const& chained = path.Append("products").Append(0).Append("name");
    matrix.Append(1).Append(std::size_t(2));
    root.Append(0);
    badKey.Append("\xFF");

    // Assert
    CheckParts(path, { "products", 0, "name" }, "a key, an index and a key");
    CheckTrue(&chained == &path, "Append() should return the path");
    CheckTrue(path == TASWTomlPath("products[0].name"), "the path should equal the one made from its text");
    CheckParts(matrix, { "matrix", 1, 2 }, "two indexes after a key");

    CheckFalse(root.IsValid(), "an index added to the root table should make the path not valid");
    CheckEquals("A path must start with a key, not an index: the root is a table."s, root.GetError(),
        "the error of an index first");
    CheckFalse(badKey.IsValid(), "a key that isn't valid UTF-8 should make the path not valid");
    CheckEquals("Part 2 of the path isn't valid: a key must be valid UTF-8."s, badKey.GetError(),
        "the error names the part");
    CheckTrue(badKey.GetParts().empty(), "a path that isn't valid has no parts");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Path, Append_KeepsFirstError)
{
    // Arrange
    TASWTomlPath fromText = "a..b";
    TASWTomlPath fromList = { "a", -1 };
    std::string const textError = fromText.GetError();
    std::string const listError = fromList.GetError();

    // Act
    fromText.Append("c");
    fromList.Append(0).Append("\xFF");

    // Assert
    CheckFalse(fromText.IsValid(), "a path that isn't valid should stay so");
    CheckEquals(textError, fromText.GetError(), "the error of the text should stay");
    CheckTrue(fromText.GetParts().empty(), "no part should be added");
    CheckFalse(fromList.IsValid(), "a path that isn't valid should stay so after more parts");
    CheckEquals(listError, fromList.GetError(), "the first error should stay");
    CheckTrue(fromList.GetParts().empty(), "no part should be added after the error");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Path, Default_IsRoot)
{
    // Act
    TASWTomlPath const path;

    // Assert
    CheckTrue(path.IsValid(), "a default path should be valid");
    CheckTrue(path.IsRoot(), "a default path should be the root table");
    CheckTrue(path.GetParts().empty(), "the root table has no parts");
    CheckEquals(""s, path.GetError(), "a valid path has no error");
    CheckEquals(""s, path.ToString(), "the root table is written as the empty string");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Path, Equality_ComparesParts)
{
    // Arrange & Act & Assert
    CheckTrue(TASWTomlPath("a.b") == TASWTomlPath({ "a", "b" }), "a text and a list with the same parts");
    CheckTrue(TASWTomlPath("'a'") == TASWTomlPath("a"), "a literal string and a bare key with the same name");
    CheckTrue(TASWTomlPath("\"a b\"") == TASWTomlPath("'a b'"), "a basic and a literal string with the same name");
    CheckTrue(TASWTomlPath(" a . b ") == TASWTomlPath("a.b"), "blanks don't matter");
    CheckTrue(TASWTomlPath("") == TASWTomlPath(), "the empty string and the default are the root table");
    CheckFalse(TASWTomlPath("a.b") == TASWTomlPath({ "a.b" }), "two keys and one key with a dot");
    CheckFalse(TASWTomlPath("a[0]") == TASWTomlPath("a.0"), "an index and a key that is a number");
    CheckFalse(TASWTomlPath("a.b") == TASWTomlPath("a"), "a longer path");
    CheckFalse(TASWTomlPath("A") == TASWTomlPath("a"), "keys are compared case-sensitively");

    // A path that isn't valid
    CheckFalse(TASWTomlPath("a..b") == TASWTomlPath(), "a path that isn't valid and the root table");
    CheckTrue(TASWTomlPath("a..b") == TASWTomlPath("a..b"), "two paths that aren't valid for the same reason");
    CheckFalse(TASWTomlPath("a..b") == TASWTomlPath(".a"), "two paths that aren't valid for other reasons");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Path, FromList_MakesParts)
{
    // Arrange
    std::string const key = "host name";
    std::string_view const view = "port";
    long const index = 3;
    unsigned short const small = 4;

    // Act
    TASWTomlPath const products = { "products", 0, "name" };
    TASWTomlPath const dotted = { "a.b" };
    TASWTomlPath const parenthesized({ "a.b" });
    TASWTomlPath const parenthesizedString({ key });
    TASWTomlPath const empty = { "" };
    TASWTomlPath const root = {};
    TASWTomlPath const types = { key, view, index, small, std::size_t(5), 6LL, static_cast<char const*>(nullptr) };

    // Assert
    CheckParts(products, { "products", 0, "name" }, "a literal 0 is an index");
    CheckTrue(products.GetParts()[1].IsIndex(), "the 0 should be an index");
    CheckEquals("products[0].name"s, products.ToString(), "the text of the list");
    CheckParts(dotted, { "a.b" }, "a list of one string is one key, dot included");
    CheckEquals("\"a.b\""s, dotted.ToString(), "a key with a dot is quoted");
    // Were the string constructors not a template, Clang would take one of them here (an exact match beats the list's
    // conversion) and GCC the list
    CheckParts(parenthesized, { "a.b" }, "a braced list in parentheses is a list too, on every compiler");
    CheckParts(parenthesizedString, { "host name" }, "a braced list of a std::string in parentheses is a list");
    CheckParts(empty, { "" }, "the empty key");
    CheckTrue(root.IsRoot(), "an empty list is the root table");
    CheckParts(types, { "host name", "port", 3, 4, 5, 6, "" },
        "std::string, std::string_view, the integer types, and a null pointer as the empty key");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Path, FromList_RejectsInvalidParts)
{
    // Arrange & Act
    TASWTomlPath const negative = { "a", -1 };
    TASWTomlPath const notUtf8 = { "a\xFF" };
    TASWTomlPath const indexFirst = { 0, "a" };
    TASWTomlPath const twoErrors = { "a", "b", std::numeric_limits<long long>::min(), "\xFF" };

    // Assert
    CheckFalse(negative.IsValid(), "a negative index");
    CheckEquals("Part 2 of the path isn't valid: an index can't be negative, or larger than a std::size_t can hold."s,
        negative.GetError(), "the error of a negative index");
    CheckFalse(notUtf8.IsValid(), "a key that isn't valid UTF-8");
    CheckEquals("Part 1 of the path isn't valid: a key must be valid UTF-8."s, notUtf8.GetError(),
        "the error of a key that isn't valid UTF-8");
    CheckFalse(indexFirst.IsValid(), "an index first");
    CheckEquals("A path must start with a key, not an index: the root is a table."s, indexFirst.GetError(),
        "the error of an index first");
    CheckContains(twoErrors.GetError(), "Part 3 of the path isn't valid: an index", "the first error is kept");

    for (TASWTomlPath const* path : { &negative, &notUtf8, &indexFirst, &twoErrors })
    {
        CheckTrue(path->GetParts().empty(), "a path that isn't valid has no parts");
        CheckFalse(path->IsRoot(), "a path that isn't valid isn't the root");
        CheckEquals(""s, path->ToString(), "a path that isn't valid is written as nothing");
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Path, FromString_Blanks)
{
    // Arrange & Act & Assert
    CheckParts(" a . b ", { "a", "b" }, "spaces around the dot, at the start and at the end");
    CheckParts("a\t.\tb", { "a", "b" }, "tabs around the dot");
    CheckParts("products [ 0 ] . name", { "products", 0, "name" }, "blanks around brackets and numbers");
    CheckParts("m [1] [2]", { "m", 1, 2 }, "blanks between indexes");
    CheckParts(" \"x y\" ", { "x y" }, "blanks around a quoted key, and inside it");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Path, FromString_EmptyIsRoot)
{
    // Arrange & Act
    TASWTomlPath const fromLiteral = "";
    TASWTomlPath const fromNull = static_cast<char const*>(nullptr);
    TASWTomlPath const fromString = std::string();
    TASWTomlPath const fromView = std::string_view();

    // Assert
    CheckTrue(fromLiteral.IsRoot(), "the empty string should be the root table");
    CheckTrue(fromNull.IsRoot(), "a null pointer should be the root table");
    CheckTrue(fromString.IsRoot(), "an empty std::string should be the root table");
    CheckTrue(fromView.IsRoot(), "an empty std::string_view should be the root table");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Path, FromString_Indexes)
{
    // Arrange
    std::size_t const largest = std::numeric_limits<std::size_t>::max();

    // Act & Assert
    CheckParts("products[0].name", { "products", 0, "name" }, "an index between keys");
    CheckParts("a[0]", { "a", 0 }, "an index at the end");
    CheckParts("matrix[1][2]", { "matrix", 1, 2 }, "two indexes");
    CheckParts("a[10].b[2].c", { "a", 10, "b", 2, "c" }, "indexes after two keys");
    CheckParts("\"x y\"[3]", { "x y", 3 }, "an index after a quoted key");
    CheckParts("a[" + std::to_string(largest) + "]", { "a", largest }, "the largest std::size_t");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Path, FromString_Keys)
{
    // Arrange & Act & Assert
    CheckParts("port", { "port" }, "a bare key");
    CheckParts("server.port", { "server", "port" }, "a dotted key");
    CheckParts("server.\"host name\".port", { "server", "host name", "port" }, "a basic string between bare keys");
    CheckParts("'a.b'.c", { "a.b", "c" }, "a literal string with a dot");
    CheckParts("\"\".x", { "", "x" }, "the empty key");
    CheckParts("\"a\\tb\\u00E9\"", { "a\tb\xC3\xA9" }, "escapes");
    CheckParts("'C:\\x'", { "C:\\x" }, "a backslash in a literal string");
    CheckParts("\"\xC3\xA9\"", { "\xC3\xA9" }, "a character outside ASCII");
    CheckParts("1.2", { "1", "2" }, "digits with a dot are two keys");
    CheckParts("true.false", { "true", "false" }, "words that are values elsewhere");
    CheckParts("a-b_c.-", { "a-b_c", "-" }, "hyphens and underscores");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Path, FromString_RejectsInvalidPaths)
{
    // Arrange & Act & Assert
    // A key missing
    CheckRejects("a..b", 3, NotAKey, "two dots");
    CheckRejects(".a", 1, NotAKey, "a dot first");
    CheckRejects("a.", 3, NotAKey, "a dot last");
    CheckRejects(" ", 2, NotAKey, "only a blank");
    CheckRejects("[0]", 1, NotAKey, "an index first: the root is a table");
    CheckRejects("a.[0]", 3, NotAKey, "an index after a dot");
    CheckRejects("a[1].", 6, NotAKey, "a dot after an index, last");
    CheckRejects("\xC3\xA9", 1, NotAKey, "a character outside ASCII, unquoted");
    CheckRejects("\"\xC3\xA9\"..x", 5, NotAKey, "a column counts characters");

    // Something other than '.', '[' or the end after a part
    CheckRejects("a b", 3, AfterPart, "two keys without a dot");
    CheckRejects("a]", 2, AfterPart, "a closing bracket");
    CheckRejects("a = 1", 3, AfterPart, "an equals sign");
    CheckRejects("a\nb", 2, AfterPart, "a line break");
    CheckRejects("a[0]b", 5, AfterPart, "a key right after an index");
    CheckRejects("'a\\'b'", 5, AfterPart, "a literal string ends at its second quote");

    // An index that isn't valid
    CheckRejects("a[]", 3, IndexDigits, "no number");
    CheckRejects("a[ ]", 4, IndexDigits, "only a blank");
    CheckRejects("a[-1]", 3, IndexDigits, "a negative number");
    CheckRejects("a[+1]", 3, IndexDigits, "a sign");
    CheckRejects("a[01]", 3, IndexDigits, "a leading zero");
    CheckRejects("a[x]", 3, IndexDigits, "a key");
    CheckRejects("a[0x1]", 4, IndexEnd, "a hexadecimal number");
    CheckRejects("a[1", 4, IndexEnd, "no closing bracket");
    CheckRejects("a[1 2]", 5, IndexEnd, "two numbers");
    CheckRejects("a[1_000]", 4, IndexEnd, "an underscore");
    CheckRejects("a[99999999999999999999]", 3, "The index 99999999999999999999 is too large.",
        "beyond std::size_t");

    // A quoted key that isn't valid (ReadKey()'s errors)
    CheckRejects("\"a", 1, "The string has no closing \".", "an unclosed string");
    CheckRejects("a.\"\"\"b\"\"\"", 3, "A key can't be a multi-line string", "a multi-line string");
    CheckRejects("\"a\\q\"", 3, "The escape \\q", "an escape TOML doesn't have");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Path, FromString_RejectsInvalidUtf8)
{
    // Arrange & Act
    TASWTomlPath const bare = "a.\xFF";
    TASWTomlPath const quoted = "\"\xC3\"";
    TASWTomlPath const later = "\xC3\xA9.\xC3";

    // Assert
    // The path isn't quoted, so a message holds only valid UTF-8
    CheckFalse(bare.IsValid(), "a byte that isn't valid UTF-8 outside a string");
    CheckEquals("The path isn't valid at column 3. It holds bytes that aren't valid UTF-8."s, bare.GetError(),
        "the error, at the first byte that isn't valid");
    CheckFalse(quoted.IsValid(), "a truncated sequence in a string");
    CheckEquals("The path isn't valid at column 2. It holds bytes that aren't valid UTF-8."s, quoted.GetError(),
        "the error in a string");
    CheckFalse(later.IsValid(), "a truncated sequence after a valid one");
    CheckEquals("The path isn't valid at column 3. It holds bytes that aren't valid UTF-8."s, later.GetError(),
        "the column counts characters");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Path, Part_KeysAndIndexes)
{
    // Arrange & Act
    TASWTomlPathPart const key = "host name";
    TASWTomlPathPart const index = std::size_t(7);
    TASWTomlPathPart const negative = -1;
    TASWTomlPathPart const lowest = std::numeric_limits<long long>::min();
    TASWTomlPathPart const null = static_cast<char const*>(nullptr);
    TASWTomlPathPart const notUtf8 = "\xFF";

    // Assert
    CheckTrue(key.IsKey() && !key.IsIndex() && key.IsValid(), "a string makes a valid key");
    CheckEquals("host name"s, key.GetKey(), "the key");
    CheckEquals(std::size_t(0), key.GetIndex(), "a key's index is 0");
    CheckTrue(index.IsIndex() && !index.IsKey() && index.IsValid(), "a number makes a valid index");
    CheckEquals(std::size_t(7), index.GetIndex(), "the index");
    CheckEquals(""s, index.GetKey(), "an index's key is empty");
    CheckTrue(negative.IsIndex() && !negative.IsValid(), "a negative number makes an index that isn't valid");
    CheckEquals(std::size_t(0), negative.GetIndex(), "an index that isn't valid is 0");
    CheckTrue(lowest.IsIndex() && !lowest.IsValid(), "the lowest long long makes an index that isn't valid");
    CheckTrue(null.IsKey() && null.IsValid(), "a null pointer makes a valid key");
    CheckEquals(""s, null.GetKey(), "a null pointer is the empty key");
    CheckTrue(notUtf8.IsKey() && !notUtf8.IsValid(), "a key that isn't valid UTF-8 isn't valid");
    CheckTrue(TASWTomlPathPart("a") == TASWTomlPathPart(std::string("a")), "equal keys");
    CheckFalse(TASWTomlPathPart("0") == TASWTomlPathPart(0), "a key and an index");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Path, ToString_ReadsBackRandomPaths)
{
    for (std::size_t i = 0; i < FuzzPathCount; ++i)
    {
        // Arrange
        std::uint32_t const seed = FuzzFirstSeed + static_cast<std::uint32_t>(i);
        std::mt19937 generator(seed);
        TASWTomlPath const path = RandomPath(generator);

        // Act
        std::string const text = path.ToString();
        TASWTomlPath const readBack = text;

        // Assert
        if (!path.IsValid() || !readBack.IsValid() || !(readBack == path))
        {
            CheckTrue(false, "seed " + std::to_string(seed) + ": the path " + PartsText(path.GetParts()) +
                " written as " + ASWToml::Detail::MessageText(text) + " should read back as the same path (" +
                path.GetError() + readBack.GetError() + ")");
        }
    }
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_Path, ToString_WritesTomlSyntax)
{
    // Arrange & Act & Assert
    CheckEquals("server.\"host name\".port"s, TASWTomlPath({ "server", "host name", "port" }).ToString(),
        "a key that can't be bare is a basic string");
    CheckEquals("\"a.b\""s, TASWTomlPath({ "a.b" }).ToString(), "a key with a dot");
    CheckEquals("\"\""s, TASWTomlPath({ "" }).ToString(), "the empty key");
    CheckEquals("\"\".x"s, TASWTomlPath({ "", "x" }).ToString(), "the empty key, then a key");
    CheckEquals("products[1].name"s, TASWTomlPath({ "products", 1, "name" }).ToString(), "an index");
    CheckEquals("m[1][2]"s, TASWTomlPath({ "m", 1, 2 }).ToString(), "two indexes");
    CheckEquals("\"C:\\\\x\""s, TASWTomlPath({ "C:\\x" }).ToString(), "a backslash is escaped, never literal");
    CheckEquals("\"a\\tb\""s, TASWTomlPath({ "a\tb" }).ToString(), "a tab is escaped");
    CheckEquals("\"\xC3\xA9\""s, TASWTomlPath({ "\xC3\xA9" }).ToString(), "a character outside ASCII");
    CheckEquals("lit.x"s, TASWTomlPath("'lit'.x").ToString(), "a quoted key that can be bare is bare");
    CheckEquals("a.b[0]"s, TASWTomlPath(" a . b [ 0 ] ").ToString(), "without the blanks");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWToml_Path)
