#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

"""Minimal example script (see README.md), adapted from grunk-occt's own
examples/run_example.py: loads occt_plugin AND geoml_plugin into one grunk::state
(unlike grunk-occt's own single-plugin script - every recipe here genuinely mixes
occt.*/geoml.* calls, the whole point of this plugin existing alongside grunk-occt),
reads each recipe under examples/, and reads back its "ok" output
(occt.export_brep's own return value). Deliberately skips legacy grunk's
build_recipe/run_recipe pair - these recipes are small enough for one self-contained
script.

Uses grunk's Python bindings (`pip install grunk`, or `pixi run install_python` from
the grunk repo) rather than C++, since most grunk users script recipes from Python.

occt_plugin.so is NOT built by this repo - it's grunk-occt's own build output (a
sibling repo in this workspace, see the workspace root CLAUDE.md). Build it there
first (`cd ../grunk-occt && pixi run build`) if you don't already have it.

An optional third argument, adolc_plugin.so (grunk-adolc's own build output, a
sibling of occt_plugin.so under grunk-occt's own build-ad/_deps/grunk-adolc-build/
- same convention as grunk-occt's own run_example.py), enables the AD derivative
check in ad_translate.grr.yml (see that recipe's own top comment) - without it,
that recipe still runs (every recipe here is meant to work against stock plugins
too) but only its primal values are checked.

Usage:
    python run_example.py /path/to/libgeoml_plugin.so /path/to/libocct_plugin.so [/path/to/libadolc_plugin.so]
"""

import sys
from pathlib import Path

import grunk

EXAMPLES_DIR = Path(__file__).resolve().parent


def run_recipe(state: "grunk.state", recipe_path: Path) -> "grunk.Recipe":
    print(f"Running {recipe_path.name} ...")

    recipe = state.read(str(recipe_path))

    ok = recipe.get_output("ok").value().as_bool()
    print(f"  export_brep {'succeeded' if ok else 'FAILED'}")
    if not ok:
        sys.exit(1)
    return recipe


def run_ad_translate(state: "grunk.state", has_ad: bool) -> None:
    recipe_path = EXAMPLES_DIR / "ad_translate.grr.yml"
    print(f"Running {recipe_path.name} ...")

    recipe = state.read(str(recipe_path))

    mx0 = recipe.get_output("moved_x_value").value().as_float()
    my0 = recipe.get_output("moved_y_value").value().as_float()
    mz0 = recipe.get_output("moved_z_value").value().as_float()
    print(f"  moved point: ({mx0:.6f}, {my0:.6f}, {mz0:.6f})")

    if not has_ad:
        print("  (no libadolc_plugin.so given - skipping derivative check)")
        return

    dx = recipe.get_output("moved_x_deriv").value().as_float()
    dy = recipe.get_output("moved_y_deriv").value().as_float()
    dz = recipe.get_output("moved_z_deriv").value().as_float()
    print(f"  AD d(moved)/d(p_x): ({dx:.6f}, {dy:.6f}, {dz:.6f})")

    # Same finite-difference methodology as grunk-occt's own run_bspline_curve:
    # perturb p_x on the *same* recipe and re-read the primal outputs (the
    # AD-seeded direction above was already read before this mutation, so it isn't
    # affected by it).
    fd_step = 1e-6
    recipe.get_feature("p_x").set_value(1.0 + fd_step)
    mx1 = recipe.get_output("moved_x_value").value().as_float()
    my1 = recipe.get_output("moved_y_value").value().as_float()
    mz1 = recipe.get_output("moved_z_value").value().as_float()
    fd = ((mx1 - mx0) / fd_step, (my1 - my0) / fd_step, (mz1 - mz0) / fd_step)
    print(f"  finite-difference check:    ({fd[0]:.6f}, {fd[1]:.6f}, {fd[2]:.6f})")

    tol = 1e-4
    for axis, ad_value, fd_value in zip("xyz", (dx, dy, dz), fd):
        if abs(ad_value - fd_value) > tol:
            sys.exit(f"  AD/finite-difference mismatch on {axis}: {ad_value} vs {fd_value}")
    print("  AD derivative matches finite differences")


def main() -> None:
    if len(sys.argv) not in (3, 4):
        sys.exit(
            f"usage: {sys.argv[0]} /path/to/libgeoml_plugin.so /path/to/libocct_plugin.so "
            "[/path/to/libadolc_plugin.so]"
        )
    geoml_plugin_path = sys.argv[1]
    occt_plugin_path = sys.argv[2]
    adolc_plugin_path = sys.argv[3] if len(sys.argv) == 4 else None

    if not Path(occt_plugin_path).exists():
        sys.exit(
            f"{occt_plugin_path} does not exist - occt_plugin.so is grunk-occt's own "
            "build output, not built by this repo. Build it first: "
            "cd ../grunk-occt && pixi run build"
        )

    state = grunk.state()
    # Load order doesn't matter between any of these (same reasoning as
    # grunk-occt's own AD-plugin-loading comment) - only that all needed plugins
    # are loaded before any recipe using them is read.
    if adolc_plugin_path is not None:
        grunk.plugin.load_native(state, adolc_plugin_path)
    grunk.plugin.load_native(state, occt_plugin_path)
    grunk.plugin.load_native(state, geoml_plugin_path)

    run_recipe(state, EXAMPLES_DIR / "load_carrier.grr.yml")
    run_recipe(state, EXAMPLES_DIR / "wing_loft.grr.yml")
    run_recipe(state, EXAMPLES_DIR / "airplane_seats.grr.yml")
    run_recipe(state, EXAMPLES_DIR / "gasturbine_cavity_update.grr.yml")
    if adolc_plugin_path is None:
        run_recipe(state, EXAMPLES_DIR / "wing_fuselage_configuration.grr.yml")
    else:
        # geoml.nurbs_curve's own std::vector<Standard_Real> weights/knots
        # parameters (generated/curves.cpp) are NOT dual-emitted for AD the way
        # scalar Standard_Real parameters are (see generated/transformations.cpp's
        # own translate) - they stay std::vector<Standard_Real> in both builds,
        # which under AD means std::vector<Standard_Adouble>. occt.real_vec(...)
        # (src/vector_extras.hpp) always builds a plain std::vector<double>
        # though, with no AD-aware counterpart exposed to Lua - so this recipe's
        # own fuselage sub-recipe (geoml.nurbs_curve(..., occt.real_vec(...), ...)
        # for its profile curves) cannot be called under the AD build at all from
        # Lua. A real, separate gap in grunk-occt's own vector_extras.hpp, not
        # something to work around here - this recipe is a Step 6 port, not part
        # of the AD verification track (ad_translate.grr.yml), so it is simply
        # skipped under AD rather than faked.
        print("Skipping wing_fuselage_configuration.grr.yml under AD (occt.real_vec has no AD-portable counterpart)")
    run_ad_translate(state, has_ad=adolc_plugin_path is not None)


if __name__ == "__main__":
    main()
