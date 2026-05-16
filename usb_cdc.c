#include "usb_cdc.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pico/stdlib.h>
#include <pico/bootrom.h>
#include <hardware/gpio.h>
#include <hardware/clocks.h>
#include <hardware/watchdog.h>
#include <tusb.h>

enum {
    CDC_CLI,    // cli and binary
    CDC_BRIDGE, // UART pass-through
};

static uint8_t cdc_buf[USB_CDC_CLI_BUF_SIZE];
static volatile uint32_t baud = 115200;

static void bridge_tx_rx() {
    uint32_t count = 0;

    /* UART -> USB*/
    while (uart_is_readable(USB_CDC_UART) && count < sizeof(cdc_buf)) {
        cdc_buf[count++] = uart_getc(USB_CDC_UART);
    }
    if (count && tud_cdc_n_connected(CDC_BRIDGE)) {
        uint32_t sent = 0;
        while (sent < count) {
            sent += tud_cdc_n_write(CDC_BRIDGE, cdc_buf + sent, count - sent);
            tud_task();
        }
        tud_cdc_n_write_flush(CDC_BRIDGE);
    }

    /* USB -> UART */
    count = tud_cdc_n_available(CDC_BRIDGE);
    if (!count)
        return;

    if (count > sizeof(cdc_buf))
        count = sizeof(cdc_buf);
    uint32_t read = tud_cdc_n_read(CDC_BRIDGE, cdc_buf, count);
    uart_write_blocking(USB_CDC_UART, cdc_buf, read);
}

/* redirect printf to CDC_CLI */
int _write(int fd, const char *buf, int count) {
    (void) fd;
    usb_cdc_cli_write((const uint8_t *) buf, count);
    return count;
}

size_t usb_cdc_task(uint8_t **buf) {
    tud_task();
    bridge_tx_rx();

    /* read cli */
    if (!tud_cdc_n_connected(CDC_CLI) || !tud_cdc_n_available(CDC_CLI))
        return 0;

    uint32_t count = tud_cdc_n_available(CDC_CLI);
    if (count > sizeof(cdc_buf))
        count = sizeof(cdc_buf);
    size_t rx_len = tud_cdc_n_read(CDC_CLI, cdc_buf, count);

    if (rx_len > 0)
        *buf = cdc_buf;

    return rx_len;
}

void usb_cdc_cli_write(const uint8_t *buf, int len) {
    if (tud_cdc_n_connected(CDC_CLI)) {
        for (int written = 0; written < len; ) {
            const uint32_t avail = tud_cdc_n_write_available(CDC_CLI);
            if (avail == 0) {
                tud_cdc_n_write_flush(CDC_CLI);
                tud_task();
                continue;
            }
            const int bytes_left = len - written;
            const int chunk = bytes_left < avail ? bytes_left : avail;
            written += tud_cdc_n_write(CDC_CLI, buf + written, chunk);
        }
        tud_cdc_n_write_flush(CDC_CLI);
    }
}

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *coding) {
    if (itf == CDC_BRIDGE) {
        uart_parity_t parity;
        switch (coding->parity) {
            case CDC_LINE_CODING_PARITY_ODD:  parity = UART_PARITY_ODD;  break;
            case CDC_LINE_CODING_PARITY_EVEN: parity = UART_PARITY_EVEN; break;
            default:                          parity = UART_PARITY_NONE; break;
        }
        unsigned int data_bits = (coding->data_bits == 7) ? 7 : 8;
        unsigned int stop_bits = (coding->stop_bits == CDC_LINE_CODING_STOP_BITS_2) ? 2 : 1;
        baud = coding->bit_rate;

        uart_set_baudrate(USB_CDC_UART, baud);
        uart_set_format(USB_CDC_UART, data_bits, stop_bits, parity);
    }
}

void usb_cdc_init() {
    /**
     * detach clk_peri from clk_sys and attached it to pll_usb,
     * so that changing system clock wouldn't affect uart baudrate
     */
    clock_configure(
        clk_peri,
        0,
        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
        48 * 1000 * 1000,
        48 * 1000 * 1000
    );
    tusb_init();
    uart_init(USB_CDC_UART, baud);
    gpio_set_function(USB_CDC_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(USB_CDC_UART_RX, GPIO_FUNC_UART);
    uart_set_hw_flow(USB_CDC_UART, false, false);
    uart_set_format(USB_CDC_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(USB_CDC_UART, true);
}
