#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

# Downloads, builds, and installs ADOL-C 2.7.2 into .deps/adolc - run via
# `pixi run install_adolc` (see README.md's "Algorithmic differentiation (AD)").
# Copied verbatim from grunk-occt/scripts/install_adolc.sh (and, before that,
# grunk-adolc's own) - a generic ADOL-C bootstrap, not specific to any one repo.
# Idempotent: skips the whole build if that prefix already looks populated.
#
# Needs autoreconf/automake/libtool on PATH - not a pixi dependency of this repo,
# since nothing else here needs autotools.
set -euo pipefail

ROOT="${PIXI_PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
ADOLC_PREFIX="$ROOT/.deps/adolc"

if [ -f "$ADOLC_PREFIX/include/adolc/adtl.h" ]; then
    echo "ADOL-C already installed at $ADOLC_PREFIX - skipping (delete this directory to force a rebuild)."
    exit 0
fi

mkdir -p "$ROOT/build/_deps"
cd "$ROOT/build/_deps"

if [ ! -d ADOL-C ]; then
    git clone https://github.com/coin-or/ADOL-C.git
fi
cd ADOL-C
git checkout releases/2.7.2

mkdir -p "$ADOLC_PREFIX"
autoreconf -fi
./configure --prefix="$ADOLC_PREFIX" --enable-atrig-erf --with-boost=no
make -j
make install

echo "ADOL-C installed to $ADOLC_PREFIX"
ls "$ADOLC_PREFIX"/lib* 2>/dev/null
