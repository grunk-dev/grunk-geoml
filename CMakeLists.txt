# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

cmake_minimum_required(VERSION 3.15)
project(grunk-geoml VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

# geoml_plugin is itself a shared object, so its own sources need to be
# position-independent - grunk's own libraries are SHARED (found via find_package
# below) and no longer need this set on their behalf. Mirrors grunk-occt's own
# CMakeLists.txt.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_PREFIX_PATH "$ENV{CONDA_PREFIX}")

# See grunk-occt/CMakeLists.txt's own identical comment: geoml_plugin (and, in the
# GRUNK_GEOML_ENABLE_AD branch below, adolc_plugin) links grunk's own SHARED libraries
# dynamically, rather than each statically embedding its own private copy - one
# physical set of grunk libraries shared by every consumer loaded into the same
# process, not one duplicated per plugin.
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
if (DEFINED ENV{CONDA_PREFIX})
    list(APPEND CMAKE_INSTALL_RPATH "$ENV{CONDA_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
endif()
if (APPLE)
    list(APPEND CMAKE_INSTALL_RPATH "@loader_path" "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
elseif (NOT WIN32)
    list(APPEND CMAKE_INSTALL_RPATH "$ORIGIN" "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()

# Defaults to OFF, unlike grunk-occt's own GRUNK_OCCT_TESTS (ON) - there is no
# tests/ directory yet (see DESIGN.md, this is scaffolding only so far, Step 5 adds
# the actual test harness). Flip the default to ON once tests/test_load.cpp and
# friends exist, mirroring grunk-occt exactly at that point.
option(GRUNK_GEOML_TESTS "Build the test harness (no tests/ yet - see DESIGN.md)" OFF)

# See grunk-occt/CMakeLists.txt's own identical option - same generated-code policy
# (checked-in generated/, regenerated only on request, requires clang/libclang).
option(GRUNK_GEOML_GENERATE_CODE "Regenerate geoml bindings using the codegen tool (requires clang/libclang)" OFF)

# --- AD build: recompiles this plugin's own generated sources against geoml's
# feature/autodiff branch (-DGEOML_USE_ADOLC=ON) linked against an AD-enabled OCCT
# fork (adOCCT), mirroring grunk-occt's own GRUNK_OCCT_ENABLE_AD exactly - same
# ADOCCT_DIR variable name deliberately, so both plugins can be pointed at the *same*
# adOCCT install (mixing occt.*/geoml.* AD-carrying values in one recipe requires
# this - see README.md's "Algorithmic differentiation (AD)"). geoml's own AD build is
# a second, separate install this project bootstraps itself (scripts/install_adgeoml.sh,
# AD_GEOML_DIR below) - unlike OCCT, geoml has no conda/pixi binary package in any
# variant, so there's no "just point find_package at a different prefix" shortcut the
# way ADOCCT_DIR already is for OpenCASCADE.
option(GRUNK_GEOML_ENABLE_AD "Build against geoml's feature/autodiff branch + an AD-enabled OCCT fork (adOCCT) instead of stock geoml/OCCT - requires ADOCCT_DIR and AD_GEOML_DIR, see README.md's \"Algorithmic differentiation (AD)\"" OFF)

# geoml's own install prefix - explicit cache var + NO_DEFAULT_PATH (see the actual
# find_package(geoml) call below), not a plain CMAKE_PREFIX_PATH entry. geoml has no
# conda/pixi package *today*, so this is currently belt-and-suspenders - but that's
# expected to change, possibly soon, and once geoml itself ships as a conda package,
# a plain CMAKE_PREFIX_PATH-based find_package(geoml) could silently resolve to that
# system-provided geoml instead of this project's own from-source build
# (scripts/install_geoml.cmake) - exactly the ambiguity ADOCCT_DIR/NO_DEFAULT_PATH
# below already guards against for OpenCASCADE. Defaults to where
# scripts/install_geoml.cmake installs it, so a plain `pixi run configure` still
# works with no extra flags; pixi.toml's own `configure` task also passes this
# explicitly, matching AD_GEOML_DIR's own explicit-flag convention below.
set(GEOML_DIR "${CMAKE_SOURCE_DIR}/.deps/geoml" CACHE PATH "Install prefix of geoml (see scripts/install_geoml.cmake)")

if(GRUNK_GEOML_ENABLE_AD)
    set(ADOCCT_DIR "" CACHE PATH "Install prefix of adOCCT (dlr-sp/adOCCT), built with -DUSE_ADOLC=ON - same install grunk-occt's own AD build should be pointed at")
    if(NOT ADOCCT_DIR)
        message(FATAL_ERROR "GRUNK_GEOML_ENABLE_AD is ON but ADOCCT_DIR is not set - see README.md's \"Algorithmic differentiation (AD)\".")
    endif()
    set(AD_GEOML_DIR "" CACHE PATH "Install prefix of geoml's feature/autodiff branch, built with -DGEOML_USE_ADOLC=ON -DGEOML_INSTALL_INTERNAL_API=ON against ADOCCT_DIR (see scripts/install_adgeoml.sh)")
    if(NOT AD_GEOML_DIR)
        message(FATAL_ERROR "GRUNK_GEOML_ENABLE_AD is ON but AD_GEOML_DIR is not set - run scripts/install_adgeoml.sh (see README.md).")
    endif()
endif()

# --- grunk: a pre-built, pre-installed dependency, exactly like grunk-occt's own
# find_package(grunk) (see its own CMakeLists.txt comment for the full "shared
# libraries, not vendored" rationale) - resolved via CMAKE_PREFIX_PATH (pixi.toml's
# own activation.env points this at .deps/grunk).
find_package(grunk REQUIRED)

# --- OCCT: stock (conda-forge `occt` package) by default, or adOCCT instead when
# GRUNK_GEOML_ENABLE_AD is ON - same find_package(OpenCASCADE) name either way,
# same NO_DEFAULT_PATH defense-in-depth as grunk-occt's own identical branch (see its
# comment - the `ad` pixi environment has no stock `occt` package at all, but someone
# can always reuse ADOCCT_DIR against a different environment by hand).
#
# geoml's own installed CMake package (geoml-config.cmake, see below) does NOT
# re-declare its own OpenCASCADE dependency - confirmed by reading it directly
# (cmake/geoml-config.cmake.in upstream just includes geoml-targets.cmake, no
# find_dependency(OpenCASCADE) at all) - so OpenCASCADE must already be found, with
# its own imported targets already in scope, *before* find_package(geoml) below, or
# the geoml imported target's own link interface (which references OpenCASCADE::*
# targets by name) fails to resolve.
if(GRUNK_GEOML_ENABLE_AD)
    find_package(OpenCASCADE CONFIG REQUIRED PATHS "${ADOCCT_DIR}" NO_DEFAULT_PATH)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ADOLC REQUIRED IMPORTED_TARGET GLOBAL adolc)

    # --- grunk-adolc (FetchContent, vendors its own source - same pattern as
    # grunk-occt's own identical block, see its comment for the full rationale,
    # including why GRUNK_ADOLC_TESTS must be forced OFF and why no
    # EXCLUDE_FROM_ALL). Pinned to the same commit grunk-occt's own CMakeLists.txt
    # currently pins, so both plugins load the exact same adolc_plugin build when
    # used together.
    include(FetchContent)

    FetchContent_Declare(grunk-adolc
        GIT_REPOSITORY https://github.com/grunk-dev/grunk-adolc.git
        GIT_TAG 42038721e93f8d6024931df88a88f153acc69cfb
    )

    FetchContent_GetProperties(grunk-adolc)
    if(NOT grunk-adolc_POPULATED)
        if(POLICY CMP0169)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(grunk-adolc)

        set(GRUNK_ADOLC_TESTS OFF CACHE BOOL "" FORCE)
        set(GRUNK_ADOLC_GENERATE_CODE OFF CACHE BOOL "" FORCE)

        add_subdirectory(${grunk-adolc_SOURCE_DIR} ${grunk-adolc_BINARY_DIR})
    endif()
else()
    find_package(OpenCASCADE REQUIRED)
endif()

# --- geoml: like grunk, a pre-built, pre-installed dependency this project
# bootstraps itself (scripts/install_geoml.cmake / scripts/install_adgeoml.sh) rather
# than a conda/pixi binary package. Resolved via an explicit PATHS + NO_DEFAULT_PATH
# (GEOML_DIR for the plain build, AD_GEOML_DIR for the AD one - set above), the same
# defense-in-depth ADOCCT_DIR uses for OpenCASCADE, not a plain CMAKE_PREFIX_PATH
# lookup - see GEOML_DIR's own comment above for why (geoml has no conda/pixi package
# today, but that's expected to change, and this avoids silently picking up a
# system-provided one later). Exports a plain (non-namespaced) `geoml` imported
# target - confirmed against grunk's own cad_autodiff example prototype, which links
# it the same way (`target_link_libraries(... geoml)`, no `geoml::` prefix).
if(GRUNK_GEOML_ENABLE_AD)
    find_package(geoml CONFIG REQUIRED PATHS "${AD_GEOML_DIR}" NO_DEFAULT_PATH)
else()
    find_package(geoml CONFIG REQUIRED PATHS "${GEOML_DIR}" NO_DEFAULT_PATH)
endif()
get_target_property(GEOML_INCLUDE_DIR geoml INTERFACE_INCLUDE_DIRECTORIES)

# --- code generator: opt-in, same rationale as grunk-occt's own identical block
# (regenerated output needs review before being trusted, not silent regeneration on
# every build). --include-dir points at geoml's own installed headers (built with
# -DGEOML_INSTALL_INTERNAL_API=ON, so geoml/internal/<name> paths are present - see
# DESIGN.md), the geoml equivalent of grunk-occt's own
# --include-dir ${OpenCASCADE_INCLUDE_DIR}.
if (GRUNK_GEOML_GENERATE_CODE)
    add_custom_target(generate_code
        COMMAND grunk codegen
                --config ${CMAKE_SOURCE_DIR}/codegen/config.yml
                --include-dir ${GEOML_INCLUDE_DIR}
                --output-dir ${CMAKE_SOURCE_DIR}/generated
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Regenerating geoml bindings (grunk codegen)"
        VERBATIM
    )
endif()

# --- geoml plugin target: guarded on generated/ and src/geoml_plugin.cpp actually
# existing - neither does yet (this repository is scaffolding only so far, see
# DESIGN.md/README.md - codegen/config.yml and the first wrapped modules land in a
# later step). Once they do, this whole branch should collapse to look exactly like
# grunk-occt/CMakeLists.txt's own occt_plugin target (same translation-unit-grouping
# logic, same generated/translation_units.txt convention, same AD dual-emission
# handling via GRUNK_GEOML_WITH_ADOLC) - keeping the `if` here rather than deleting it
# is deliberate, so a bare `cmake -S . -B build` against this scaffold-only commit
# configures cleanly instead of failing on a missing file(STRINGS ...) read.
if (EXISTS "${CMAKE_SOURCE_DIR}/generated/translation_units.txt" AND EXISTS "${CMAKE_SOURCE_DIR}/src/geoml_plugin.cpp")
    file(STRINGS "${CMAKE_SOURCE_DIR}/generated/translation_units.txt" GEOML_TU_LINES)

    set(GEOML_PLUGIN_SOURCES "")

    list(LENGTH GEOML_TU_LINES GEOML_TU_LINE_COUNT)
    if(GEOML_TU_LINE_COUNT EQUAL 1 AND GEOML_TU_LINES STREQUAL "*")
        file(GLOB GENERATED_SOURCES ${CMAKE_SOURCE_DIR}/generated/*.cpp)

        set(GEOML_PLUGIN_UNITY_CPP "${CMAKE_BINARY_DIR}/geoml_plugin_unity.cpp")
        set(_unity_lines "// AUTO-GENERATED unity build file - see CMakeLists.txt. Do not edit or commit.\n")
        string(APPEND _unity_lines "#include \"${CMAKE_SOURCE_DIR}/src/geoml_plugin.cpp\"\n")
        foreach(_generated_src ${GENERATED_SOURCES})
            string(APPEND _unity_lines "#include \"${_generated_src}\"\n")
        endforeach()
        file(WRITE "${GEOML_PLUGIN_UNITY_CPP}" "${_unity_lines}")

        set_source_files_properties("${GEOML_PLUGIN_UNITY_CPP}" PROPERTIES
            OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/src/geoml_plugin.cpp;${GENERATED_SOURCES}"
        )
        list(APPEND GEOML_PLUGIN_SOURCES "${GEOML_PLUGIN_UNITY_CPP}")
    else()
        list(APPEND GEOML_PLUGIN_SOURCES "${CMAKE_SOURCE_DIR}/src/geoml_plugin.cpp")

        set(_tu_index 0)
        foreach(_tu_line ${GEOML_TU_LINES})
            separate_arguments(_tu_modules UNIX_COMMAND "${_tu_line}")
            list(LENGTH _tu_modules _tu_module_count)

            if(_tu_module_count EQUAL 1)
                list(APPEND GEOML_PLUGIN_SOURCES "${CMAKE_SOURCE_DIR}/generated/${_tu_line}.cpp")
            else()
                set(_tu_cpp "${CMAKE_BINARY_DIR}/geoml_plugin_tu_${_tu_index}.cpp")
                set(_tu_content "// AUTO-GENERATED translation-unit file - see CMakeLists.txt. Do not edit or commit.\n")
                set(_tu_depends "")
                foreach(_tu_module ${_tu_modules})
                    set(_tu_module_src "${CMAKE_SOURCE_DIR}/generated/${_tu_module}.cpp")
                    string(APPEND _tu_content "#include \"${_tu_module_src}\"\n")
                    list(APPEND _tu_depends "${_tu_module_src}")
                endforeach()
                file(WRITE "${_tu_cpp}" "${_tu_content}")
                set_source_files_properties("${_tu_cpp}" PROPERTIES OBJECT_DEPENDS "${_tu_depends}")
                list(APPEND GEOML_PLUGIN_SOURCES "${_tu_cpp}")
            endif()

            math(EXPR _tu_index "${_tu_index} + 1")
        endforeach()
    endif()

    add_library(geoml_plugin SHARED ${GEOML_PLUGIN_SOURCES})
    target_link_libraries(geoml_plugin PRIVATE grunk::plugin geoml ${OpenCASCADE_LIBRARIES})
    # geoml's own "internal" headers (geoml/internal/<name>/*.h - see GEOML_DIR's
    # own comment above) use quoted includes that only resolve against geoml's own
    # multi-directory build convention: a bare `#include "geoml_internal.h"` (no
    # path at all) resolves only if geoml/internal itself is on the include search
    # path, and a one-level-qualified `#include <geometry/Point.h>` only if
    # geoml/internal's own PARENT is - confirmed the hard way (real
    # "geoml_internal.h: No such file or directory" / "ListPNamedShape.h: No such
    # file or directory" compile errors otherwise, exactly the same shape as the
    # identical problem already worked around for the *codegen* tool's own single
    # --include-dir limitation - see codegen/config.yml's own header comment). geoml
    # itself gets this for free (its own CMakeLists.txt adds one -I per internal
    # subdirectory, plus the source root itself) - anything consuming
    # GEOML_INSTALL_INTERNAL_API's installed headers from outside geoml's own build
    # tree, like this plugin, has to reconstruct the same search-path shape by hand.
    set(GEOML_INTERNAL_SUBDIRS boolean_operations common contrib exports geometry
        imports logging math system topology)
    # geoml/geoml_internal.h itself (geoml_internal.h, needed by nearly every
    # internal header via a bare quoted include) lives directly under geoml/ -
    # a sibling of geoml/internal/, not inside it - so geoml/ itself needs to be on
    # the search path too, mirroring geoml's own build's `-I$SRC/geoml` entry.
    set(GEOML_INTERNAL_INCLUDE_DIRS "${GEOML_INCLUDE_DIR}/geoml/internal" "${GEOML_INCLUDE_DIR}/geoml")
    foreach(_geoml_internal_subdir ${GEOML_INTERNAL_SUBDIRS})
        list(APPEND GEOML_INTERNAL_INCLUDE_DIRS "${GEOML_INCLUDE_DIR}/geoml/internal/${_geoml_internal_subdir}")
    endforeach()
    target_include_directories(geoml_plugin PRIVATE ${GEOML_INCLUDE_DIR} ${GEOML_INTERNAL_INCLUDE_DIRS} ${OpenCASCADE_INCLUDE_DIR})
    if(GRUNK_GEOML_ENABLE_AD)
        # Activates every generated module's own #ifdef GRUNK_GEOML_WITH_ADOLC dual-
        # emission overload, mirroring grunk-occt's own GRUNK_OCCT_WITH_ADOLC exactly
        # (see occt_ad_support.hpp's own to_real() for the pattern this repo's own
        # geoml_ad_support.hpp, added alongside src/geoml_plugin.cpp, should copy).
        # The actual Standard_Real/Standard_Adouble registration comes from
        # grunk-adolc's own plugin (built above), not from anything here.
        target_compile_definitions(geoml_plugin PRIVATE GRUNK_GEOML_WITH_ADOLC)
        target_link_libraries(geoml_plugin PRIVATE PkgConfig::ADOLC)
    endif()
    target_compile_options(geoml_plugin PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj>)

    if (WIN32)
        add_custom_command(TARGET geoml_plugin POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:geoml_plugin>
                $<TARGET_FILE_DIR:geoml_plugin>
            COMMAND_EXPAND_LISTS
        )
    endif()

    install(TARGETS geoml_plugin
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
else()
    message(STATUS "generated/ and src/geoml_plugin.cpp not present yet - skipping geoml_plugin target (see DESIGN.md; this repository is scaffolding only so far).")
endif()

# --- test harness: same shape as grunk-occt's own (tests/, one gtest file per
# geoml module wrapped) - not added yet (GRUNK_GEOML_TESTS defaults OFF above), so
# this block only ever runs once someone flips it ON after tests/ actually exists.
if(GRUNK_GEOML_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    include(GoogleTest)

    set(GEOML_TEST_SOURCES
        tests/test_load.cpp
        tests/test_recipe.cpp
    )
    if(GRUNK_GEOML_ENABLE_AD)
        list(APPEND GEOML_TEST_SOURCES tests/test_ad.cpp)
    endif()

    add_executable(runUnitTests ${GEOML_TEST_SOURCES})
    target_link_libraries(runUnitTests PRIVATE grunk::plugin grunk::recipe GTest::gtest_main)
    target_compile_definitions(runUnitTests PRIVATE
        GEOML_PLUGIN_SO_PATH="$<TARGET_FILE:geoml_plugin>"
        EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples"
    )
    if(GRUNK_GEOML_ENABLE_AD)
        target_link_libraries(runUnitTests PRIVATE PkgConfig::ADOLC)
        target_compile_definitions(runUnitTests PRIVATE
            GRUNK_GEOML_WITH_ADOLC
            ADTL_PLUGIN_SO_PATH="$<TARGET_FILE:adolc_plugin>"
        )
        add_dependencies(runUnitTests adolc_plugin)
    endif()
    add_dependencies(runUnitTests geoml_plugin)

    if (WIN32)
        add_custom_command(TARGET runUnitTests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:runUnitTests>
                $<TARGET_FILE_DIR:runUnitTests>
            COMMAND_EXPAND_LISTS
        )
    endif()

    gtest_discover_tests(runUnitTests)
endif()
