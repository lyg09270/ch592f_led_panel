#include "dev_config.h"

#if DEV_RGB_MATRIX_USE_WS2812_GPIO == 1

#include "drv_ws2812_gpio.h"
#include "drv_ws2812_layout.h"

#include <stddef.h>

static bool ws2812_gpio_initialized;

static inline __attribute__((always_inline))
void ws2812_gpio_write_bit(bool value)
{
    uint32_t start;

    WS2812_GPIO_HW_HIGH();
    start = WS2812_GPIO_HW_TIME_NOW();
    WS2812_GPIO_HW_WAIT_UNTIL(
        start,
        value ? WS2812_GPIO_HW_T1H_CYCLES
              : WS2812_GPIO_HW_T0H_CYCLES);
    WS2812_GPIO_HW_LOW();
    WS2812_GPIO_HW_WAIT_UNTIL(start, WS2812_GPIO_HW_BIT_CYCLES);
}

static inline __attribute__((always_inline))
void ws2812_gpio_write_byte(uint8_t value)
{
    uint8_t mask;

    for(mask = 0x80U; mask != 0U; mask >>= 1U)
    {
        ws2812_gpio_write_bit((value & mask) != 0U);
    }
}

bool drv_ws2812_gpio_init(dev_rgb_matrix_t *dev)
{
    if((dev == NULL) ||
       (dev->width != DEV_RGB_MATRIX_WIDTH) ||
       (dev->height != DEV_RGB_MATRIX_HEIGHT))
    {
        return false;
    }

    if(!WS2812_GPIO_HW_INIT())
    {
        return false;
    }

    WS2812_GPIO_HW_RESET();
    ws2812_gpio_initialized = true;
    return true;
}

bool drv_ws2812_gpio_write(
    dev_rgb_matrix_t *dev,
    const uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                        [DEV_RGB_MATRIX_WIDTH]
                        [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    uint32_t irq_state;
    uint8_t panel;
    uint16_t serial_strip;
    uint16_t serial_offset;
    uint16_t physical_x;
    uint16_t physical_y;
    uint16_t logical_x;
    uint16_t logical_y;

    if((dev == NULL) || (pixels == NULL) ||
       !ws2812_gpio_initialized ||
       (dev->width != DEV_RGB_MATRIX_WIDTH) ||
       (dev->height != DEV_RGB_MATRIX_HEIGHT))
    {
        return false;
    }

    WS2812_GPIO_HW_RESET();
    WS2812_GPIO_HW_ENTER_CRITICAL(irq_state);

    for(panel = 0U; panel < DEV_RGB_MATRIX_PANEL_COUNT; panel++)
    {
        for(serial_strip = 0U;
            serial_strip < DEV_RGB_MATRIX_PANEL_HEIGHT;
            serial_strip++)
        {
            for(serial_offset = 0U;
                serial_offset < DEV_RGB_MATRIX_PANEL_WIDTH;
                serial_offset++)
            {
#if WS2812_GPIO_SERPENTINE_LAYOUT == 1
                drv_ws2812_serial_to_physical(serial_strip,
                                               serial_offset,
                                               &physical_x,
                                               &physical_y);
#else
                physical_x = serial_offset;
                physical_y = serial_strip;
#endif
                drv_ws2812_map_pixel(panel, physical_x, physical_y,
                                     &logical_x, &logical_y);

                ws2812_gpio_write_byte(drv_ws2812_output_channel(
                    pixels[logical_y][logical_x][1],
                    dev->correction_g, dev->brightness));
                ws2812_gpio_write_byte(drv_ws2812_output_channel(
                    pixels[logical_y][logical_x][0],
                    dev->correction_r, dev->brightness));
                ws2812_gpio_write_byte(drv_ws2812_output_channel(
                    pixels[logical_y][logical_x][2],
                    dev->correction_b, dev->brightness));
            }
        }
    }

    WS2812_GPIO_HW_LOW();
    WS2812_GPIO_HW_EXIT_CRITICAL(irq_state);
    WS2812_GPIO_HW_RESET();
    return true;
}

#endif /* DEV_RGB_MATRIX_USE_WS2812_GPIO */
