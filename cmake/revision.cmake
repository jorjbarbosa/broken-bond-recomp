# Script mode, at every build (cmake/sentry.cmake): the repository's revision
# for the crash reports' release, with -dirty for tracked changes outside the
# Markdown files (the local documents change no binary), into OUTPUT, which is
# written only when the revision changed.
# Usage: cmake -DGIT=<git> -DSOURCE=<repository> -DOUTPUT=<header> -P revision.cmake
set(revision "")
if(GIT)
    execute_process(COMMAND "${GIT}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${SOURCE}" OUTPUT_VARIABLE revision OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND "${GIT}" diff --quiet --ignore-submodules -- . ":(exclude)*.md"
        WORKING_DIRECTORY "${SOURCE}" RESULT_VARIABLE dirty)
    if(revision AND dirty EQUAL 1)
        string(APPEND revision "-dirty")
    endif()
endif()
if(NOT revision)
    set(revision unknown)
endif()
set(text "#define BROKENBOND_BUILD_REVISION \"${revision}\"\n")
set(previous "")
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" previous)
endif()
if(NOT previous STREQUAL text)
    file(WRITE "${OUTPUT}" "${text}")
endif()
