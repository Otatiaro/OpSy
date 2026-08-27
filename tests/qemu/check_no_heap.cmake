# Fails if a linked OpSy image contains a heap.
#
# Invoked by CMakeLists.txt with -DNM=... -DIMAGE=... .
#
# OpSy never allocates: every object it manages lives in storage the caller
# provided, and the lists that hold them are intrusive. An image built on it
# should therefore contain no allocator at all -- and on a part with no heap
# region in its linker script, one that does may not even link.
#
# Nothing states that in the source, though, so it is easy to lose. It only
# takes one construct that reaches the global operator new or delete, and the
# linker pulls in the allocator behind it: a virtual destructor whose deleting
# destructor has no class-level operator delete to call, a std::function, a
# container, a stream. None of that fails to build. The allocator simply
# appears, along with whatever _sbrk it sits on.
#
# So the property is checked where it is visible: in the symbols of the linked
# image.
#
# WHAT IS NOT A HEAP, and is therefore allowed:
#
#   placement new       operator new(<size>, void*) and its delete counterpart
#                       operator delete(void*, void*) construct an object in
#                       storage the caller already owns. They allocate
#                       nothing -- placement new returns the pointer it was
#                       given -- and are how OpSy builds objects in place.
#
#   class-level         a name like some::type::operator delete(void*) is a
#   operator delete     member, and members are only reached through that
#                       class. OpSy defines one on the classes whose virtual
#                       destructors would otherwise call the global one; that
#                       is what keeps the allocator out, so flagging it would
#                       report the fix as the fault.
#
# Everything else named operator new or operator delete is the global
# allocator, whatever its argument list -- including the nothrow forms, which
# allocate and return null rather than not allocating.

if(NOT EXISTS "${IMAGE}")
    message(FATAL_ERROR "image not found: ${IMAGE}")
endif()

execute_process(
    COMMAND ${NM} --demangle ${IMAGE}
    OUTPUT_VARIABLE symbols
    ERROR_VARIABLE nm_errors
    RESULT_VARIABLE nm_status
)

if(NOT nm_status EQUAL 0)
    message(FATAL_ERROR "nm failed (${nm_status}):\n${nm_errors}")
endif()

# The allocator and the system call it sits on, matched as whole names so an
# unrelated symbol that merely starts the same way does not trip the check.
set(allocator_functions
    malloc free calloc realloc
    _malloc_r _free_r _calloc_r _realloc_r
    _sbrk _sbrk_r
)

string(REPLACE "\n" ";" symbol_lines "${symbols}")

set(found "")

foreach(line ${symbol_lines})
    # A defined symbol reads "<address> <type> <name>"; anything without that
    # shape is a line nm printed for another reason.
    if(NOT line MATCHES "^[0-9a-fA-F]+ [A-Za-z] (.+)$")
        continue()
    endif()

    set(name "${CMAKE_MATCH_1}")

    foreach(allocator ${allocator_functions})
        if(name STREQUAL "${allocator}")
            list(APPEND found "${name}")
        endif()
    endforeach()

    # Unqualified, so global: a member would read "class::operator delete".
    if(name MATCHES "^operator (new|delete)(\\[\\])?\\((.*)\\)$")
        set(arguments "${CMAKE_MATCH_3}")
        # Placement forms take the storage to build in as a trailing void*.
        if(NOT arguments MATCHES ", void\\*$")
            list(APPEND found "${name}")
        endif()
    endif()
endforeach()

if(found)
    list(REMOVE_DUPLICATES found)
    string(REPLACE ";" ", " found_list "${found}")
    message(FATAL_ERROR
        "the image contains a heap: ${found_list}\n"
        "Something in it reaches the global allocator. To find what, link with "
        "-Wl,--cref and look for what references the symbol, or check for a "
        "polymorphic class whose deleting destructor has no class-level "
        "operator delete to call.")
endif()
