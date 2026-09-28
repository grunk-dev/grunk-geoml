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

## Known generator gaps blacklisted for now - revisit if this coverage is ever needed

None of these are geoml-specific idioms - each is a real, narrow `grunk.codegen`
limitation that happened to be exercised first by geoml's own headers. Grouped by
root cause (not by module) since the same underlying gap recurs across several
symbols; a real fix for any of these belongs in `grunk` itself (same treatment
already given the `std::string`/`std::vector<T>`/canonical-spelling bugs above), not
as a `hooks.py` workaround, since none of them are OCCT/geoml-specific.

**1. A nested type used as a return type isn't qualified with its enclosing class.**
`internal_geometry`'s blacklist: `BSplineAlgorithms::getKinkParameters` (one of its
two overloads - the other, `Handle(Geom_BSplineCurve) -> std::vector<double>`, is
fine and is lost too, since a static method's overloads can only be blacklisted
together, not individually), `BSplineFit::Fit`, `BSplineFit::FitOptimal`. In each
case the real return type (`BSplineAlgorithms::SurfaceKinks`, `BSplineFit::error`) is
a type declared *inside* the owning class, not at namespace scope - the generator
emits it as a bare, unqualified name ("'SurfaceKinks'/'error' does not name a type"),
since the blanket `using namespace geoml;` this plugin's own `generated_banner`
injects (see `config.yml`'s own comment) only helps namespace-level lookup, not
class-nested-type scoping, and the generator has no logic at all for "this name is
actually a member of the class this method belongs to." **What a real fix looks
like**: in whatever function renders a callable's return-type text (`emit_return_type`/
the base `emit_callable`'s own return-type handling in `generate.py`), when a return
type isn't a top-level/registered/passthrough type, check whether it names a type
nested inside the callable's own owning class (reachable via libclang's cursor
semantic-parent walk from the method's own cursor) and, if so, qualify it as
`OwningClass::TypeName` before emission. Purely mechanical, library-agnostic, and a
natural sibling fix to the std::string/canonical-spelling bugs already fixed
upstream - a good next candidate if this coverage is ever actually needed (none of
`getKinkParameters`'s `SurfaceKinks` overload, `BSplineFit::Fit`, or
`BSplineFit::FitOptimal` block any currently-planned example).

**2. A class with an implicitly-deleted move/copy constructor still gets a by-value
constructor lambda.** `internal_common`'s blacklist: `TracePoint`, `DebugScope`
(each holds a non-copyable `std::atomic` member). `internal_geometry`'s blacklist:
`InterpolatePointsWithKinks` (holds a `Cache<...>` member wrapping a non-copyable
`std::mutex`). All three have no explicit destructor of their own, so the
generator's `owns_resource_unsafely()` heuristic (which only checks for an explicit
destructor anywhere in the base chain to decide "route to `unique_ptr` ctor mode
instead of by-value") never flags them, and the emitted by-value constructor lambda
fails to compile ("use of deleted function ...(&&)"). **What a real fix looks
like**: extend (or add a sibling check alongside) `owns_resource_unsafely()` to also
route to `unique_ptr` mode when a class's move constructor is implicitly deleted -
libclang exposes `CursorKind.CONSTRUCTOR` cursors with an `is_move_constructor`-style
flag, so the check would be "does this class have a move constructor at all, and is
it deleted/absent" rather than trying to enumerate every possible non-copyable
member type by name.

**3. An abstract class still gets a default/`unique_ptr` constructor attempt.**
`internal_exports`'s blacklist: `CADExporter` (pure-virtual base class, only ever
meant to be subclassed from C++, not constructed directly - not a real coverage loss
for Lua scripting, since the real entry point, `ExporterFactory::Create`, is already
separately rejected for returning `std::unique_ptr<CADExporter>`, a return shape this
generator doesn't support at all). `owns_resource_unsafely()` correctly detects
"needs heap allocation" (real destructor in the chain) but never checks "is this
concrete C++" before emitting `std::make_unique<T>(...)` - "invalid new-expression of
abstract class type". **What a real fix looks like**: check
`cursor.is_abstract_record()` (libclang exposes this) before choosing a ctor mode
at all, and skip constructor emission entirely for an abstract class regardless of
its destructor situation - a genuine, if narrow, gap, but low priority given the
real entry point is blocked on the separate `std::unique_ptr` return-type gap anyway.

**Lower-priority, not yet confirmed against the actual header**: `internal_geometry`'s
`Transformation::Decompose` - the generated zero-arg overload doesn't match any real
overload ("no matching function"), most likely because the real `Decompose()` takes
mutable out-parameters (the same "no Lua-side identity to mutate a bare number/bool
in place" limitation documented throughout grunk-occt's own `config.yml`) - worth
actually reading the header before assuming that's the cause.

## Step 5: test harness ported, real `pixi run` workflow validated for the first time

Everything up to this point had only ever been built/tested via manual `cmake`/env-var
invocations reusing `grunk-occt`'s own pixi environment directly - this plugin's own
`pixi.toml` tasks (`configure`/`build`/`test`/`test_hooks`) had never actually been run.
Doing so for real (`pixi install`, then `pixi run -e default configure && ... build &&
... test`) surfaced a real, substantial gap: geoml's pinned commit (written against
OCCT 7.6.2, see its own `conanfile.py`) doesn't build against OCCT 7.9 (what this
project's own pixi environment provides) at all, unpatched. This had gone unnoticed
because earlier manual verification (Step 2 onward) reused a `.deps/geoml` checkout
that had been hand-patched once, ad hoc, and never captured anywhere - `scripts/
install_geoml.cmake` itself (what `pixi run configure` actually invokes, cloning
fresh into `build/_deps/geoml`) never had these fixes.

Fixed properly this time: a real, versioned `scripts/geoml-occt79-compat.patch`,
applied automatically (idempotently - checked via `git apply --reverse --check`) by
`install_geoml.cmake` right after checkout. Six fixes, all genuine OCCT-7.9 API drift
or a pre-existing geoml install-rule gap, none specific to
`-DGEOML_INSTALL_INTERNAL_API=ON` or to this plugin's own usage:
- `TopLoc_Location::HashCode(int)` and `TopoDS_Shape::HashCode(int)` were both removed
  in OCCT 7.9 (`Shape.cpp`, `StepReader.cpp`×2) - the former has a no-arg replacement,
  the latter's replaced with `std::hash<TopoDS_Shape>`.
- `math_Vector` became a type alias (`using math_Vector = math_VectorBase<double>;`)
  rather than a real class in OCCT 7.9 - broke an elaborated `class math_Vector`
  forward-declaration in `BSplineFit.h` (removed the `class` keyword) and two files
  that relied on it being transitively included via `math_Matrix.hxx` and never
  included `math_Vector.hxx` directly (`BSplineFit.cpp`, `math/Matrix.h`).
- `Standard_values.h` (an old M_PI-providing header) was removed outright -
  `CommonFunctions.h` replaced with `<cmath>`.
- geoml's own `install(DIRECTORY geoml/ ... FILES_MATCHING PATTERN "*.h")` never
  matched `*.hpp`, silently dropping `boolean_ops/modeling.hpp`/
  `primitives/modeling.hpp` - same class of gap grunk-occt's own `config.yml`
  documents for `BRepBuilderAPI.hxx`.

None of this is a `grunk`/`grunk.codegen` bug (unlike the two fixed upstream earlier)
- it's geoml's own source needing a compatibility patch for a newer OCCT, the same
role `examples/cpp/cad_autodiff`'s own `patches/geoml-feature-autodiff-occt79.patch`
plays for the AD variant. Worth reporting upstream to DLR-SC/geoml directly - not yet
done.

With that patch in place, `pixi run -e default configure && build && test` and
`pixi run -e default test_hooks` all pass cleanly from a completely fresh clone (no
manually-patched checkouts, no reused `.deps/` from earlier sessions). Test harness:
`tests/test_load.cpp` plus one real `tests/test_<module>.cpp` per *public* module
(`surfaces`/`curves`/`utilities` - wait, no dedicated `utilities` test, see below -
`boolean_ops`/`geom_topo`/`naming_choosing`/`predicates`/`primitives`/
`transformations`), `tests/test_recipe.cpp` (reads a new, minimal
`examples/geoml_smoke.grr.yml` fixture - not one of the plan's own real examples,
Step 6 - purely to exercise the recipe layer), and `tests/python/test_hooks.py`
(mirrors grunk-occt's own structure, plus new tests for this plugin's own
`Handle(X)`/`std::vector<Handle(X)>` extension and the `std::vector<T>` core-fallback
composition). No dedicated tests for `data_structures` (its own module registers
nothing at all - every symbol in `Array2d.h`/`conversions.h` was rejected) or
`utilities` (its own two real functions both need a pre-built
`Handle(Geom_BSplineSurface)` - disproportionate fixture setup for now, deferred to
Step 6's real examples). This test suite deliberately never loads `occt_plugin.so`
(no build-time dependency on `grunk-occt` for its own tests) - OCCT values (`gp_Pnt`,
`Handle(X)`, enums, ...) are constructed directly in C++ and pushed into Lua, not
built via `occt.*.new(...)` calls; a real consequence of this, found while writing
`tests/test_curves.cpp`, is that a `Handle(Geom_BSplineCurve)` returned by a geoml
function has no *methods* callable on it in this test suite specifically (no plugin
registers `Geom_BSplineCurve` here) - these tests only confirm construction succeeds,
not the resulting curve's own geometric properties (already covered by grunk-occt's
own equivalent tests, with `occt_plugin.so` loaded there).

`GRUNK_GEOML_TESTS` flipped to default `ON`, matching grunk-occt's own convention.
