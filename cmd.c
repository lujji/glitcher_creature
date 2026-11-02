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
    CMD_SET_TRIGGER_EDGE,
    CMD_SET_TRIGGER_POLARITY,
    CMD_SEQ_INIT,
    CMD_SEQ_PUSH_DURATION,
    CMD_SEQ_PUSH_PATTERN,
    CMD_SEQ_PUSH_SWITCHER,
    CMD_SET_FREQ,
    CMD_GET_STATUS,
    CMD_GET_SETTINGS,
    CMD_GPIO_WRITE,
    CMD_GPIO_READ,
    CMD_GPIO_SET_PULL,
    CMD_REBOOT
};

enum STATUS {
    ACK = 0xAA,
    NACK = 0x55,
    INVALID_CMD = 0x66,
    INVALID_LEN = 0x77
};

static bool check_cmd_len(enum CMD cmd, int len) {
    switch (cmd) {
        case CMD_ARM:
        case CMD_DISARM:
        case CMD_FORCE_TRIGGER:
        case CMD_GET_STATUS:
        case CMD_GET_SETTINGS:
            return len >= 1;

        case CMD_SET_TRIGGER_POLARITY:
        case CMD_SEQ_INIT:
        case CMD_GPIO_READ:
        case CMD_REBOOT:
            return len >= 2; // u8

        case CMD_GPIO_WRITE:
        case CMD_GPIO_SET_PULL:
            return len >= 3; // u8, u8

        case CMD_SET_TRIGGER_EDGE:
        case CMD_SET_FREQ:
            return len >= 5; // u32

        case CMD_SEQ_PUSH_DURATION:
        case CMD_SEQ_PUSH_PATTERN:
            return (len >= 5) && ((len - 1) % 4 == 0); // u32[]

        case CMD_SEQ_PUSH_SWITCHER:
            return len >= 9; // u32, u32

        default:
            return true; // INVALID_CMD
    }
}

int cmd_process(uint8_t *buf, int len, int buf_size) {
    if (buf == NULL || buf_size < 20 || len < 1)
        return 0;

    enum STATUS ok = ACK;
    int idx = 0;
    const enum CMD cmd = buf[idx++];

    if (!check_cmd_len(cmd, len)) {
        ok = INVALID_LEN;
    } else if (cmd == CMD_ARM) {
        ok = glitch_arm() ? ACK : NACK;
    } else if (cmd == CMD_DISARM) {
        glitch_disarm();
    } else if (cmd == CMD_FORCE_TRIGGER) {
        ok = glitch_force_trigger() ? ACK : NACK;
    } else if (cmd == CMD_SET_TRIGGER_EDGE) {
        uint32_t edges;
        memcpy(&edges, &buf[idx], sizeof(edges));
        glitch_set_trigger_nth_edge(edges);
    } else if (cmd == CMD_SET_TRIGGER_POLARITY) {
        bool rising_edge = buf[idx];
        glitch_set_trigger_polarity(rising_edge);
    } else if (cmd == CMD_SEQ_INIT) {
        bool zeros_first = buf[idx];
        glitch_init_sequence(zeros_first);
    } else if (cmd == CMD_SEQ_PUSH_DURATION || cmd == CMD_SEQ_PUSH_PATTERN) {
        for (; idx < len; idx += sizeof(uint32_t)) {
            uint32_t v;
            memcpy(&v, &buf[idx], sizeof(v));

            bool success;
            if (cmd == CMD_SEQ_PUSH_DURATION) {
                success = glitch_push_duration(v);
            } else {
                success = glitch_push_pattern(v);
            }

            if (!success) {
                ok = NACK;
                break;
            }
        }
    } else if (cmd == CMD_SEQ_PUSH_SWITCHER) {
        uint32_t mask, duration;
        memcpy(&mask, &buf[idx], sizeof(mask));
        idx += sizeof(mask);
        memcpy(&duration, &buf[idx], sizeof(duration));
        ok = glitch_push_switcher(mask, duration) ? ACK : NACK;
    } else if (cmd == CMD_SET_FREQ) {
        uint32_t freq;
        memcpy(&freq, &buf[idx], sizeof(freq));
        ok = cmd_set_freq(freq) ? ACK : NACK;
    } else if (cmd == CMD_GET_STATUS) {
        buf[idx++] = ACK;
        buf[idx++] = glitch_get_trigger_state();
        return idx;
    } else if (cmd == CMD_GET_SETTINGS) {
        const uint32_t freq = clock_get_hz(clk_sys);
        struct glitch_config config;
        glitch_get_settings(&config);

        buf[idx++] = ACK;
        buf[idx++] = glitch_get_trigger_state();
        memcpy(buf + idx, &config.rising_edge, sizeof(config.rising_edge));
        idx += sizeof(config.rising_edge);
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
        ok = INVALID_CMD;
    }

    buf[1] = ok;
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
