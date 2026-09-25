# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

# Clones, builds, and installs geoml (https://github.com/DLR-SC/geoml, pinned commit
# on `main`) into .deps/geoml - run via `cmake -P scripts/install_geoml.cmake` (see
# pixi.toml's own install_geoml task). Same shape as scripts/install_grunk.cmake
# (a plain `cmake -P` script, idempotent - skips the whole build if that prefix
# already looks populated - and platform-portable without a separate bash/PowerShell
# port), but a genuinely new bootstrap this plugin needs that grunk-occt didn't:
# unlike OCCT, geoml has no conda/pixi binary package in any environment, so it has
# to be built from source here, the same way grunk itself is.
#
# Built with -DGEOML_INSTALL_INTERNAL_API=ON, deliberately: geoml's own "internal"
# implementation headers (math, geometry, topology, ...) live at the top level of its
# source tree (siblings of geoml/), and only get re-nested under the legacy-shaped
# include/geoml/internal/<name> layout this plugin's own codegen/config.yml module
# list is written against (matching legacy-geo/config.yml.in's own header globs) when
# this option is on - see DESIGN.md.
#
# Built against whatever OCCT this project's own configure already resolved
# ($ENV{CONDA_PREFIX} - the conda-forge `occt` package, same as grunk-occt's own
# default build) via CMAKE_PREFIX_PATH passed through explicitly below, since this
# script runs as its own separate `cmake -P` process, before this project's own
# configure starts (same reasoning as install_grunk.cmake's own GRUNK_PREFIX/
# find_package(grunk) split). geoml's own CMakeLists.txt resolves OCCT itself via
# its cmake/UseOpenCASCADE.cmake (a plain find_package(OpenCASCADE REQUIRED) - no
# conan involved despite geoml also carrying a conanfile.py; the grunk cad_autodiff
# example's own geoml_plugin prototype already builds geoml this same way, plain
# CMake + CMAKE_PREFIX_PATH, confirmed working there).
#
# For the AD-enabled build (geoml's feature/autodiff branch, -DGEOML_USE_ADOLC=ON,
# against adOCCT) see scripts/install_adgeoml.sh instead - a separate script, not a
# branch of this one, mirroring how grunk-occt's own OCCT/adOCCT split is a CMake
# *option* on one project but geoml's *build* of itself against one or the other is
# still two entirely separate installs (.deps/geoml vs .deps/adgeoml), never
# reconfigured in place.

cmake_minimum_required(VERSION 3.15)

set(GEOML_REV "3cce0898aad1042d86c7e92754ff82f38b385658")

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(GEOML_PREFIX "${PROJECT_ROOT}/.deps/geoml")
set(GEOML_CHECKOUT "${PROJECT_ROOT}/build/_deps/geoml")

if(EXISTS "${GEOML_PREFIX}/lib/cmake/geoml/geoml-config.cmake" OR EXISTS "${GEOML_PREFIX}/lib64/cmake/geoml/geoml-config.cmake")
    message(STATUS "geoml already installed at ${GEOML_PREFIX} - skipping (delete this directory to force a rebuild).")
    return()
endif()

function(run_or_die)
    execute_process(${ARGV} RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Command failed (exit ${_result}): ${ARGV}")
    endif()
endfunction()

if(NOT EXISTS "${GEOML_CHECKOUT}/.git")
    file(MAKE_DIRECTORY "${PROJECT_ROOT}/build/_deps")
    run_or_die(COMMAND git clone https://github.com/DLR-SC/geoml.git geoml
        WORKING_DIRECTORY "${PROJECT_ROOT}/build/_deps")
endif()

run_or_die(COMMAND git fetch origin "${GEOML_REV}" WORKING_DIRECTORY "${GEOML_CHECKOUT}")
run_or_die(COMMAND git checkout "${GEOML_REV}" WORKING_DIRECTORY "${GEOML_CHECKOUT}")

run_or_die(COMMAND "${CMAKE_COMMAND}" -G Ninja -S . -B build
        -D CMAKE_BUILD_TYPE=Release
        -D CMAKE_INSTALL_PREFIX=${GEOML_PREFIX}
        -D CMAKE_PREFIX_PATH=$ENV{CONDA_PREFIX}
        -D CMAKE_POSITION_INDEPENDENT_CODE=ON
        -D GEOML_INSTALL_INTERNAL_API=ON
    WORKING_DIRECTORY "${GEOML_CHECKOUT}")
run_or_die(COMMAND "${CMAKE_COMMAND}" --build build --parallel WORKING_DIRECTORY "${GEOML_CHECKOUT}")
run_or_die(COMMAND "${CMAKE_COMMAND}" --install build WORKING_DIRECTORY "${GEOML_CHECKOUT}")

message(STATUS "geoml installed to ${GEOML_PREFIX}")
