#include "cmd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pico/stdlib.h>
#include <pico/bootrom.h>
#include <hardware/adc.h>
#include <hardware/gpio.h>
#include <hardware/clocks.h>
#include <hardware/uart.h>
#include <hardware/structs/systick.h>
#include <hardware/structs/bus_ctrl.h>
#include <hardware/sync.h>
#include <hardware/watchdog.h>
#include "usb_cdc.h"
#include "glitch.h"

enum CMD {
    CMD_ARM = 0x80,
    CMD_DISARM,
    CMD_FORCE_TRIGGER,
    CMD_SET_HOLDOFF,
    CMD_SET_DURATION,
    CMD_SET_PATTERN,
    CMD_SET_TRIGGER_EDGE,
    CMD_SET_TRIGGER_POLARITY,
    CMD_SET_FREQ,
    CMD_GET_STATUS,
    CMD_GET_SETTINGS,
    CMD_GPIO_WRITE,
    CMD_GPIO_READ,
    CMD_GPIO_SET_PULL,
    CMD_REBOOT,
};

enum {
    ACK = 0xAA,
    NACK = 0x55,
};

static bool check_cmd_len(enum CMD cmd, int len) {
    switch (cmd) {
        case CMD_ARM:
        case CMD_DISARM:
        case CMD_FORCE_TRIGGER:
        case CMD_GET_STATUS:
        case CMD_GET_SETTINGS:
        case CMD_GPIO_READ:
            return len >= 1;
        case CMD_SET_HOLDOFF:
        case CMD_SET_DURATION:
        case CMD_SET_PATTERN:
        case CMD_SET_TRIGGER_EDGE:
        case CMD_SET_FREQ:
            return len >= 5;
        case CMD_GPIO_WRITE:
        case CMD_REBOOT:
            return len >= 2;
        default:
            return false;
    }
}

int cmd_process(uint8_t *buf, int len, int buf_size) {
    if (buf == NULL || buf_size < 20 || len < 1)
        return 0;

    bool ok = true;
    int idx = 0;
    const enum CMD cmd = buf[idx++];

    if (!check_cmd_len(cmd, len)) {
        ok = false;
    } else if (cmd == CMD_ARM) {
        ok = glitch_arm();
    } else if (cmd == CMD_DISARM) {
        glitch_disarm();
    } else if (cmd == CMD_FORCE_TRIGGER) {
        glitch_force_trigger();
    } else if (cmd == CMD_SET_HOLDOFF) {
        uint32_t holdoff;
        memcpy(&holdoff, &buf[idx], sizeof(holdoff));
        glitch_set_holdoff(holdoff);
    } else if (cmd == CMD_SET_DURATION) {
        uint32_t duration;
        memcpy(&duration, &buf[idx], sizeof(duration));
        glitch_set_pattern_mode(false);
        glitch_set_pattern(duration);
    } else if (cmd == CMD_SET_PATTERN) {
        uint32_t pattern;
        memcpy(&pattern, &buf[idx], sizeof(pattern));
        glitch_set_pattern_mode(true);
        glitch_set_pattern(pattern);
    } else if (cmd == CMD_SET_TRIGGER_EDGE) {
        uint32_t edges;
        memcpy(&edges, &buf[idx], sizeof(edges));
        glitch_set_trigger_nth_edge(edges);
    } else if (cmd == CMD_SET_TRIGGER_POLARITY) {
        bool rising_edge = buf[idx];
        glitch_set_trigger_polarity(rising_edge);
    } else if (cmd == CMD_SET_FREQ) {
        uint32_t freq;
        memcpy(&freq, &buf[idx], sizeof(freq));
        ok = cmd_set_freq(freq);
    } else if (cmd == CMD_GET_STATUS) {
        buf[idx++] = ACK;
        buf[idx++] = glitch_get_trigger_state() ? 1 : 0;
        return idx;
    } else if (cmd == CMD_GET_SETTINGS) {
        const uint32_t freq = clock_get_hz(clk_sys);
        struct glitch_config config;
        glitch_get_settings(&config);

        buf[idx++] = ACK;
        buf[idx++] = glitch_get_trigger_state() ? 1 : 0;
        memcpy(buf + idx, &config.pattern_mode, sizeof(config.pattern_mode));
        idx += sizeof(config.pattern_mode);
        memcpy(buf + idx, &config.rising_edge, sizeof(config.rising_edge));
        idx += sizeof(config.rising_edge);
        memcpy(buf + idx, &config.holdoff, sizeof(config.holdoff));
        idx += sizeof(config.holdoff);
        memcpy(buf + idx, &config.pattern, sizeof(config.pattern));
        idx += sizeof(config.pattern);
        memcpy(buf + idx, &config.trigger_edges, sizeof(config.trigger_edges));
        idx += sizeof(config.trigger_edges);
        memcpy(buf + idx, &freq, sizeof(freq));
        idx += sizeof(freq);
        hard_assert(idx < buf_size);
        return idx;
    } else if (cmd == CMD_GPIO_WRITE) {
        uint8_t pin = buf[idx++];
        uint8_t value = buf[idx++];
        cmd_gpio_write(pin, value);
    } else if (cmd == CMD_GPIO_READ) {
        uint8_t pin = buf[idx];
        buf[idx++] = ACK;
        buf[idx++] = cmd_gpio_read(pin) ? 1 : 0;
        return idx;
    } else if (cmd == CMD_GPIO_SET_PULL) {
        uint8_t pin = buf[idx++];
        uint8_t pull = buf[idx++];
        cmd_gpio_set_pull(pin, pull & (1 << 0), pull & (1 << 1));
    } else if (cmd == CMD_REBOOT) {
        bool bootloader = buf[idx] != 0;
        cmd_reboot(bootloader);
    } else {
        /* unknown command */
        ok = false;
    }

    buf[1] = ok ? ACK : NACK;
    return 2;
}

void cmd_gpio_write(unsigned int pin, int value) {
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, value);
}

bool cmd_gpio_read(unsigned int pin) {
    gpio_set_dir(pin, GPIO_IN);
    return gpio_get(pin);
}

void cmd_gpio_set_pull(unsigned int pin, bool up, bool down) {
    gpio_set_pulls(pin, up, down);
}

void cmd_reboot(bool bootloader) {
    if (bootloader) {
        reset_usb_boot(0, 0);
    } else {
        watchdog_reboot(0, 0, 0);
    }
}

bool cmd_set_freq(uint32_t freq) {
    return set_sys_clock_hz(freq, false);
}
