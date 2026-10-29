#include "dev_config.h"

#if DEV_RGB_MATRIX_USE_WS2812_SPI == 1

#include "drv_ws2812_spi.h"
#include "drv_ws2812_layout.h"

#include <stddef.h>

#define WS2812_SPI_BYTES_PER_CHANNEL       4UL
#define WS2812_SPI_BYTES_PER_PIXEL        12UL
#define WS2812_SPI_PANEL_DATA_BYTES                              \
    (DEV_RGB_MATRIX_PANEL_WIDTH *                               \
     DEV_RGB_MATRIX_PANEL_HEIGHT *                              \
     WS2812_SPI_BYTES_PER_PIXEL)

#if !WS2812_SPI_HW_CLOCK_IS_VALID
#error "WS2812 blocking SPI clock configuration is invalid"
#endif

#if WS2812_SPI_PANEL_DATA_BYTES > WS2812_SPI_HW_MAX_WRITE_BYTES
#error "One RGB matrix panel exceeds the blocking SPI transfer limit"
#endif

static uint8_t ws2812_spi_stream_pixels
    [DEV_RGB_MATRIX_PANEL_COUNT *
     DEV_RGB_MATRIX_PANEL_WIDTH *
     DEV_RGB_MATRIX_PANEL_HEIGHT *
     DEV_RGB_MATRIX_RGB888_CHANNELS];
static bool ws2812_spi_initialized;

static void ws2812_spi_stream_channel(uint8_t value)
{
    static const uint16_t encoded_nibbles[16] = {
        0x8888U, 0x888EU, 0x88E8U, 0x88EEU,
        0x8E88U, 0x8E8EU, 0x8EE8U, 0x8EEEU,
        0xE888U, 0xE88EU, 0xE8E8U, 0xE8EEU,
        0xEE88U, 0xEE8EU, 0xEEE8U, 0xEEEEU
    };
    uint16_t high = encoded_nibbles[value >> 4U];
    uint16_t low = encoded_nibbles[value & 0x0FU];

    WS2812_SPI_HW_STREAM_PUT4(
        (uint8_t)(high >> 8U),
        (uint8_t)high,
        (uint8_t)(low >> 8U),
        (uint8_t)low);
}

bool drv_ws2812_spi_init(dev_rgb_matrix_t *dev)
{
    if((dev == NULL) ||
       (dev->width != DEV_RGB_MATRIX_WIDTH) ||
       (dev->height != DEV_RGB_MATRIX_HEIGHT))
    {
        return false;
    }

    if(!WS2812_SPI_HW_INIT())
    {
        return false;
    }

    WS2812_SPI_HW_LATCH();

    ws2812_spi_initialized = true;
    return true;
}

bool drv_ws2812_spi_write(
    dev_rgb_matrix_t *dev,
    const uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                        [DEV_RGB_MATRIX_WIDTH]
                        [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    uint32_t irq_state;
    uint8_t panel;
    uint8_t *stream_pixel = ws2812_spi_stream_pixels;
    uint32_t stream_index;
    uint32_t channel_sum = 0UL;
    uint16_t serial_strip;
    uint16_t serial_offset;
    uint16_t physical_x;
    uint16_t physical_y;
    uint16_t logical_x;
    uint16_t logical_y;

    if((dev == NULL) || (pixels == NULL) || !ws2812_spi_initialized ||
       (dev->width != DEV_RGB_MATRIX_WIDTH) ||
       (dev->height != DEV_RGB_MATRIX_HEIGHT))
    {
        return false;
    }

    WS2812_SPI_HW_LATCH();

    /*
     * Perform rotation, layout and brightness work before the timing-critical
     * SPI stream.  The critical section below only expands bytes through a
     * four-entry lookup table, which is fast enough to keep the FIFO nonempty.
     */
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
#if WS2812_SPI_SERPENTINE_LAYOUT == 1
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

                uint8_t output;

                output = drv_ws2812_output_channel(
                    pixels[logical_y][logical_x][1],
                    dev->correction_g, dev->brightness);
                *stream_pixel++ = output;
                channel_sum += output;
                output = drv_ws2812_output_channel(
                    pixels[logical_y][logical_x][0],
                    dev->correction_r, dev->brightness);
                *stream_pixel++ = output;
                channel_sum += output;
                output = drv_ws2812_output_channel(
                    pixels[logical_y][logical_x][2],
                    dev->correction_b, dev->brightness);
                *stream_pixel++ = output;
                channel_sum += output;
            }
        }
    }

#if DEV_RGB_MATRIX_POWER_LIMIT_MA > 0U
    {
        const uint32_t pixel_count =
            DEV_RGB_MATRIX_PANEL_COUNT *
            DEV_RGB_MATRIX_PANEL_WIDTH *
            DEV_RGB_MATRIX_PANEL_HEIGHT;
        const uint32_t idle_current =
            pixel_count * DEV_RGB_MATRIX_PIXEL_IDLE_MA;
        const uint32_t channel_budget =
            (DEV_RGB_MATRIX_POWER_LIMIT_MA > idle_current)
                ? (((DEV_RGB_MATRIX_POWER_LIMIT_MA - idle_current) * 255UL) /
                   DEV_RGB_MATRIX_CHANNEL_FULL_MA)
                : 0UL;

        if((channel_sum > channel_budget) && (channel_sum != 0UL))
        {
            uint32_t index;

            for(index = 0U; index < sizeof(ws2812_spi_stream_pixels); index++)
            {
                ws2812_spi_stream_pixels[index] = (uint8_t)(
                    ((uint32_t)ws2812_spi_stream_pixels[index] *
                     channel_budget) / channel_sum);
            }
        }
    }
#endif

    WS2812_SPI_HW_ENTER_CRITICAL(irq_state);
    stream_index = 0U;
    for(panel = 0U; panel < DEV_RGB_MATRIX_PANEL_COUNT; panel++)
    {
        uint16_t panel_byte;

        if(panel == 0U)
        {
            WS2812_SPI_HW_STREAM_BEGIN(WS2812_SPI_PANEL_DATA_BYTES);
        }
        else
        {
            /*
             * Extend the byte counter while the previous panel's tail is
             * still buffered.  The FIFO never empties between panels, so the
             * WS2812 chain sees one continuous 1024-pixel frame.
             */
            WS2812_SPI_HW_STREAM_CONTINUE(WS2812_SPI_PANEL_DATA_BYTES);
        }
        for(panel_byte = 0U;
            panel_byte < (DEV_RGB_MATRIX_PANEL_WIDTH *
                          DEV_RGB_MATRIX_PANEL_HEIGHT *
                          DEV_RGB_MATRIX_RGB888_CHANNELS);
            panel_byte++)
        {
            ws2812_spi_stream_channel(
                ws2812_spi_stream_pixels[stream_index++]);
        }

    }
    WS2812_SPI_HW_STREAM_WAIT_COMPLETE();
    WS2812_SPI_HW_EXIT_CRITICAL(irq_state);

    WS2812_SPI_HW_LATCH();
    return true;
}

#endif /* DEV_RGB_MATRIX_USE_WS2812_SPI */
