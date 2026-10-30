/*
 * MCPX DSP emulator
 *
 * Copyright (c) 2015 espes
 * Copyright (c) 2020-2025 Matt Borgerson
 *
 * Adapted from Hatari DSP M56001 emulation
 * (C) 2001-2008 ARAnyM developer team
 * Adaption to Hatari (C) 2008 by Thomas Huth
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "native/dev/apu/apu_base.h"
#include "dsp_cpu.h"
#include "dsp_dma.h"
#include "dsp_state.h"
#include "dsp.h"
#include "debug.h"

/* Defines */
#define BITMASK(x)  ((1<<(x))-1)

#define INTERRUPT_ABORT_FRAME (1 << 0)
#define INTERRUPT_START_FRAME (1 << 1)
#define INTERRUPT_DMA_EOL (1 << 7)

static uint32_t read_peripheral(dsp_core_t* core, uint32_t address);
static void write_peripheral(dsp_core_t* core, uint32_t address, uint32_t value);

DSPState *dsp_init(void *rw_opaque,
                   dsp_scratch_rw_func scratch_rw,
                   dsp_fifo_rw_func fifo_rw)
{
    DPRINTF("dsp_init\n");

    DSPState* dsp = (DSPState*)malloc(sizeof(DSPState));
    memset(dsp, 0, sizeof(*dsp));

    dsp->core.read_peripheral = read_peripheral;
    dsp->core.write_peripheral = write_peripheral;

    dsp->dma.core = &dsp->core;
    dsp->dma.rw_opaque = rw_opaque;
    dsp->dma.scratch_rw = scratch_rw;
    dsp->dma.fifo_rw = fifo_rw;

    dsp_reset(dsp);

#if NC_HAVE_DSP_JIT
    if (g_apu_config.use_dsp && g_apu_config.use_dsp_jit) {
        dsp->jit = dsp_jit_create(dsp);
    }
#endif

    return dsp;
}

void dsp_reset(DSPState* dsp)
{
    dsp56k_reset_cpu(&dsp->core);
    dsp->save_cycles = 0;
#if NC_HAVE_DSP_JIT
    dsp->jit_state_valid = false;
    dsp->jit_dma_reads = 0;
    if (dsp->jit) {
        dsp56300_reset(dsp->jit);
    }
#endif
}

void dsp_destroy(DSPState* dsp)
{
#if NC_HAVE_DSP_JIT
    if (dsp->jit) {
        dsp56300_destroy(dsp->jit);
    }
#endif
    free(dsp);
}

static uint32_t read_peripheral(dsp_core_t* core, uint32_t address) {
    DSPState* dsp = container_of(core, DSPState, core);

    uint32_t v = 0xababa;
    switch(address) {
    case 0xFFFFB3:
        v = 0; // core->num_inst; // ??
        break;
    case 0xFFFFC5:
        v = dsp->interrupts;
        if (dsp->dma.eol) {
            v |= INTERRUPT_DMA_EOL;
        }
        break;
    case 0xFFFFD4:
        v = dsp_dma_read(&dsp->dma, DMA_NEXT_BLOCK);
        break;
    case 0xFFFFD5:
        v = dsp_dma_read(&dsp->dma, DMA_START_BLOCK);
        break;
    case 0xFFFFD6:
#if NC_HAVE_DSP_JIT
        /* The JIT has no C callback per instruction. Complete synchronous
         * DMA after the guest has observed RUNNING, as xemu's JIT does. */
        if (dsp->jit && (dsp->dma.control & DMA_CONTROL_RUNNING) && ++dsp->jit_dma_reads > 2) {
            dsp->dma.control &= ~DMA_CONTROL_RUNNING;
            dsp->dma.control |= DMA_CONTROL_STOPPED;
            dsp->jit_dma_reads = 0;
        }
#endif
        v = dsp_dma_read(&dsp->dma, DMA_CONTROL);
        break;
    case 0xFFFFD7:
        v = dsp_dma_read(&dsp->dma, DMA_CONFIGURATION);
        break;
    }

    return v;
}

