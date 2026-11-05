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
- `airplane_seats.grr.yml` - a full, faithful, line-by-line re-port of the real
  original (`grunk-dev/grunk#189`'s own `seat_model.grr.yml`/`seat_model.py`): a
  row of airplane seats built from 5 nested sub-`recipes:` (`seat` - itself
  containing `seat_cushion`/`backrest` - `armrest`, `support`), each with its own
  `modules:` helpers for the `BRepFilletAPI_MakeFillet`/`BRep_Builder` sequences
  they need, composed and repeated (`geoml.repeat_shape`) into the final row. See
  the recipe's own top comment for the real fillet edge/face indices (re-verified
  against this build's own OCCT topology, not copied from the legacy recipe's own
  non-portable values) and `../DESIGN.md`'s "airplane_seats: real line-by-line
  re-port" for two real limitations this recipe ran into along the way: `modules:`
  scripts have no access to Lua's base-library helpers like `ipairs`/`table.unpack`
  (`grunk::state` only opens `sol::lib::base` into Lua's real globals table, which a
  module script's own execution environment is never chained to - the helpers here
  use only core `{...}`/`#`/numeric-`for` syntax instead), and why every helper
  function calls `:shape()` on its own `geoml::Shape` result *before* returning it
  (a plain `TopoDS_Shape` needs no `:as(Type)` dance at all downstream - `:as()`
  itself turned out to have a real, separately-documented grunk-core bug of its own).
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
- `ad_translate.grr.yml` - **recipe-level AD check**: a `Standard_Real` seeded via
  `adolc.adouble` (same convention as `grunk-occt`'s own
  `examples/bspline_curve.grr.yml`) is built into an `occt.gp_Pnt`, crosses the
  plugin boundary into `geoml.translate`, and the result's own `occt.gp_Pnt`
  accessors are read back - proving AD values actually propagate through *both*
  plugins at the real recipe/Lua level (`tests/test_ad.cpp` already verifies the
  same function/closed form directly in C++, with no recipe layer involved). Only
  exercises a real derivative against AD-enabled builds of all three plugins
  (`occt_plugin`/`adolc_plugin` from `grunk-occt`'s own `[feature.ad]`,
  `geoml_plugin` from this repo's own `[feature.ad]`); against stock plugins it
  still runs (primal values only - `adolc` is `nil`). See the recipe's own top
  comment for a real, previously-undocumented constraint this surfaced: a
  registered free function's return value (`geoml.translate`, not a
  constructor/member-function call) needs an explicit `:as(Type)` before a native
  method can be called on it in the decorated `steps:` environment - the same
  "no static type information available" limitation `../DESIGN.md`'s "A second,
  genuine limitation" entry already documents for `modules:` function returns,
  now confirmed to apply to plugin-registered free functions too.
- `wing_fuselage_configuration.grr.yml` - the largest recipe here by far: a full,
  faithful re-port of `fuselage_wing_configuration.cpp` (the same original
  `load_carrier.grr.yml`/`wing_loft.grr.yml` already trace back to, but its own
  much bigger fuselage/wing section, deliberately tackled last). Two
  `recipes:` sub-recipes (`fuselage`: mid cylinder -> rear B-spline surface + cap
  -> nose ogive + cap, all hand-built from literal control nets; `main_wing`: 6
  literal airfoil-station profiles -> upper/lower curve-network surfaces + 2 end
  caps), fused into one solid in the outer `steps:`. See the recipe's own top
  comment for the full old-API-to-modern-geoml mapping (several of the original's
  own `geoml::` functions - `b_spline_surface`, `revolving_surface`, `move`,
  `extract_control_point_column_row`, `create_point_net_from_arrays` - no longer
  exist in current geoml at all, confirmed by grepping `.deps/geoml/include/`
  directly) and two new, previously-undocumented recipe-environment quirks found
  while building it: `:as(Type)` corrupting an unrelated lazy value elsewhere in
  the same recipe graph, and a `modules:` function's multiple return values not
  surviving a multi-assignment in the decorated `steps:` environment. **Not run
  under the `-e ad` environment** (`examples/run_example.py` skips it there,
  with a message explaining why): `geoml.nurbs_curve`'s own
  `std::vector<Standard_Real>` weights/knots parameters are not dual-emitted for
  AD the way scalar `Standard_Real` parameters are, and `occt.real_vec` has no
  AD-portable counterpart exposed to Lua - a real, separate gap in grunk-occt's
  own `src/vector_extras.hpp`, not something papered over here.
- `run_example.py` - a small, self-contained example script using grunk's Python
  bindings, adapted from `grunk-occt`'s own script of the same name - loads *both*
  `occt_plugin` and `geoml_plugin` (unlike grunk-occt's own single-plugin version),
  reads each recipe, and checks its `ok` output (`occt.export_brep`'s own return
  value). Takes an optional third argument, `adolc_plugin.so`, to additionally
  enable `ad_translate.grr.yml`'s own derivative check (same
  `has_ad`/finite-difference pattern as `grunk-occt`'s own script).
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
`airplane_seats.brep`, `gasturbine_cavity_update.brep`,
`wing_fuselage_configuration.brep`) into the current working directory - open any
of them in a CAD viewer that reads BREP (e.g. FreeCAD) to inspect the result.
`ad_translate.grr.yml` writes no file (its outputs are plain numbers, not a
shape).

To also run `ad_translate.grr.yml`'s AD derivative check:

```
cd ../grunk-occt && pixi run -e ad build  # occt_plugin.so + adolc_plugin.so
cd ../grunk-geoml && pixi run -e ad build # geoml_plugin.so (AD-enabled)
pixi run -e ad run_examples
```
