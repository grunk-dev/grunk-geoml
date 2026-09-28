# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

# Clones, builds, and installs grunk into .deps/grunk (or .deps/<GRUNK_INSTALL_NAME>,
# see below) - run via `cmake -P scripts/install_grunk.cmake` (see pixi.toml's own
# install_grunk task). Copied verbatim from grunk-occt/scripts/install_grunk.cmake -
# same dependency, same pinning mechanism, same rationale (see that file's own,
# longer comment for the full "shared libraries, not vendored" story this plugin
# inherits unchanged).
#
# GRUNK_REV was bumped 2026-09-28 past grunk-occt's own current pin
# (583da89f0184ef6333000c45573da65a9dd551d0), to
# b836a656f97765f0b48f7a527cf977454a830873 - grunk-geoml/codegen's two real,
# generic grunk.codegen bugs (Param.kind()'s over-broad canonical-spelling
# rejection breaking std::string/std::vector<T>; expand_headers()'s
# basename-reduction breaking nested header layouts) were fixed directly in grunk
# itself (see DESIGN.md's "Handle(X) cross-plugin sharing"/two-bugs-fixed-upstream
# entries) rather than staying as this plugin's own hooks.py workarounds, and this
# is the commit that lands them. grunk-occt's own pin has NOT been bumped to match
# yet (out of scope here - not touched unless separately asked) - the two plugins
# temporarily build against different grunk commits until that happens; both
# commits are otherwise compatible (the bump is additive/bugfix-only, no removed
# API), so this is not itself a build hazard, just worth resyncing eventually.
#
# Optional -D GRUNK_INSTALL_NAME=<name>: an alternate install-directory name under
# .deps/ (e.g. "grunk-ad" for [feature.ad.tasks]'s own invocation) - defaults to
# "grunk", same convention as grunk-occt.

cmake_minimum_required(VERSION 3.15)

if(NOT DEFINED GRUNK_INSTALL_NAME)
    set(GRUNK_INSTALL_NAME "grunk")
endif()
set(GRUNK_REV "b836a656f97765f0b48f7a527cf977454a830873")

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
