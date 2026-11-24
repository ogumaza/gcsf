// SPDX-License-Identifier: MIT

/*
 * What GCSF's guest drivers share. ntdlib's drivers/guest.h defines the C types, the status values, the types of the
 * game's functions that a driver calls and the reads of the game's memory. This file adds the magic and the version of
 * the parameter block (docs/gcsf.md, "Driver"), the most ports that a player sets, and the helpers that switch external
 * interrupts off and on.
 *
 * Every driver's parameter block, `driver_params`, starts with the magic, the version, the status word, the sound ID
 * and the sound's handle. The ripper writes the game's profile after them. The status word reports the driver's
 * progress to the player. A step that fails stops the driver with STATUS_ERROR and the step's number. Step 1 is the
 * driver's check of its block.
 *
 * Every driver includes this file first. After a change to it, rebuild every driver's blob with driver/build.py.
 */

#ifndef GCSF_DRIVER_H
#define GCSF_DRIVER_H

#include "guest.h"

#define PARAMS_MAGIC 0x47435344u /* 'GCSD' */
#define PARAMS_VERSION 1u

/* The most ports that a player sets, for a driver with a port list. The word after a driver's entry holds the list's
 * address (docs/gcsf.md, "Driver"). An entry gives a port's number in its high 16 bits and the port's value in its low
 * 16 bits. */
#define MAX_PORTS 16u

/* The bit of the machine state register that lets external interrupts in, such as the audio interface's and the
 * DSP's. */
#define MSR_EE 0x8000u

/* Turns external interrupts off. Returns the machine state to restore. */
static inline u32 disable_interrupts(void) {
    u32 msr;
    __asm__ volatile("mfmsr %0" : "=r"(msr));
    __asm__ volatile("mtmsr %0" : : "r"(msr & ~MSR_EE) : "memory");
    return msr;
}

/* Puts back the machine state that disable_interrupts returned. */
static inline void restore_interrupts(u32 msr) {
    __asm__ volatile("mtmsr %0" : : "r"(msr) : "memory");
}

#endif