static void write_peripheral(dsp_core_t* core, uint32_t address, uint32_t value) {
    DSPState* dsp = container_of(core, DSPState, core);

    switch(address) {
    case 0xFFFFC4:
        if (value & 1) {
            core->is_idle = true;
#if NC_HAVE_DSP_JIT
            if (dsp->jit) {
                dsp56300_set_halt_requested(dsp->jit, true);
            }
#endif
        }
        break;
    case 0xFFFFC5:
        dsp->interrupts &= ~value;
        if (value & INTERRUPT_DMA_EOL) {
            dsp->dma.eol = false;
        }
        break;
    case 0xFFFFD4:
        dsp_dma_write(&dsp->dma, DMA_NEXT_BLOCK, value);
        break;
    case 0xFFFFD5:
        dsp_dma_write(&dsp->dma, DMA_START_BLOCK, value);
        break;
    case 0xFFFFD6:
#if NC_HAVE_DSP_JIT
        dsp->jit_dma_reads = 0;
#endif
        dsp_dma_write(&dsp->dma, DMA_CONTROL, value);
        break;
    case 0xFFFFD7:
        dsp_dma_write(&dsp->dma, DMA_CONFIGURATION, value);
        break;
    }

}


void dsp_step(DSPState* dsp)
{
#if NC_HAVE_DSP_JIT
    if (dsp->jit) {
        dsp56300_step(dsp->jit);
        dsp_jit_save(dsp);
        return;
    }
#endif
    dsp56k_execute_instruction(&dsp->core);
}

void dsp_run(DSPState* dsp, int cycles)
{
#if NC_HAVE_DSP_JIT
    if (dsp->jit) {
        dsp_jit_run(dsp, cycles);
        return;
    }
#endif
    dsp_run_interpreter(dsp, cycles);
}

void dsp_invalidate_cache(DSPState* dsp)
{
    memset(dsp->core.pram_opcache, 0, sizeof(dsp->core.pram_opcache));
#if NC_HAVE_DSP_JIT
    if (dsp->jit) {
        dsp56300_invalidate_cache(dsp->jit);
    }
#endif
}

void dsp_state_prepare_save(DSPState* dsp)
{
#if NC_HAVE_DSP_JIT
    dsp->jit_state_valid = false;
    if (dsp->jit) {
        dsp_jit_save(dsp);
    }
#endif
}

void dsp_state_finish_load(DSPState* dsp)
{
    dsp_invalidate_cache(dsp);
#if NC_HAVE_DSP_JIT
    if (dsp->jit) {
        dsp_jit_load(dsp);
    }
#endif
}

const char *dsp_engine_name(DSPState* dsp)
{
#if NC_HAVE_DSP_JIT
    if (dsp->jit) {
        return "JIT";
    }
#endif
    return "interpreter";
}

void dsp_bootstrap(DSPState* dsp)
{
    // scratch memory is dma'd in to pram by the bootrom
    dsp->dma.scratch_rw(dsp->dma.rw_opaque,
        (uint8_t*)dsp->core.pram, 0, 0x800*4, false);
    for (int i = 0; i < 0x800; i++) {
        if (dsp->core.pram[i] & 0xff000000) {
            DPRINTF("Bootstrap %04x: %08x\n", i, dsp->core.pram[i]);
            dsp->core.pram[i] &= 0x00ffffff;
        }
    }
    dsp_invalidate_cache(dsp);
}

void dsp_start_frame(DSPState* dsp)
{
    dsp->interrupts |= INTERRUPT_START_FRAME;
    dsp->core.is_idle = false;
    dsp->core.cycle_count = 0;
#if NC_HAVE_DSP_JIT
    if (dsp->jit) {
        Dsp56300State state;
        dsp56300_get_state(dsp->jit, &state);
        state.halt_requested = false;
        state.cycle_count = 0;
        state.cycle_budget = 0;
        dsp56300_set_state(dsp->jit, &state);
    }
#endif
}

uint32_t dsp_read_memory(DSPState* dsp, char space, uint32_t address)
{
    int space_id;

    switch (space) {
    case 'X':
        space_id = DSP_SPACE_X;
        break;
    case 'Y':
        space_id = DSP_SPACE_Y;
        break;
    case 'P':
        space_id = DSP_SPACE_P;
        break;
    default:
        assert(false);
        return 0;
    }

#if NC_HAVE_DSP_JIT
    if (dsp->jit) {
        return dsp56300_read_memory(dsp->jit, space_id, address);
    }
#endif
    return dsp56k_read_memory(&dsp->core, space_id, address);
}

void dsp_write_memory(DSPState* dsp, char space, uint32_t address, uint32_t value)
{
    int space_id;

    switch (space) {
    case 'X':
        space_id = DSP_SPACE_X;
        break;
    case 'Y':
        space_id = DSP_SPACE_Y;
        break;
    case 'P':
        space_id = DSP_SPACE_P;
        break;
    default:
        assert(false);
        return;
    }

#if NC_HAVE_DSP_JIT
    if (dsp->jit) {
        dsp56300_write_memory(dsp->jit, space_id, address, value);
        return;
    }
#endif
    dsp56k_write_memory(&dsp->core, space_id, address, value);
}
