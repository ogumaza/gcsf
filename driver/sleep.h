// SPDX-License-Identifier: MIT

/*
 * The end and the failure of a driver whose thread sleeps once the sound has started: mario_golf.c, warioware.c,
 * musyx.c, crystal_chronicles.c and mario_party.c. The sound then plays from the interrupts. A driver includes this
 * file after `driver_params`. Its Params hold `status` and `os_sleep_thread`.
 */

#ifndef GCSF_DRIVER_SLEEP_H
#define GCSF_DRIVER_SLEEP_H

/* An OSThreadQueue (head and tail) that nothing wakes. */
static u32 never_woken[2];

/* The driver's thread sleeps for good. The OS then runs its idle loop. The player's CPU skips that loop to the next
 * interrupt. */
__attribute__((noreturn)) static void sleep_forever(void) {
    for (;;)
        ((fn1)driver_params.os_sleep_thread)((u32)never_woken);
}

/* Stops the driver at `step`. The status word reports the step, and the driver's thread sleeps. */
__attribute__((noreturn)) static void fail(u32 step) {
    driver_params.status = STATUS_ERROR | step;
    sleep_forever();
}

#endif
