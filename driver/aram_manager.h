// SPDX-License-Identifier: MIT

/*
 * The ARAM manager of a driver for a game on JSystem: four_swords.c, wind_waker.c, luigis_mansion.c,
 * super_mario_sunshine.c, jungle_beat.c, double_dash.c, pikmin2.c, pacman_vs.c and collectors_edition.c. A driver
 * includes this file after `driver_params`. Its Params hold `create_aram`, `aram_audio_size`, `aram_graph_size` and
 * `aram_priorities`.
 */

#ifndef GCSF_DRIVER_ARAM_MANAGER_H
#define GCSF_DRIVER_ARAM_MANAGER_H

/* JKRAram::create, with the profile's ARAM for audio and for graphics and the ARAM manager's three thread priorities.
 * `aram_priorities` holds the priorities, a byte each, from bit 16 down. */
static void create_aram_manager(void) {
    volatile struct Params* p = &driver_params;
    ((fn5)p->create_aram)(p->aram_audio_size, p->aram_graph_size, (p->aram_priorities >> 16) & 0xff,
                          (p->aram_priorities >> 8) & 0xff, p->aram_priorities & 0xff);
}

#endif
