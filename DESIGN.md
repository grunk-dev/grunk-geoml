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
