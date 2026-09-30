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

Usage:
    python run_example.py /path/to/libgeoml_plugin.so /path/to/libocct_plugin.so
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


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(f"usage: {sys.argv[0]} /path/to/libgeoml_plugin.so /path/to/libocct_plugin.so")
    geoml_plugin_path = sys.argv[1]
    occt_plugin_path = sys.argv[2]

    if not Path(occt_plugin_path).exists():
        sys.exit(
            f"{occt_plugin_path} does not exist - occt_plugin.so is grunk-occt's own "
            "build output, not built by this repo. Build it first: "
            "cd ../grunk-occt && pixi run build"
        )

    state = grunk.state()
    # Load order between the two doesn't matter (same reasoning as grunk-occt's own
    # AD-plugin-loading comment) - both need to be loaded before any recipe using
    # both namespaces is read.
    grunk.plugin.load_native(state, occt_plugin_path)
    grunk.plugin.load_native(state, geoml_plugin_path)

    run_recipe(state, EXAMPLES_DIR / "load_carrier.grr.yml")
    run_recipe(state, EXAMPLES_DIR / "wing_loft.grr.yml")
    run_recipe(state, EXAMPLES_DIR / "airplane_seats.grr.yml")
    run_recipe(state, EXAMPLES_DIR / "gasturbine_cavity_update.grr.yml")


if __name__ == "__main__":
    main()
