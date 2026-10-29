#ifndef DEV_RGB_MATRIX_H
#define DEV_RGB_MATRIX_H

#include <stdbool.h>
#include <stdint.h>
#include "dev_config.h"

#ifndef DEV_RGB_MATRIX_WIDTH
#define DEV_RGB_MATRIX_WIDTH     \
    (DEV_RGB_MATRIX_PANEL_WIDTH * DEV_RGB_MATRIX_TILE_COLUMNS)
#endif

#ifndef DEV_RGB_MATRIX_HEIGHT
#define DEV_RGB_MATRIX_HEIGHT    \
    (DEV_RGB_MATRIX_PANEL_HEIGHT * DEV_RGB_MATRIX_TILE_ROWS)
#endif

#define DEV_RGB_MATRIX_RGB888_CHANNELS    3U

typedef struct dev_rgb_matrix
{
    uint16_t width;
    uint16_t height;
    uint8_t correction_r;
    uint8_t correction_g;
    uint8_t correction_b;
    uint8_t brightness;

    bool (*init)(struct dev_rgb_matrix *dev);
    bool (*set_correction)(struct dev_rgb_matrix *dev,
                           uint8_t red,
                           uint8_t green,
                           uint8_t blue);
    bool (*set_brightness)(struct dev_rgb_matrix *dev,
                           uint8_t brightness);
    bool (*write)(
        struct dev_rgb_matrix *dev,
        const uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                            [DEV_RGB_MATRIX_WIDTH]
                            [DEV_RGB_MATRIX_RGB888_CHANNELS]);
} dev_rgb_matrix_t;

extern dev_rgb_matrix_t dev_rgb_matrix;

#endif /* DEV_RGB_MATRIX_H */
