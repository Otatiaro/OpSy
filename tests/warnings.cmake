# The warning set every Cortex-M suite builds with.
#
# It is the set a strict downstream project compiles OpSy with -- everything
# GCC offers that bears on correctness, as errors -- so the headers are held
# to it here rather than by the first project that turns it on. A header that
# warns under a user's flags is a header that does not build for that user.
#
# Several of these only report something once the optimiser has run
# (-Wnull-dereference, -Wstack-usage, -Wformat-truncation), which is why each
# suite builds at more than one -O level.
#
# Use: include(../warnings.cmake) then opsy_strict_warnings(<target>).

function(opsy_strict_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wshadow -Wcast-qual -Wnull-dereference -Wpointer-arith
        -Wconversion -Wsign-conversion -Wdouble-promotion -Wfloat-equal
        -Wformat=2 -Wundef -Wredundant-decls -Wmissing-declarations -Wvla
        -Wold-style-cast -Wzero-as-null-pointer-constant -Wnon-virtual-dtor
        -Wsuggest-override -Wextra-semi -Wdeprecated-copy-dtor
        -Wmismatched-tags
        -Wno-psabi
    )

    # Spelled differently, or absent, in clang: given only to GCC rather than
    # dropped, so the GCC jobs carry the full set. -Wctad-maybe-unsupported is
    # here too: clang reports it on std::lock_guard guard{m}, because libstdc++
    # does not mark its deduction guides the way clang looks for, while GCC
    # knows them.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE
            -Wcast-align=strict -Wlogical-op -Wduplicated-cond -Wduplicated-branches
            -Wformat-overflow=2 -Wformat-truncation=2 -Walloca -Wstack-usage=1024
            -Woverloaded-virtual=2 -Wvolatile -Wcomma-subscript -Wredundant-tags
            -Wctad-maybe-unsupported
        )
    else()
        target_compile_options(${target} PRIVATE
            -Wcast-align -Woverloaded-virtual -Wdeprecated-volatile -Wdeprecated-comma-subscript
        )
    endif()
endfunction()
