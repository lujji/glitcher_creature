#ifndef GLITCH_H
#define GLITCH_H

#include <stdint.h>
#include <stdbool.h>

struct glitch_config {
    bool pattern_mode;      // binary pattern or simple pulse
    bool rising_edge;       // trigger on rising or falling edge
    uint32_t trigger_edges; // Nth edge trigger, 0 = first edge
    uint32_t holdoff;       // delay from trigger to glitch
    uint32_t pattern;       // pattern or duration depending on mode
};

/**
 * Initialize glitch module and PIO state machine
 */
void glitch_init();

/**
 * Free PIO state machine
 */
void glitch_deinit();

/**
 * Arm the glitcher and wait for trigger
 */
bool glitch_arm();

/**
 * Disarm the glitcher
 */
void glitch_disarm();

/**
 * Check if glitch module has been triggered
 */
bool glitch_get_trigger_state();

/**
 * Set glitch pattern mode.
 * A binary pattern will be shifted out on the glitch output pin instead of a simple pulse
 */
void glitch_set_pattern_mode(bool enabled);

/**
 * Set trigger polarity (rising or falling edge)
 */
void glitch_set_trigger_polarity(bool rising_edge);

/**
 * Trigger from Nth edge
 *
 * @param edges - 0 = first edge
 */
void glitch_set_trigger_nth_edge(uint32_t edges);

/**
 * Set glitch delay (holdoff time between trigger and glitch)
 *
 * @param delay - holdoff time in cycles
 */
void glitch_set_holdoff(uint32_t delay);

/**
 * Set glitch pattern or duration based on mode.
 *
 * In pattern mode the binary pattern will be shifted out on the glitch output pin, LSB first.
 * Each bit is equivalent to a pulse width of 1/F_CPU/2.
 *
 * In duration mode glitch output will be set for the number of cycles specified by pattern parameter.
 * One cycle corresponds to a pulse width of 1/F_CPU/2.
 *
 * Example:
 *  F_CPU = 250MHz
 *  in pattern mode 0b111 will produce a 3 * 1/250MHz/2 = 6ns glitch
 *  in duration mode 0b111 will produce a 7 * 1/250MHz/2 = 14ns glitch
 *
 * @param pattern - glitch pattern or pulse duration
 */
void glitch_set_pattern(uint32_t pattern);

/**
 * Force trigger (can be used for quick testing)
 */
void glitch_force_trigger();

/**
 * Get current glitch module settings
 */
void glitch_get_settings(struct glitch_config *config);

#endif // GLITCH_H
