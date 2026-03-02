#
# This function will prevent in-source builds
#
function(AssureOutOfSourceBuilds)
    # make sure the user doesn't play dirty with symlinks
    get_filename_component(srcdir "${CMAKE_SOURCE_DIR}" REALPATH)
    get_filename_component(bindir "${CMAKE_BINARY_DIR}" REALPATH)

    # disallow in-source builds
    if("${srcdir}" STREQUAL "${bindir}")
        message("######################################################")
        message("# VersatileEngine should not be configured & built in the source directory.")
        message("# You must run cmake in a separate build directory.")
        message("# For example:")
        message("#   cmake -B build")
        message("#   cmake --build build")
        message("######################################################")
        message(FATAL_ERROR "Quitting configuration")
    endif()
endfunction()

AssureOutOfSourceBuilds()