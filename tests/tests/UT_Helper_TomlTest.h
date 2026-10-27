/* **************************************************************************
UT_Helper_TomlTest.h
Author: Anthony S. West - ASW Software

Test helpers of the toml-test corpus (tests/toml-test): the comparison of
values with a valid file's expected values, which the document tests and
the JSON tests share.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#ifndef UT_Helper_TomlTestH
#define UT_Helper_TomlTestH
//---------------------------------------------------------------------------
#include <string>
//---------------------------------------------------------------------------
#include "ASWToml_Value.h"
#include "TomlTest_Corpus.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

//---------------------------------------------------------------------------

// How the values 'root' holds differ from the expected values of the toml-test file 'file', one sentence per path
// that differs; empty when they don't. A table's or array's size is compared, a string as the corpus escapes it, an
// integer or float as a number (NaN with NaN, and a zero's sign too), a boolean by its word, and a date or time read
// by the library's reader and compared as a value.
std::string ValuesDiffer(TTomlTestFile const& file, ASWToml::TASWTomlValue const& root);

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef UT_Helper_TomlTestH
