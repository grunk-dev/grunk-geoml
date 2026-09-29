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

- `load_carrier.grr.yml` - an open-ended rounded rectangular tube (rectangle ->
  extrude -> fillet the 4 long edges -> cut 4 small corner-dimple spheres -> hollow
  into an open shell), positioned with `geoml.translate` (not `geoml.Transform` -
  this recipe never needs the latter). Ported from an old draft PR
  (`grunk-dev/grunk#189`, `examples/load_carrier/load_carrier.grr`), a faithful,
  operation-by-operation port - see the recipe's own top comment for the real
  edge/face indices this build's own OCCT topology numbering needed, re-derived
  rather than copied from the legacy recipe's own (non-portable) index values.
- `wing_loft.grr.yml` - three airfoil-ish section curves (built with
  `geoml.interpolate_points_to_b_spline_curve`), placed span-wise with
  `geoml.Transform`, lofted into one B-spline surface (`geoml.interpolate_curves`)
  and turned into a real face via the *native* `occt.BRepBuilderAPI_MakeFace`
  binding, exported as a `.brep`. Ported from `legacy-geo/tests/test_data/wing.grr`,
  completing every step of the original. The curve list `geoml.interpolate_curves`
  needs is built with the native `occt.Geom_Curve.as_vec(...)`
  (`grunk-occt`'s own `src/geom/geom_extras.hpp`) - `geoml.curve_vec`
  (`../src/curve_vec_extras.hpp`), this recipe's own hand-written escape hatch for
  the same step, is gone now that grunk's `with_std_vector()` supports `Handle(X)`
  element types directly - see `../DESIGN.md`'s "interpolate_curves segfault, real
  root cause found" and its two follow-ups ("fixed upstream and fully landed", and
  the `with_std_vector`/`grunk-dev/grunk-occt#1` one) for the full story (not a
  geoml bug; the `Handle(X)` derived-to-base conversion limitation that also
  affected `BRepBuilderAPI_MakeFace` is fixed for real now, both here and in
  `grunk-occt`, so no escape hatch is needed anywhere in this recipe anymore).
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
escape hatch - the generator has no C++-`operator()`-to-Lua-`__call` mapping) is
called directly (`xf(point)`) in `wing_loft.grr.yml`'s own real recipe
`steps:` block. This used to fail there ("attempt to call a
sol.grunk::Feature&lt;...&gt; value") even though the identical call worked fine in a
plain `grunk::state::create_env()` environment: grunk's own decorated/parametric
environment (which every recipe actually runs in, for dependency tracking) wraps
every value in a `grunk::Feature<object>`, and that wrapping didn't forward Lua's
`__call` metamethod on an arbitrary userdata the way it forwards named
function/method calls. Fixed upstream in `grunk` itself (`DynamicFeature` now
registers `sol::meta_function::call` generically, the same pattern already used
for `+`/`-`/`*`/`/` etc. - see `../DESIGN.md`) - the `apply` colon-call workaround
this README used to document is gone.

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
