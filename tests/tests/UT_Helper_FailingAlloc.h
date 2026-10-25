/* **************************************************************************
UT_Helper_FailingAlloc.h
Author: Anthony S. West - ASW Software

Test helpers for running out of memory: TFailingAlloc, which makes an
allocation fail with std::bad_alloc through a replaced global operator new,
and CanFailAllocations(), whether a build can.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#ifndef UT_Helper_FailingAllocH
#define UT_Helper_FailingAllocH
//---------------------------------------------------------------------------
#include <cstddef>
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

/////////////////////////////////////////////////////////////////////////////
// TFailingAlloc
//
// Makes one allocation of the current thread fail with std::bad_alloc, to test what code does when memory runs out.
// The test executable replaces the global operator new and delete (UT_Helper_FailingAlloc.cpp) with ones that use
// malloc() and free(), and that fail only while a TFailingAlloc is armed. Not in a sanitizer build, whose runtime has
// its own operator new (see CanFailAllocations()). Only allocations made through the executable's operator new count:
// code in a standard library DLL calls its own. One object at a time.
/////////////////////////////////////////////////////////////////////////////
class TFailingAlloc
{
public:
    // Makes allocation number 'number' (1 for the next one) of the current thread fail
    explicit TFailingAlloc(std::size_t number) noexcept;
    TFailingAlloc(TFailingAlloc const&) = delete;
    ~TFailingAlloc();

    TFailingAlloc& operator=(TFailingAlloc const&) = delete;

    // Whether the allocation failed (false when there were fewer allocations)
    bool HasFailed() const noexcept;
};

//---------------------------------------------------------------------------

// Whether a TFailingAlloc can make allocations fail: false in a sanitizer build, which keeps the runtime's operator
// new, and with MSVC's library with iterator debugging on (its Debug default), whose noexcept move constructors
// allocate, so a failed allocation there calls std::terminate()
bool CanFailAllocations() noexcept;

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef UT_Helper_FailingAllocH
