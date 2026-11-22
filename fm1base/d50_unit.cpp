// SPDX-License-Identifier: GPL-3.0-or-later
// The D-50's one compiled unit on fm1-base (fm1.toml o2): the engine's glue (d50/d50.cpp) and the two C files
// it needs. Here and not in the platform's unity build, where their static names (decode, fixed, ...) would meet the platform's.
extern "C" {
#include "../d50/d50_inflate.c"
// (d50_math.c and fabsf: inline, from inc/math.h)
}
#include "../d50/d50.cpp"
