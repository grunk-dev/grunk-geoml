# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

# Clones, builds, and installs grunk into .deps/grunk (or .deps/<GRUNK_INSTALL_NAME>,
# see below) - run via `cmake -P scripts/install_grunk.cmake` (see pixi.toml's own
# install_grunk task). Copied verbatim from grunk-occt/scripts/install_grunk.cmake -
# same dependency, same pinning mechanism, same rationale (see that file's own,
# longer comment for the full "shared libraries, not vendored" story this plugin
# inherits unchanged). Pinned to the same GRUNK_REV grunk-occt currently pins, so
# both plugins build against the exact same grunk commit.
#
# Optional -D GRUNK_INSTALL_NAME=<name>: an alternate install-directory name under
# .deps/ (e.g. "grunk-ad" for [feature.ad.tasks]'s own invocation) - defaults to
# "grunk", same convention as grunk-occt.

cmake_minimum_required(VERSION 3.15)

if(NOT DEFINED GRUNK_INSTALL_NAME)
    set(GRUNK_INSTALL_NAME "grunk")
endif()
set(GRUNK_REV "583da89f0184ef6333000c45573da65a9dd551d0")

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(GRUNK_PREFIX "${PROJECT_ROOT}/.deps/${GRUNK_INSTALL_NAME}")
set(GRUNK_CHECKOUT "${PROJECT_ROOT}/build/_deps/${GRUNK_INSTALL_NAME}")

if(EXISTS "${GRUNK_PREFIX}/lib/cmake/grunk/grunkConfig.cmake")
    message(STATUS "grunk already installed at ${GRUNK_PREFIX} - skipping (delete this directory to force a rebuild).")
    return()
endif()

function(run_or_die)
    execute_process(${ARGV} RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Command failed (exit ${_result}): ${ARGV}")
    endif()
endfunction()

if(NOT EXISTS "${GRUNK_CHECKOUT}/.git")
    file(MAKE_DIRECTORY "${PROJECT_ROOT}/build/_deps")
    run_or_die(COMMAND git clone https://github.com/grunk-dev/grunk.git "${GRUNK_INSTALL_NAME}"
        WORKING_DIRECTORY "${PROJECT_ROOT}/build/_deps")
endif()

run_or_die(COMMAND git fetch origin "${GRUNK_REV}" WORKING_DIRECTORY "${GRUNK_CHECKOUT}")
run_or_die(COMMAND git checkout "${GRUNK_REV}" WORKING_DIRECTORY "${GRUNK_CHECKOUT}")

run_or_die(COMMAND "${CMAKE_COMMAND}" -G Ninja -S . -B build
        -D CMAKE_BUILD_TYPE=Release
        -D CMAKE_INSTALL_PREFIX=${GRUNK_PREFIX}
        -D GRUNK_WITH_TASKFLOW=OFF
        -D GRUNK_WITH_DYNAMIC=ON
        -D GRUNK_WITH_RECIPE=ON
        -D GRUNK_WITH_PLUGIN=ON
        -D GRUNK_TESTS=OFF
        -D GRUNK_PYTHON=OFF
    WORKING_DIRECTORY "${GRUNK_CHECKOUT}")
run_or_die(COMMAND "${CMAKE_COMMAND}" --build build --parallel WORKING_DIRECTORY "${GRUNK_CHECKOUT}")
run_or_die(COMMAND "${CMAKE_COMMAND}" --install build WORKING_DIRECTORY "${GRUNK_CHECKOUT}")

message(STATUS "grunk installed to ${GRUNK_PREFIX}")
