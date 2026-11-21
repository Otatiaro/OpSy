# The evaluation semantic of OpSy's contracts, which follows NDEBUG the way
# the asserts they replaced did.
#
# A configuration that ships (NDEBUG) checks nothing: ignore, and the checks
# cost nothing. Every other configuration checks, and a failed check calls
# handle_contract_violation, which each test image defines: observe, whose
# handler is all that runs on a violation. GCC's default is enforce, which
# follows the handler with std::terminate; a bare-metal image has no use for
# the C++ runtime's terminate path, and observe with a handler that does not
# return is the same stop without it.
#
# Use: include(../contracts.cmake) then opsy_contract_semantic("<flags>" <out>)
# and add <out> to the compile options of what builds with <flags>.

function(opsy_contract_semantic flags out)
    if("${flags}" MATCHES "NDEBUG")
        set(${out} -fcontract-evaluation-semantic=ignore PARENT_SCOPE)
    else()
        set(${out} -fcontract-evaluation-semantic=observe PARENT_SCOPE)
    endif()
endfunction()
