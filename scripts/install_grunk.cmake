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
# c0cc16d83437811b0868ea78a04dbeafeaa0f57e - lands two real, generic grunk fixes,
# both previously carried as this plugin's own local workarounds:
#   - Param.kind()'s over-broad canonical-spelling rejection breaking
#     std::string/std::vector<T> (see DESIGN.md's "Handle(X) cross-plugin
#     sharing"/two-bugs-fixed-upstream entries) - landed in an earlier bump to
#     b836a656f97765f0b48f7a527cf977454a830873.
#   - DynamicFeature never registered sol::meta_function::call, so calling a
#     Feature-wrapped value as obj(...) (e.g. geoml.Transform's own operator())
#     failed inside a real recipe's decorated/parametric environment with
#     "attempt to call a sol.grunk::Feature<...> value", even though the exact
#     same call worked fine in a plain, undecorated script (see this repo's own
#     src/transform_extras.hpp, whose "apply" method workaround this bump lets
#     Step 4 of the plan remove). Fixed generically (the same
#     make_dynamic_action pattern already used for +/-/*// etc.), not
#     Transform-specific - any Feature-wrapped type with its own bound
#     operator() now works the same way. Landed in c0cc16d, with a new
#     permanent grunk test (operators.usertype_call_lua).
# Bumped again 2026-09-29, past grunk-occt's own now-matching pin, to
# d9b079905d4eccaa08d51265ab2f6ef6b76786cd - lands grunk.codegen's new
# sol_base_classes.hpp generation (SOL_BASE_CLASSES/SOL_DERIVED_CLASSES,
# aggregated across a whole run), the missing piece (alongside this plugin's
# own src/geoml_sol_traits.hpp rebind_actual_type member and codegen/hooks.py's
# by-value emit_param() override, both already in place) that lets sol2's own
# Handle(Derived)->Handle(Base) unique-usertype conversion work generically,
# once this plugin's own bindings are regenerated - see DESIGN.md's "The real
# root cause, fixed upstream" entry, and grunk-occt/DESIGN.md's own longer
# writeup for the full three-part mechanism. grunk-occt's own pin now matches
# (bumped to the same commit) - the two plugins build against the same grunk
# revision again.
# Bumped again 2026-09-29, to 13ab6232fdcd7b7e20a916392cfbb607417fb143 - lands
# a fix to with_std_vector()'s own cast_to_element (was taking its element by
# reference, which sol2's base-aware unique-usertype checker excludes from any
# Derived->Base conversion attempt at all - the same reference-exclusion this
# project already worked around for plain Handle(X) parameters). This is what
# lets src/curve_vec_extras.hpp's hand-written escape hatch be replaced by the
# native occt.Geom_Curve.as_vec(...) - see DESIGN.md.
#
# Optional -D GRUNK_INSTALL_NAME=<name>: an alternate install-directory name under
# .deps/ (e.g. "grunk-ad" for [feature.ad.tasks]'s own invocation) - defaults to
# "grunk", same convention as grunk-occt.

cmake_minimum_required(VERSION 3.15)

if(NOT DEFINED GRUNK_INSTALL_NAME)
    set(GRUNK_INSTALL_NAME "grunk")
endif()
set(GRUNK_REV "13ab6232fdcd7b7e20a916392cfbb607417fb143")

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
