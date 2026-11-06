// SPDX-License-Identifier: MIT

/*
 * GCSF guest driver for a MusyX game whose code sets the MIDI controllers of a song's channels once the song plays,
 * such as Resident Evil. It's driver/musyx.c with MUSYX_SONG_CONTROLS defined. An OS alarm sets a song's controllers
 * through sndSeqCntrl after the song's start. driver/musyx.c describes it.
 *
 * Build with `driver/build.py musyx_song_controls` (clang), after any change to driver/musyx.c too. The result is
 * committed as src/rip/driver_musyx_song_controls_blob.h.
 */

#define MUSYX_SONG_CONTROLS 1

#include "musyx.c"
