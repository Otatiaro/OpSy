/**
 ******************************************************************************
 * @file    test_allocator.cpp
 * @brief   Behavioural tests for @c opsy::utility::allocator .
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#include "host_test.hpp"

#include <utility/allocator.hpp>

#include <cstdint>
#include <vector>

namespace
{

using opsy::utility::allocator;

// ─────────────────────── regression: trailing merge ────────────────────────
// deallocate() skipped the merge with the next chunk in two ways: the guard
// read < N - 2 where a head index is valid up to N - 2 included, and the
// freeness test read > 0 where a free chunk of zero payload carries 0.
//
// allocate() creates exactly such a chunk whenever needed_slots + 2 uses up
// the trailing chunk exactly, so a single max-sized allocate/deallocate pair
// left the allocator permanently unable to allocate anything — while
// run_check() kept reporting the arena as coherent.

OPSY_TEST(allocator_max_allocation_round_trips)
{
	// 32 slots: allocate(112) needs 28, and 28 + 2 == available_slots (30).
	allocator<32> arena;
	CHECK(arena.empty());
	CHECK(arena.available() == 112);

	void* block = arena.allocate(112);
	CHECK(block != nullptr);
	CHECK(arena.run_check());

	arena.deallocate(block);

	// The bug left all three of these wrong: not empty, nothing available,
	// and every later allocation refused.
	CHECK(arena.run_check());
	CHECK(arena.empty());
	CHECK(arena.available() == 112);
	CHECK(arena.allocate(4) != nullptr);
}

OPSY_TEST(allocator_every_single_allocation_size_round_trips)
{
	// The zero-payload trailing chunk only appears at one specific size per
	// arena, so sweep every size rather than spot-checking.
	constexpr std::size_t n = 32;
	for (std::size_t bytes = 1; bytes <= (n - 2) * sizeof(int); ++bytes)
	{
		allocator<n> arena;
		if (void* block = arena.allocate(bytes))
		{
			arena.deallocate(block);
			CHECK(arena.run_check());
			CHECK(arena.empty());
		}
	}
}

OPSY_TEST(allocator_lifo_sequences_always_return_to_empty)
{
	// Deterministic pseudo-random LIFO traffic: no std::random, so the
	// sequence is identical on every platform and a failure is reproducible.
	constexpr std::size_t n = 64;
	for (unsigned seed = 1; seed <= 64; ++seed)
	{
		allocator<n> arena;
		std::vector<void*> live;
		uint32_t state = seed;

		for (int step = 0; step < 200; ++step)
		{
			state = state * 1664525u + 1013904223u; // Numerical Recipes LCG
			const bool allocating = live.empty() || ((state >> 16) % 100) < 60;

			if (allocating)
			{
				const std::size_t bytes = 1 + ((state >> 8) % (n * sizeof(int)));
				if (void* block = arena.allocate(bytes))
					live.push_back(block);
			}
			else
			{
				arena.deallocate(live.back());
				live.pop_back();
			}

			CHECK(arena.run_check());
		}

		while (!live.empty())
		{
			arena.deallocate(live.back());
			live.pop_back();
		}

		CHECK(arena.run_check());
		CHECK(arena.empty());
		CHECK(arena.available() == (n - 4) * sizeof(int));
	}
}

OPSY_TEST(allocator_refuses_what_does_not_fit)
{
	allocator<32> arena;
	CHECK(arena.allocate(0) == nullptr);                  // zero bytes is not an allocation
	CHECK(arena.allocate(arena.size() + 1) == nullptr);   // past the arena
	CHECK(arena.allocate(116) == nullptr);                // 29 slots: no room for the split indicators
	CHECK(arena.empty());                                 // a refusal must not disturb the arena
	CHECK(arena.run_check());
}

OPSY_TEST(allocator_owns_only_its_own_payload)
{
	allocator<32> arena;
	void* block = arena.allocate(16);
	CHECK(block != nullptr);
	CHECK(arena.owns(block));

	int outside = 0;
	CHECK(!arena.owns(&outside));
	arena.deallocate(block);
}

} // namespace
