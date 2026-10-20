// SPDX-License-Identifier: MIT

/*
 * GCSF guest driver for a MusyX game whose sample file doesn't fit in main memory, such as Pokémon Channel. It's
 * driver/musyx.c with MUSYX_UPLOAD defined. MusyX takes each group's samples through its upload callback, a chunk at a
 * time from the rip's sample file. The driver also sets MusyX's master volume. driver/musyx.c describes both.
 *
 * Build with `driver/build.py musyx_upload` (clang), after any change to driver/musyx.c too. The result is committed as
 * src/rip/driver_musyx_upload_blob.h.
 */

#define MUSYX_UPLOAD 1

#include "musyx.c"
