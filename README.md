<!--
SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>

SPDX-License-Identifier: MPL-2.0
-->

# grunk-geoml

A [grunk](https://github.com/grunk-dev/grunk) plugin that exposes [geoml](https://github.com/DLR-SC/geoml)
(a thin, higher-level API layered directly over raw OpenCASCADE types - curve/surface interpolation and
lofting, boolean operations, shape naming/tracking and predicates, coordinate transforms) as building
blocks for grunk models, on top of [grunk-occt](https://github.com/grunk-dev/grunk-occt)'s own OCCT
bindings.

**Status**: early / WIP, not started building yet - this repository currently only carries its
scaffolding (build system, CI, licensing) and design log; the code generator config and the first
wrapped modules land in a later step. See `DESIGN.md` for the plan this is being built against.

geoml's own public API takes/returns raw OCCT types directly - `gp_*`, `TopoDS_*`, and, critically,
`Handle(Geom_*)` (`opencascade::handle<T>`) - the same types `grunk-occt` already wraps. This plugin
deliberately does **not** re-register those types itself: sol2 usertypes are keyed by C++ type identity
and shared automatically across every plugin loaded into the same Lua state, so a `Handle(Geom_BSplineCurve)`
produced by a `geoml.*` call and one produced by an `occt.*` call are the exact same Lua-visible type,
with zero bridging code - see `DESIGN.md`'s "Handle(X) cross-plugin sharing" section for how this was
verified (it directly contradicts an earlier, unverified claim in grunk-occt's own `DESIGN.md` that this
needed special shared-registration machinery - it doesn't).

## Algorithmic differentiation (AD)

Like `grunk-occt`, this plugin is designed to also build against an AD-enabled toolchain: geoml's own
[`feature/autodiff`](https://github.com/DLR-SC/geoml/tree/feature/autodiff) branch
(`-DGEOML_USE_ADOLC=ON`) linked against [adOCCT](https://github.com/dlr-sp/adOCCT), mirroring
`grunk-occt`'s `GRUNK_OCCT_ENABLE_AD` build option. This plugin's own equivalent option is
`GRUNK_GEOML_ENABLE_AD`.

**Mixing `occt.*` and `geoml.*` AD-carrying values in one recipe requires both plugins to have been
built with their AD option pointed at the *same* adOCCT install.** Two AD builds against *different*
adOCCT installs would produce two independently-compiled, ODR-incompatible `Standard_Real`/
`adtl::adouble` types loaded into one process - silently wrong or a hard crash, not a clean error. There
is no build-time check for this today; keep exactly one `ADOCCT_DIR` per machine/CI job and point both
plugins' AD builds at it.

## Project structure

```
codegen/    # this plugin's own config.yml + hooks.py (not yet added - see DESIGN.md)
generated/  # generator output, checked into git once codegen/ lands
src/        # plugin entry point + a handful of hand-written registrations for the
            # few things the generator can't produce on its own
examples/   # runnable example recipes (not yet added)
tests/      # one gtest file per geoml module wrapped (not yet added)
```

## Dependencies

| Dependency | Source |
| -- | -- |
| grunk | pinned commit, built and installed automatically into `.deps/grunk` (`scripts/install_grunk.cmake`) |
| geoml | pinned commit on `main`, built and installed automatically into `.deps/geoml` (`scripts/install_geoml.cmake`), built with `-DGEOML_INSTALL_INTERNAL_API=ON` against stock OCCT |
| occt (OpenCASCADE) | conda-forge, `7.9.3` (same as `grunk-occt`) |
| lua, sol2, taskflow, yaml-cpp, libparametric | conda-forge (needed to build grunk itself) |
| gtest | conda-forge, the test framework |
| ADOL-C, adOCCT, geoml `feature/autodiff` (`-DGEOML_USE_ADOLC=ON`) | built from source, only needed for the AD build (see above) |

## License

This project is licensed under the Mozilla Public License 2.0 - see the
[license file](LICENSES/MPL-2.0.txt) for details.
