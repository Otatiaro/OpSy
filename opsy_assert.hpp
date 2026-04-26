/**
 ******************************************************************************
 * @file    opsy_assert.hpp
 * @author  Thomas Legrand
 * @brief   Replacement for @c <cassert> that traps instead of calling printf
 *
 *          Standard newlib @c assert(expr) expands to
 *          @code
 *              ((expr) ? (void)0
 *                      : __assert_func(__FILE__, __LINE__,
 *                                      __FUNCTION__, #expr))
 *          @endcode
 *          which forces the compiler to bake @c __FILE__, @c __FUNCTION__
 *          (often a long mangled C++ template name in OpSy) and the
 *          stringified expression into @c .rodata at every call site.
 *          Even when @c __assert_func itself is stubbed out the strings
 *          are kept alive — they are passed as arguments — so the linker
 *          cannot DCE them. On a typical OpSy build this adds up to about
 *          ten kilobytes of flash, mostly from asserts in templated
 *          containers (@c embedded_iterator, @c embedded_list).
 *
 *          This header replaces the macro by a direct branch to
 *          @c opsy::trap (which is itself overridable through
 *          @c <opsy_config.hpp>; see @c config.hpp). The breakpoint fires
 *          right at the assert site, so the debugger's program counter
 *          plus DWARF debug info still expose the file, line, function
 *          and the source expression — nothing useful is lost, only the
 *          duplicate copy that lived in flash at runtime.
 *
 *          Every OpSy header that previously included @c <cassert>
 *          includes this header instead, so the override is not silently
 *          re-clobbered by a later @c <cassert> include in the chain. The
 *          file is left at the top level of OpSy on purpose so it can be
 *          included from any subdirectory with @c "opsy_assert.hpp".
 ******************************************************************************
 * @copyright Copyright 2019 Thomas Legrand under the MIT License
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

// NOTE: deliberately NOT using @c #pragma once. The standard @c <cassert> is
// designed to be re-installable: it always @c #undef + @c #define the @c assert
// macro on every inclusion so that toggling @c NDEBUG between two includes
// works as expected. We mimic that behavior — if a transitive include drags
// @c <cassert> in *after* our first inclusion (libstdc++'s @c <atomic> /
// @c <mutex> / @c <optional> sometimes do), the next time an OpSy header
// includes @c "opsy_assert.hpp" it must be able to re-install our macro on top
// of the standard one. With @c #pragma once that second inclusion would be a
// no-op and the standard macro would silently win. The traditional include
// guard is therefore scoped to the namespace declaration only; the macro
// definition lives below the guard and runs on every inclusion.
#ifndef OPSY_ASSERT_HPP_
#define OPSY_ASSERT_HPP_
namespace opsy
{
[[noreturn]] void trap();  // defined inline in config.hpp; overridable via opsy_config.hpp
}
#endif

#undef assert
#ifdef NDEBUG
#define assert(expr) (static_cast<void>(0))
#else
#define assert(expr) (__builtin_expect(!(expr), 0) ? ::opsy::trap() : static_cast<void>(0))
#endif
