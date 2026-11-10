#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

# Downloads, builds, and installs adOCCT (https://github.com/dlr-sp/adOCCT) into
# .deps/adocct, built against the ADOL-C install produced by `pixi run
# install_adolc` - run via `pixi run install_adocct` (see README.md's
# "Algorithmic differentiation (AD)"). Copied verbatim from
# grunk-occt/scripts/install_adocct.sh - a generic adOCCT bootstrap, not
# specific to any one repo; this plugin's own AD build points at the *same*
# .deps/adocct install grunk-occt's own AD build uses (see README.md). Idempotent:
# skips the build if that prefix already has an OpenCASCADEConfig.cmake (i.e.
# install completed last time).
#
# Installs to .deps/, not build/_deps/ - deliberately outside the CMake build
# directory (see install_adolc.sh's own comment for why: a routine `rm -rf build`
# must never force rebuilding all of OCCT from scratch too). Only the throwaway
# clone+build directory lives under build/_deps/.
#
# adOCCT is a full OCCT source build - same third-party dependency closure as any
# OCCT build (freetype etc.) - reused here via CMAKE_PREFIX_PATH=$CONDA_PREFIX
# (conda-forge's own `occt` package, already a root dependency of grunk-occt's own
# `default` environment, pulls the same closure) rather than resolved a second
# time. USE_ADOLC=ON/ADOLC_REVERSE_MODE=OFF (tapeless/forward mode, matching
# geoml's own GEOML_USE_ADOLC_MODE default - see install_adgeoml.sh) and
# 3RDPARTY_ADOLC_DIR are taken directly from grunk-occt's own working build.
#
# BUILD_MODULE_Visualization/ApplicationFramework/Draw=OFF: none of this
# plugin's own registration source (nor grunk-occt's) touches
# AIS_*/V3d_*/OpenGl_*/Xw_* (Visualization), OCAF/TDocStd
# (ApplicationFramework), or OCCT's own DRAWEXE test harness (Draw) - see
# grunk-occt/scripts/install_adocct.sh's own, longer comment for the full
# forensic writeup (X11/TKService stays required via DataExchange regardless
# of Visualization's flag; TKOpenGl's GL/glx.h requirement does NOT, which is
# what this flag actually fixes). Pruning these cuts the from-source build
# down to only what DataExchange/ModelingAlgorithms/ModelingData/
# FoundationClasses/DETools actually need.
#
# Stopgap, not a permanent fix: this module pruning (and the X11 apt
# dependency in ci.yml) exists only because adOCCT is built from source in
# CI. Once grunk-occt/adOCCT publish prebuilt binary packages,
# build-and-test-ad should download those instead of running this script at
# all, and this entire file (plus the X11 apt-get step in ci.yml) should be
# deleted rather than kept in sync - see grunk-occt/DESIGN.md's "AD
# validation milestone" section.
set -euo pipefail

ROOT="${PIXI_PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
ADOLC_PREFIX="$ROOT/.deps/adolc"
ADOCCT_PREFIX="$ROOT/.deps/adocct"

if [ -f "$ADOCCT_PREFIX/lib/cmake/opencascade/OpenCASCADEConfig.cmake" ] || \
   [ -f "$ADOCCT_PREFIX/lib64/cmake/opencascade/OpenCASCADEConfig.cmake" ]; then
    echo "adOCCT already installed at $ADOCCT_PREFIX - skipping (delete this directory to force a rebuild)."
    exit 0
fi

if [ ! -f "$ADOLC_PREFIX/include/adolc/adtl.h" ]; then
    echo "ADOL-C not found at $ADOLC_PREFIX - run 'pixi run install_adolc' first." >&2
    exit 1
fi

if [ -z "${CONDA_PREFIX:-}" ]; then
    echo "CONDA_PREFIX is not set - run this via 'pixi run install_adocct', not directly." >&2
    exit 1
fi

mkdir -p "$ROOT/build/_deps"
cd "$ROOT/build/_deps"
if [ ! -d adOCCT ]; then
    git clone https://github.com/dlr-sp/adOCCT.git
fi
cd adOCCT

mkdir -p build && cd build
cmake -G Ninja \
    -D CMAKE_INSTALL_PREFIX="$ADOCCT_PREFIX" \
    -D CMAKE_PREFIX_PATH="$CONDA_PREFIX" \
    -D USE_ADOLC=ON \
    -D ADOLC_REVERSE_MODE=OFF \
    -D 3RDPARTY_ADOLC_DIR="$ADOLC_PREFIX" \
    -D BUILD_MODULE_Visualization=OFF \
    -D BUILD_MODULE_ApplicationFramework=OFF \
    -D BUILD_MODULE_Draw=OFF \
    ..
ninja install

echo "adOCCT installed to $ADOCCT_PREFIX"
