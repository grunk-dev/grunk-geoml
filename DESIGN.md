<!--
SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>

SPDX-License-Identifier: MPL-2.0
-->

# Design log

This is a running decision log, same role as [`grunk-occt`'s own `DESIGN.md`](https://github.com/grunk-dev/grunk-occt/blob/main/DESIGN.md)
- record *why*, not just *what*, especially for anything non-obvious or that took real
investigation to get right. Sections are added as decisions get made, not written up
front as a design speculatively.

## Handle(X) cross-plugin sharing - verified, no special machinery needed

`grunk-occt/DESIGN.md`'s own "Cross-plugin Handle(X) sharing" section claims a plugin
cannot safely return/accept `Handle(X)` (`opencascade::handle<X>`) for an OCCT type `X`
it doesn't itself fully `register_type<X, sol::automagic_flags::none>()`, and blames
this on sol2's automagic comparison-operator generation being scoped
per-translation-unit. **This claim does not hold up and does not reproduce.**

A from-scratch read of the actual vendored sol2 3.5.0 source (not just DESIGN.md's own
description of it) found that `automagic_flags` never actually reaches the
comparison-operator gate at all - `constant_automagic_enrollments` hard-codes every
flag `true` regardless of its own template parameter - and the real gate
(`insert_default_registrations`'s `if constexpr (supports_op_less<X>::value)`) is
correct, unconditional SFINAE that doesn't care whether `X` was registered anywhere, by
anyone, in any translation unit. Neither `Standard_Type` nor `BRepTools_History` (the
types named in `grunk-occt/DESIGN.md`'s own reported compile errors) define
`operator<`/`==` anywhere in OCCT, and no plausible false-SFINAE path was found tracing
the real `stack_push.hpp`/`usertype_core.hpp` instantiation chain by hand.

Rather than trust either the original claim or this counter-analysis, it was settled
empirically with two tests built and run directly against `grunk-occt`'s own toolchain,
using `Geom_Circle` (the exact class family from the original bug report):

- **Test 1 (opaque passthrough, single translation unit)**: a function
  returning/accepting `Handle(Geom_Circle)`, with `Geom_Circle` never registered
  anywhere in that TU, compiled cleanly, linked cleanly, and ran correctly for
  passthrough, `c == c` (pointer-identity equality, no `operator==` needed), and
  `tostring(c)`.
- **Test 2 (opaque-then-real, two independent translation units, one `grunk::state`)**:
  Plugin A (opaque producer, never registers `Geom_Circle`) and Plugin B (a
  `grunk-occt` stand-in that fully registers `Geom_Circle` with a real `Radius()`
  method) loaded into one `grunk::state`, in both load orders - a `Handle(Geom_Circle)`
  produced by Plugin A's opaque function correctly dispatched to Plugin B's real
  `:Radius()` method every time, regardless of load order.

Both passed in full. **Conclusion: `grunk-geoml` needs no shared cross-repo
registration infrastructure and no changes to `grunk-occt` for `Handle(X)` interop.**
Its own `codegen/hooks.py` just needs the same, small, local relaxation already applied
to plain (non-`Handle`) unregistered object types: accept `Handle(X)` for any `X`, not
only `X` already registered by this plugin's own run. `grunk-occt/DESIGN.md`'s claimed
mechanism for its own workaround is now known to be incorrect - not corrected in that
file as of this writing, since no code there needs to change as a result (the fix, if
ever made, would be purely informational/cosmetic).

One incidental, unrelated finding from the same spike: calling `pairs()` on any
non-container OCCT userdata (registered or not - confirmed via a control test using a
value fully registered by its own producing "plugin") *unprotected* (no Lua `pcall`) at
the top level of `grunk::environment::eval` throws an uncaught C++ exception that
aborts the host process; wrapped in Lua's own `pcall` it's caught cleanly. This is a
pre-existing, general grunk/sol2-integration robustness gap, not specific to
`Handle(X)` or cross-plugin sharing - out of scope for this plugin, flagged here only
so it isn't rediscovered as if it were new.

## `hooks.py` duplication across OCCT-based plugins is intentional, not deferred debt

`grunk-occt`'s and `grunk-adolc`'s own `codegen/hooks.py` files (the `CodeGenerator`
subclass teaching `grunk`'s shared, library-agnostic `grunk.codegen` generator core
each plugin's own library-specific idioms) already duplicate a fair amount of OCCT
idiom-handling logic (`Standard_Real` AD dual-emission, `Handle(X)`-via-
`Standard_Transient` constructor-mode detection, `NCollection_Array1/2` conversions,
ambiguity-safe overload ordering). `grunk-geoml` needs the same idioms again, since
geoml's own public API uses the exact same OCCT types and conventions throughout - a
third copy.

Decided explicitly (not by default/inertia): **this is correct architecture, not
something to harmonize away by adding a shared OCCT-hooks module inside the `grunk`
repo itself.** `grunk` core is meant to stay entirely library-agnostic - the whole point
of the `hooks.py` customization point is that library-specific idioms live at the
plugin level, not in the shared generator. If cross-plugin sharing of this logic is
ever wanted, it has to happen plugin-to-plugin (e.g. `grunk-geoml` importing
`grunk-occt`'s `hooks.py` directly as a small Python dependency between the two plugin
repos), never by adding OCCT-specific code to `grunk` itself. This overrides an earlier
proposal (considered and rejected during planning) to extract a `grunk.codegen.occt_hooks`
module into the `grunk` repo - noted here so that proposal doesn't get resurrected
without re-litigating why it was rejected.

## Deferred: `wing_fuel_system` example

The `wing_fuel_system` example (from an old grunk draft PR, grunk-dev/grunk#215) is
deferred - its CPACS wingbox-extraction half needs a `tigl-grunk`/`grunk-tigl` plugin
that doesn't exist yet in the current sol2/lua pattern (a separately-planned port, per
the workspace root `CLAUDE.md`'s "Next steps"). Checked before deferring: the example's
own added files contain zero `geo::`/geoml-namespaced calls - everything in them is
`tigl::` or `grocc::` - so deferring it costs nothing in terms of geoml module coverage
today; revisit once `grunk-tigl` exists.

## grunk pin bumped, canonical-spelling fix unlocks more than std::string

`grunk`'s own two real, generic `grunk.codegen` bugs found while building this
plugin's coverage (`Param.kind()`'s canonical-spelling fallback rejecting any class
whose canonical spelling isn't a bare identifier, not just a pointer typedef;
`expand_headers()` collapsing a glob match to its bare basename, breaking nested
header layouts) were fixed directly in `grunk` itself rather than staying as this
plugin's own `hooks.py` workarounds - see grunk PR (branch `codegen-fixes-for-geoml`,
commit `b836a656f97765f0b48f7a527cf977454a830873`). `pixi.toml`/
`scripts/install_grunk.cmake`'s `GRUNK_REV` bumped from `583da89f0184ef6333000c45573da65a9dd551d0`
to that commit (grunk-occt's own pin has *not* been bumped to match - out of scope
here; the two commits differ only in `python/grunk/codegen/`, zero changes anywhere
under `grunk/` itself, confirmed via `git diff --stat`, so there's no ABI hazard from
`occt_plugin.so`/`geoml_plugin.so` being built against different grunk revisions in
the meantime). `codegen/hooks.py` simplified to drop the now-redundant local
`std::string`/generic-`std::vector<T>` handling (core's own new built-ins do this
identically now); kept only the genuinely plugin-specific extension core doesn't
know about - `std::vector<Handle(X)>` (e.g. `interpolate_curve_network`'s curve
list), since OCCT's `Handle(X)` idiom is out of core's own library-agnostic scope.

The canonical-spelling fix turned out to unlock more than just `std::string`: *any*
non-pointer smart-pointer-typedef'd parameter was being incorrectly rejected by the
same over-broad check, not just `std::string`'s own libstdc++ template instantiation
- geoml's own `PNamedShape`/`PLogger` (`std::shared_ptr<X>` typedefs) as
*parameters* (previously only ever accepted as return types, an asymmetry this
fixes) are now correctly accepted too. Regenerating against the properly-pinned
`grunk` went from 528 accepted/38 rejected/14 blacklisted (an interim count from a
`PYTHONPATH`-based test against the *uncommitted* fix, before the real pin bump) to
**516 accepted/38 rejected/17 blacklisted** after resolving what the newly-unlocked
surface area exposed:
- `internal_boolean_operations`/`internal_geometry`: `CBopCommon`/`CCutShape`/
  `CTrimShape`'s new `PNamedShape`/`BOPAlgo_PaveFiller`-taking constructor overloads,
  and `Transformation::operator()(PNamedShape)`, needed `CNamedShape.h`/
  `BOPAlgo_PaveFiller.hxx` added to their modules' own `extra_includes` (ordinary
  incomplete-type completeness, same pattern as every other `extra_includes` entry
  in this file - `PNamedShape`/`BOPAlgo_PaveFiller` were previously rejected
  outright, so nobody had to give them this treatment before).
- `internal_geometry`: three more newly-blacklisted entries, all pre-existing
  categories of real generator gap (not new bug classes) that simply weren't
  reachable before - `BSplineFit::Fit`/`FitOptimal` (return a bare `error`, which is
  actually an `enum` nested inside `class BSplineFit` itself, not reachable via the
  blanket `using namespace geoml;` - the same root cause as the already-documented
  `BSplineAlgorithms::getKinkParameters`/`SurfaceKinks` gap above it in this file)
  and `InterpolatePointsWithKinks` (a deleted move constructor from a non-copyable
  `Cache<...>`/`std::mutex` member, the same category `internal_common`'s own
  `TracePoint`/`DebugScope` entries already document).

Full rebuild (`cmake --build build --parallel`) succeeds cleanly with zero errors.
Re-verified the `nurbs_curve` smoke test (`geoml.nurbs_curve` called with
`occt.gp_Pnt.as_vec(...)`/`occt.real_vec(...)`/`occt.int_vec(...)`-built vectors,
`occt_plugin.so` + `geoml_plugin.so` loaded into one `grunk::state`) still passes.
