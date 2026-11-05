#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

# Clones, builds, and installs geoml's `feature/autodiff` branch, built with
# -DGEOML_USE_ADOLC=ON against an AD-enabled OCCT fork (adOCCT), into .deps/adgeoml -
# run via `pixi run install_adgeoml` (see README.md's "Algorithmic differentiation
# (AD)"). Mirrors grunk-occt/scripts/install_adocct.sh's own shape (bash, Linux-only,
# matching the `ad` pixi feature both scripts belong to; idempotent - skips the build
# if that prefix already looks populated). Deliberately a separate script from
# scripts/install_geoml.cmake, not a branch of it - this is a wholly separate geoml
# checkout (a different branch, a different OCCT it links against), never
# reconfigured in place from the stock install.
#
# GEOML_USE_ADOLC_MODE defaults to geoml's own CMake default ("forward") - not
# overridden here, matching adOCCT's own forward-mode-only build
# (ADOLC_REVERSE_MODE=OFF, see install_adocct.sh) that this install links against;
# confirmed by reading geoml's own src/CMakeLists.txt directly rather than assumed.
#
# Depends on ADOL-C and adOCCT already being installed (scripts/install_adolc.sh,
# scripts/install_adocct.sh - the same installs grunk-occt's own AD build uses; point
# both plugins' AD builds at the *same* ADOCCT_DIR, see README.md).
set -euo pipefail

ROOT="${PIXI_PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
ADOLC_PREFIX="$ROOT/.deps/adolc"
ADOCCT_PREFIX="$ROOT/.deps/adocct"
ADGEOML_PREFIX="$ROOT/.deps/adgeoml"
GEOML_REV="6676470c39dfabcc6d9d2c34a04654eb9e3a324a"

if [ -f "$ADGEOML_PREFIX/lib/cmake/geoml/geoml-config.cmake" ] || \
   [ -f "$ADGEOML_PREFIX/lib64/cmake/geoml/geoml-config.cmake" ]; then
    echo "AD-enabled geoml already installed at $ADGEOML_PREFIX - skipping (delete this directory to force a rebuild)."
    exit 0
fi

if [ ! -f "$ADOLC_PREFIX/include/adolc/adtl.h" ]; then
    echo "ADOL-C not found at $ADOLC_PREFIX - run 'pixi run install_adolc' first." >&2
    exit 1
fi

if [ ! -f "$ADOCCT_PREFIX/lib/cmake/opencascade/OpenCASCADEConfig.cmake" ] && \
   [ ! -f "$ADOCCT_PREFIX/lib64/cmake/opencascade/OpenCASCADEConfig.cmake" ]; then
    echo "adOCCT not found at $ADOCCT_PREFIX - run 'pixi run install_adocct' first." >&2
    exit 1
fi

mkdir -p "$ROOT/build/_deps"
cd "$ROOT/build/_deps"
if [ ! -d geoml-autodiff ]; then
    git clone -b feature/autodiff https://github.com/DLR-SC/geoml.git geoml-autodiff
fi
cd geoml-autodiff
git fetch origin "$GEOML_REV"
git checkout "$GEOML_REV"

# Same OCCT-7.9-compatibility gap as the stock build (scripts/install_geoml.cmake's
# own identical patch application, see its own comment for the full list of fixes
# and why) - geoml's `feature/autodiff` branch shares the same pre-7.9 source for
# the handful of files this patch touches, confirmed by applying cleanly here too.
# Not AD-specific at all; adOCCT is itself OCCT 7.9-based, so the exact same
# HashCode(int)/math_Vector/Standard_values.h/*.hpp-install-rule issues reproduce
# building against it. --reverse --check is a read-only probe (see git-apply(1)),
# keeping this idempotent against an already-patched checkout the same way the
# rest of this script is against an already-installed prefix.
if ! git apply --reverse --check "$ROOT/scripts/geoml-occt79-compat.patch" 2>/dev/null; then
    git apply "$ROOT/scripts/geoml-occt79-compat.patch"
fi

# A second, genuinely AD-specific compatibility gap on top of the one above - found
# building this branch for real, for the first time, against the adOCCT/ADOL-C
# versions this project pins:
#   - src/common/ad_utility_function.h's own getPrimal<double>/getPrimal<Standard_Real>/
#     getPrimal<adtl::adouble> explicit specializations collide (an ODR/redefinition
#     error) with adOCCT's own Standard_Real.hxx, which now provides an equivalent
#     generic getPrimal<T>(x) { return x.getValue(); } template itself - geoml's own
#     branch predates adOCCT gaining this. Guarded out when building against an
#     AD-enabled adOCCT (GEOML_ADOLC_FORWARD/GEOML_ADOLC_REVERSE), relying on
#     adOCCT's own copy instead.
#   - src/common/CommonFunctions.cpp's GetNearestFace stored a running minimum
#     distance in a plain `float`, static_cast from Standard_Real - under AD,
#     Standard_Real is Standard_Adouble (ADOL-C's adtl::adouble), which has no
#     narrowing conversion to float at all. Switched to a plain Standard_Real
#     (the natural type for a value that's only ever compared against another
#     Standard_Real, never consumed as output) - no cast needed, works unchanged
#     for both the AD and non-AD build.
#   - src/math/MathFunctions.cpp's pow_deriv did `int(k)` directly on a
#     Standard_Real to check whether an exponent is (close to) a natural number -
#     adtl::adouble has no int conversion operator at all. This check is a
#     discrete, value-only branch condition (not something that needs to carry a
#     derivative itself), so it now extracts the primal value via getPrimal(k)
#     first, the same utility used throughout this branch for exactly this
#     purpose - the file didn't already include that header, now does.
# Worth reporting upstream to DLR-SC/geoml directly (these are real gaps in
# `feature/autodiff`'s own AD-portability, not anything specific to this plugin);
# not yet done - see DESIGN.md.
if ! git apply --reverse --check "$ROOT/scripts/geoml-ad-occt79-compat.patch" 2>/dev/null; then
    git apply "$ROOT/scripts/geoml-ad-occt79-compat.patch"
fi

mkdir -p build && cd build
# PKG_CONFIG_PATH (a process environment variable, not a CMake cache variable) is
# what geoml's own cmake/UseADOLC.cmake pkg_check_modules(ADOLC ...) call needs to
# find ADOL-C's adolc.pc - same mechanism grunk-adolc's own CMakeLists.txt relies on.
export PKG_CONFIG_PATH="$ADOLC_PREFIX/lib64/pkgconfig:$ADOLC_PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
cmake -G Ninja \
    -D CMAKE_BUILD_TYPE=Release \
    -D CMAKE_INSTALL_PREFIX="$ADGEOML_PREFIX" \
    -D CMAKE_PREFIX_PATH="$ADOCCT_PREFIX" \
    -D CMAKE_POSITION_INDEPENDENT_CODE=ON \
    -D GEOML_INSTALL_INTERNAL_API=ON \
    -D GEOML_USE_ADOLC=ON \
    ..
ninja install

echo "AD-enabled geoml installed to $ADGEOML_PREFIX"
