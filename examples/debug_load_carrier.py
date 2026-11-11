#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

"""TEMPORARY diagnostic for the "sol: no matching function call takes this
number of arguments and the specified types" failure seen running
load_carrier.grr.yml on macos-latest/windows-latest CI (ubuntu-latest is
unaffected, same generated code, so this is a per-compiler sol2 overload-
resolution divergence, not a modeling bug - see DESIGN.md's own "sol::overload
... tries each candidate in declaration order" entry for a related, previously
found class of this problem). The real error gives no file/line info at all
("stack traceback: [C]: in ?"), so this queries every intermediate value the
recipe's own `steps:` computes, in the same order those steps compute them,
and stops at the first one that raises - since grunk's recipe evaluation is a
lazy DAG, querying any output forces everything upstream of it to evaluate
too, so the first name this script can't successfully query is exactly the
first step that actually fails.

Not meant to stay in the repo long-term: once the failing call is identified
and fixed, delete this file, drop the matching entries from
load_carrier.grr.yml's own `outputs:` map (added only so this script could
query them), and remove the `debug_load_carrier` pixi tasks/CI step that run
it.

Usage: python debug_load_carrier.py /path/to/libgeoml_plugin.so /path/to/libocct_plugin.so
"""

import sys
from pathlib import Path

import grunk

EXAMPLES_DIR = Path(__file__).resolve().parent

# Same order load_carrier.grr.yml's own `steps:` computes these in.
OUTPUTS_IN_ORDER = [
    "c1", "vec_y", "vec_z", "extr_dir",
    "c2", "c3", "c4",
    "srf", "extr_vec", "extruded",
    "center_1", "center_2", "center_3", "center_4",
    "sphere1", "sphere2", "sphere3", "sphere4",
    "sphere_compound",
    "rounded_box",
    "rounded_box_wrapped", "spheres_wrapped",
    "cut_shape", "cut_topods",
    "face1", "face2",
    "shell_prelim",
    "load_carrier",
    "ok", "n_faces",
]


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(f"usage: {sys.argv[0]} /path/to/libgeoml_plugin.so /path/to/libocct_plugin.so")
    geoml_plugin_path, occt_plugin_path = sys.argv[1], sys.argv[2]

    state = grunk.state()
    grunk.plugin.load_native(state, occt_plugin_path)
    grunk.plugin.load_native(state, geoml_plugin_path)

    recipe = state.read(str(EXAMPLES_DIR / "load_carrier.grr.yml"))

    for name in OUTPUTS_IN_ORDER:
        print(f"Evaluating output '{name}' ...", flush=True)
        try:
            recipe.get_output(name).value()
        except Exception as exc:
            print(f"FIRST FAILURE at output '{name}': {exc}")
            sys.exit(1)
        print("  ok", flush=True)

    print("All intermediate outputs evaluated successfully (the failure is not deterministically "
          "reachable this way - may depend on something not captured by querying outputs in this order).")


if __name__ == "__main__":
    main()
