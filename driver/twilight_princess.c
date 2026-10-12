// SPDX-License-Identifier: MIT

/*
 * GCSF guest driver for Twilight Princess. The game's sound layer, Z2, sits on JAudio2. The driver is compiled for
 * Gekko and stored in the game's .gcsflib. The player boots the game's startup code: the OS initialisation and the
 * static constructors. The static constructors construct the game's audio manager. The rip patches the game's main to
 * branch here.
 *
 * The driver checks its parameter block, then runs ntdlib's start-up of Z2 (jaudio/z2.h) with the GameCube's steps:
 * the video interface, whose retraces pace the driver as they pace the game's frames, and the ARAM manager with its
 * transfer buffer. The game's audio process also reads the reset state. The game's main allocates that state, and the
 * driver replaces the game's main. `driver_params.status` reports the driver's progress to the player (see
 * docs/gcsf.md).
 *
 * Build with `driver/build.py twilight_princess` (clang). The result is committed as
 * src/rip/driver_twilight_princess_blob.h. Building GCSF itself requires no PowerPC toolchain.
 */

#include "jaudio/z2.h"

#define PARAMS_MAGIC 0x47435344u /* 'GCSD' */
#define PARAMS_VERSION 1u

struct Params {
    u32 magic;
    u32 version;
    u32 status; /* written by the driver */
    struct Z2Params z2;
};

__attribute__((section(".params"), used)) volatile struct Params driver_params = {
    .magic = PARAMS_MAGIC,
    .version = PARAMS_VERSION,
};

static void z2_init_machine(volatile struct Z2Params* p) {
    ((z2_fn0)p->vi_init)();
}

static void z2_wait_frame(volatile struct Z2Params* p) {
    for (u32 i = 0; i < p->frame_retraces; i++)
        ((z2_fn0)p->vi_wait_for_retrace)();
}

static void z2_create_aram(volatile struct Z2Params* p) {
    ((z2_fn5)p->create_aram)(p->aram_audio_size, p->aram_graph_size, (p->aram_priorities >> 16) & 0xff,
                             (p->aram_priorities >> 8) & 0xff, p->aram_priorities & 0xff);
}

static void z2_set_aram_buffer(volatile struct Z2Params* p, u32 system_heap) {
    ((z2_fn3)p->set_aram_buffer)(0, p->aram_buffer_size, system_heap);
}

__attribute__((noreturn)) void driver_main(void) {
    volatile struct Params* p = &driver_params;
    if (p->magic != PARAMS_MAGIC || p->version != PARAMS_VERSION)
        z2_fail(&p->z2, &p->status, 1);
    z2_main(&p->z2, &p->status);
}

__attribute__((naked, section(".text.entry"))) void driver_entry(void) {
    __asm__ volatile("b driver_main");
}
