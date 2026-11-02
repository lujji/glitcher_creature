#include "cli.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pico/stdlib.h>
#include <pico/bootrom.h>
#include <limits.h>
#include <math.h>
#include <hardware/adc.h>
#include <hardware/gpio.h>
#include <hardware/clocks.h>
#include <hardware/uart.h>
#include "embedded_cli.h"
#include "cmd.h"
#include "glitch.h"
#include "config.h"

#define CLI_BUFFER_SIZE 1024

struct pulse_cfg {
    uint32_t holdoff;
    uint32_t width;
    uint32_t pattern;
    bool pattern_mode;
    bool loaded;
};

static const char* logo[] = {
    "         __   __  __          __    ________        ",
    "   ____ |  | |__|/  |_  ____ |  |__ \\_____  \\______ ",
    "  / ___\\|  | |  \\   __\\/ ___\\|  |  \\  _(__  /_  __ \\",
    " / /_/  >  |_|  ||  | \\  \\___|   Y  \\/       \\  | \\/",
    " \\___  /|____/__||__|  \\___  >___|  /______  /__|   ",
    "/_____/                    \\/     \\/       \\/       "
};
static CLI_UINT cliBuffer[BYTES_TO_CLI_UINTS(CLI_BUFFER_SIZE)];
static EmbeddedCli *cli;
static struct glitch_config cfg;
static char printf_buf[128];
static bool adc_mode;
static struct pulse_cfg pulse;

static void cli_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(printf_buf, sizeof(printf_buf), format, args);
    va_end(args);

    embeddedCliPrint(cli, printf_buf);
}

static void writeChar(EmbeddedCli *embeddedCli, char c) {
    putchar(c);
}

static void cli_print_logo(EmbeddedCli *embeddedCli) {
    putchar('\r');
    putchar('\n');
    for (int i = 0; i < sizeof(logo) / sizeof(logo[0]); i++)
        printf("%s\r\n", logo[i]);
    printf("\r\ncontrols:\r\n");
    printf("  [.] - force trigger  [/] - reset target\r\n");
    printf("  ['] - increase pulse [;] - decrease pulse\r\n\r\n");
    printf("commands:\r\n");
}

static void draw_square_wave(uint32_t v) {
    const int len = 31;

    for (int i = len, prev = 0; i >= -1; i--) {
        bool cur = i >= 0 && ((v >> i) & 1);

        if (!prev && cur)
            printf("┌─"); // rising
        else if (prev && !cur)
            printf("┐ "); // falling
        else if (cur)
            printf("──"); // 1
        else
            printf("  "); // 0

        prev = cur;
    }
    putchar('\r');
    putchar('\n');

    for (int i = len, prev = 0; i >= -1; i--) {
        bool cur = i >= 0 && ((v >> i) & 1);

        if (prev != cur)
            printf("│ ");
        else
            printf("  ");

        prev = cur;
    }
    putchar('\r');
    putchar('\n');

    for (int i = len, prev = 0; i >= -1; i--) {
        bool cur = i >= 0 && ((v >> i) & 1);

        if (!prev && cur)
            printf("┘ ");
        else if (prev && !cur)
            printf("└─");
        else if (!cur)
            printf("──");
        else
            printf("  ");

        prev = cur;
    }
    putchar('\r');
    putchar('\n');
}

static inline float get_voltage() {
    return adc_read() * 3.3f / 4096.0f;
}

static void print_pattern() {
    const float bit_ns = 0.5e9f / clock_get_hz(clk_sys);
    if (!pulse.loaded) {
        cli_printf("  - pulse: not set");
    } else if (pulse.pattern_mode) {
        draw_square_wave(pulse.pattern);
        cli_printf("  - pattern: holdoff %u (%.1f ns), 0x%08X (bit = %.1f ns)",
            pulse.holdoff, pulse.holdoff * bit_ns, pulse.pattern, bit_ns);
    } else {
        if (pulse.width <= 32)
            draw_square_wave(0xFFFFFFFF << (32 - pulse.width));
        cli_printf("  - pulse: holdoff %u (%.1f ns), width %u (%.1f ns)",
            pulse.holdoff, pulse.holdoff * bit_ns, pulse.width, pulse.width * bit_ns);
    }
}

static void load_glitch() {
    const bool ok = pulse.pattern_mode ?
        glitch_set_pulse(pulse.holdoff, 32, pulse.pattern) :
        glitch_set_pulse(pulse.holdoff, pulse.width, 0xFFFFFFFF);

    if (!ok) {
        cli_printf(glitch_get_trigger_state() == GLITCH_ARMED ?
            "already armed!" : "invalid pulse!");
        return;
    }

    pulse.loaded = true;
    print_pattern();
}

static void reset_handler(EmbeddedCli *cli, char *args, void *context) {
    cmd_reboot(false);
}

static void bootloader_handler(EmbeddedCli *cli, char *args, void *context) {
    cmd_reboot(true);
}

