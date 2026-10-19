// SPDX-License-Identifier: MIT

/*
 * The wait for the wave archives of a JAudio2 section, for a driver that loads every wave bank of the section:
 * four_swords.c, double_dash.c and collectors_edition.c. A driver includes this file after `driver_params`. Its Params
 * hold `section_data_offset`, `wave_banks_offset` and `wave_arc_state_offset`.
 */

#ifndef GCSF_DRIVER_SECTION_WAVES_H
#define GCSF_DRIVER_SECTION_WAVES_H

/* A wave archive's state once it is in ARAM. */
#define WAVES_LOADED 2u

/* Whether every wave archive of the section's wave banks is in ARAM. The section's data holds its table of wave banks
   at `wave_banks_offset`, and the table's count after it. A bank's vtable gives its archive at slot 0x10 and the
   number of its archives at slot 0x14. An archive without a file on the disc (entry number below 0) is never loaded. */
static int waves_loaded(u32 section) {
    volatile struct Params* p = &driver_params;
    const u32 data = word(section + p->section_data_offset);
    const u32 banks = word(data + p->wave_banks_offset);
    const u32 count = word(data + p->wave_banks_offset + 4);
    for (u32 b = 0; b < count; b++) {
        const u32 bank = word(banks + b * 4);
        if (!bank)
            continue;
        const u32 arcs = ((fn1)virtual_function(bank, 0x14))(bank);
        for (u32 i = 0; i < arcs; i++) {
            const u32 arc = ((fn2)virtual_function(bank, 0x10))(bank, i);
            if (arc && (s32)word(arc + p->wave_arc_state_offset + 4) >= 0 &&
                word(arc + p->wave_arc_state_offset) != WAVES_LOADED)
                return 0;
        }
    }
    return 1;
}

#endif
