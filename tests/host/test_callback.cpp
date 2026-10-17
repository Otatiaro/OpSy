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

#include <optional>
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
	CHECK(doubler(21) == 42);
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

// ──────────── regression: a move copied the functor's bytes ────────────────
// Moving a callback copied its storage byte for byte. No functor was ever
// constructed in the destination -- calling it was undefined -- and one that
// keeps a pointer into itself went on pointing at the source.

/** @brief Functor that knows where it lives, so a move that skips its move constructor is observable. */
struct self_aware
{
	self_aware() : self(this) {}
	self_aware(const self_aware&) : self(this) {}
	self_aware(self_aware&&) noexcept : self(this) {}
	self_aware& operator=(const self_aware&) = delete;
	self_aware& operator=(self_aware&&) = delete;
	~self_aware() = default;

	bool operator()() const { return self == this; }

	const self_aware* self;
};

OPSY_TEST(callback_move_construction_moves_the_functor)
{
	opsy::callback<bool()> source = self_aware{};
	opsy::callback<bool()> destination = std::move(source);
	CHECK(destination());
	CHECK(!source);
}

OPSY_TEST(callback_move_assignment_moves_the_functor)
{
	opsy::callback<bool()> source = self_aware{};
	opsy::callback<bool()> destination;
	destination = std::move(source);
	CHECK(destination());
	CHECK(!source);
}

OPSY_TEST(callback_move_into_larger_storage_moves_the_functor)
{
	opsy::callback<bool()> source = self_aware{};
	opsy::callback<bool(), 8> destination = std::move(source);
	CHECK(destination());
}

OPSY_TEST(callback_move_destroys_the_moved_from_functor)
{
	reset_counters();
	int destroyed_before = 0;
	{
		opsy::callback<void()> source = tracked{7};
		CHECK(g_alive == 1);
		destroyed_before = g_destroyed; // the temporary tracked{7} is already counted
		opsy::callback<void()> destination = std::move(source);
		CHECK(g_alive == 1);                          // one constructed in the destination...
		CHECK(g_destroyed == destroyed_before + 1);   // ...and the source's destroyed
	}
	CHECK(g_alive == 0);
	CHECK(g_destroyed == destroyed_before + 2);
}

// ───────────────── the interface of std::move_only_function ─────────────────

int g_free_function_calls = 0;
int counted() { return ++g_free_function_calls; }

OPSY_TEST(an_unqualified_signature_takes_a_mutable_lambda_and_keeps_its_state)
{
	opsy::callback<int()> counter = [n = 0]() mutable { return ++n; };
	CHECK(counter() == 1);
	CHECK(counter() == 2);
	CHECK(counter() == 3);
}

// What a signature accepts, decided at compile time as std::move_only_function
// decides it: const asks for a const call, noexcept for a noexcept one.
namespace accepts
{
using mutable_type = decltype([n = 0]() mutable { return ++n; });
using const_type = decltype([] { return 1; });
using noexcept_type = decltype([]() noexcept { return 1; });

static_assert(std::is_constructible_v<opsy::callback<int()>, mutable_type>);
static_assert(!std::is_constructible_v<opsy::callback<int() const>, mutable_type>);
static_assert(std::is_constructible_v<opsy::callback<int() const>, const_type>);
static_assert(!std::is_constructible_v<opsy::callback<int() noexcept>, const_type>);
static_assert(std::is_constructible_v<opsy::callback<int() noexcept>, noexcept_type>);
static_assert(noexcept(std::declval<opsy::callback<int() noexcept>&>()()));
static_assert(!noexcept(std::declval<opsy::callback<int()>&>()()));
static_assert(std::is_invocable_v<const opsy::callback<int() const>&>);
static_assert(!std::is_invocable_v<const opsy::callback<int()>&>);
static_assert(std::is_invocable_v<opsy::callback<int() &&>&&>);
static_assert(!std::is_invocable_v<opsy::callback<int() &&>&>);
static_assert(std::is_nothrow_move_constructible_v<opsy::callback<void()>>);
}

OPSY_TEST(an_rvalue_signature_calls_its_target_as_an_rvalue)
{
	struct consumable
	{
		int operator()() && { return 7; }
		int operator()() & { return -1; }
	};
	opsy::callback<int() &&> once = consumable{};
	CHECK(std::move(once)() == 7);
}

OPSY_TEST(nullptr_means_empty)
{
	opsy::callback<void()> from_null = nullptr;
	CHECK(!from_null);
	CHECK(from_null == nullptr);

	reset_counters();
	opsy::callback<void()> holding = tracked{ 1 };
	CHECK(holding != nullptr);
	holding = nullptr;              // empties, and destroys what it held
	CHECK(!holding);
	CHECK(g_alive == 0);
}

OPSY_TEST(a_null_function_pointer_gives_an_empty_callback)
{
	int (*none)() = nullptr;
	opsy::callback<int()> from_none = none;
	CHECK(!from_none);

	opsy::callback<int()> from_function = &counted;
	CHECK(from_function);
	g_free_function_calls = 0;
	CHECK(from_function() == 1);
}

OPSY_TEST(swap_exchanges_the_targets)
{
	opsy::callback<int()> one = [] { return 1; };
	opsy::callback<int()> two = [] { return 2; };
	swap(one, two);
	CHECK(one() == 2);
	CHECK(two() == 1);

	opsy::callback<int()> empty;
	one.swap(empty);
	CHECK(!one);
	CHECK(empty() == 2);
}

OPSY_TEST(a_target_can_be_constructed_in_place)
{
	struct adder
	{
		explicit adder(int a, int b) noexcept : sum(a + b) {}
		int operator()() const { return sum; }
		int sum;
	};
	opsy::callback<int() const> made(std::in_place_type<adder>, 2, 3);
	CHECK(made() == 5);
}

} // namespace