static void status_handler(EmbeddedCli *cli, char *args, void *context) {
    const float clk = clock_get_hz(clk_sys) / MHZ;
    const float ns = 1000.0f / clk;
    glitch_get_settings(&cfg);
    cli_printf("  - CPU frequency: %.2f MHz (%.2f ns)", clk, ns);
    cli_printf("  - adc: %.2f V", get_voltage());
    print_pattern();
    cli_printf("  - trigger edge: %d (0 = 1st edge)", cfg.trigger_edges);
    cli_printf("  - trigger polarity: %s edge", cfg.rising_edge ? "rising" : "falling");
    static const char *state[] = {"idle", "armed", "triggered"};
    cli_printf("  - state: %s", state[glitch_get_trigger_state()]);
}

static void set_holdoff_handler(EmbeddedCli *cli, char *args, void *context) {
    const char *arg1 = embeddedCliGetToken(args, 1);
    if (arg1 == NULL) {
        cli_printf("usage: set_holdoff [cycles]");
        return;
    }

    pulse.holdoff = strtoul(arg1, NULL, 10);
    load_glitch();
}

static void set_duration_handler(EmbeddedCli *cli, char *args, void *context) {
    const char *arg1 = embeddedCliGetToken(args, 1);
    if (arg1 == NULL) {
        cli_printf("usage: set_width [cycles]");
        return;
    }

    pulse.width = strtoul(arg1, NULL, 10);
    pulse.pattern_mode = false;
    load_glitch();
}

static void set_pattern_handler(EmbeddedCli *cli, char *args, void *context) {
    const char *arg1 = embeddedCliGetToken(args, 1);
    if (arg1 == NULL) {
        cli_printf("usage: set_pattern [32bit MSB-first]");
        return;
    }

    uint32_t bits = 0;
    int n = 0;
    for (; arg1[n] == '0' || arg1[n] == '1'; n++)
        bits = (bits << 1) | (arg1[n] - '0');

    if (n == 0 || n > 32 || arg1[n] != '\0') {
        cli_printf("invalid pattern");
        return;
    }

    pulse.pattern = bits << (32 - n); // left-align
    pulse.pattern_mode = true;
    load_glitch();
}

static void set_freq_handler(EmbeddedCli *cli, char *args, void *context) {
    const char *arg1 = embeddedCliGetToken(args, 1);
    if (arg1 == NULL) {
        cli_printf("usage: set_freq [MHz]");
        return;
    }

    uint32_t freq = strtoul(arg1, NULL, 10);
    if (cmd_set_freq(freq * MHZ)) {
        cli_printf("frequency set to %d MHz\n", freq);
    } else {
        cli_printf("failed to set frequency\n");
    }
}

static void set_trigger_edge_handler(EmbeddedCli *cli, char *args, void *context) {
    const char *arg1 = embeddedCliGetToken(args, 1);
    if (arg1 == NULL) {
        cli_printf("usage: set_trigger_edge [N, 0 = first edge]");
        return;
    }

    uint32_t edges = strtoul(arg1, NULL, 10);
    glitch_set_trigger_nth_edge(edges);
}

static void set_trigger_polarity_handler(EmbeddedCli *cli, char *args, void *context) {
    const char *arg1 = embeddedCliGetToken(args, 1);
    if (arg1 == NULL) {
        cli_printf("usage: set_trigger_polarity [1 = rising, 0 = falling]");
        return;
    }

    uint32_t polarity = strtoul(arg1, NULL, 10);
    glitch_set_trigger_polarity(polarity);
}

static void arm_handler(EmbeddedCli *cli, char *args, void *context) {
    if (!glitch_arm()) {
        cli_printf(glitch_get_trigger_state() == GLITCH_IDLE ?
            "sequence not valid" : "already armed");
    }
}

static void disarm_handler(EmbeddedCli *cli, char *args, void *context) {
    glitch_disarm();
}

static void gpio_write_handler(EmbeddedCli *cli, char *args, void *context) {
    const char *arg1 = embeddedCliGetToken(args, 1);
    const char *arg2 = embeddedCliGetToken(args, 2);
    if (arg1 == NULL || arg2 == NULL) {
        cli_printf("usage: gpio_write [pin] [value]");
        return;
    }

    uint32_t pin = strtoul(arg1, NULL, 10);
    uint32_t value = strtoul(arg2, NULL, 10);
    cmd_gpio_write(pin, value);
}

static void gpio_read_handler(EmbeddedCli *cli, char *args, void *context) {
    const char *arg1 = embeddedCliGetToken(args, 1);
    if (arg1 == NULL) {
        cli_printf("usage: gpio_read [pin]");
        return;
    }

    uint32_t pin = strtoul(arg1, NULL, 10);
    uint32_t value = cmd_gpio_read(pin);
    cli_printf("gpio_read: pin %d = %d\n", pin, value);
}

