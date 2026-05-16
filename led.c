#include "led.h"
#include <pico/stdlib.h>
#include <pico/time.h>
#include <hardware/irq.h>
#include <hardware/pwm.h>
#include <stdlib.h>
#include "config.h"

#define LED1_MAX_BRIGHTNESS (128*128)

static void pwm_isr() {
    static uint32_t duty;
    static uint8_t up;
    const uint8_t duty_min = 90;

    pwm_clear_irq(pwm_gpio_to_slice_num(LED0_PIN));

    duty = up ? duty + 1 : duty - 1;
    if (duty > 255 || duty < duty_min) {
        duty = up ? 255 : duty_min;
        up ^= 1;
    }

    pwm_set_gpio_level(LED0_PIN, duty * duty);
}

void led_blink() {
    static uint32_t ticks_prev = 0;
    static uint32_t next_flicker = 0;
    static int flicker = 0;

    uint32_t ticks = to_ms_since_boot(get_absolute_time());
    /* flicker randomly every 2-4s */
    if (flicker >= 0 && (ticks - ticks_prev >= 20)) {
        flicker--;
        ticks_prev = ticks;
        pwm_set_gpio_level(LED1_PIN, flicker & 1 ? LED1_MAX_BRIGHTNESS : 0);
    } else if (ticks - ticks_prev >= next_flicker) {
        ticks_prev = ticks;
        pwm_set_gpio_level(LED1_PIN, 0);
        flicker = 4;
        next_flicker = 2100 + rand() % 2001;
    }
}

void led_init() {
    gpio_set_function(LED0_PIN, GPIO_FUNC_PWM);
    int slice_num = pwm_gpio_to_slice_num(LED0_PIN);

    /* heartbeat led */
    pwm_clear_irq(slice_num);
    pwm_set_irq_enabled(slice_num, true);
    irq_set_exclusive_handler(PWM_DEFAULT_IRQ_NUM(), pwm_isr);
    irq_set_enabled(PWM_DEFAULT_IRQ_NUM(), true);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 20.0f);
    pwm_init(slice_num, &config, true);

    /* always on led */
    gpio_set_function(LED1_PIN, GPIO_FUNC_PWM);
    slice_num = pwm_gpio_to_slice_num(LED1_PIN);
    pwm_init(slice_num, &config, true);
    pwm_set_gpio_level(LED1_PIN, LED1_MAX_BRIGHTNESS);

    gpio_set_drive_strength(LED0_PIN, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(LED1_PIN, GPIO_DRIVE_STRENGTH_12MA);
}
