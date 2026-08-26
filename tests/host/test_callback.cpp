/**
 ******************************************************************************
 * @file    test_callback.cpp
 * @brief   Behavioural tests for @c opsy::callback .
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#include "host_test.hpp"

#include <callback.hpp>

#include <utility>

namespace
{

int g_alive = 0;
int g_destroyed = 0;

/** @brief Functor that owns something, so failing to destroy it is observable. */
struct tracked
{
	explicit tracked(int value) : tag(value) { ++g_alive; }
	tracked(const tracked& other) : tag(other.tag) { ++g_alive; }
	tracked(tracked&& other) noexcept : tag(other.tag) { ++g_alive; }
	tracked& operator=(const tracked&) = default;
	tracked& operator=(tracked&&) = default;
	~tracked() { --g_alive; ++g_destroyed; }

	void operator()() const {}

	int tag;
};

void reset_counters()
{
	g_alive = 0;
	g_destroyed = 0;
}

// ────────────── regression: assignment over a live functor ─────────────────
// operator=(Function&&) set valid_ and placement-new'd straight over the
// storage without destroying what was already there, unlike the
// operator=(callback&&) next to it. Any functor owning a handle, a buffer or
// a lock leaked.

OPSY_TEST(callback_destroys_the_previous_functor_on_assignment)
{
	reset_counters();
	{
		opsy::callback<void()> subject;
		subject = tracked{1};
		CHECK(g_alive == 1);

		const int destroyed_before = g_destroyed;
		subject = [] {};                 // trivially destructible replacement
		CHECK(g_destroyed > destroyed_before);
		CHECK(g_alive == 0);
	}
	CHECK(g_alive == 0);
}

OPSY_TEST(callback_destroys_the_previous_functor_when_replaced_by_another)
{
	reset_counters();
	{
		opsy::callback<void()> subject;
		subject = tracked{2};
		subject = tracked{3};
		CHECK(g_alive == 1);             // only the last one is still held
	}
	CHECK(g_alive == 0);                 // and the destructor runs at scope exit
}

OPSY_TEST(callback_holding_a_functor_releases_it_at_scope_exit)
{
	reset_counters();
	{
		opsy::callback<void()> subject;
		subject = tracked{4};
		CHECK(g_alive == 1);
	}
	CHECK(g_alive == 0);
}

OPSY_TEST(callback_invokes_and_returns)
{
	opsy::callback<int(int)> doubler;
	doubler = [](int value) { return value * 2; };
	const auto result = doubler(21);
	CHECK(result.has_value());
	CHECK(result.value_or(0) == 42);
}

OPSY_TEST(callback_move_assignment_also_destroys_the_previous_functor)
{
	reset_counters();
	{
		opsy::callback<void()> destination;
		destination = tracked{5};
		CHECK(g_alive == 1);

		opsy::callback<void()> source;
		source = tracked{6};
		CHECK(g_alive == 2);

		destination = std::move(source); // must destroy tracked{5}
		CHECK(g_alive == 1);
	}
	CHECK(g_alive == 0);
}

} // namespace
