/* **************************************************************************
UT_Helper_Threads.h
Author: Anthony S. West - ASW Software

Test helpers for thread tests: RunTogether(), which runs functions on threads
of their own that start together, TThreadFailures, the problems the threads
find, MaybeYield(), which makes the threads interleave in more ways,
TCountingCallback, a log callback that counts messages from any thread and
notices being destroyed while it runs (TLifeId), and WriterPath(), the path
a writer thread counts up.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#ifndef UT_Helper_ThreadsH
#define UT_Helper_ThreadsH
//---------------------------------------------------------------------------
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <random>
#include <string>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWToml_Types.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

// The seed of the random numbers of the first thread of a thread test (each next thread's is one more)
constexpr std::uint32_t ThreadsSeed = 20261025;

// The functions RunTogether() runs, one thread each
using TWorkVector = std::vector<std::function<void ()> >;


/////////////////////////////////////////////////////////////////////////////
// TLifeId
//
// A number of its own (a copy gets a new one) from its construction until its destructor sets it to 0, so code that
// reads it before and after a wait can tell whether the object was destroyed meanwhile, even when another one has
// taken its memory. Its stores are volatile, so the compiler can't leave out the destructor's as a store to an
// object that ends.
/////////////////////////////////////////////////////////////////////////////
struct TLifeId
{
    volatile std::uint32_t Id;

    TLifeId() noexcept;
    TLifeId(TLifeId const& other) noexcept;
    ~TLifeId();
};


/////////////////////////////////////////////////////////////////////////////
// TCountingCallback
//
// A log callback that adds 1 to a count for each message, from any thread. It gives up its time slice and then counts
// the message only if it still exists, so a callback destroyed while it runs (e.g. replaced by SetLogCallback()) is a
// message that isn't counted, and a use after free that AddressSanitizer reports.
/////////////////////////////////////////////////////////////////////////////
class TCountingCallback
{
private:
    std::atomic<int>* m_Count;
    TLifeId m_Life;

public:
    explicit TCountingCallback(std::atomic<int>& count);

    void operator()(ASWToml::TASWTomlMessage const& message) const;
};


/////////////////////////////////////////////////////////////////////////////
// TThreadFailures
//
// The problems the threads of a test find, which any thread may add. The test checks them on its own thread when the
// threads are done, since the checks aren't made to be called from several threads at once.
/////////////////////////////////////////////////////////////////////////////
class TThreadFailures
{
private:
    mutable std::mutex m_Mutex;
    std::size_t m_Count = 0;
    std::string m_First;

public:
    // Adds 'problem'
    void Add(std::string const& problem);

    // "" when no problem was added; else how many there were, the seed of the threads' random numbers, and the first
    std::string Summary() const;
};

//---------------------------------------------------------------------------

// Gives up the rest of the thread's time slice now and then (in 1 of 4 calls, as 'generator' picks them), so the
// threads of a test interleave in more ways
void MaybeYield(std::mt19937& generator);

// Runs each function of 'work' on a thread of its own, all of them starting together, and waits for them to finish.
// An exception a function throws, or a thread that can't be started, is added to 'failures'.
void RunTogether(TWorkVector const& work, TThreadFailures& failures);

// The path the writer thread number 'index' of a thread test counts up: "writer<index>.count"
std::string WriterPath(std::size_t index);

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef UT_Helper_ThreadsH
