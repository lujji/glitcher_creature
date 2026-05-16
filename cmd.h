#ifndef CMD_H
#define CMD_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Process commands
 *
 * @param buf command buffer, response is written back into the same buffer
 * @param len length of data in buf
 * @param buf_size maximum buf size
 * @return length of the response in bytes
 */
int cmd_process(uint8_t *buf, int len, int buf_size);

/**
 * Set CPU frequency, returns true on success
 */
bool cmd_set_freq(uint32_t freq);

/**
 * Set GPIO state, pin will be configured as output
 */
void cmd_gpio_write(unsigned int pin, int value);

/**
 * Read GPIO state, pin will be configured as input
 */
bool cmd_gpio_read(unsigned int pin);

/**
 * Configure GPIO pull-up/down
 */
void cmd_gpio_set_pull(unsigned int pin, bool up, bool down);

/**
 * Reboot the device
 */
void cmd_reboot(bool bootloader);

#endif // CMD_H
