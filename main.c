#include <stdint.h>
#include <pico/stdlib.h>
#include <hardware/gpio.h>
#include "cmd.h"
#include "cli.h"
#include "usb_cdc.h"
#include "glitch.h"
#include "led.h"
#include "config.h"

void main() {
    gpio_init(GPIO_RST);
    gpio_set_dir(GPIO_RST, GPIO_OUT);
    gpio_set_pulls(GPIO_RST, false, false);

    usb_cdc_init();
    stdio_init_all();
    glitch_init();
    cli_init();
    led_init();

    cmd_set_freq(250000000);
    glitch_set_pattern(0b11);

    while (1) {
        /* usb task */
        uint8_t *buf = NULL;
        size_t len = usb_cdc_task(&buf);

        cli_process((char *) buf, len);

        if (len > 0 && buf[0] >= 128) {
            /* process usb command */
            int rsp_len = cmd_process(buf, len, USB_CDC_CLI_BUF_SIZE);
            if (rsp_len > 0)
                usb_cdc_cli_write(buf, rsp_len);
        }

        led_blink();
    }
}
