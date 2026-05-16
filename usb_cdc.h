#ifndef USB_CDC_H
#define USB_CDC_H

#include <stdint.h>
#include <stddef.h>
#include <hardware/uart.h>

#define USB_CDC_UART            uart0
#define USB_CDC_UART_TX         0
#define USB_CDC_UART_RX         1
#define USB_CDC_CLI_BUF_SIZE    64

/**
 * Initialize USB CDC.
 * Creates two interfaces: CLI and UART bridge
 */
void usb_cdc_init();

/**
 * Handles USB tasks and UART bridging. Should be called in main loop
 *
 * @param buf pointer to received data from cli/cmd interface
 * @return length of received data in buf
 */
size_t usb_cdc_task(uint8_t **buf);

/**
 * Write data into CLI interface
 */
void usb_cdc_cli_write(const uint8_t *data, int len);

#endif // USB_CDC_H
