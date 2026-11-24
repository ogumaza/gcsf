// SPDX-License-Identifier: MIT

/*
 * The first heaps of a driver for a game on JSystem: double_dash.c, collectors_edition.c, four_swords.c and
 * pikmin2.c. A driver includes this file after `driver_params` and frames.h. Its Params contain `create_root_heap`,
 * `create_exp_heap`, `max_heaps` and `system_heap_size`.
 */

#ifndef GCSF_DRIVER_JSYSTEM_HEAPS_H
#define GCSF_DRIVER_JSYSTEM_HEAPS_H

/* The root heap with room for `max_heaps` heaps, then an expanded heap of `system_heap_size` bytes in it. The new heap
 * becomes JKRHeap's system heap and the current heap. Stops the driver at step 2 if either heap can't be made. Returns
 * the root heap, and the system heap in `system`. */
static u32 create_jsystem_heaps(u32* system) {
    volatile struct Params* p = &driver_params;
    const u32 root = ((fn2)p->create_root_heap)(p->max_heaps, 0);
    *system = root ? ((fn3)p->create_exp_heap)(p->system_heap_size, root, 0) : 0;
    if (!*system)
        fail(2);
    return root;
}

#endif
