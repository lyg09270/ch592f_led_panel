#ifndef DRV_WS2812_LAYOUT_H
#define DRV_WS2812_LAYOUT_H

#include "dev_rgb_matrix.h"

/*
 * Shared logical-to-physical mapping for every WS2812 transport.
 * Rotation describes how the physical panel is mounted clockwise relative
 * to the logical screen.  Panel index is always the serial-chain order.
 */

#if (DEV_RGB_MATRIX_PANEL_WIDTH != DEV_RGB_MATRIX_PANEL_HEIGHT)
#error "90/270 degree tiled rotation currently requires square panels"
#endif

#if DEV_RGB_MATRIX_PANEL_0_ROTATION > DEV_RGB_MATRIX_ROTATION_270
#error "Panel 0 rotation must be 0, 90, 180, or 270 degrees"
#endif
#if (DEV_RGB_MATRIX_PANEL_0_TILE_X >= DEV_RGB_MATRIX_TILE_COLUMNS) || \
    (DEV_RGB_MATRIX_PANEL_0_TILE_Y >= DEV_RGB_MATRIX_TILE_ROWS)
#error "Panel 0 tile position is outside the logical matrix"
#endif

#if DEV_RGB_MATRIX_PANEL_COUNT > 1U
#if DEV_RGB_MATRIX_PANEL_1_ROTATION > DEV_RGB_MATRIX_ROTATION_270
#error "Panel 1 rotation must be 0, 90, 180, or 270 degrees"
#endif
#if (DEV_RGB_MATRIX_PANEL_1_TILE_X >= DEV_RGB_MATRIX_TILE_COLUMNS) || \
    (DEV_RGB_MATRIX_PANEL_1_TILE_Y >= DEV_RGB_MATRIX_TILE_ROWS)
#error "Panel 1 tile position is outside the logical matrix"
#endif
#endif

#if DEV_RGB_MATRIX_PANEL_COUNT > 2U
#if DEV_RGB_MATRIX_PANEL_2_ROTATION > DEV_RGB_MATRIX_ROTATION_270
#error "Panel 2 rotation must be 0, 90, 180, or 270 degrees"
#endif
#if (DEV_RGB_MATRIX_PANEL_2_TILE_X >= DEV_RGB_MATRIX_TILE_COLUMNS) || \
    (DEV_RGB_MATRIX_PANEL_2_TILE_Y >= DEV_RGB_MATRIX_TILE_ROWS)
#error "Panel 2 tile position is outside the logical matrix"
#endif
#endif

#if DEV_RGB_MATRIX_PANEL_COUNT > 3U
#if DEV_RGB_MATRIX_PANEL_3_ROTATION > DEV_RGB_MATRIX_ROTATION_270
#error "Panel 3 rotation must be 0, 90, 180, or 270 degrees"
#endif
#if (DEV_RGB_MATRIX_PANEL_3_TILE_X >= DEV_RGB_MATRIX_TILE_COLUMNS) || \
    (DEV_RGB_MATRIX_PANEL_3_TILE_Y >= DEV_RGB_MATRIX_TILE_ROWS)
#error "Panel 3 tile position is outside the logical matrix"
#endif
#endif

static inline void drv_ws2812_panel_config(uint8_t panel,
                                            uint8_t *tile_x,
                                            uint8_t *tile_y,
                                            uint8_t *rotation)
{
    switch(panel)
    {
#if DEV_RGB_MATRIX_PANEL_COUNT > 3U
        case 3U:
            *tile_x = DEV_RGB_MATRIX_PANEL_3_TILE_X;
            *tile_y = DEV_RGB_MATRIX_PANEL_3_TILE_Y;
            *rotation = DEV_RGB_MATRIX_PANEL_3_ROTATION;
            break;
#endif
#if DEV_RGB_MATRIX_PANEL_COUNT > 2U
        case 2U:
            *tile_x = DEV_RGB_MATRIX_PANEL_2_TILE_X;
            *tile_y = DEV_RGB_MATRIX_PANEL_2_TILE_Y;
            *rotation = DEV_RGB_MATRIX_PANEL_2_ROTATION;
            break;
#endif
#if DEV_RGB_MATRIX_PANEL_COUNT > 1U
        case 1U:
            *tile_x = DEV_RGB_MATRIX_PANEL_1_TILE_X;
            *tile_y = DEV_RGB_MATRIX_PANEL_1_TILE_Y;
            *rotation = DEV_RGB_MATRIX_PANEL_1_ROTATION;
            break;
#endif
        default:
            *tile_x = DEV_RGB_MATRIX_PANEL_0_TILE_X;
            *tile_y = DEV_RGB_MATRIX_PANEL_0_TILE_Y;
            *rotation = DEV_RGB_MATRIX_PANEL_0_ROTATION;
            break;
    }
}

/* Convert one panel-local serial position into its unrotated x/y position. */
static inline void drv_ws2812_serial_to_physical(uint16_t strip,
                                                  uint16_t offset,
                                                  uint16_t *physical_x,
                                                  uint16_t *physical_y)
{
    bool reversed = ((strip & 1U) != 0U);

#if DEV_RGB_MATRIX_FIRST_STRIP_REVERSED == 1U
    reversed = !reversed;
#endif

#if DEV_RGB_MATRIX_SERPENTINE_BY_COLUMN == 1U
    *physical_x = strip;
    *physical_y = reversed
                      ? (uint16_t)(DEV_RGB_MATRIX_PANEL_HEIGHT - 1U - offset)
                      : offset;
#else
    *physical_x = reversed
                      ? (uint16_t)(DEV_RGB_MATRIX_PANEL_WIDTH - 1U - offset)
                      : offset;
    *physical_y = strip;
#endif
}

static inline void drv_ws2812_map_pixel(uint8_t panel,
                                         uint16_t physical_x,
                                         uint16_t physical_y,
                                         uint16_t *logical_x,
                                         uint16_t *logical_y)
{
    uint8_t tile_x;
    uint8_t tile_y;
    uint8_t rotation;
    uint16_t local_x;
    uint16_t local_y;

    drv_ws2812_panel_config(panel, &tile_x, &tile_y, &rotation);

    switch(rotation)
    {
        case DEV_RGB_MATRIX_ROTATION_90:
            local_x = DEV_RGB_MATRIX_PANEL_WIDTH - 1U - physical_y;
            local_y = physical_x;
            break;
        case DEV_RGB_MATRIX_ROTATION_180:
            local_x = DEV_RGB_MATRIX_PANEL_WIDTH - 1U - physical_x;
            local_y = DEV_RGB_MATRIX_PANEL_HEIGHT - 1U - physical_y;
            break;
        case DEV_RGB_MATRIX_ROTATION_270:
            local_x = physical_y;
            local_y = DEV_RGB_MATRIX_PANEL_HEIGHT - 1U - physical_x;
            break;
        default:
            local_x = physical_x;
            local_y = physical_y;
            break;
    }

    *logical_x = (uint16_t)(tile_x * DEV_RGB_MATRIX_PANEL_WIDTH + local_x);
    *logical_y = (uint16_t)(tile_y * DEV_RGB_MATRIX_PANEL_HEIGHT + local_y);
}

static inline uint8_t drv_ws2812_output_channel(uint8_t value,
                                                 uint8_t correction,
                                                 uint8_t brightness)
{
    value = DEV_RGB_MATRIX_CORRECT(value, correction);
    return DEV_RGB_MATRIX_CORRECT(value, brightness);
}

#endif /* DRV_WS2812_LAYOUT_H */
