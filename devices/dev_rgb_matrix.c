#include "dev_rgb_matrix.h"
#include "dev_config.h"

#include <stddef.h>

static bool dev_rgb_matrix_set_correction(dev_rgb_matrix_t *dev,
                                          uint8_t red,
                                          uint8_t green,
                                          uint8_t blue)
{
    if(dev == NULL)
    {
        return false;
    }

    dev->correction_r = red;
    dev->correction_g = green;
    dev->correction_b = blue;
    return true;
}

static bool dev_rgb_matrix_set_brightness(dev_rgb_matrix_t *dev,
                                          uint8_t brightness)
{
    if(dev == NULL)
    {
        return false;
    }

    dev->brightness = brightness;
    return true;
}

#if DEV_RGB_MATRIX_USE_WS2812_SPI == 1
#include "drv_ws2812_spi.h"

dev_rgb_matrix_t dev_rgb_matrix = {
    .width = DEV_RGB_MATRIX_WIDTH,
    .height = DEV_RGB_MATRIX_HEIGHT,
    .correction_r = DEV_RGB_MATRIX_CORRECTION_R,
    .correction_g = DEV_RGB_MATRIX_CORRECTION_G,
    .correction_b = DEV_RGB_MATRIX_CORRECTION_B,
    .brightness = 255U,
    .init = drv_ws2812_spi_init,
    .set_correction = dev_rgb_matrix_set_correction,
    .set_brightness = dev_rgb_matrix_set_brightness,
    .write = drv_ws2812_spi_write
};

#elif DEV_RGB_MATRIX_USE_WS2812_SPI_DMA == 1
#include "drv_ws2812_spi_dma.h"

dev_rgb_matrix_t dev_rgb_matrix = {
    .width = DEV_RGB_MATRIX_WIDTH,
    .height = DEV_RGB_MATRIX_HEIGHT,
    .correction_r = DEV_RGB_MATRIX_CORRECTION_R,
    .correction_g = DEV_RGB_MATRIX_CORRECTION_G,
    .correction_b = DEV_RGB_MATRIX_CORRECTION_B,
    .brightness = 255U,
    .init = drv_ws2812_spi_dma_init,
    .set_correction = dev_rgb_matrix_set_correction,
    .set_brightness = dev_rgb_matrix_set_brightness,
    .write = drv_ws2812_spi_dma_write
};

#elif DEV_RGB_MATRIX_USE_WS2812_GPIO == 1
#include "drv_ws2812_gpio.h"

dev_rgb_matrix_t dev_rgb_matrix = {
    .width = DEV_RGB_MATRIX_WIDTH,
    .height = DEV_RGB_MATRIX_HEIGHT,
    .correction_r = DEV_RGB_MATRIX_CORRECTION_R,
    .correction_g = DEV_RGB_MATRIX_CORRECTION_G,
    .correction_b = DEV_RGB_MATRIX_CORRECTION_B,
    .brightness = 255U,
    .init = drv_ws2812_gpio_init,
    .set_correction = dev_rgb_matrix_set_correction,
    .set_brightness = dev_rgb_matrix_set_brightness,
    .write = drv_ws2812_gpio_write
};
#endif
