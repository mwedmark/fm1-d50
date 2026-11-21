#!/usr/bin/env bash
# fm1-base and felucca-bsp for the build on GitHub (issue #36): the fork's d50-layout at a fixed commit (fm1.toml data_lo, also in the
# host and web builds), and felucca-bsp upstream at a fixed commit with tools/ci/felucca-bsp-d50-layout.patch. Usage: fetch_platform.sh DIR
set -euo pipefail
FM1_BASE_COMMIT=936eec20b1ce1605b22cfd787acc42a546ec8f5e   # mwedmark/fm1-base d50-layout
BSP_COMMIT=2a5af3dfc8bccb92e37d48a3e7e0bf9a5827e51f        # hugelton/felucca-bsp, before the patch
base="$1"
here="$(cd "$(dirname "$0")" && pwd)"
git clone -q https://github.com/mwedmark/fm1-base.git "$base"
git -C "$base" checkout -q "$FM1_BASE_COMMIT"
rm -rf "$base/bsp"
git clone -q https://github.com/hugelton/felucca-bsp.git "$base/bsp"
git -C "$base/bsp" checkout -q "$BSP_COMMIT"
git -C "$base/bsp" apply "$here/felucca-bsp-d50-layout.patch"
echo "fm1-base ${FM1_BASE_COMMIT:0:7}, felucca-bsp ${BSP_COMMIT:0:7} + the d50-layout patch in $base"
