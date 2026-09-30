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
last piece of Step 6, not done here.

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
