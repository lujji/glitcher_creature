#ifndef CONFIG_H
#define CONFIG_H

/* LEDs */
#define LED0_PIN            27 // default=25
#define LED1_PIN            28

/* glitch output - connect to the gate of the MOSFET */
#define GLITCH_OUT_PIN      7

/* trigger input */
#define GLITCH_TRIGGER_PIN  6

/* target reset */
#define GPIO_RST            29

#endif // CONFIG_H
