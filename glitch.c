#include "glitch.h"
#include <stdio.h>
#include <string.h>
#include <hardware/pio.h>
#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include "glitch.pio.h"
#include "config.h"

static_assert(
    ((GLITCH_OUT_PIN < 16) && (GLITCH_TRIGGER_PIN < 16)) ||
    ((GLITCH_OUT_PIN >= 16) && (GLITCH_TRIGGER_PIN >= 16)),
    "PIO pins must both be in GPIO ranges 0-15 or 16-47"
);

static PIO pio;
static unsigned int sm;
static unsigned int offset;
static const struct pio_program *program = &glitch_pattern_program;
static struct glitch_config cfg = {
    .pattern_mode = true,
    .rising_edge = true,
    .trigger_edges = 0,
    .holdoff = 0,
    .pattern = 0b1101001011
};

void glitch_init() {
    hard_assert(pio_claim_free_sm_and_add_program_for_gpio_range(program, &pio, &sm, &offset, GLITCH_OUT_PIN, 1, true));
    glitch_disarm();
}

void glitch_deinit() {
    pio_remove_program_and_unclaim_sm(program, pio, sm, offset);
}

void glitch_set_pattern_mode(bool enabled) {
    if (cfg.pattern_mode == enabled)
        return;
    cfg.pattern_mode = enabled;
    program = enabled ? &glitch_pattern_program : &glitch_program;
    glitch_deinit();
    glitch_init();
}

bool glitch_arm() {
    if (glitch_get_trigger_state()) {
        pio_sm_put_blocking(pio, sm, cfg.trigger_edges);
        pio_sm_put_blocking(pio, sm, cfg.holdoff);
        pio_sm_put_blocking(pio, sm, cfg.pattern);
        return true;
    } else {
        return false;
    }
}

void glitch_disarm() {
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    glitch_program_init(pio, sm, offset, GLITCH_OUT_PIN, GLITCH_TRIGGER_PIN, cfg.pattern_mode, cfg.rising_edge);
}

bool glitch_get_trigger_state() {
    uint8_t pc = pio_sm_get_pc(pio, sm);
    // printf("PC = %d, offset = %d: %s triggered\n", pc, offset, (pc > offset + 1) ? "not" : "");
    return pc <= (offset + 1);
}

void glitch_set_trigger_nth_edge(uint32_t edges) {
    cfg.trigger_edges = edges;
}

void glitch_set_trigger_polarity(bool rising_edge) {
    cfg.rising_edge = rising_edge;
    glitch_deinit();
    glitch_init();
}

void glitch_set_holdoff(uint32_t delay) {
    cfg.holdoff = delay;
}

void glitch_set_pattern(uint32_t pattern) {
    cfg.pattern = pattern;
}

void glitch_force_trigger() {
    glitch_disarm();
    glitch_arm();

    unsigned int dst = cfg.pattern_mode ? glitch_pattern_offset_delay : glitch_offset_trigger;
    pio_sm_exec(pio, sm, pio_encode_jmp(offset + dst));
}

void glitch_get_settings(struct glitch_config *out) {
    if (out)
        memcpy(out, &cfg, sizeof(struct glitch_config));
}
