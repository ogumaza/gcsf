// SPDX-License-Identifier: MIT

/*
 * The allocator of a driver that hands out the OS's arena itself: musyx.c and mario_party.c. A driver declares
 * `heap_next` and `heap_end` and includes this file where it defines its functions. Nothing that a rip plays frees
 * memory. colosseum_bonus.c keeps an allocator with another form of the check. One form for all three drivers would
 * change the code in either that blob or the other two.
 */

#ifndef GCSF_DRIVER_ARENA_H
#define GCSF_DRIVER_ARENA_H

/* `size` bytes of the arena, at a 32-byte boundary: the DSP and the disc require that for their transfers. Returns 0
 * when the arena is used up. */
static u32 heap_alloc(u32 size) {
    const u32 at = (heap_next + 31) & ~31u;
    if (at + size < at || at + size > heap_end)
        return 0;
    heap_next = at + size;
    return at;
}

#endif
