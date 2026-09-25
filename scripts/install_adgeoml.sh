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
