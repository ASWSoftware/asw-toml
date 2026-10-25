/* **************************************************************************
UT_Helper_Threads.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "UT_Helper_Threads.h"
//---------------------------------------------------------------------------
#include <exception>
#include <thread>
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

// The Id of the next TLifeId
std::atomic<std::uint32_t> g_NextLifeId{ 1 };

void RunWhenStarted(std::atomic<bool> const& start, std::function<void()> const& function,
    TThreadFailures& failures);

//---------------------------------------------------------------------------
/*
    RunWhenStarted

    A thread of RunTogether(): waits until 'start' is set, then calls 'function', adding an exception it throws to
    'failures'.
*/
void RunWhenStarted(std::atomic<bool> const& start, std::function<void()> const& function,
    TThreadFailures& failures)
{
    while (!start)
        std::this_thread::yield();

    try
    {
        function();
    }
    catch (std::exception const& e)
    {
        failures.Add(std::string("a thread threw: ") + e.what());
    }
    catch (...)
    {
        failures.Add("a thread threw an exception that isn't a std::exception");
    }
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void MaybeYield(std::mt19937& generator)
{
    if (generator() % 4 == 0)
        std::this_thread::yield();
}
//---------------------------------------------------------------------------
void RunTogether(TWorkVector const& work, TThreadFailures& failures)
{
    std::atomic<bool> start{ false };
    std::vector<std::thread> threads;
    try
    {
        for (std::function<void()> const& function : work)
            threads.emplace_back(RunWhenStarted, std::cref(start), std::cref(function), std::ref(failures));
    }
    catch (std::exception const& e)
    {
        failures.Add(std::string("a thread couldn't be started: ") + e.what());
    }

    start = true;
    for (std::thread& thread : threads)
        thread.join();
}
//---------------------------------------------------------------------------
std::string WriterPath(std::size_t index)
{
    return "writer" + std::to_string(index) + ".count";
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TLifeId
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TLifeId::TLifeId() noexcept
    : Id(g_NextLifeId++)
{
}
//---------------------------------------------------------------------------
TLifeId::TLifeId(TLifeId const& /*other*/) noexcept
    : Id(g_NextLifeId++)
{
}
//---------------------------------------------------------------------------
TLifeId::~TLifeId()
{
    Id = 0;
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TCountingCallback
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TCountingCallback::TCountingCallback(std::atomic<int>& count)
    : m_Count(&count)
{
}
//---------------------------------------------------------------------------
void TCountingCallback::operator()(ASWToml::TASWTomlMessage const& /*message*/) const
{
    std::uint32_t const id = m_Life.Id;
    std::this_thread::yield();
    if (id != 0 && m_Life.Id == id)
        ++*m_Count;
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TThreadFailures
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
void TThreadFailures::Add(std::string const& problem)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Count++ == 0)
        m_First = problem;
}
//---------------------------------------------------------------------------
std::string TThreadFailures::Summary() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Count == 0)
        return std::string();

    return std::to_string(m_Count) + " problem(s), with the threads seeded from " + std::to_string(ThreadsSeed) +
        "; the first: " + m_First;
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests
