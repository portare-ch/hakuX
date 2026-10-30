/*
 * MCPX DSP emulator - DSP56300 JIT backend
 *
 * Copyright (c) 2026 Matt Borgerson
 *
 * Adapted from xemu's hw/xbox/mcpx/apu/dsp/dsp_jit.c.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "native/dev/apu/apu_base.h"
#include "dsp_state.h"

static uint32_t peripheral_read(void *opaque, uint32_t address)
{
    DSPState *dsp = opaque;
    return dsp->core.read_peripheral(&dsp->core, address);
}

static void peripheral_write(void *opaque, uint32_t address, uint32_t value)
{
    DSPState *dsp = opaque;
    dsp->core.write_peripheral(&dsp->core, address, value);
}

Dsp56300Jit *dsp_jit_create(DSPState *dsp)
{
    /* Both X aliases must address the same mixbuffer the VP fills. The
     * legacy xram array's upper 0x400 words are not a separate mixbuffer. */
    Dsp56300MemoryRegion x[] = {
        { .start = 0, .end = 0xc00, .kind = DSP56300_REGION_BUFFER,
          .data.buffer = { .base = dsp->core.xram } },
        { .start = 0xc00, .end = 0x1000, .kind = DSP56300_REGION_BUFFER,
          .data.buffer = { .base = dsp->core.mixbuffer } },
        { .start = 0x1400, .end = 0x1800, .kind = DSP56300_REGION_BUFFER,
          .data.buffer = { .base = dsp->core.mixbuffer } },
        { .start = 0xffff80, .end = 0x1000000, .kind = DSP56300_REGION_CALLBACK,
          .data.callback = { .opaque = dsp, .read = peripheral_read, .write = peripheral_write } },
    };
    Dsp56300MemoryRegion y[] = {
        { .start = 0, .end = DSP_YRAM_SIZE, .kind = DSP56300_REGION_BUFFER,
          .data.buffer = { .base = dsp->core.yram } },
    };
    Dsp56300MemoryRegion p[] = {
        { .start = 0, .end = DSP_PRAM_SIZE, .kind = DSP56300_REGION_BUFFER,
          .data.buffer = { .base = dsp->core.pram } },
    };
    const Dsp56300CreateInfo info = {
        .memory_map = {
            .x_regions = x, .x_count = ARRAY_SIZE(x),
            .y_regions = y, .y_count = ARRAY_SIZE(y),
            .p_regions = p, .p_count = ARRAY_SIZE(p),
        },
    };
    return dsp56300_create(&info);
}

void dsp_jit_run(DSPState *dsp, int cycles)
{
    const uint32_t before = dsp56300_cycle_count(dsp->jit);
    dsp56300_run(dsp->jit, cycles);
    const uint32_t after = dsp56300_cycle_count(dsp->jit);
    if (after == before && !dsp56300_halt_requested(dsp->jit)) {
        /* WAIT/STOP executes nothing until an interrupt can wake the DSP;
         * spinning the APU's frame loop cannot make that happen. */
        Dsp56300State state;
        dsp56300_get_state(dsp->jit, &state);
        if (state.power_state) {
            dsp56300_set_halt_requested(dsp->jit, true);
        }
    }
    dsp->core.is_idle = dsp56300_halt_requested(dsp->jit);
    dsp->core.cycle_count = after;
}

/* The old interpreter has twelve interrupt slots; only these four are
 * architectural DSP56300 exceptions. Preserve its state-file field layout. */
static const unsigned legacy_irq[] = {
    DSP_INTER_RESET, DSP_INTER_ILLEGAL, DSP_INTER_STACK_ERROR, DSP_INTER_SWI,
};
static const unsigned architectural_irq[] = { 0, 2, 1, 4 };

void dsp_jit_save(DSPState *dsp)
{
    Dsp56300State *state = &dsp->jit_state;
    dsp56300_get_state(dsp->jit, state);
    dsp->jit_state_valid = true;
    dsp_core_t *core = &dsp->core;
    core->pc = state->pc;
    core->cur_inst_len = state->pc_advance;
    core->pc_on_rep = state->pc_on_rep;
    core->loop_rep = state->loop_rep;
    core->cycle_count = state->cycle_count;
    core->is_idle = state->halt_requested;
    dsp->save_cycles = state->cycle_budget;
    memcpy(core->registers, state->registers, sizeof(core->registers));
    memcpy(core->stack, state->stack, sizeof(core->stack));
    core->interrupt_state = state->interrupts.state;
    core->interrupt_instr_fetch = state->interrupts.vector_addr;
    core->interrupt_save_pc = state->interrupts.saved_pc;
    core->interrupt_ipl_to_raise = state->interrupts.ipl_to_raise;
    core->interrupt_pipeline_count = state->interrupts.pipeline_stage;
    core->interrupt_counter = 0;
    memset(core->interrupt_is_pending, 0, sizeof(core->interrupt_is_pending));
    for (unsigned i = 0; i < ARRAY_SIZE(legacy_irq); i++) {
        unsigned old = legacy_irq[i], slot = architectural_irq[i];
        core->interrupt_ipl[old] = state->interrupts.ipl[slot];
        core->interrupt_is_pending[old] =
            (state->interrupts.pending_bits[slot / 64] >> (slot % 64)) & 1;
        core->interrupt_counter += core->interrupt_is_pending[old];
    }
}

void dsp_jit_load(DSPState *dsp)
{
    if (dsp->jit_state_valid) {
        dsp56300_set_state(dsp->jit, &dsp->jit_state);
        return;
    }
    const dsp_core_t *core = &dsp->core;
    Dsp56300State state = {
        .pc = core->pc,
        .pc_advance = core->cur_inst_len,
        .pc_on_rep = core->pc_on_rep,
        .cycle_count = core->cycle_count,
        .cycle_budget = dsp->save_cycles,
        .loop_rep = core->loop_rep,
        .halt_requested = core->is_idle,
        .interrupts = {
            .state = core->interrupt_state,
            .vector_addr = core->interrupt_instr_fetch,
            .saved_pc = core->interrupt_save_pc,
            .ipl_to_raise = core->interrupt_ipl_to_raise,
            .pipeline_stage = core->interrupt_pipeline_count,
        },
    };
    memcpy(state.registers, core->registers, sizeof(state.registers));
    memcpy(state.stack, core->stack, sizeof(state.stack));
    for (unsigned i = 0; i < ARRAY_SIZE(legacy_irq); i++) {
        unsigned old = legacy_irq[i], slot = architectural_irq[i];
        state.interrupts.ipl[slot] = core->interrupt_ipl[old];
        if (core->interrupt_is_pending[old]) {
            state.interrupts.pending_bits[slot / 64] |= 1ull << (slot % 64);
        }
    }
    /* Legacy states used 16-bit address modifiers. Linear addressing and
     * negative increments must be widened for the 24-bit DSP56300. */
    for (unsigned i = 0; i < 8; i++) {
        if (state.registers[DSP_REG_M0 + i] == 0xffff) {
            state.registers[DSP_REG_M0 + i] = 0xffffff;
        }
        if (state.registers[DSP_REG_N0 + i] & 0x8000) {
            state.registers[DSP_REG_N0 + i] |= 0xff0000;
        }
    }
    dsp56300_set_state(dsp->jit, &state);
}
