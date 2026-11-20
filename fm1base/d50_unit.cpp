// SPDX-License-Identifier: GPL-3.0-or-later
// The D-50's one compiled unit on fm1-base (fm1.toml o2): the engine's glue (fm1_open/app/d50/d50.cpp, its FM1_BASE part) and the two C files
// it needs. Here and not in the platform's unity build, where their static names (decode, fixed, ...) would meet the platform's.
extern "C" {
#include "../fm1_open/app/d50/d50_inflate.c"
// (d50_math.c and fabsf: inline, from inc/math.h)
}
#include "../fm1_open/app/d50/d50.cpp"
