<!--
SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>

SPDX-License-Identifier: MPL-2.0
-->

# Examples

Real, runnable `grunk::recipe` YAML files mixing the `occt` and `geoml` plugins -
the whole point of `grunk-geoml` existing alongside `grunk-occt` (`Handle(X)` values,
`gp_*`/`TopoDS_*` types, cross freely between the two, see `../DESIGN.md`'s
"Handle(X) cross-plugin sharing").

## Contents

- `load_carrier.grr.yml` - a box with two cylindrical through-holes, built via
  `geoml.create_box`/`geoml.create_cylinder`, positioned with `geoml.Transform`, and
  combined with `geoml.boolean_subtract`. Ported from an old draft PR
  (`grunk-dev/grunk#189`), simplified - the original also filleted edges, shelled
  the result, and removed two faces, none of which this plugin's own coverage
  supports yet (see `../DESIGN.md`'s "Known generator gaps blacklisted for now").
- `wing_loft.grr.yml` - three airfoil-ish section curves (built with
  `geoml.interpolate_points_to_b_spline_curve`), placed span-wise with
  `geoml.Transform`, exported as a compound of edges. Ported from
  `legacy-geo/tests/test_data/wing.grr`, stopping one step short of the original: the
  final loft-to-surface step (`geoml.interpolate_curves`) segfaults on this plugin's
  own build for any input tried - a real, reproducible bug, most likely a further
  geoml-vs-OCCT-7.9 incompatibility beyond `scripts/geoml-occt79-compat.patch`'s own
  6 fixes, not a binding problem (see this recipe's own top comment and
  `../DESIGN.md`) - worth reporting upstream to DLR-SC/geoml.
- `airplane_seats.grr.yml` - a parametric expression (`seat_z = h_sitting_surface -
  h_cushion`, plain Lua arithmetic on two parameters - already reactive in grunk's
  current API, no `grunk.expression(...)` call needed, see the recipe's own top
  comment) feeding a `geoml.create_box` cushion stand-in. Ported from an old draft
  PR (`grunk-dev/grunk#189`), simplified from the original's raw-OCCT
  compound/edge/fillet construction to a single box - the point is the reactive
  expression, not cushion CAD fidelity. `run_example.py` also demonstrates the
  reactivity directly (changes `h_cushion`, re-reads `seat_z` with no re-read from
  disk).
- `gasturbine_cavity_update.grr.yml` - **recipe composition**: a named, inline
  sub-recipe (`recipes: { cavity: {...} }`) is invoked from the outer `steps:` block
  via `recipes.cavity():get("cavity_shape")` and subtracted from a larger block.
  Ported from an old draft PR (`grunk-dev/grunk#189`), simplified from the
  original's own 2024-era gas-turbine geometry to a plain box standing in for "a
  cavity cross-section" - the point is the composition mechanism, not the
  geometry. See this file's own top comment for the exact Lua syntax (`recipes.<name>()`
  returns a `RecipeCaller`; `.get("<output>")` reads one of its outputs) - grunk's
  own `docs/usage.rst` documents a *different*-looking dot-notation
  (`multiplication.x = a`) for the same underlying mechanism that did not work
  against this project's actual pinned grunk revision (confirmed empirically, not
  assumed - see `../DESIGN.md`); the syntax above is what was actually verified
  working.
- `run_example.py` - a small, self-contained example script using grunk's Python
  bindings, adapted from `grunk-occt`'s own script of the same name - loads *both*
  `occt_plugin` and `geoml_plugin` (unlike grunk-occt's own single-plugin version),
  reads each recipe, and checks its `ok` output (`occt.export_brep`'s own return
  value).
- `geoml_smoke.grr.yml` - **not** one of the examples above; a minimal fixture
  created for `tests/test_recipe.cpp`, not meant to demonstrate anything on its own.

## A real, generic grunk-core limitation found while porting these

`geoml.Transform`'s own `operator()` (`src/transform_extras.hpp`, a hand-written
escape hatch - the generator has no C++-`operator()`-to-Lua-`__call` mapping) works
fine when called directly (`xf(point)`) in a plain `grunk::state::create_env()`
environment, but **not** inside a real recipe's `steps:` block: grunk's own
decorated/parametric environment (which every recipe actually runs in, for
dependency tracking) wraps every value in a `grunk::Feature<object>`, and that
wrapping does not forward Lua's `__call` metamethod on an arbitrary userdata the way
it forwards named function/method calls - "attempt to call a
sol.grunk::Feature&lt;...&gt; value". A real, generic gap in `grunk`'s own dynamic
layer, not fixable from this plugin's own registration code. Worked around by also
registering the exact same overload set under a plain `apply` method
(`xf:apply(point)`, ordinary colon-call syntax, which *does* work in a recipe) - both
examples above use `:apply(...)`, not `(...)`.

## Running the examples

```
cd ../grunk-occt && pixi run build   # occt_plugin.so - not built by this repo
cd ../grunk-geoml && pixi run build  # geoml_plugin.so
pixi run run_examples                # or: python examples/run_example.py build/libgeoml_plugin.so ../grunk-occt/build/libocct_plugin.so
```

Requires grunk's own Python bindings, same as grunk-occt's own examples - see that
repo's `examples/README.md` for how to get them if `import grunk` fails.

Each run writes a `.brep` file (`load_carrier.brep`, `wing_loft.brep`,
`airplane_seats.brep`, `gasturbine_cavity_update.brep`) into the current working
directory - open any of them in a CAD viewer that reads BREP (e.g. FreeCAD) to
inspect the result.
