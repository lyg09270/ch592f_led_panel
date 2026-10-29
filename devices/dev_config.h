#ifndef DEV_CONFIG_H
#define DEV_CONFIG_H

/* Select exactly one WS2812 transport implementation. */
#define DEV_RGB_MATRIX_USE_WS2812_SPI        1
#define DEV_RGB_MATRIX_USE_WS2812_SPI_DMA    0
#define DEV_RGB_MATRIX_USE_WS2812_GPIO       0

/*
 * Logical display layout.
 *
 * Four 16x16 panels share one WS2812 data chain.  Panel numbers below are
 * physical chain order: MCU -> panel 0 -> panel 1 -> panel 2 -> panel 3.
 * Set this switch to 0 to restore the original single-panel 16x16 mode.
 */
#ifndef DEV_RGB_MATRIX_USE_TILED_2X2
#define DEV_RGB_MATRIX_USE_TILED_2X2          1
#endif

#define DEV_RGB_MATRIX_PANEL_WIDTH            16U
#define DEV_RGB_MATRIX_PANEL_HEIGHT           16U

/*
 * Pixel 0 starts the first vertical strip. Pixels then snake by columns,
 * rather than by horizontal rows, on the installed 16x16 panels.
 */
#define DEV_RGB_MATRIX_SERPENTINE_BY_COLUMN     1U
#define DEV_RGB_MATRIX_FIRST_STRIP_REVERSED     0U

#define DEV_RGB_MATRIX_ROTATION_0              0U
#define DEV_RGB_MATRIX_ROTATION_90             1U
#define DEV_RGB_MATRIX_ROTATION_180            2U
#define DEV_RGB_MATRIX_ROTATION_270            3U

#if DEV_RGB_MATRIX_USE_TILED_2X2 == 1
#define DEV_RGB_MATRIX_PANEL_COUNT              4U
#define DEV_RGB_MATRIX_TILE_COLUMNS             2U
#define DEV_RGB_MATRIX_TILE_ROWS                2U

/* Physical chain order: top-left, top-right, bottom-right, bottom-left. */
#define DEV_RGB_MATRIX_PANEL_0_TILE_X           0U
#define DEV_RGB_MATRIX_PANEL_0_TILE_Y           0U
#define DEV_RGB_MATRIX_PANEL_0_ROTATION         DEV_RGB_MATRIX_ROTATION_0
#define DEV_RGB_MATRIX_PANEL_1_TILE_X           1U
#define DEV_RGB_MATRIX_PANEL_1_TILE_Y           0U
#define DEV_RGB_MATRIX_PANEL_1_ROTATION         DEV_RGB_MATRIX_ROTATION_90
#define DEV_RGB_MATRIX_PANEL_2_TILE_X           1U
#define DEV_RGB_MATRIX_PANEL_2_TILE_Y           1U
#define DEV_RGB_MATRIX_PANEL_2_ROTATION         DEV_RGB_MATRIX_ROTATION_180
#define DEV_RGB_MATRIX_PANEL_3_TILE_X           0U
#define DEV_RGB_MATRIX_PANEL_3_TILE_Y           1U
#define DEV_RGB_MATRIX_PANEL_3_ROTATION         DEV_RGB_MATRIX_ROTATION_270
#else
#define DEV_RGB_MATRIX_PANEL_COUNT              1U
#define DEV_RGB_MATRIX_TILE_COLUMNS             1U
#define DEV_RGB_MATRIX_TILE_ROWS                1U
#define DEV_RGB_MATRIX_PANEL_0_TILE_X           0U
#define DEV_RGB_MATRIX_PANEL_0_TILE_Y           0U
#define DEV_RGB_MATRIX_PANEL_0_ROTATION         DEV_RGB_MATRIX_ROTATION_0
#endif

/* Analog microphone volume detector. */
#define DEV_MICROPHONE_USE_ADC                1

/* Temporarily uncorrected for comparison (0xFFFFFF). */
#define DEV_RGB_MATRIX_CORRECTION_R           255U
#define DEV_RGB_MATRIX_CORRECTION_G           255U
#define DEV_RGB_MATRIX_CORRECTION_B           255U

/*
 * Four-panel power guard.  Set to 0 to disable, or adjust to the continuous
 * current available from the 5 V LED supply.  The estimate assumes modern
 * WS2812-compatible pixels at about 1 mA idle plus 12 mA per full channel.
 */
#if DEV_RGB_MATRIX_USE_TILED_2X2 == 1
#define DEV_RGB_MATRIX_POWER_LIMIT_MA         5000UL
#else
#define DEV_RGB_MATRIX_POWER_LIMIT_MA         2000UL
#endif
#define DEV_RGB_MATRIX_PIXEL_IDLE_MA             1UL
#define DEV_RGB_MATRIX_CHANNEL_FULL_MA           12UL

#define DEV_RGB_MATRIX_CORRECT(VALUE, GAIN)                         \
    ((uint8_t)((((uint16_t)(VALUE) * (uint16_t)(GAIN)) + 127U) /   \
               255U))

#if (DEV_RGB_MATRIX_CORRECTION_R > 255U) || \
    (DEV_RGB_MATRIX_CORRECTION_G > 255U) || \
    (DEV_RGB_MATRIX_CORRECTION_B > 255U)
#error "RGB matrix color correction must be in the range 0..255"
#endif

#if (DEV_RGB_MATRIX_USE_WS2812_SPI + \
     DEV_RGB_MATRIX_USE_WS2812_SPI_DMA + \
     DEV_RGB_MATRIX_USE_WS2812_GPIO) != 1
#error "Select exactly one WS2812 transport in dev_config.h"
#endif

#if (DEV_RGB_MATRIX_USE_TILED_2X2 != 0) && \
    (DEV_RGB_MATRIX_USE_TILED_2X2 != 1)
#error "DEV_RGB_MATRIX_USE_TILED_2X2 must be 0 or 1"
#endif

#endif /* DEV_CONFIG_H */
