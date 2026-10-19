// SPDX-License-Identifier: MIT

/*
 * What GCSF's guest drivers share: the C types, the parameter block's magic, version and status values (docs/gcsf.md,
 * "Driver"), the types of the game's functions that a driver calls, and the helpers that read the game's memory and
 * switch external interrupts off and on.
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

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;

#define PARAMS_MAGIC 0x47435344u /* 'GCSD' */
#define PARAMS_VERSION 1u

/* Values of Params.status. They're macros because an enumerator can't hold 0x80000000 before C23. */
#define STATUS_BOOTING 0u
#define STATUS_PLAYING 1u
#define STATUS_FINISHED 2u
#define STATUS_ERROR 0x80000000u /* | step number */

/* The frames that a driver waits for the game before it gives up on a step. */
#define SETUP_FRAMES 600u

/* The most waves that a .minigcsf names, for a driver whose block holds a wave list after the sound's handle. */
#define MAX_WAVES 64u

/* The bit of the machine state register that lets external interrupts in, such as the audio interface's and the
 * DSP's. */
#define MSR_EE 0x8000u

/* The game's functions, by their number of arguments. A driver passes each argument and takes each result as a u32. */
typedef u32 (*fn0)(void);
typedef u32 (*fn1)(u32);
typedef u32 (*fn2)(u32, u32);
typedef u32 (*fn3)(u32, u32, u32);
typedef u32 (*fn4)(u32, u32, u32, u32);
typedef u32 (*fn5)(u32, u32, u32, u32, u32);
typedef u32 (*fn6)(u32, u32, u32, u32, u32, u32);
typedef u32 (*fn7)(u32, u32, u32, u32, u32, u32, u32);
typedef u32 (*fn9)(u32, u32, u32, u32, u32, u32, u32, u32, u32);

/* The word and the byte at `address`. */
static inline u32 word(u32 address) {
    return *(volatile u32*)address;
}

static inline u8 byte(u32 address) {
    return *(volatile u8*)address;
}

/* The big-endian halfword and word at `p`. */
static inline u32 be16(const u8* p) {
    return (u32)p[0] << 8 | p[1];
}

static inline u32 be32(const u8* p) {
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

/* A virtual function of `object`. The vtable is the object's first word. */
static inline u32 virtual_function(u32 object, u32 slot) {
    return word(word(object) + slot);
}

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