static void gpio_set_pull_handler(EmbeddedCli *cli, char *args, void *context) {
    const char *arg1 = embeddedCliGetToken(args, 1);
    const char *arg2 = embeddedCliGetToken(args, 2);
    if (arg1 == NULL || arg2 == NULL) {
        cli_printf("usage: gpio_set_pull [pin] [pull: 0 = none, 1 = up, 2 = down]");
        return;
    }

    uint32_t pin = strtoul(arg1, NULL, 10);
    uint32_t pull = strtoul(arg2, NULL, 10);
    if (pull > 2) {
        cli_printf("invalid pull value: %d\n", pull);
        return;
    }
    bool up = (pull == 1);
    bool down = (pull == 2);
    cmd_gpio_set_pull(pin, up, down);
}

static void dump_handler(EmbeddedCli *cli, char *args, void *context) {
    glitch_print_sequence();
}

static void adc_read_handler(EmbeddedCli *cli, char *args, void *context) {
    cli_printf("adc mode, press [q] to exit");
    adc_mode = true;
}

static const struct CliCommandBinding binding[] = {
    {"status", "show glitcher configuration", false, NULL, status_handler},
    {"arm", "arm glitcher and wait for trigger", false, NULL, arm_handler},
    {"disarm", "disarm glitcher", false, NULL, disarm_handler},
    {"set_holdoff", "set delay between trigger and glitch", true, NULL, set_holdoff_handler},
    {"set_duration", "set glitch duration (disables pattern mode)", true, NULL, set_duration_handler},
    {"set_pattern", "set glitch pattern (enables pattern mode)", true, NULL, set_pattern_handler},
    {"set_trigger_edge", "trigger from Nth edge", true, NULL, set_trigger_edge_handler},
    {"set_trigger_polarity", "trigger from rising/falling edge", true, NULL, set_trigger_polarity_handler},
    {"set_freq", "set glitcher CPU frequency", true, NULL, set_freq_handler},
    {"gpio_write", "write value to gpio pin", true, NULL, gpio_write_handler},
    {"gpio_read", "read value from gpio pin", true, NULL, gpio_read_handler},
    {"gpio_set_pull", "set pull-up/down for gpio pin", true, NULL, gpio_set_pull_handler},
    {"dump", "print uploaded sequence", false, NULL, dump_handler},
    {"adc_read", "read ADC channel in real-time", false, NULL, adc_read_handler},
    {"reset", "reboot glitcher", false, NULL, reset_handler},
    {"bootloader", "reboot glitcher and enter bootloader", false, NULL, bootloader_handler},
};

void cli_process(char *buf, int len) {
    if (adc_mode) {
        printf("\r  voltage: %.2f V", get_voltage());
        if (len > 0 && buf[0] == 'q') {
            adc_mode = false;
            cli_printf("\nexit adc mode");
        }
        return;
    }

    if (len <= 0)
        return;

    // process ascii only
    uint8_t c = buf[0];
    if (c >= 128)
        return;

    if (c == '`') {
        cmd_reboot(true);
    } else if (c == '.') {
        cli_printf("glitch!");
        glitch_force_trigger();
        // hstx_test();
    } else if (c == '/') {
        // toggle reset
        cli_printf("reset target");
        cmd_gpio_write(GPIO_RST, 0);
        sleep_ms(100);
        cmd_gpio_write(GPIO_RST, 1);
    } else if (c == '\'') {
        // increment pulse width
        if (pulse.pattern_mode)
            pulse.pattern = (pulse.pattern >> 1) | 0x80000000;
        else
            pulse.width++;
        putchar('\r');
        putchar('\n');
        load_glitch();
    } else if (c == ';') {
        // decrement pulse width
        if (pulse.pattern_mode)
            pulse.pattern <<= 1;
        else if (pulse.width > 1)
            pulse.width--;
        putchar('\r');
        putchar('\n');
        load_glitch();
    } else {
        for (size_t i = 0; i < len; i++) {
            embeddedCliReceiveChar(cli, buf[i]);
            embeddedCliProcess(cli);
        }
    }
}

void cli_init() {
    EmbeddedCliConfig *config = embeddedCliDefaultConfig();
    config->cliBuffer = cliBuffer;
    config->cliBufferSize = CLI_BUFFER_SIZE;
    config->maxBindingCount = sizeof(binding) / sizeof(binding[0]);

    uint16_t config_size = embeddedCliRequiredSize(config);
    hard_assert(config_size <= CLI_BUFFER_SIZE);

    cli = embeddedCliNew(config);

    cli->writeChar = writeChar;
    cli->printLogo = cli_print_logo;

    for (int i = 0; i < sizeof(binding) / sizeof(binding[0]); i++)
        embeddedCliAddBinding(cli, binding[i]);

    adc_init();
    adc_gpio_init(26);
    adc_select_input(0);

    gpio_init(GPIO_RST);
    // gpio_set_dir(GPIO_RST, GPIO_OUT);
    // gpio_put(GPIO_RST, 1);
    gpio_set_dir(GPIO_RST, GPIO_IN);

    pulse.width = 1;
}
