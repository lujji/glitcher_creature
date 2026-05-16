#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#define CFG_TUSB_RHPORT0_MODE   OPT_MODE_DEVICE
#define CFG_TUSB_OS             OPT_OS_PICO

#define CFG_TUSB_DEBUG          0

#define CFG_TUD_ENDPOINT0_SIZE  64

#define CFG_TUD_CDC             2 // 2 CDC interfaces
#define CFG_TUD_MSC             0
#define CFG_TUD_HID             0
#define CFG_TUD_MIDI            0

// CDC fifo size
#define CFG_TUD_CDC_RX_BUFSIZE  256
#define CFG_TUD_CDC_TX_BUFSIZE  256

#endif /* TUSB_CONFIG_H */
