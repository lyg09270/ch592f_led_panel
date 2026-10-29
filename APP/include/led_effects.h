#ifndef LED_EFFECTS_H
#define LED_EFFECTS_H

#include <stdbool.h>
#include <stdint.h>

#include "dev_rgb_matrix.h"

#define LED_EFFECT_COUNT    15U

bool led_effects_start(uint8_t effect, uint8_t speed, uint8_t intensity,
                       uint8_t brightness);
void led_effects_stop(void);
bool led_effects_is_running(void);
void led_effects_render(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS]);

#endif /* LED_EFFECTS_H */
