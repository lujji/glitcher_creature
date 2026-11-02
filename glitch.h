#ifndef GLITCH_H
#define GLITCH_H

#include <stdint.h>
#include <stdbool.h>

enum GLITCH_STATE {
    GLITCH_IDLE,
    GLITCH_ARMED,
    GLITCH_TRIGGERED,
};

struct glitch_config {
    uint32_t trigger_edges; // Nth edge trigger, 0 = first edge
    bool rising_edge;       // trigger on rising or falling edge
    bool zeros_first;       // start sequence with zeros or pattern
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
enum GLITCH_STATE glitch_get_trigger_state();

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
 * Sequence generator API
 *
 * - pattern/duration: resolution = 1/f/2
 * - switcher: resolution = 1/f + 2 cycles
 */
void glitch_init_sequence(bool zeros_first);
bool glitch_push_pattern(uint32_t pattern);
bool glitch_push_duration(uint32_t duration);
bool glitch_push_switcher(uint32_t mask, uint32_t duration);

/**
 * Generate DMA stream for single-shot glitch (cli only)
 */
bool glitch_set_pulse(uint32_t holdoff, uint32_t width, uint32_t pattern);

/**
 * Force trigger (can be used for quick testing)
 */
bool glitch_force_trigger();

/**
 * Get current glitch module settings
 */
void glitch_get_settings(struct glitch_config *config);

/**
 * Print bitstream sequence (debug)
 */
void glitch_print_sequence();

#endif // GLITCH_H
