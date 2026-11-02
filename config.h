#ifndef CONFIG_H
#define CONFIG_H

/* LEDs */
#define LED0_PIN            27 // default=25
#define LED1_PIN            28

/* glitch output (12..19) - connect to the gate of the MOSFET */
#define GLITCH_OUT_PIN      16

/* number of pins connected in parallel */
#define GLITCH_OUT_COUNT    2

/* capacitor bank switcher output 0 */
#define SWITCH_OUT_PIN      20

/* number of switcher outputs*/
#define SWITCH_OUT_COUNT    4

/* trigger input */
#define GLITCH_TRIGGER_PIN  6

/* target reset */
#define GPIO_RST            4

#endif // CONFIG_H
