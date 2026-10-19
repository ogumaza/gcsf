// SPDX-License-Identifier: MIT

/*
 * The wait and the failure of a driver that runs the game's frames itself: four_swords.c, wind_waker.c,
 * luigis_mansion.c, super_mario_sunshine.c, pikmin.c, jungle_beat.c, double_dash.c, pikmin2.c, pacman_vs.c and
 * collectors_edition.c. A driver includes this file after `driver_params`. Its Params hold `status`, `frame_retraces`
 * and `vi_wait_for_retrace`.
 */

#ifndef GCSF_DRIVER_FRAMES_H
#define GCSF_DRIVER_FRAMES_H

/* One frame of the game: `frame_retraces` video retraces. */
static void wait_frame(void) {
    for (u32 i = 0; i < driver_params.frame_retraces; i++)
        ((fn0)driver_params.vi_wait_for_retrace)();
}

/* Stops the driver at `step`. The status word reports the step, and the driver waits for ever. */
__attribute__((noreturn)) static void fail(u32 step) {
    driver_params.status = STATUS_ERROR | step;
    for (;;)
        wait_frame();
}

#endif
