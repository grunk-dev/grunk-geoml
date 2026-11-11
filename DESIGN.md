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

## Step 6 (partial): `load_carrier`/`wing_loft` examples, and a real grunk-core recipe/`__call` gap

Ported the two simplest examples from the plan's priority list -
`examples/load_carrier.grr.yml` (a box with two cylindrical through-holes -
`geoml.create_box`/`create_cylinder`, `geoml.Transform`, `geoml.boolean_subtract`,
simplified from the original draft PR since this plugin has no fillet/shell/
face-removal coverage yet) and `examples/wing_loft.grr.yml` (three placed airfoil-ish
section curves exported as a compound of edges, stopping one step short of the
legacy `wing.grr`'s own final loft-to-surface step - see below). Both run
successfully end to end via `pixi run run_examples`, producing real, non-trivial
`.brep` output, with `occt_plugin.so`/`geoml_plugin.so` loaded together.

**A real, generic grunk-core limitation found porting `wing_loft`**: `Transform`'s
`operator()` (verified working via a plain `create_env()` script when its escape
hatch was added earlier) fails inside an actual recipe's `steps:` block - "attempt
to call a sol.grunk::Feature<...> value". Root cause: grunk's own decorated/
parametric environment (every recipe runs in this one, for dependency tracking)
wraps every value in a `grunk::Feature<object>`, and that wrapping forwards named
function/method calls but not Lua's `__call` metamethod on an arbitrary userdata.
Not fixable from this plugin's own registration code - worked around by also
registering the same overload set under a plain `apply` method
(`src/transform_extras.hpp`), so recipes use `xf:apply(point)` (ordinary colon-call,
which does work) instead of `xf(point)`. `sol::meta_function::call` itself is left
in place too - still genuinely useful outside a recipe's own decorated environment
(a plain script, or this plugin's own `tests/test_transformations.cpp`). Worth
reporting upstream to `grunk` itself at some point - out of scope to fix here (would
mean changing `grunk::dynamic`'s own `ActionDynamic`/decorated-environment wrapping,
not this plugin).

**FIXED upstream in `grunk` itself, pin bumped, workaround removed (2026-09-28).**
Root-caused precisely (not just worked around): `DynamicFeature`'s own usertype
(`grunk/dynamic/state.hpp`) already forwards `+`/`-`/`*`/`/`/`%`/`^`/unary-minus
to the wrapped raw value via a generic `make_dynamic_action` pattern (a small
Lua closure doing the raw operation, wrapped so the call becomes a properly
dependency-tracked parametric Action), but never registered
`sol::meta_function::call` at all - so a Feature-wrapped callable had no `__call`
metamethod to dispatch to, genuinely and simply missing, not some deeper
decorated-environment design limitation. Fixed with the exact same pattern
already used for the arithmetic operators: `grunk.__dynamic_call(self, ...)`
(`return self(...)`) wrapped via `make_dynamic_action` and registered as
`Feature`'s own `sol::meta_function::call`. Fully generic - works for *any*
Feature-wrapped type with its own bound `operator()`, not just `Transform`.
Verified two ways before landing: a new permanent grunk test
(`operators.usertype_call_lua`, `tests/cpp/dynamic_operators.cpp`) confirms both
that the call itself succeeds and that the result is genuinely
dependency-tracked (changing the callee's state re-derives the result, not a
one-shot eager call); a live spike against the real `geoml.Transform` through
`state.read()` (the actual decorated recipe environment, not just `env.eval()`)
confirmed `xf(p)` now works exactly like `xf:apply(p)` used to. Landed as grunk
commit `c0cc16d83437811b0868ea78a04dbeafeaa0f57e`. `scripts/install_grunk.cmake`/
`pixi.toml`'s `GRUNK_REV` bumped to it (past the `b836a656f97765f0b48f7a527cf977454a830873`
pin from the "grunk pin bumped" entry above), `pixi.lock` regenerated
(`pixi lock`), `.deps/grunk` reinstalled and the whole project rebuilt clean
(22/22 C++ tests pass) against the new pin. `src/transform_extras.hpp`'s own
`apply` method (the ductape workaround) removed - `sol::meta_function::call` is
now the only registration, and `examples/wing_loft.grr.yml` (the one example
that actually calls `Transform::operator()`) updated to call `xf(p)` directly;
re-verified end-to-end via the real recipe (`ok=1`, real `.brep` export).

**`interpolate_curves` segfault - REVISED, real root cause found and confirmed
(2026-09-28), correcting the entry below verbatim below it (kept for history -
its "ruled out as a binding problem" conclusion was wrong).** This is a
`grunk-geoml` binding bug, not a geoml algorithm bug - nothing here needs to be
fixed in DLR-SC/geoml itself.

Forensic method: a pure C++ program (no Lua/grunk at all) built against a real
`-O0 -g` debug build of the exact same pinned geoml commit (patched with
`scripts/geoml-occt79-compat.patch`), calling `geoml::interpolate_curves` directly
with the exact three section curves `wing_loft.grr.yml` builds (three 4-point
"airfoil-ish" stations, each through `interpolate_points_to_b_spline_curve`) -
**this does not crash and returns a valid, non-null surface.** The same curves,
built the same way but through the real Lua/grunk plugin path (`geoml.curve_vec(...)`
then `geoml.interpolate_curves(...)`, exactly as `wing_loft.grr.yml` calls them),
**does crash**, every time. This alone falsifies the "geoml's own algorithm"
theory - the crash is specific to something that happens only when the curves
cross the Lua binding boundary.

`gdb` on the crashing Lua path (plugin `.so` built unstripped, so real symbols)
gives a precise, unambiguous backtrace: the actual fault is a null-pointer
dereference (`rdi=0x0`, i.e. `this=nullptr`) inside `Standard_Transient::IsKind()`,
called from `GeomConvert::CurveToBSplineCurve(Handle(Geom_Curve) const&, ...)`,
called from `geoml::CurvesToSurface::CurvesToSurface(std::vector<Handle(Geom_Curve)>
const&, bool)`, called from `geoml::interpolate_curves`. I.e.: `interpolate_curves`
is being handed at least one **null** `Handle(Geom_Curve)` inside its input vector,
and crashes exactly the way any code would when told a curve exists but its handle
is actually empty - `interpolate_curves`'s own algorithm is not at fault, it's
being lied to by its caller.

Traced the null handle to its exact source: `geoml.curve_vec(...)`
(`src/curve_vec_extras.hpp`) builds its `std::vector<Handle(Geom_Curve)>` by calling
`a.get<opencascade::handle<Geom_Curve>>()` on each `sol::variadic_args` element `a`
at runtime. **Confirmed directly (temporary diagnostic prints, since reverted -
see `src/curve_vec_extras.hpp`'s own comment) that this call silently returns a
NULL handle whenever `a`'s actual registered type is a *derived* `Geom_Curve`
subtype (`Geom_BSplineCurve`, the only kind `interpolate_points_to_b_spline_curve`
ever actually produces - `Geom_Curve` itself is abstract, so a literal
`Handle(Geom_Curve)` value can never occur in practice) - no exception, no error,
just a quietly wrong result.** Also confirmed, as a direct A/B comparison: the
exact same value, passed as a real, statically-typed
`opencascade::handle<Geom_Curve> const&` **function parameter** (not extracted at
runtime via `.get<T>()`/`sol::stack::get<T>()` from a generic `sol::object`), does
correctly perform the `Handle(Geom_BSplineCurve)` -> `Handle(Geom_Curve)` upcast
(non-null, correct pointer) - i.e. sol2's ordinary, statically-bound
function-argument conversion path handles this derived-to-base `Handle(X)` case
correctly; the generic, runtime `sol::object::get<T>()`/`sol::stack::get<T>()`
path used inside a `sol::variadic_args` loop does not. This directly contradicts
this entry's own prior claim ("a single `Handle(Geom_BSplineCurve)`-for-
`Handle(Geom_Curve)` argument already works fine, confirmed via `geoml.CurveToEdge`")
- that claim is true only because `CurveToEdge` receives its argument as a real
typed function parameter, not through `curve_vec`'s own runtime-extraction path;
the two are not the same mechanism, and the difference matters.

**FIXED and verified end-to-end (2026-09-28), after several dead ends worth
recording.** Chasing "the right fix direction" from the paragraph above (route
every `curve_vec` argument through a genuine sol2 function call) turned out to
be a red herring in its specific formulation. What was actually tried, in
order, each confirmed insufficient by direct rebuild-and-rerun before moving to
the next:

1. `sol::stack::get<T>()` instead of `sol::object::get<T>()` - same result, still
   null.
2. A `sol::overload` set of fixed-arity functions (`curve_vec_arity_1..12`, each
   with N real `Handle(Geom_Curve)` parameters) - failed differently: sol2's own
   *overload-resolution type-checking* rejected every call ("no matching function
   call"), meaning the derived-to-base mismatch breaks the overload-candidate
   check too, not just extraction.
3. A non-overloaded `curve_vec_arity_N` (still N `Handle(Geom_Curve)` parameters,
   no `sol::overload` involved) - compiled and matched, but *segfaulted inside
   sol2 itself* (`sol::stack::qualified_getter<opencascade::handle<Geom_Curve>,
   void>::get`), called while binding the 2nd/3rd argument. Real crash, not just
   wrong data - `qualified_getter` (sol2's path for a signature with more than
   one parameter) is broken for this case in a different, worse way than the
   single-parameter getters above.
4. A pure-Lua dispatcher (`curve_vec_begin()`/`curve_vec_add(one curve)`/
   `curve_vec_end()`, each a genuine single-parameter C++ function, invoked from
   a small Lua closure built once via `lua.script(...)`) - still null, even
   though every individual function had exactly one parameter. This falsified
   the "must be single-parameter" theory from the paragraph above: a direct,
   single-argument call to `diag_curve_is_null(mid_curve)` typed as literal Lua
   source in the *recipe's own script chunk* returned a correct, non-null
   result on the very same curve value that `curve_vec_add` (same signature)
   returned null for when called from a *separately pre-compiled* Lua chunk
   (this file's own dispatcher closure, built via a different `lua.script(...)`
   call at plugin-registration time). Never fully root-caused why - deep sol2
   internals, not practical to chase further given the fix below sidesteps it
   entirely.

**The actual fix**: stop asking sol2 to produce a `Handle(Geom_Curve)` (the
*abstract* base) from anything, ever - request the *exact* concrete type
instead (`Handle(Geom_BSplineCurve)`, confirmed by inspection to be the only
concrete `Geom_Curve` subtype geoml's own public curve API ever returns), which
needs no sol2-side upcast at all, and perform the base-class widening in plain
C++ afterward. This is where the true, deeper mechanism finally surfaced (see
`opencascade::handle<T>`'s own `Standard_Handle.hxx`): this OCCT build (no
`OCCT_HANDLE_NOCAST`) only defines the derived-to-base upcast as an **implicit
reference-conversion operator** (`operator const handle<Base>&() const`), not a
value-constructing converting constructor - so `Handle(Geom_Surface) x = derived;`
(materializing a *new* base-typed object) doesn't even compile in plain C++,
while passing `derived` directly to something taking `const Handle(Base)&`
works fine (binds via the operator, then an ordinary same-type copy happens
downstream, e.g. inside `std::vector::push_back`). Every sol2 failure mode
above is a variant of sol2 trying to *materialize* a new `Handle(Geom_Curve)`
value somewhere in its own generic machinery (a getter's return value, an
overload candidate, a multi-parameter binder) - which was never going to work
in this OCCT build regardless of which sol2 code path was used, hence the long
string of "different failure, same root shape" results above. `curve_vec`'s
final, working form: `sol::variadic_args`, each element extracted via
`a.as<opencascade::handle<Geom_BSplineCurve>>()` (exact type, no upcast at the
sol2 boundary), then `v.push_back(bspline)` into a
`std::vector<opencascade::handle<Geom_Curve>>` (plain C++ reference-binding
push, not a value conversion) - see `src/curve_vec_extras.hpp`'s own header
comment for the full writeup kept alongside the code.

**Same fix, same root cause, found again one step later**: getting from the
resulting `Handle(Geom_BSplineSurface)` to a `TopoDS_Face` needs
`BRepBuilderAPI_MakeFace`, whose generated constructor overload set
(grunk-occt's own `generated/brepbuilderapi.cpp`) only takes the abstract
`Handle(Geom_Surface)` - calling it with a `Handle(Geom_BSplineSurface)` hit the
exact same "no matching function call" overload-resolution failure as attempt
(2) above, for the identical reason. Not fixable in grunk-geoml's own
`wing_loft.grr.yml` (the overload set lives in grunk-occt, a different repo,
out of scope to change here) - worked around the same way, locally: a new
`src/surface_face_extras.hpp`/`geoml.bspline_surface_to_face(surf, tolerance)`
takes `Handle(Geom_BSplineSurface)` directly and passes it straight through to
`BRepBuilderAPI_MakeFace(surf, tolerance)` (reference-binding, no
materialization - the exact same discipline as `curve_vec`'s own fix).

**Verified fully end-to-end**, both via a standalone spike and via the real
`wing_loft.grr.yml` recipe run through `state.read()` (the actual decorated/
recipe environment, not just `env.eval()`): `geoml.interpolate_curves` returns
a genuinely valid, non-null `Handle(Geom_BSplineSurface)` (`NbUPoles=4
NbVPoles=3`, matching the 4 control points x 3 section curves), and
`geoml.bspline_surface_to_face` turns it into a real `TopoDS_Face` that exports
to a valid, non-trivial `.brep` file. `wing_loft.grr.yml` now completes every
step of the original legacy recipe (interpolate -> transform x3 -> loft ->
face -> export), not just the first three.

## The real root cause, fixed upstream and fully landed (2026-09-28/29)

The two workarounds above are exactly that - workarounds. The user (also
`grunk-occt`'s own maintainer) asked to fix the underlying bug in `grunk-occt`
directly instead of re-deriving a local escape hatch every time it bites a
different function. That investigation found the real, precise, three-part
mechanism (sol2's own `unique_usertype_traits::rebind_actual_type` being
required at all for base-class-aware conversion to even attempt; the
compile-time-only `SOL_BASE_CLASSES`/`SOL_DERIVED_CLASSES` macro pair being a
*separate* mechanism from the runtime `.add_bases<...>()` call this generator
already emits; and sol2's own base-aware code path unconditionally excluding
*reference* parameters, so every `Handle(X) const&` parameter this generator
ever emits needs to become a plain by-value `opencascade::handle<X>` instead) -
see `../grunk-occt/DESIGN.md`'s own "`Handle(Derived)`->`Handle(Base)` argument
conversion: real root cause and fix" for the full writeup, verified there via
a real `grunk codegen` regeneration + full rebuild + all 50 of that repo's own
tests.

This plugin's own `src/geoml_sol_traits.hpp` (the `rebind_actual_type` fix) and
`codegen/hooks.py` (the by-value `emit_param()` override) carry the identical
fixes, verified working here too via the same kind of live spike (a hand-built
`sol_base_classes.hpp` + manually by-value-patched `generated/curves.cpp`/
`generated/surfaces.cpp`, confirming `geoml.interpolate_curves`'s own real
`Handle(Geom_Curve)` parameter and `occt.BRepBuilderAPI_MakeFace`'s
`Handle(Geom_Surface)` parameter both correctly accept a
`Handle(Geom_BSplineCurve)`/`Handle(Geom_BSplineSurface)` argument with *zero*
plugin-level escape hatches, end to end).

**Regenerated for real (2026-09-29), after two more real gaps found and fixed.**
The initial `pixi run generate_code` attempt against this plugin's own real
`config.yml`/geoml headers hit a short chain of missing `#include`s inside
geoml's own internal headers - `geoml/internal/common/sorting.h` uses
`std::invalid_argument` without including `<stdexcept>`, and
`geoml/internal/geometry/IntersectionPoint.h` declares a `gp_Pnt`-holding
struct without including `<gp_Pnt.hxx>`. Both are genuinely **unrelated to this
whole Handle(X) investigation** - real, pre-existing geoml source-hygiene bugs
(a full build transitively pulls the missing standard/OCCT headers in via some
other path and never notices; only libclang's own strict, isolated per-header
parsing exposes them) - now patched in `scripts/geoml-occt79-compat.patch`
(the same, existing OCCT-7.9-compat patch this plugin already applies to its
own pinned geoml checkout) and `scripts/install_geoml.cmake`'s own comment
updated accordingly. Still worth reporting upstream to DLR-SC/geoml directly.

**A second, real gap found only once regeneration actually ran**: the newly
regenerated `generated/sol_base_classes.hpp` came back with *zero*
`Geom_Curve`/`Geom_Surface`-family entries at all, so the original failure
(`BRepBuilderAPI_MakeFace`'s `Handle(Geom_Surface)` parameter still rejecting a
`Handle(Geom_BSplineSurface)` argument) persisted even after a clean
regeneration. Root cause: `grunk.codegen`'s new `sol_base_classes.hpp`
generation (see `../grunk-occt/DESIGN.md`) only ever covers classes *this run's
own* codegen discovers - i.e. classes actually *defined* somewhere under this
plugin's own `--include-dir`. `Geom_Curve`/`Geom_Surface` and their concrete
subtypes are OCCT types this plugin only ever *references* (as return types of
`interpolate_points_to_b_spline_curve`/`interpolate_curves`/etc.) - they're
defined entirely under `grunk-occt`'s own, completely separate
`--include-dir`/codegen run, which this plugin's own run never sees (and vice
versa - `grunk-occt`'s own generated `sol_base_classes.hpp` has no way to reach
this plugin's own, separately-compiled `.so` either). Per this plugin's own
"Handle(X) cross-plugin sharing" finding above, that's normally fine (sol2's
ordinary Handle(X) push/pull needs no shared registration at all) - but
`SOL_BASE_CLASSES`/`SOL_DERIVED_CLASSES` are explicit, compile-time-only
template specializations that must be visible in *this* plugin's own
translation units specifically. Fixed by hand-adding the four macro
invocations this plugin actually needs directly to `src/geoml_sol_traits.hpp`
(already included by every generated module via this project's own custom
banner) - safe for the same reason `unique_usertype_traits` above already is:
a self-contained template specialization, this plugin's own copy only ever
instantiated within its own translation units, no ODR conflict with
`grunk-occt`'s own separately-compiled copy of the same declarations.

**Verified fully working end to end** after both fixes: `occt.
BRepBuilderAPI_MakeFace.new(surf, tol)` now accepts a real
`Handle(Geom_BSplineSurface)` natively, with *zero* plugin-level escape
hatches - `src/surface_face_extras.hpp`/`geoml.bspline_surface_to_face(...)`
removed entirely, `examples/wing_loft.grr.yml` updated to call
`occt.BRepBuilderAPI_MakeFace.new(loft_surface, face_tolerance):Face()`
directly. Re-ran the real recipe path for both examples: `load_carrier.grr.yml`
(`ok=1 n_faces=23`) and `wing_loft.grr.yml` (`ok=1`), plus the full existing
22/22 `runUnitTests` and 18/18 `test_hooks.py` - all green.

**`curve_vec_extras.hpp` stayed for a while, unchanged, deliberately** - it
solved a different, unrelated problem (building a `std::vector<Handle(Geom_Curve)>`
from individual Lua arguments at all, since sol2 has no plain-table-to-vector
conversion for a by-reference parameter) that this fix did not touch;
`interpolate_curves`'s own `std::vector<Handle(Geom_Curve)>` parameter is a
container of handles, not a bare `Handle(X)`, so the by-value `emit_param()`
fix (which only ever applies to a bare `"handle"`-kind parameter) never
reached it. Removed for real once `with_std_vector()` itself gained
`Handle(X)` support - see "`curve_vec_extras.hpp` removed" below.

## `curve_vec_extras.hpp` removed - `grunk`'s `with_std_vector()` gains `Handle(X)` support (2026-09-29)

A background investigation (prompted by "the `curve_vec_extras.hpp` ductape
could maybe be a generic grunk/sol2 feature instead") found grunk's own
`with_std_vector<VecElement>()` (`grunk/dynamic/usertype_proxy.hpp`, already
used by `grunk-occt`'s `occt.gp_Pnt.as_vec(...)`) was *already designed* for
exactly this - the `VecElement` template parameter exists specifically so a
usertype's own `.as_vec(...)` can build `std::vector<Handle<T>>` instead of
`std::vector<T>`, accepting any registered subtype via sol2's inheritance
machinery. It had never actually worked for a unique-usertype element type,
though (an earlier attempt to use it this way had been abandoned with an
unexplained "unrecognized userdata" error). Root-caused to two bugs, both
fixed in `grunk` (commit `13ab6232fdcd7b7e20a916392cfbb607417fb143`,
merged/pushed by the user):

1. `with_std_vector`'s internal `cast_to_element` took `VecElement const&` - a
   *reference* - which sol2's own base-aware unique-usertype
   checker/getter excludes outright (`!std::is_reference_v<X>`), the identical
   constraint this project's own `emit_param()` fix above already had to work
   around for plain `Handle(X)` function parameters. Fixed to take
   `VecElement` by value.
2. Whichever translation unit instantiates `with_std_vector<Handle<T>>()`
   needs `T`'s own `SOL_BASE_CLASSES`/`SOL_DERIVED_CLASSES` visible via
   `#include` at that point, same as every other Handle(X) push/pull in this
   whole investigation - an unspecialized primary template silently means "no
   known bases" with no compile error, independent of what `add_bases<...>()`
   registers at runtime.

This plugin's own `GRUNK_REV` pin bumped to the same commit. `grunk-occt`
opted `Geom_Curve` into it directly
(`ns.modify_type<Geom_Curve>("Geom_Curve").with_std_vector<opencascade::handle<Geom_Curve>>()`,
`grunk-occt/src/geom/geom_extras.hpp`) - giving `occt.Geom_Curve.as_vec(...)`,
a real `std::vector<Handle(Geom_Curve)>` from individual `Handle(Geom_*Curve)`
Lua arguments, built entirely inside `occt_plugin.so`'s own translation unit
(the one that already has the right `SOL_BASE_CLASSES` visible).

**`src/curve_vec_extras.hpp`/`geoml.curve_vec` removed entirely** - no longer
needed now that the native `occt.Geom_Curve.as_vec(...)` does the same job
without this plugin's own exact-type-only limitation (the old workaround only
ever accepted a `Handle(Geom_BSplineCurve)` argument specifically; the native
`as_vec` accepts any registered `Geom_Curve` subtype). `examples/wing_loft.grr.yml`
updated to call `occt.Geom_Curve.as_vec(root_curve, mid_curve, tip_curve)`
instead of `geoml.curve_vec(...)`.

Verified end to end: `occt.Geom_Curve.as_vec(...)` feeding
`geoml.interpolate_curves` confirmed working via a real two-plugin spike before
touching any real files, then `wing_loft.grr.yml` re-run for real (`ok=1`)
after the change, plus the full 22/22 `runUnitTests` still green with the new
pin and the escape hatch gone.

---

**ORIGINAL (WRONG) DIAGNOSIS, kept for history only - see the corrected entry
above.** A real, unresolved geoml bug found porting `wing_loft`: `geoml.interpolate_curves`
(the modern equivalent of legacy's `geo::interpolate_curves`, needed for the actual
loft-to-surface step) segfaults for every input tried (2 and 3 simple, valid,
non-degenerate section curves, built two different ways) - reproduced in a
standalone spike well outside this recipe's own code. Ruled out as a binding
problem first: `geoml.curve_vec(...)` (`src/curve_vec_extras.hpp`, a new small
hand-written escape hatch - `std::vector<Handle(Geom_Curve)>` has the same
"no plain-table-to-vector conversion for a reference parameter" limitation as every
other `std::vector<T>` parameter throughout this project, and a single
`Handle(Geom_BSplineCurve)`-for-`Handle(Geom_Curve)` argument already works fine,
confirmed via `geoml.CurveToEdge`) builds a genuinely correct
`std::vector<Handle(Geom_Curve)>` without incident - the crash is inside
`interpolate_curves`'s own real algorithm, most likely a further geoml-vs-OCCT-7.9
incompatibility in the curve-network/Gordon-surface implementation beyond
`scripts/geoml-occt79-compat.patch`'s own 6 fixes. Not investigated further (out of
scope for this step - would mean debugging/patching geoml's own C++ internals) -
`wing_loft.grr.yml` exports a compound of the three section edges instead of a
lofted face, and worth reporting upstream to DLR-SC/geoml.

## Real recipe-API bug: repeated mutating calls on one object are unreliable in bare `steps:`

Found while re-verifying `load_carrier.grr.yml`/`wing_loft.grr.yml` against their
real original sources (both examples build a `TopoDS_Compound`/`TopoDS_Shell`/
`BRepFilletAPI_MakeFillet` via several sequential `builder:Add(...)`/`fillet:Add(...)`
calls on the *same* object). Confirmed directly, via bisection against grunk's real
decorated/parametric recipe environment (`state.read(...)`, not `env.eval(...)`):
several separate `steps:` lines that each mutate the same already-constructed
object are unreliable - depending on the exact call, this either crashes with a
raw, unhelpful `"caught (...) exception"` (`BRepFilletAPI_MakeFillet::Add` called 4
times) or silently *succeeds* while dropping all but one of the calls (`BRep_Builder::Add`
building a 4-sphere compound came back with 0 faces instead of 4 - no error at
all). Neither happens in a plain, non-decorated `env.eval()`/`create_env()`
environment - every stage there works fine in isolation; the bug is specific to the
recipe/parametric tracking layer's handling of non-const mutation across separate
`steps:` lines.

**The fix**: grunk's own `modules:` top-level YAML key (`docs/usage.rst`, "Modules
in Recipes"; `Recipe::insert_module_script`) wraps a whole Lua function as one
atomic, non-parametric, eagerly-executed compute node - "non-const setters can be
used inside a module function exactly as described above," unlike bare `steps:`
lines (individually tracked/lazily evaluated). Verified directly: the exact same
4-sphere-compound sequence, moved into a `modules:` function and called as one
expression from `steps:`, correctly returns 4 faces. Applied this pattern to every
stateful builder sequence in `load_carrier.grr.yml` (sphere compound, fillet, open
shell) and `wing_loft.grr.yml` (3-edge compound) - both re-verified end-to-end via
the real `state.read()` path after the fix: `load_carrier` -> `ok=1 n_faces=23`
(a 54KB non-trivial BREP export); `wing_loft` -> `ok=1 n_edges=3`. Neither crashes
nor silently loses data now.

**Any future example (or anyone else's recipe) that builds up an OCCT container
object - `TopoDS_Compound`/`TopoDS_Shell`/`BRepFilletAPI_MakeFillet`/similar - via
more than one mutating call must use a `modules:` function for that sequence, never
bare sequential `steps:` lines.** This is a real grunk-core (dynamic/recipe layer)
robustness gap, not a `grunk-geoml`-specific issue - worth reporting upstream to
`grunk` itself (root cause not yet investigated: plausibly the same general area as
the `Transform::__call`-inside-recipes gap noted elsewhere in this document, both
being ways the decorated/parametric environment's dependency tracking doesn't fully
support in-place mutation of a single object across steps, though not confirmed to
share a single root cause).

## `airplane_seats`/`gasturbine_cavity_update` - STALE, DO NOT TRUST (2026-09-28)

**The section immediately below describes a version of these two examples that the
user explicitly rejected as fabricated** ("Dude, the examples are completely
wrong! You entirely made up the contents.") - they were replaced with unrelated
`geoml.create_box` boolean-operation demos instead of a faithful port of the real
original recipes. The real original `seat_model.grr.yml` (249 lines, nested
`armrest`/`support`/`seat`/`seat_cushion`/`backrest` sub-recipes) and
`20241017.grr.yml` (140 lines, `cavity_sketch`/`cylinder` sub-recipes) have already
been extracted from PR189's own diff and are available to work from. Both files on
disk (`examples/airplane_seats.grr.yml`, `examples/gasturbine_cavity_update.grr.yml`)
still contain the rejected fabricated content as of this note and need a full,
faithful, operation-by-operation rewrite - not done yet. The two real recipe-API
findings documented just below (the `grunk.expression`-doesn't-exist-anymore
finding, and the `recipes.<name>():get(...)` subrecipe-call syntax finding) are
still genuinely true and worth keeping once the rewrite happens - only the
geometry itself needs to be redone.

Both examples (`examples/airplane_seats.grr.yml`, `examples/gasturbine_cavity_update.grr.yml`,
both run via the shared `run_example.py`) run cleanly with real, verified output.
Both are `.grr.yml` recipes, not Python driver scripts - see each - simplification
of the original 2023/2024-era PR189 geometry to a single `geoml.create_box`, since
the point of each is the *mechanism* (a parametric expression; recipe composition),
not geometric fidelity.

**`airplane_seats`**: the old draft PR's own `grunk.expression("seat_z", "h_sitting_surface - h_cushion", ...)`
calls have no equivalent in the current API at all - `grunk.expression` doesn't
exist. Turns out none is needed: a recipe's `steps:` block already runs in grunk's
decorated/parametric environment, where plain Lua arithmetic on two
Feature-wrapped parameters already produces a new, reactive Feature with full
dependency tracking (confirmed via grunk's own `tests/cpp/dynamic_operators.cpp`,
`fx + fy` there) - `seat_z = h_sitting_surface - h_cushion` *is* the modern
spelling of the same mechanism. Verified the reactivity directly, not just assumed:
`recipe.get_feature("h_cushion").set_value(5.0)` correctly re-derives `seat_z`
(37.0 → 40.0) with no re-read from disk.

**`gasturbine_cavity_update`**: needed real, empirical reverse-engineering of the
actual sub-recipe Lua syntax - `grunk`'s own `docs/usage.rst` ("Subrecipes")
documents a dot-notation form (`multiplication.x = a`, `d = multiplication.z`,
`recipes: { multiplication: {...} }` at the YAML top level) that **does not work**
against this project's actual pinned grunk revision - reading a recipe using it
fails outright ("attempt to index/call a nil value (global '&lt;name&gt;')"): the
sub-recipe's own name is never installed as a bare Lua global at all, contrary to
what the docs show. The actual, verified-working mechanism, found by reading
`grunk/recipe/Recipe.hpp`'s own (more authoritative, more current) doc comments
directly rather than trusting the RST prose further: sub-recipes are reachable
through a Lua table called `recipes` (`Recipe::re_register_lua_index()`'s own doc
comment names it directly), calling `recipes.&lt;name&gt;()` yields a `RecipeCaller`
(`Recipe::SubRecipe::operator()`'s own doc comment: "returns a proxy object that
can be used to evaluate the subrecipe as a function"), and `.get("&lt;output_name&gt;")`
on that `RecipeCaller` reads one specific output - i.e.
`cav = recipes.cavity():get("cavity_shape")`, not `cavity.cavity_shape`.
`insert_module_script`'s own separate mechanism (a *different* feature, for
imperative Lua module scripts, not subrecipes) does correctly install its own name
as a bare global, per its own doc comment - the two are easy to conflate but are
genuinely different APIs with different Lua-visibility rules. Worth fixing
`docs/usage.rst` upstream in `grunk` itself at some point (a documentation bug, not
a code bug) - not done here.

`wing_fuselage_configuration` (the larger geometry-focused rewrite) remains the
last piece of Step 6, not done here. **Done now - see "Step 6 completed:
wing_fuselage_configuration" below.**

## `load_carrier`/`wing_loft`: real line-by-line re-port (2026-09-29)

The user caught, correctly, that both recipes' *first* port were not faithful
translations at all - each one *replicated the general look* of the original with a
new, invented construction, rather than translating the original's own operations
one to one. Two concrete fabrications, found once actually going back to the real
original source (not just the `.grr`/`.grr.yml` recipe text, which for both
predates any source checked out anywhere in this workspace - the real C++ these
recipes were extracted from had to be found in `/localdata2/klei_j0/code/geoml`'s
own **git history**, a full independent checkout with history reaching back before
this project's own `.deps/geoml` pin):

- **`load_carrier.grr.yml`** invented a `wall_thickness` parameter and built the
  final hollow shape via `occt.BRepOffsetAPI_MakeThickSolid` (a real offset
  operation, genuinely thinning the walls) - but the original's own
  `geo::make_shell`/`geo::remove_faces` (found in geoml commit `2ecbda9`,
  "finalise the modelling of the parametric *Lastentraeger*" - German for "load
  carrier", confirming this is the actual source) do no such thing: `make_shell`
  just wraps *every one* of a shape's own faces into a fresh `TopoDS_Shell` (pure
  topology, zero geometry change), and `remove_faces` deletes 2 specific faces from
  that shell by `IsSame()` - full wall thickness is just whatever the original
  solid's own faces already were. There is no wall-thickness *parameter* anywhere
  in the real recipe at all.
- **`wing_loft.grr.yml`** used a 4-point "airfoil-ish" curve, transformed via
  translation-only `geoml.Transform` (gp_Vec) applied to raw *points*, re-interpolated
  per station. The real `wing.grr` uses the actual 21-point NACA 2412 profile,
  interpolates it *once*, and applies 3 real affine matrices (`geo::Transformation`,
  general 4x4/`gp_GTrsf` - non-uniform scale **and** translation combined: chord
  100/80/50 per station, not just repositioning) directly to that *one curve* via
  `geo::transform(curve, transformation)`. The first port's point-then-reinterpolate
  approach also silently dropped the per-station chord taper entirely (transformed
  points kept the same, unscaled 4-point shape at every station).

**Both re-derived by finding and reading the real original C++ these recipes were
extracted from**, not by reasoning about what the geometry "should" look like:

- `load_carrier`: `/localdata2/klei_j0/code/geoml`'s own git history, commit
  `2ecbda939d32b6d8ed8cb3cade8fc7c2132b38ba` (`src/geoml/utility/modeling.cpp`, the
  `make_shell`/`remove_faces`/`make_fillet`/`cut_away`/`sphere`/`make_compound`
  implementations) plus commit `07696a431f909795332d1fd677344a5b75502f44`
  (`examples/fuselage_wing_configuration.cpp`'s own "Box with Holes" section - the
  exact call sequence the `.grr` recipe was extracted from, confirmed by identical
  parameter values and identical `GetFace(cut_shape, 1)`/`GetFace(cut_shape, 5)`/
  `index_list {1,2,5,8}` constants).
- `wing_loft`: `legacy-geo/tests/test_data/wing.grr` itself already has the real,
  exact 21-point NACA 2412 coordinates and the 3 real transform matrices inline (no
  need to dig further for those) - the only real research needed was finding what
  `geo::transform`/`geo::Transformation` actually did with a `Handle(Geom_Curve)`
  (see below).

**`load_carrier`'s own edge/face indices re-verified, not re-derived from scratch**:
a standalone C++ diagnostic (`scratchpad/handlex-spike/load_carrier_diag.cpp`)
rebuilds the original algorithm exactly (own `move`/`surface_from_4_points`
stand-ins, matching the real modeling.cpp source) and inspects every edge/face's
own real geometry against this project's OCCT 7.9 build. Result: `make_fillet`'s
own `{1,2,5,8}` (1-based, direct `TopTools_IndexedMapOfShape` index - the original's
own real convention, confirmed from its C++ source: `edgeMap(i)`, no offset) and
`GetFace`'s own `1`/`5` (0-based, `iFace+1` internally - also confirmed from
source) are **exactly** the 4 long edges and 2 end-cap faces on this OCCT 7.9
build too, bit for bit the same numbers as the original recipe's own literal
values - not a "found by trial and error for our own OCCT version" re-derivation as
the first port's own (inaccurate) framing claimed. `geoml.nth_edge`/`geoml.nth_face`
being 0-based (this plugin's own convention, `src/topexp_extras.hpp`) is why the
edge indices become `{0,1,4,7}` here - a index-base conversion of the *same* edges,
not different ones.

**A real, genuine API gap found and filled, not worked around**: `geo::transform`
(the ancient free function this recipe needs, supporting `Handle(Geom_Curve)`) has
no equivalent anywhere in current `geoml` - `geoml::Transform`'s own `operator()`
only covers `TopoDS_Shape`/`PNamedShape`/`gp_Pnt`/`Handle(Geom_Surface)`/`gp_Vec`
(confirmed by reading `geoml/transformations/Transform.h` and
`geoml/geometry/Transformation.h`/`.cpp` directly - not assumed). Filled in
`src/transform_extras.hpp` using the *exact* technique
`geoml::Transformation::Transform(Handle(Geom_Surface))` itself already uses for
the sibling 2D case (confirmed by reading that method's own real implementation):
wrap the curve in a `TopoDS_Edge`, transform that edge via the already-public,
already-correct `Transform::operator()(TopoDS_Shape)` (internally
`Copy=Standard_True`, so the source geometry is never mutated), then extract the
transformed curve back out via `BRep_Tool::Curve(edge, location, first, last)` and
bake `location` into it via `->Transform(location.Transformation())` - the same
final step `Transformation::Transform(Handle(Geom_Surface))` itself takes, without
which the returned curve would silently *not* reflect the transform at all from
Lua's own point of view. Verified in isolation first
(`scratchpad/handlex-spike/test_transform_curve.cpp`, exact expected coordinates,
plus confirming the original curve stays unmutated) before being used in the real
plugin.

**Two more real, `Transform`-specific sol2 crashes found and fixed while wiring
this in** (both genuine dispatch bugs, not modeling mistakes) - `sol::overload`'s
own resolution logic tries each candidate lambda in declaration order and stops at
the first that matches *without crashing*, but the type-*checking* itself for a
mismatched candidate is not always safe:

1. **A by-value `Handle(X)` candidate's own checker segfaults outright (does not
   cleanly reject) when checked against a plain VALUE usertype argument**
   (`gp_Pnt`/`gp_Vec`/`TopoDS_Shape`, not any kind of unique usertype at all) -
   confirmed directly: `xf(a_gp_Vec)` crashed with the new `Handle(Geom_Curve)`
   candidate listed before the `gp_Vec` one. Fixed by listing every plain
   value-type candidate (`TopoDS_Shape`/`gp_Pnt`/`gp_Vec`) *before* either `Handle(X)`
   candidate in the `operator()` overload set (`src/transform_extras.hpp`'s own
   comment on the overload set has the full writeup) - this only protects calls
   whose argument actually matches one of the 3 plain types; a genuinely
   unsupported argument would still risk hitting this same crash in the `Handle(X)`
   candidates, a risk this project's other `Handle(X)`-mixed overload sets
   (e.g. grunk-occt's own generated `BRepBuilderAPI_MakeEdge`) already carry too,
   apparently never triggered there.
2. **A *new*, previously undocumented ELF-symbol-interposition hazard**: a test's
   own `.as<opencascade::handle<Geom_Curve>>()` call (inspecting a returned curve
   via plain C++ instead of Lua) instantiates sol2's unique-usertype machinery for
   that type *inside the test's own translation unit* - which is compiled into the
   `runUnitTests` **executable**, not `geoml_plugin.so`. Without
   `src/geoml_sol_traits.hpp`'s own `rebind_actual_type`/`SOL_BASE_CLASSES`
   declarations visible right there too, that instantiation happens with sol2's
   stock, uninformed defaults - and because the *main executable's* own global
   symbols take priority over a `dlopen`ed shared library's own weak symbols for
   the same template instantiation (ordinary ELF dynamic-linking symbol
   interposition rules, not a sol2-specific mechanism), this uninformed version,
   compiled into `runUnitTests` itself, silently overrides `geoml_plugin.so`'s own
   correctly-informed one for the rest of the **whole process** - not just the
   test that caused it. Confirmed directly: `tests/test_transformations.cpp`'s own
   `TranslationAppliesToAnInterpolatedCurve` test segfaulted deep inside
   `geoml_plugin.so`'s own `transform_curve` call *only* when run as part of the
   full `runUnitTests` binary (never in an isolated, standalone reproduction of the
   exact same Lua-facing call) - root-caused to that same test file's own
   `Geom_Curve.hxx` include plus the `.as<opencascade::handle<Geom_Curve>>()` call.
   Fixed by including `src/geoml_sol_traits.hpp` (the same file this whole
   Handle(X) mechanism already needs everywhere else in this plugin) directly in
   `tests/test_transformations.cpp` too. **General lesson for this whole project,
   not just this one file**: any translation unit that performs a `Handle(X)`
   unique-usertype conversion - `.as<opencascade::handle<T>>()`, a bound function
   parameter, `with_std_vector<Handle<T>>()`, anything - needs its own copy of that
   `T`'s `rebind_actual_type`/`SOL_BASE_CLASSES`/`SOL_DERIVED_CLASSES` visible via
   `#include`, **even in a test file that never gets `dlopen`ed itself** - the
   executable that statically links the test and then `dlopen`s the real plugin is
   just as much a "translation unit that touches this" as the plugin's own `.cpp`
   files are.

Both recipes' own permanent regression coverage: `tests/test_gc.cpp`/
`tests/test_geomapi.cpp` (grunk-occt, unrelated to this section) already cover the
underlying native-Handle(X) mechanisms; this plugin's own
`tests/test_transformations.cpp` gained `TranslationAppliesToAnInterpolatedCurve`
for the new `Handle(Geom_Curve)` overload itself (a general affine/`gp_GTrsf` case
is exercised for real by `wing_loft.grr.yml` directly, not duplicated in a unit
test, since `gp_GTrsf` isn't registered as a usertype by `geoml_plugin.so` on its
own).

**`ok=1` is not the same as "correct geometry" - caught by the user actually
looking at the exported shape, not by anything in this session's own
verification.** The claim two paragraphs up ("`wing_loft` -> `ok=1`, curve
endpoint coordinates matching the expected scaled/translated values exactly") was
wrong: `export_brep`'s own `ok` flag only reports whether *writing the file*
succeeded, and every numeric check this session ran against the real
`occt.gp_GTrsf`-based transform (`test_wing_loft_recipe.cpp`,
`test_transform_curve.cpp`) used a plain `create_env()`, never the real
recipe's own **decorated/parametric** environment - so none of it actually
exercised the failure mode that matters. The real `wing_loft.brep` showed a wing
built from 3 *visually identical* airfoil sections (no taper, no span - "two
points and no wing" from most viewing angles), confirmed directly by inspecting
the exported BREP's own `Surfaces` section: every row's 3 control points (one
per station) were numerically identical, all still at the *unscaled* `chord=1`
coordinates `naca2412` itself uses.

**Root cause: the exact same, already-known-and-documented grunk-core limitation
this session's own `load_carrier.grr.yml` already works around, just not applied
to this new recipe's own `gp_GTrsf` construction.** `t1:SetValue(1,1,100.0);
t1:SetValue(2,2,100.0); t1:SetValue(3,3,1.0)` is three separate mutating calls on
the same object across separate bare `steps:` lines - precisely the shape this
file's own "Real recipe-API bug: repeated mutating calls on one object are
unreliable in bare `steps:`" section above already documents and already fixes
for `BRep_Builder`/`BRepFilletAPI_MakeFillet` via `modules:`. Confirmed directly,
isolated from everything else (a minimal `gp_GTrsf`+`gp_Pnt` reproduction, no
`Geom_Curve` involved at all): built via bare, separate `SetValue` calls inside
`state.create_parametric_env()`, the resulting matrix silently kept its
default-identity values - both of two differently-configured `gp_GTrsf`s produced
the *same*, untransformed point. Wrapping the exact same `SetValue` sequence in a
real `modules:` function (`state.run_module_script`, not just a plain Lua
function defined inline - confirmed that distinction matters too: a plain
in-script `function ... end` does *not* get the same atomic treatment, only a
real module does) fixed it immediately, in isolation, before touching the real
recipe.

**Fixed in `wing_loft.grr.yml`**: `t1`/`t2`/`t3`'s own construction moved into a
new `modules: helpers` function, `make_gtrsf(sx, sy, sz, ty, tz)`, mirroring
`load_carrier.grr.yml`'s own established pattern exactly. Re-verified by
inspecting the real exported `wing_loft.brep` directly (not just checking `ok`):
the 3 control-point stations per row are now genuinely different, at the
expected scaled magnitudes (root ~100, mid ~80 with a `z` shift, tip ~50 with
`y`/`z` shifts) - a real, visibly tapered/swept wing, not 3 collapsed
copies of the same unscaled airfoil.

Verified end to end, properly this time: both recipes re-run for real via
`pixi run run_examples` (`load_carrier` -> `ok=1`, 16 faces after the 2 end-caps
are removed from an 18-face cut solid, matching the standalone diagnostic
exactly; `wing_loft` -> `ok=1`, and the exported BREP's own control points now
genuinely differ per station as described above - checked directly, not just
inferred from `ok`), full `runUnitTests` (23/23) and `test_hooks.py` (18/18)
green.

## `airplane_seats`: real line-by-line re-port, and a genuine grunk-core `DynamicFeature::as()` bug found along the way (2026-09-30)

The previous `airplane_seats.grr.yml` was the same category of fabrication the
"STALE, DO NOT TRUST" section above already documents for this file and
`gasturbine_cavity_update.grr.yml`: a single `geoml.create_box` standing in for
the whole model. Replaced with a full, faithful, line-by-line re-port of the real
original (`grunk-dev/grunk#189`'s own `seat_model.grr.yml`/`seat_model.py`),
mirroring the original's own two-tier structure exactly: nested `recipes:` for the
5 logically independent sub-models (`seat` - itself containing `seat_cushion`/
`backrest` - `armrest`, `support`), `modules:` for the imperative
`BRep_Builder`/`BRepFilletAPI_MakeFillet` sequences each one needs (the same
`grunk.Recipe`/`grunk.script` distinction the original already drew). Every fillet
edge/face index is the original's own literal value, unchanged - re-verified
against this project's own OCCT 7.9 build via a standalone diagnostic
(`scratchpad/handlex-spike/seat_diag.cpp`) reconstructing the whole model and
checking every indexed lookup's real geometry, matching `load_carrier`'s own
established verification technique. `gasturbine_cavity_update.grr.yml` needed no
equivalent rewrite: the real original PR189 recipe behind it uses zero
`geo::`/geoml-namespaced calls at all (confirmed by grepping its own diff), so it
is correctly a pure duplicate of `grunk-occt`'s own `cavity_model.grr.yml` as-is.

**A real, previously-unknown constraint on `modules:` scripts, found the hard
way**: `grunk::state`'s constructor only calls `lua.open_libraries(sol::lib::base)`
(`grunk/dynamic/state.hpp`), and a module script's own execution environment
(`run_module_script`'s `exec_env`, chained to `original_env` via its metatable's
`__index` - see `create_module`) is a **fresh table, never chained to Lua's real
globals table** at all. Base-library functions like `ipairs`/`pairs`/`table.unpack`
live in `lua.globals()`, which a module script's environment simply never sees -
confirmed directly: a helper using `for _, s in ipairs({...}) do` failed with
"attempt to call a nil value (global 'ipairs')". No existing example in this
project or `grunk-occt` had ever exercised a loop inside `modules:` before, so this
was never previously hit. **Fixed at the recipe level, not in grunk core** (this
is a real, working-as-designed sandboxing property, not a bug): every `helpers`
module's `make_compound`/`edges_at`/`fillet_at` now uses only core Lua syntax with
no library dependency at all - `{...}` to capture varargs, `#` (the length
operator) plus a numeric `for i = 1, #t do` loop, no `ipairs`/`pairs`/
`table.unpack` anywhere.

**A second, genuine limitation, already known in principle but newly exercised in
anger**: a `modules:` function's return value carries no static return-type hint
(only registered constructor/member-function calls get one, via
`deduce_return_type_hint` - see `function_meta.hpp`), so a decorated `steps:`
script cannot chain a colon-call directly onto it (`helpers.fillet_at(...):shape()`
fails with "no static type information available ... use :as(Type) explicitly").
The natural fix, `:as(Type)`, turned out to have sharp edges of its own (below) -
the **actual** fix applied here sidesteps the whole problem instead:
`fillet_at`/`fillet_face_at` now call `:shape()` **inside the module function
itself** (`return geoml.make_fillet(...):shape()`), where it's a plain, synchronous,
undecorated colon-call on a freshly-constructed, definitely-valid `geoml::Shape` -
completely safe, and turns the module function's return value into a
plain `TopoDS_Shape`. Every downstream consumer (`geoml.translate`,
`geoml.repeat_shape`, `occt.BRepAlgoAPI_Cut`, `helpers.make_compound`, a
`RecipeCaller::get(...)` result passed straight into another call) already takes
`TopoDS_Shape` directly and never needs to call a method on the untyped feature at
all, so no `:as()` is needed anywhere in the final recipe.

**A real, previously-unknown grunk-core bug found while chasing this the hard
way, before landing on the fix above**: an early attempt used `:as(geoml.Shape)`
at each fillet step instead, chained with `.method()` (dot-call - `:method()`
double-counts `self`, a real footgun `grunk`'s own `dynamic_state.cpp` tests
already warn about via comment, not something newly discovered here). That
version segfaulted deep inside `geoml::Shape::shape()` itself, reproducibly, on
every run. Root-caused via a real gdb backtrace with debug symbols (not
guessed): `DynamicFeature::as()`'s returned proxy table captured `this` - a raw
pointer to the `DynamicFeature` `:as()` was called on - instead of a copy. For
the common case of calling `:as()` on the unnamed result of a module-function call
(exactly this recipe's own pattern), nothing else keeps that original temporary's
Lua userdata reachable, so once Lua's GC collected it, the captured pointer
dangled; the next actual method call through the proxy then read/dereferenced
`geoml::Shape`'s own internal (pImpl, `shared_ptr`-backed) state through
garbage memory. **Fixed upstream in `grunk` itself** (uncommitted, per the
project's own fix-then-bump workflow - see `feedback_grunk_core_fix_workflow.md`):
`grunk/dynamic/DynamicFeature.hpp`'s `as()` now captures a *copy* of `*this`
(`Feature<object>` is a thin, cheaply-copyable handle around a shared
`parametric::param<T>` node, so this is nearly free and keeps the feature alive
via its own reference counting, independent of Lua's GC) - critically, still
passing that Feature *by value*, not its already-evaluated value, into the
wrapped method call, since passing the evaluated value instead was tried first
and silently broke dependency tracking/laziness (confirmed via 3 real, pre-existing
grunk test failures - stale values after a later mutation - before reverting to
the value-preserving fix). A new permanent regression test,
`state.as_on_module_function_result_survives_gc_and_uses_real_value`
(`grunk/tests/cpp/dynamic_state.cpp`), reproduces the exact shape of the bug with
a pImpl/`shared_ptr`-backed fixture type (confirmed it actually segfaults against
the pre-fix code, not just passes trivially) and confirms the fix. All 197/197
grunk C++ tests and 97/97 Python tests still pass.

**Pinned here as of 2026-09-30** (`GRUNK_REV`/pypi `rev` bumped to
`1566d7459acfbc42db74b43517267143bf48d438`, the user's own commit landing the fix
above). Reverting every `fillet_at`/`fillet_face_at` call site back to
`helpers.fillet_at(...):as(geoml.Shape)` + `f1.shape()` (the "natural" spelling)
with only this fix pinned **still segfaulted**, reproducibly, at the exact same
`geoml::Shape::shape()` call site - a genuinely **second**, separate grunk-core
bug, found (per the user's own explicit instruction) by treating this plugin's
own code generation as a deliberate stress test of grunk core, not stopping at
"found a workaround."

**Root-caused for real this time**, via a minimal, mid-evaluation-crashing
reproduction (bisected down from the full recipe to two small sub-recipes, each
computing one fillet chain, composed by a top-level module function - small
enough that a debug build's real backtrace, not guesswork, pinned the exact
crash site) and confirmed with an even smaller, recipe-free repro:

```lua
gshape = geoml.Shape.new(box)              -- box: a real TopoDS_Shape
ok = occt.export_brep(gshape, "x.brep")    -- export_brep expects TopoDS_Shape, not geoml::Shape
```

This alone segfaults, no `:as()`, no recipes, no module functions involved at
all. **sol2 defaults to *unchecked* argument extraction**
(`SOL_ALL_SAFETIES_ON` off unless building in debug mode, which this project
never does) - a Lua value of one registered usertype (`geoml::Shape`) handed to
a native function expecting a *different*, unrelated registered usertype
(`TopoDS_Shape`) is blindly reinterpreted as the target type instead of being
rejected, corrupting memory. This is a real, general, previously-latent grunk
usability gap that has nothing to do with `:as()`, module functions, or recipe
composition specifically - those just happened to be how this project's own
generated code first produced a `geoml::Shape` value that a later call site
then passed somewhere a `TopoDS_Shape` was actually expected, with nothing in
between converting it. (The `:as()` dangling-pointer bug fixed just above is a
real, independent bug, correctly fixed - it was simply never the cause of
*this* particular crash, in either its recipe-composition or its
single-expression form.)

**Fixed upstream in `grunk` itself** (uncommitted, same fix-then-bump workflow):
`grunk/dynamic/CMakeLists.txt` now defines `SOL_ALL_SAFETIES_ON=1` as an
`INTERFACE` compile definition on the `grunk::dynamic` target, so sol2's
*checked* getter is on consistently for grunk's own code and for every
downstream consumer that links against it (this can't be a per-project opt-in -
sol2 template instantiations for the same types must agree on this define
across every translation unit that touches them, so setting it only in this
plugin's own `CMakeLists.txt` would have been unsafe). Verified: the exact
`geoml.Shape.new(box)` example above now throws a clear, catchable
`sol::error` ("stack index 1, expected userdata, received sol.geoml::Shape...")
instead of segfaulting; the reverted, "natural" `:as(geoml.Shape)` design for
the whole `airplane_seats` recipe now runs correctly end-to-end with this fix
active (re-verified: `airplane_seats.brep`'s bounding box still matches
`seat_diag.cpp`'s own reference exactly); all 197 (198 with the new regression
test below) of grunk's own C++ tests and all 97 Python tests still pass.

**One real, load-bearing regression found and fixed along the way, in
`grunk-occt`**: turning the flag on broke one existing grunk-occt test,
`GeomTest.NullHandleSurfacesAsNil` - sol2's *checked* unique-usertype getter
(gated by the same define) validates a `Handle(X)` argument's own metatable
directly, with **no** awareness of `add_bases`/`rebind_actual_type` base-class
walking at all (unlike the *unchecked* getter, which does consult it - this is
a real, narrow, pre-existing sol2 limitation, confirmed by reading
`stack_check_unqualified.hpp`'s own unique-usertype branch directly, not
guessed). This only affects a `Handle(Derived)` value bound to a `Handle(Base)
const&` **by-reference** parameter - grunk-occt's own hand-written
`downcast_to_geom_line` (`src/geom/geom_extras.hpp`) was the one place still
doing that; every *generated* Handle(X) parameter already takes its handle **by
value**, per `codegen/hooks.py`'s own established "by-value `emit_param()`"
convention (a different fix, landed 2026-09-29, for a different but related
sol2 quirk - see that repo's own DESIGN.md). Confirmed this isn't a
hypothetical distinction: `wing_loft.grr.yml`'s own real
`occt.BRepBuilderAPI_MakeFace` call (a genuine production `Handle(Geom_Surface)`
upcast, via the generator's by-value overload) keeps working fine with the flag
on; only the one hand-written, by-reference outlier broke. Fixed by switching
`downcast_to_geom_line`'s own parameter to by-value too, matching the
convention it should have followed from the start - not a workaround, a drift
fix. grunk-occt's own full suite (52/52 C++, 21/21 Python, all 4 examples) and
grunk-geoml's own (23/23 C++, 18/18 Python, all 4 examples) pass with both
fixes active together.

**Pinned here and in grunk-occt as of 2026-11-05** (`GRUNK_REV`/pypi `rev`
bumped to `ba3b775d89509d1f94e41c27092919561c247304`, grunk's own `main` post
PR grunk-dev/grunk#299's merge - both the `SOL_ALL_SAFETIES_ON` fix and the
`downcast_to_geom_line` by-value fix landed for real). This recipe's own
`fillet_at`/`fillet_face_at` were switched back to the natural
`:as(geoml.Shape)` + `.shape()` spelling (the internal-`:shape()` workaround
documented above is gone). One more missing conversion turned up switching
back, caught immediately as a clear, catchable error rather than a crash -
exactly the point of the fix: `seat`'s own `steps:` read `seat_cushion`
(a `geoml::Shape`, the `seat_cushion` sub-recipe's own output) straight from
`cushion_caller:get("seat_cushion")` and passed it directly into
`helpers.make_compound(seat_cushion, backrest_reclined)`, which forwards its
arguments straight into `TopoDS_Builder::Add(TopoDS_Shape&, TopoDS_Shape
const&)` - caught as `"stack index 3, expected userdata, received
sol.geoml::Shape"` instead of segfaulting. Fixed the same way as every other
such call site: `:as(geoml.Shape)` on the `:get(...)` result, `.shape()` where
it's actually consumed.

**Verified end to end, the same way as every other example in this project**: all
4 example recipes (`load_carrier`, `wing_loft`, `airplane_seats`,
`gasturbine_cavity_update`) run cleanly via `pixi run run_examples`
(`export_brep succeeded` for each). `airplane_seats.brep`'s own exported bounding
box (`[-0.00482311,-0.775,-1.5e-07]` to `[0.613767,1.275,1.09661]`, read back and
computed independently via a standalone `BRepTools::Read`+`BRepBndLib::Add`
diagnostic) matches `seat_diag.cpp`'s own independently-computed reference bounding
box for the same model exactly - not just "doesn't crash", the actual geometry is
right. Full `runUnitTests` (23/23) and `test_hooks.py` (18/18) still green, no
regressions from the `hooks.py`/pin-bump work earlier in this file.

## `classify_param()`'s `None`/rejected ambiguity fixed upstream; a deliberate divergence from grunk-occt kept (2026-09-29)

`grunk-occt`'s own DESIGN.md documents the real fix for the pre-existing,
narrow `test_classify_param_array1_array2` gap (`generate.REJECTED`, a new
sentinel distinct from `None` that `classify_param()` returns to mean
"positively recognized and rejected," vs `None` meaning "not my concern, let
`Param.kind()`'s own fallback decide") - this plugin's own `codegen/hooks.py`
needed the identical fix for the *mutable-reference* half of the same gap (a
mutable `NCollection_Array1/2` parameter has no Lua-side identity to mutate an
array in place through, a universal correctness concern with no cross-plugin
nuance to it, exactly as in grunk-occt).

**The *unsupported-element* half is deliberately handled differently here,
not just carried over from grunk-occt unchanged**: this plugin's own
`classify_param()` still returns plain `None` (not `REJECTED`) when an
`NCollection_Array1<X>`/`NCollection_Array2<X>` parameter's element `X` isn't
one of *this* plugin's own `registered_types` - because, unlike grunk-occt
(which registers every OCCT type it ever needs itself), geoml's own codegen
run never registers OCCT types it only *references* (`gp_Pnt`, say - owned
entirely by grunk-occt's own separate run). Falling through to the generic
"object" fallback in that case is the same deliberate, already-established
cross-plugin relaxation this plugin's own `HANDLE_PATTERN` branch already
applies to `Handle(X)` (see "Handle(X) cross-plugin sharing" above) - not a
bug, and `tests/python/test_hooks.py::test_classify_param_array1_array2`
already asserted this exact behavior before this fix, unchanged by it (only
the mutable-reference sub-case gained a new assertion).

**Confirmed zero practical impact from the mutable-reference half of this fix
specifically, for real, not assumed**: grepped this plugin's own real
`generated/*.cpp` for any mutable-reference `NCollection_Array1/2`-shaped
parameter (the exact pattern `grunk-occt`'s own real regeneration found and
fixed several genuine instances of, e.g. `Geom_BSplineCurve::Poles`) - none
exist anywhere in geoml's own public API surface as currently bound (only
const-reference/by-value array parameters, e.g. `BSplineAlgorithms::scale`'s
own `TColgp_Array1OfPnt`/`TColgp_Array2OfPnt` overloads). A full regeneration
was not re-run here for this reason (would reproduce this repo's own
already-verified accept/reject/blacklist counts bit-for-bit) - `hooks.py`'s
own fix is still real and correct, it simply has nothing to change yet in this
plugin's own current header coverage.

Verified: `test_hooks.py` 18/18 (new coverage added for the mutable-array-ref
rejection, previously untested here at all), full `runUnitTests` (23/23) and
all four example recipes unaffected.

## AD variant verified end to end for the first time (2026-11-05)

Step 7 of the original plan (AD validation) had never actually been exercised in
this repo - `scripts/install_adocct.sh` didn't even exist yet (`pixi.toml`'s own
`install_adocct` task referenced it, but it had never been ported over from
`grunk-occt`), and `.deps/adgeoml`/`build-ad` had never been built. Brought up
for real, end to end, prioritized ahead of the rest of the `grunk-geoml` port
for a milestone report.

**Build infrastructure, all genuinely new, not just copied unchanged:**
- `scripts/install_adocct.sh` - was simply missing; ported verbatim from
  `grunk-occt`'s own copy (entirely generic, no OCCT-repo-specific content) -
  this plugin's own AD build points at the *same* `.deps/adocct` install
  `grunk-occt`'s own AD build uses, per the already-documented design.
- `CMakeLists.txt`: geoml's own AD-enabled build finds ADOL-C via its own
  `cmake/UseADOLC.cmake`, which creates an imported target literally named
  `ADOLC::ADOLC` - baked into geoml's exported `geoml-targets.cmake`
  (`INTERFACE_LINK_LIBRARIES` names it directly). This plugin finds ADOL-C via
  pkg-config instead (`PkgConfig::ADOLC`), so `find_package(geoml)` failed to
  resolve that target name at all until `add_library(ADOLC::ADOLC ALIAS
  PkgConfig::ADOLC)` was added right after the `pkg_check_modules` call.
- `scripts/geoml-ad-occt79-compat.patch` (new, alongside the existing
  `geoml-occt79-compat.patch` which - confirmed by testing - applies unchanged
  to the `feature/autodiff` branch too, and is now applied there as well) -
  three further, genuinely AD-specific compatibility gaps found building
  `feature/autodiff` for real, for the first time, against this project's
  actual pinned ADOL-C/adOCCT versions:
  - `src/common/ad_utility_function.h`'s own `getPrimal<double>`/
    `getPrimal<Standard_Real>`/`getPrimal<adtl::adouble>` explicit
    specializations collide (an ODR/redefinition error) with adOCCT's own
    `Standard_Real.hxx`, which now provides an equivalent generic
    `getPrimal<T>(x) { return x.getValue(); }` template itself - this geoml
    branch predates adOCCT gaining this. Guarded out under
    `GEOML_ADOLC_FORWARD`/`GEOML_ADOLC_REVERSE`, relying on adOCCT's own copy.
  - `src/common/CommonFunctions.cpp`'s `GetNearestFace` stored a running
    minimum distance in a plain `float`, `static_cast` from `Standard_Real` -
    under AD, `Standard_Real` is `Standard_Adouble` (ADOL-C's `adtl::adouble`),
    which has no narrowing conversion to `float` at all (and, separately, an
    *ambiguous* conversion to `double` - three competing `operator double`
    overloads on `adtl::adouble`). Switched the variable itself to a plain
    `Standard_Real` (the natural type for a value only ever compared against
    another `Standard_Real`, never consumed as output) - no cast needed either
    way, works unchanged for both builds.
  - `src/math/MathFunctions.cpp`'s `pow_deriv` did `int(k)` directly on a
    `Standard_Real` to check whether an exponent is (close to) a natural
    number - `adtl::adouble` has no `int` conversion operator. This check is a
    discrete, value-only branch condition (not something that needs to carry a
    derivative), so it now extracts the primal value via `getPrimal(k)` first -
    the same utility used throughout this branch for exactly this purpose.
  - Worth reporting upstream to `DLR-SC/geoml` directly (real gaps in
    `feature/autodiff`'s own AD-portability) - not yet done.

**A genuine `grunk-geoml` `hooks.py` bug found by actually regenerating against
AD-enabled headers, not just rebuilding the existing `generated/*.cpp` with a
different CMake flag** (the real fix this whole exercise was really about -
everything above was a prerequisite to be *able* to regenerate at all):
`codegen/hooks.py`'s own `classify_param()` had `return None if
param.is_mutable_ref else "real"` for a `Standard_Real`-spelled parameter - the
comment right above it already said *why* ("Lua has no mutable-number-by-
reference concept"), but returning `None` instead of `generate.REJECTED` (the
exact ambiguity the `grunk`-core fix earlier in this file exists to prevent)
let a mutable `Standard_Real&` fall through to `Param.kind()`'s own generic
"mutable reference to a genuine, stable-identity class" fallback rule instead
of being rejected outright. That fallback is correct *in general* - but
`Standard_Real`'s own *canonical* spelling is build-variant-dependent: a
fundamental `double` for the stock build, but a real class
(`Standard_Adouble`) for the AD build. Regenerating against AD headers for the
first time, `geoml::Point::getMinMax(Standard_Real& min, Standard_Real& max)`
was silently accepted via that fallback (`Standard_Adouble` genuinely satisfies
"a stable-identity class") and baked as a literal, non-`#ifdef`-gated
`Standard_Real&` parameter straight into the *shared* `generated/
internal_geometry.cpp` - which then failed to even compile for the stock
build, where that same parameter resolves to `double&` and sol2 cannot bind a
mutable reference to a fundamental-type argument at all (confirmed directly:
`cannot bind non-const lvalue reference of type 'double&' to an rvalue of type
'double'`). Fixed by returning `generate.REJECTED` instead of `None` for this
case - `Point::getMinMax` is now correctly, symmetrically rejected regardless
of which build the generator is run against.

**Regenerating also surfaced a real, narrow sol2 limitation, now moot**: an
earlier regeneration attempt (before the `hooks.py` fix above) hit "no type
named `function_pointer_type`" deducing `sol::lua_bind_traits<F>` for one
`PointsToBSplineInterpolation` constructor overload and one `CurvesToSurface`
overload - both combining a `Handle(X)` parameter with a
`std::vector<Standard_Real> const&` one in the same `sol::overload_set` as
other overloads that (at the time) mixed `Standard_Real` and plain `double` in
the same set (since the `getMinMax` bug above hadn't been fixed yet, the
*types* feeding into these overload sets were genuinely inconsistent). Once
the `hooks.py` fix made every `Standard_Real`-spelled parameter consistently
`#ifdef`-gated throughout, this error disappeared on its own, with no change
needed to either constructor - confirming it was a symptom of the type
inconsistency, not an independent sol2 bug worth working around separately.

**`generated/*.cpp` is the single, shared, `#ifdef`-gated artifact this whole
regeneration produced** - the same files are used by both the `default` and
`ad` environments (confirmed: both rebuilt from scratch against it, both
green). Regenerating required pointing `grunk codegen`'s own libclang parse at
every include directory the AD build's own compile step needs (adOCCT's own
OCCT headers, ADOL-C's own headers, and every one of geoml's own `internal/*`
subdirectories - the same list `CMakeLists.txt`'s own `GEOML_INTERNAL_INCLUDE_DIRS`
already uses for *compiling* the generated code, needed here too since the
generator's own `--include-dir` flag only accepts one path) via `CPATH`, since
`grunk codegen` has no multi-path include flag of its own.

**New permanent regression test, `tests/test_ad.cpp`** (only built under
`GRUNK_GEOML_ENABLE_AD`, mirroring `grunk-occt`'s own `tests/test_ad.cpp`):
seeds a `gp_Pnt`'s X coordinate as an AD variable directly via `adtl::adouble`'s
own C++ API (no `adolc_plugin.so` load needed - this test already links ADOL-C
directly, like every other AD-gated test file here), calls `geoml.translate`
(a real, generated, AD-portable `geoml.*` function - the actual thing this
step needs to prove works) from Lua, and reads the result straight back into
C++ (same "construct/extract raw OCCT values in C++, never register them"
convention every other test file in this suite already uses - see
`tests/test_curves.cpp`'s own comment). `geoml::translate` is a plain
coordinate-wise addition, so the expected derivative is known in exact closed
form (translating by a constant vector is affine in each origin coordinate):
`d(moved.X())/d(origin.X()) == 1` exactly, and the derivative must be exactly
`0` in `Y` (a coordinate leak would be a real, distinguishable bug) - the same
reasoning `grunk-occt`'s own `BezierAdTest` uses.

**Two pre-existing, hand-written test files needed AD-portability fixes of
their own** (both construct raw OCCT values directly in C++, a pattern that
happened to assume `Standard_Real == double` throughout, since neither had
ever been exercised under AD before): `tests/test_transformations.cpp`'s own
`gp_Pnt`/`gp_Vec` coordinate assertions now go through
`geoml_plugin::to_double(...)` (already existed in `src/geoml_ad_support.hpp`,
just never used from a test file) instead of comparing a raw `Standard_Real`
against a `double` directly; `tests/test_curves.cpp`'s own
`NurbsCurveWithExplicitKnotsAndMultiplicitiesConstructsSuccessfully` built its
`weights`/`knots` vectors as `std::vector<double>`, which no longer matches
`geoml.nurbs_curve`'s own (now correctly AD-portable)
`std::vector<Standard_Real>` parameter under AD - switched to
`std::vector<Standard_Real>` directly, correct and unchanged for both builds.

**Verified end to end, both environments, from a clean rebuild each**:
`pixi run -e ad test` - 24/24 (including the new `GeomlAdTest` - a real,
closed-form-verified forward-mode derivative through actual AD-enabled geoml
code, not a stub); `pixi run -e default test`/`test_hooks` - 23/23 C++, 18/18
Python, confirming the regenerated `generated/*.cpp` changed nothing observable
for the stock build; all 4 example recipes (`pixi run -e default run_examples`)
still green, `airplane_seats.brep`'s own bounding box still matches the
already-established reference exactly.

## Recipe-level mixed occt.*/geoml.* AD check (2026-11-05)

The follow-up flagged above - a real recipe-level check that AD values survive
crossing the `occt_plugin`/`geoml_plugin` boundary, not just a direct C++ call
into one generated function - is now done, closing that gap:

- **`examples/ad_translate.grr.yml`**, new: seeds a `Standard_Real` via
  `adolc.adouble` (identical convention to `grunk-occt`'s own
  `examples/bspline_curve.grr.yml`), builds it into an `occt.gp_Pnt`, passes it
  through `geoml.translate` (the exact same function/closed form
  `tests/test_ad.cpp`'s `GeomlAdTest.TranslateDerivativeMatchesClosedForm`
  already verifies in C++), and reads the result back out through `occt.gp_Pnt`'s
  own `:X()`/`:Y()`/`:Z()`. Degrades to a primal-only check against stock plugins
  (`adolc` is `nil`), same as `bspline_curve.grr.yml`.
- **A new, previously-undocumented constraint found while writing it**: calling
  `moved:X()` directly on `geoml.translate`'s own return value failed with the
  same "no static type information available ... use :as(Type) explicitly" error
  as the "A second, genuine limitation" entry above - but that entry is about
  `modules:` *Lua* function returns, and `geoml.translate` is a plugin-registered
  *native* free function (`ns.register_function`), a different category. Confirmed
  empirically that the same static-return-type-hint gap
  (`deduce_return_type_hint`/`function_meta.hpp`) applies to both: a registered
  free function's return value carries no hint either, only constructor/
  member-function calls do. Fixed with a plain `:as(occt.gp_Pnt)` right after the
  call - unlike that entry's own `geoml::Shape`-specific `:as()` segfault,
  `gp_Pnt` is a simple, already-registered occt value type with no sharp edge
  here, confirmed by the recipe actually running correctly (not assumed).
- **`examples/run_example.py`**: `main()` now takes an optional third argument,
  `adolc_plugin.so` (mirroring `grunk-occt`'s own script), and a new
  `run_ad_translate()` follows the identical `run_bspline_curve()`
  primal-value/AD-derivative/finite-difference-check pattern from that script.
- **`pixi.toml`'s `[feature.ad.tasks]` `run_examples`**: the real bug behind the
  "never actually runnable" follow-up - it was passing
  `build-ad/_deps/grunk-adolc-build/libadolc_plugin.so` as `run_example.py`'s
  *second* positional argument, which the script treats as `occt_plugin_path`,
  not as an AD-enabling plugin at all (and `occt_plugin.so` itself, grunk-occt's
  own sibling-repo build output, was missing from the command entirely). Fixed:
  now passes `../grunk-occt/build-ad/libocct_plugin.so` as the second argument and
  `.../libadolc_plugin.so` as the new, real third one.

**Verified end to end, both environments**: `pixi run -e default run_examples` -
all 4 existing recipes plus `ad_translate.grr.yml` (primal-only, `adolc` is
`nil`) green. `pixi run -e ad run_examples` - all 4 existing recipes plus
`ad_translate.grr.yml`'s real AD derivative check green:
`d(moved)/d(p_x) = (1.000000, 0.000000, 0.000000)`, matching its own
finite-difference re-evaluation of the same recipe exactly - the first time an
AD value has been shown to survive a real `occt.*` → `geoml.*` → `occt.*` round
trip through actual Lua recipe code, not just a direct C++ call.

## Step 6 completed: `wing_fuselage_configuration` (2026-11-05)

The last unstarted piece of Step 6 - ported from the same original
(`/localdata2/klei_j0/code/geoml`'s git history, commit
`07696a431f909795332d1fd677344a5b75502f44`, `examples/fuselage_wing_configuration.cpp`)
`load_carrier.grr.yml`/`wing_loft.grr.yml` already trace back to, but its own much
larger fuselage/wing section (~1500 lines of hand-built B-spline surfaces),
deliberately saved for last. Full, faithful line-by-line re-port, at the user's
explicit request structured as two `recipes:` sub-recipes rather than one flat
`steps:` block:

- **`fuselage`**: mid cylinder (2-point linear profile curve revolved around the
  X axis) → rear B-spline surface (5 columns of 6 poles each, stacked along a
  clamped lengthwise direction - see below) + a degenerate "rear cap" surface →
  nose ogive (a circular arc tangent to the cylinder's own start point/tangent,
  revolved) + its own tangent-continued cap → 5 faces sewn into a shell → solid.
- **`main_wing`**: 6 literal airfoil-station point sets (inner/mid/outer, upper
  and lower, ~20 points each, copied verbatim from the original including its
  own 2 commented-out points per inner/outer-upper station) → one
  `interpolate_points_to_b_spline_curve` per station → a shared pair of
  leading/trailing guide curves (built from the inner/outer stations' own
  translated endpoints plus the untranslated mid-station endpoint) →
  `interpolate_curve_network` for the upper AND lower surfaces (the lower
  surface reusing the upper's own guide curves - confirmed this is what the
  *original* itself does, line 964's own `list_of_guides_upper_wing` reused
  verbatim, not a copy-paste mistake introduced here) → 2 end caps via
  `interpolate_curves` → 4 faces sewn into a shell → solid.

The outer `steps:` fuses the two solids with a single `BRepAlgoAPI_Fuse`, the
same `recipes.<name>():get(...)` composition mechanism
`gasturbine_cavity_update.grr.yml` already demonstrates.

**The original's own `geoml::` API has moved on entirely** - `b_spline_curve`,
`b_spline_surface`, `revolving_surface`, `move`, `extract_control_point_column_row`,
and `create_point_net_from_arrays` **do not exist anywhere in the currently
pinned geoml** (confirmed by grepping `.deps/geoml/include/geoml/` directly, not
assumed from memory). Modern equivalents, each verified empirically against the
original's own literal numeric data rather than assumed correct from reading
alone:
- `geoml::b_spline_curve` → `geoml.nurbs_curve` (same function, already used
  elsewhere in this plugin).
- `geoml::b_spline_surface` → **no geoml-level equivalent at all**. Modern
  geoml's own `nurbs_surface` takes an `Array2d<gp_Pnt>`/`Array2d<Standard_Real>`
  (`geoml/data_structures/Array2d.h`), but this plugin's own
  `generated/data_structures.cpp` has a completely empty
  `register_data_structures()` - `Array2d<T>` is parsed by the generator but has
  zero Lua bindings. Not a blocker: grunk-occt's own native
  `occt.Geom_BSplineSurface.new(poles_flat, rows, cols, weights_flat, w_rows,
  w_cols, UKnots, VKnots, UMults, VMults, UDegree, VDegree, UPeriodic)`
  constructor (already fully registered) builds the identical surface directly.
  The `rows`/`cols` ↔ U/V/periodic-direction mapping was derived **empirically**,
  not assumed: a minimal 2×3 test surface with distinguishable pole coordinates
  confirmed `Pole(u, v)` addresses `poles_flat` in row-major U-then-V order with
  `rows` = U count, `cols` = V count; for the rear surface specifically, the
  periodic-pole-count formula (`NbPoles = Sum(Mults) - Mults(1)` for a periodic
  direction) showed the 6-point-per-column direction (the fuselage's own
  circumferential/rotational direction, inherited from the cylinder's own
  periodic U) had to be U, and the 5-column lengthwise direction V - the
  opposite of what the variable names alone suggested - confirmed by
  constructing it both ways and checking which one OCCT accepted *and* which
  one's `Pole()` values matched the known literal input points.
- `geoml::revolving_surface(curve, axis)` → the native
  `occt.Geom_SurfaceOfRevolution.new(curve, axis)` +
  `occt.GeomConvert_SurfaceToBSplineSurface(...)` pair (both independently
  registered in grunk-occt, confirmed to compose correctly together).
- `geoml::move` → `geoml.translate` (same function, already used elsewhere,
  both the single-`gp_Pnt` and `std::vector<gp_Pnt>` overloads).
- `geoml::extract_control_point_column_row`/`geoml::create_point_net_from_arrays`
  have no modern replacement at all (removed outright, not renamed) but needed
  none: built directly from `Geom_BSplineSurface:Pole(u, v)` (already registered)
  and plain point lists.

**Two new, previously-undocumented recipe-environment quirks**, both found
while wiring the fuselage sub-recipe's nose section and both documented inline
at their own call sites:
- `:as(Type)`, already known to have a real `geoml::Shape`-specific segfault
  (see "airplane_seats: real line-by-line re-port" above), has a *separate*
  sharp edge too: calling it on `profile_curve_cylinder`/`arc_b_spline_curve`
  (both needed for `:FirstParameter()`/`:D1()` method calls) corrupted an
  entirely unrelated, already-built lazy value elsewhere in the same recipe
  graph - `rear_srf_poles`'s own `occt.gp_Pnt.as_vec(...)` call started failing
  with "no matching function call", and the error disappeared completely once
  the unrelated `:as()` calls were removed. Worked around by moving every
  native-type-dependent call (`FirstParameter`/`LastParameter`/`D1`) into
  `modules:` helper functions instead (module code runs outside the decorated
  environment, so no `:as()` is needed there at all) - the same general
  "push it into a module" strategy `airplane_seats.grr.yml` already established
  for a different reason.
- A `modules:` function's multiple return values don't both survive a
  `a, b = helpers.fn(...)` multi-assignment in the decorated `steps:`
  environment - the second one silently comes back `nil` (confirmed directly:
  `tangent_at`'s own gp_Vec result was nil until split into two separate
  single-return functions, `point_at`/`tangent_at`, each calling the cheap `D1`
  independently).

**Deliberately not ported**: the original's own dead horizontal-tail
("Hoehenleitwerk")/vertical-tail ("Seitenleitwerk") computation (~440 lines,
building several more B-spline surfaces) - confirmed, by reading all the way to
the original's own final assembly, that none of these surfaces' outputs are
ever consumed by the final shape at all. Omitting it loses no faithfulness to
the real output geometry; it was dead code in the original too.

**Not run under `-e ad`**: `geoml.nurbs_curve`'s own `std::vector<Standard_Real>`
weights/knots parameters (`generated/curves.cpp`) are not dual-emitted for AD
the way scalar `Standard_Real` parameters are (compare `generated/
transformations.cpp`'s own `translate`) - they stay `std::vector<Standard_Real>`
in both builds, which under AD means `std::vector<Standard_Adouble>`.
`occt.real_vec` (`grunk-occt`'s own `src/vector_extras.hpp`) always builds a
plain `std::vector<double>` with no AD-aware counterpart exposed to Lua, so the
fuselage sub-recipe's `geoml.nurbs_curve(..., occt.real_vec(...), ...)` calls
cannot be made from Lua at all under the AD build - confirmed by trying,
"sol: no matching function call takes this number of arguments and the
specified types". A real, separate gap in grunk-occt's own `vector_extras.hpp`,
not something to paper over here (`wing_fuselage_configuration.grr.yml` is a
Step 6 port, not part of the AD verification track
`ad_translate.grr.yml`/`tests/test_ad.cpp` cover) - `examples/run_example.py`
simply skips this recipe under `-e ad`, with a message explaining why.

**Verified end to end**: both sub-recipes export a well-formed solid
independently, with bounding boxes matching their own literal input data
exactly (e.g. the wing's own Y-max of `-375.046` is an exact match to the inner
station's translated Y coordinate, `-1875.046 + 1500`); the final fused shape's
own bounding box, `[1000,-16963.5,-4000] - [37500,3464.1,2000]`, is exactly the
union of the two. `pixi run -e default run_examples` (all 6 recipes, including
this one) and `pixi run -e default test` (23/23, unaffected) both green from a
clean state; `pixi run -e ad run_examples` green too, with this recipe's own
skip message and every other recipe (including `ad_translate.grr.yml`'s real
derivative check) unaffected.

## `geoml.Shape.new(TopoDS_Compound)` cross-platform sol2 failure: widened constructor overload set (2026-11-11)

`load_carrier.grr.yml` passed on `ubuntu-latest` (GCC) but failed on
`macos-latest`/`windows-latest` (AppleClang/MSVC) with sol2's own generic
`"sol: no matching function call takes this number of arguments and the
specified types"` - no file/line information at all. A temporary diagnostic
(`examples/debug_load_carrier.py`, extra `outputs:` in `load_carrier.grr.yml`,
a `debug_load_carrier` pixi task, and a CI step running it only on failure -
all since removed) queried every intermediate value the recipe's own `steps:`
compute, in order, and localized the failure to exactly one call:
`spheres_wrapped = geoml.Shape.new(sphere_compound)`. The step right before it,
`rounded_box_wrapped = geoml.Shape.new(rounded_box)`, calls the exact same
constructor and succeeded on every platform - the only difference is that
`rounded_box` already *is* a `TopoDS_Shape` (from
`BRepFilletAPI_MakeFillet::Shape()`), while `sphere_compound` is a
`TopoDS_Compound` (from `helpers.make_compound`, built via
`BRep_Builder::MakeCompound`).

`geoml::Shape`'s only non-default real constructor takes `TopoDS_Shape const&`
(`geoml/naming_choosing/Shape.h`). `TopoDS_Compound -> TopoDS_Shape` is a plain,
non-virtual public base-class relationship, correctly registered in
grunk-occt's own `generated/sol_base_classes.hpp`
(`SOL_BASE_CLASSES(TopoDS_Compound, TopoDS_Shape)` /
`SOL_DERIVED_CLASSES(TopoDS_Shape, ..., TopoDS_Compound, ...)` - pure
compile-time `sol::base<T>`/`sol::derive<T>` trait specializations, no
static-init-order dependency, confirmed by reading `sol/forward.hpp`'s own
macro expansion). So sol2 has to walk that base-class chain at runtime to see
that a `TopoDS_Compound` userdata satisfies a `TopoDS_Shape const&` parameter -
and that derived-to-base lookup through a *reference* constructor parameter is
exactly the restriction this project already has one documented instance of,
for a different type shape: `hooks.py`'s own `Handle(X)`-by-value
`emit_param()` override (both here and in grunk-occt) exists because "sol2's
own base-class-aware ... unique-usertype conversion ... only ever applies to a
*by-value* parameter" (`qualified_checker`/`qualified_getter` explicitly
exclude reference parameters from that code path). This is that same sol2
restriction, hit for a plain value type (`TopoDS_Shape`) instead of a
`Handle(X)`, and crossing a plugin boundary (the `TopoDS_Compound` userdata is
pushed by grunk-occt's plugin, consumed by grunk-geoml's `Shape` ctor).

**Fix**: rather than attempt a by-value workaround for `Shape`'s own
constructor parameter (whose cross-platform safety for a *plain* usertype, as
opposed to a `Handle(X)` unique-usertype, was never confirmed), `codegen/
hooks.py`'s `classify_ctor_mode`/`emit_constructor` now special-case the
`Shape` class by name (`SHAPE_TOPODS_WIDEN_CLASS`/
`SHAPE_TOPODS_WIDEN_CTOR_MODE`): its real `TopoDS_Shape`-taking constructor is
widened into one exact-match candidate lambda per concrete `TopoDS_Shape`
subtype grunk-occt's own `sol_base_classes.hpp` registers (`TopoDS_CompSolid`,
`TopoDS_Compound`, `TopoDS_Edge`, `TopoDS_Face`, `TopoDS_Shell`,
`TopoDS_Solid`, `TopoDS_Vertex`, `TopoDS_Wire`), each candidate still calling
the exact same real `Shape(TopoDS_Shape const&)` constructor via a genuine,
always-safe C++ implicit upcast - this isn't inventing API surface, only
giving sol2 an exact type to match per candidate instead of a base-class
lookup to perform. `codegen/config.yml`'s own `naming_choosing` module gained
an `extra_includes` entry for the 8 subtype headers (needed by the generated
lambdas, not by `Shape.h` itself).

**Verified**: regenerated `generated/naming_choosing.cpp` (the only file that
should change - a stray local `.deps/geoml` build missing the AD-portability
`Standard_Real` dual-emission entirely caused several *other* modules to
regenerate differently too; those were discarded, not committed, since they
reflect a local environment mismatch, not an intended change).
`pixi run -e default build`/`test` (23/23)/`run_examples` (all 6 recipes,
including `load_carrier.grr.yml`) all green from a clean state on Linux.
`codegen/hooks.py`'s own gap this surfaced: `generate_code`'s CMake target only
ever passes a single `--include-dir ${GEOML_INCLUDE_DIR}`, with no equivalent
of the plain build's own `GEOML_INTERNAL_INCLUDE_DIRS`/`OpenCASCADE_INCLUDE_DIR`
- parsing any header that reaches an OCCT header or a `geoml/internal/*` bare
quoted include fails outright. Not fixed here (regeneration was done via a
one-off script monkey-patching `build_clang_args()` to add the missing
`-isystem` dirs) - `generate_code`'s own `CMakeLists.txt` custom_target should
gain the same include dirs the `geoml_plugin` target itself already gets,
worth a real fix next time this target needs to run.

Cross-platform CI confirmation (the only check that actually exercises
AppleClang/MSVC) is still pending as of this entry.

## `geoml.count_faces(TopoDS_Shell)` cross-platform sol2 failure: same restriction, hand-written call sites (2026-11-11)

A second, follow-up CI run (after the `geoml.Shape.new` fix above actually
landed and got past that call) surfaced the *same* sol2 restriction
(reference-parameter derived-to-base dispatch not reliably applying across
AppleClang/MSVC, see the entry above for the full mechanism) at a different
call site, this time on `windows-latest`/MSVC only:

```
[sol2] An exception occurred: stack index 1, expected userdata, received
sol.TopoDS_Shell: value at this index does not properly reflect the desired
type (bad argument into 'int(const TopoDS_Shape&)')
```

localized to `examples/load_carrier.grr.yml`'s `helpers.remove_two_faces`,
specifically `geoml.count_faces(shell)` where `shell` is a `TopoDS_Shell`
(built by `helpers.make_shell`) and `count_faces` is declared
`[](TopoDS_Shape const& shape) -> int` in `src/topexp_extras.hpp`.

Unlike `geoml::Shape`'s constructor, `nth_face`/`nth_edge`/`count_faces`/
`count_edges` (all in `src/topexp_extras.hpp`) are **hand-written**
`ns.register_function(...)` calls, not generated — so the fix is applied by
hand, same shape as the codegen one: every concrete `TopoDS_Shape` subtype
gets its own exact-match overload candidate (via `sol::overload(...)`, which
`grunk::plugin_namespace::register_function`/`create_function_meta` already
support directly - confirmed via the existing precedent in
`src/transform_extras.hpp`'s `Transform::operator()` overload set), all
delegating to the one real `TopoDS_Shape`-taking implementation through a
genuine, always-safe C++ implicit upcast.

One wrinkle not present in the codegen case: a `sol::overload` candidate must
have a fixed, non-template `operator()` - sol2 inspects each candidate's own
`function_traits` to build its stack-arg checkers, so a single generic/
templated lambda (`auto&&... args`) covering all 4 functions does not work
(confirmed directly: `register_function` rejected it, "no matching member
function"). `src/topexp_extras.hpp` uses two small macros instead
(`GEOML_PLUGIN_TOPODS_SHAPE_OVERLOAD_1`/`_2`, for the 1-arg and 2-arg
functions respectively), each expanding to 9 concrete, non-template lambdas.

Verified locally (Linux): `pixi run -e default build` (clean), `pixi run -e
default test` (23/23 ctest passed), `pixi run -e default run_examples` (all 6
recipes passed, including `load_carrier.grr.yml`: `export_brep succeeded`).
Cross-platform CI confirmation (macOS/Windows, the actual repro platform for
this one) is still pending as of this entry - same caveat as the entry above.
