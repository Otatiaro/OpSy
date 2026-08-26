/**
 ******************************************************************************
 * @file    test_embedded_list.cpp
 * @brief   Behavioural tests for @c opsy::embedded_list .
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#include "host_test.hpp"

#include <embedded_list.hpp>

#include <algorithm>
#include <iterator>
#include <ranges>
#include <utility>
#include <vector>

namespace
{

struct node : opsy::embedded_node<node>
{
	explicit node(int value) : id(value) {}
	int id;
};

using list = opsy::embedded_list<node>;

std::vector<int> contents(list& subject)
{
	std::vector<int> result;
	for (auto it = subject.begin(); it != subject.end(); ++it)
		result.push_back(it->id);
	return result;
}

// ────────────────── regression: erasing another list's head ────────────────
// erase()'s "first but not last" branch set first_ = i.next() without checking
// that the item actually heads *this* list — the check its sibling branch
// already had. scheduler::ready_ and condition_variable::waiting_list_ share
// the same task_lists::waiting node pair, so a task linked into one and erased
// from the other took exactly this path.

OPSY_TEST(embedded_list_erase_ignores_a_foreign_head)
{
	node x0{0}, x1{1}, x2{2}, x3{3};
	list a, b;
	a.push_front(x1); a.push_front(x0);   // a = [x0, x1]
	b.push_front(x3); b.push_front(x2);   // b = [x2, x3]

	b.erase(x0);                          // x0 heads a, and is not in b at all

	// The bug spliced a's node x1 into b, dropped x2 and x3, and left a
	// claiming size 2 while backing only x0.
	CHECK(contents(a) == std::vector<int>({0, 1}));
	CHECK(a.size() == 2);
	CHECK(contents(b) == std::vector<int>({2, 3}));
	CHECK(b.size() == 2);
}

OPSY_TEST(embedded_list_erase_removes_from_every_position)
{
	for (int count = 1; count <= 6; ++count)
		for (int target = 0; target < count; ++target)
		{
			std::vector<node> nodes;
			nodes.reserve(static_cast<std::size_t>(count));
			for (int i = 0; i < count; ++i)
				nodes.emplace_back(i);

			list subject;
			for (int i = count - 1; i >= 0; --i)
				subject.push_front(nodes[static_cast<std::size_t>(i)]);

			subject.erase(nodes[static_cast<std::size_t>(target)]);

			std::vector<int> expected;
			for (int i = 0; i < count; ++i)
				if (i != target)
					expected.push_back(i);

			CHECK(contents(subject) == expected);
			CHECK(subject.size() == expected.size());
		}
}

OPSY_TEST(embedded_list_erase_on_an_empty_list_is_inert)
{
	node lonely{7};
	list subject;
	subject.erase(lonely);
	CHECK(subject.empty());
	CHECK(subject.size() == 0);
}

// ─────────────── regression: const iteration, clear, move ──────────────────
// begin() const and cbegin() returned first_ where const_iterator's converting
// constructor is explicit; the const iterator's operator++ returned a raw
// pointer; clear() assigned an Item* to an iterator; and the move constructor
// was explicit, so the documented move forms resolved to the deleted copy.
// Each of these was a hard compile error, so this case existing at all is the
// regression test — scheduler::all_tasks() hands users exactly this type.

OPSY_TEST(embedded_list_supports_const_iteration)
{
	node x0{10}, x1{20};
	list subject;
	subject.push_front(x1);
	subject.push_front(x0);

	const list& immutable = subject;

	int through_begin = 0;
	for (auto it = immutable.begin(); it != immutable.end(); ++it)
		through_begin += it->id;
	CHECK(through_begin == 30);

	int through_cbegin = 0;
	for (auto it = subject.cbegin(); it != subject.cend(); ++it)
		through_cbegin += it->id;
	CHECK(through_cbegin == 30);

	int through_range_for = 0;
	for (const auto& item : immutable)
		through_range_for += item.id;
	CHECK(through_range_for == 30);
}

OPSY_TEST(embedded_list_clear_empties_and_unlinks)
{
	node x0{1}, x1{2}, x2{3};
	list subject;
	subject.push_front(x2);
	subject.push_front(x1);
	subject.push_front(x0);

	subject.clear();
	CHECK(subject.empty());
	CHECK(subject.size() == 0);

	// clear() must reset the nodes, not just drop the head: push_front asserts
	// the node is free, so a stale link would trap here.
	list reused;
	reused.push_front(x0);
	CHECK(contents(reused) == std::vector<int>({1}));
}

OPSY_TEST(embedded_list_is_move_constructible)
{
	node x0{5}, x1{6};
	list source;
	source.push_front(x1);
	source.push_front(x0);

	list moved = std::move(source);   // resolved to the deleted copy before the fix

	CHECK(contents(moved) == std::vector<int>({5, 6}));
	CHECK(moved.size() == 2);
	CHECK(source.empty());
}


// ───────────────── iterator concepts and <ranges> interop ──────────────────
// The iterators advertise bidirectional_iterator_tag. They used to satisfy no
// iterator concept at all — operator++ returned by value instead of by
// reference, and there was no post-increment — so <ranges> and <algorithm>
// were unusable on a list whose own tag promised otherwise.

static_assert(std::input_or_output_iterator<list::iterator>);
static_assert(std::forward_iterator<list::iterator>);
static_assert(std::bidirectional_iterator<list::iterator>);
static_assert(std::forward_iterator<list::const_iterator>);
static_assert(std::bidirectional_iterator<list::const_iterator>);
static_assert(std::ranges::range<list>);
static_assert(std::ranges::bidirectional_range<list>);

OPSY_TEST(embedded_list_works_with_ranges_algorithms)
{
	node x0{3}, x1{1}, x2{2};
	list subject;
	subject.push_front(x2);
	subject.push_front(x1);
	subject.push_front(x0);   // [3, 1, 2]

	CHECK(std::ranges::count_if(subject, [](const node& n) { return n.id > 1; }) == 2);

	const auto found = std::ranges::find_if(subject, [](const node& n) { return n.id == 1; });
	CHECK(found != subject.end());
	CHECK(found->id == 1);

	std::vector<int> scaled;
	for (const int value : subject | std::views::transform([](const node& n) { return n.id * 10; }))
		scaled.push_back(value);
	CHECK(scaled == std::vector<int>({30, 10, 20}));
}

OPSY_TEST(embedded_list_iterators_increment_and_decrement_correctly)
{
	node x0{1}, x1{2};
	list subject;
	subject.push_front(x1);
	subject.push_front(x0);

	auto it = subject.begin();
	CHECK(it->id == 1);

	// Pre-increment returns a reference, so chaining advances the iterator
	// itself rather than a temporary.
	auto& same = ++it;
	CHECK(&same == &it);
	CHECK(it->id == 2);

	// Post-increment yields the value from before.
	auto before = it++;
	CHECK(before->id == 2);
	CHECK(it == subject.end());

	// Note: --end() is NOT supported, unlike std::list. end() is a null
	// iterator with no link back to the list, so there is no last element to
	// step onto. Walk back from a real element instead.
	auto back = subject.begin();
	++back;
	CHECK(back->id == 2);

	auto before_dec = back--;
	CHECK(before_dec->id == 2);
	CHECK(back->id == 1);
}

} // namespace
