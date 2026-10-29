#include "dev_config.h"

#if DEV_RGB_MATRIX_USE_WS2812_SPI_DMA == 1

#include "drv_ws2812_spi_dma.h"
#include "drv_ws2812_layout.h"

#include <stddef.h>

#define WS2812_SPI_DMA_BYTES_PER_CHANNEL       3UL
#define WS2812_SPI_DMA_BYTES_PER_PIXEL         9UL
#define WS2812_SPI_DMA_PANEL_DATA_BYTES                              \
    (DEV_RGB_MATRIX_PANEL_WIDTH *                                    \
     DEV_RGB_MATRIX_PANEL_HEIGHT *                                   \
     WS2812_SPI_DMA_BYTES_PER_PIXEL)

/* Enter the short hand-over critical section with about 213 us remaining. */
#define WS2812_SPI_DMA_HANDOVER_BYTES          64U
#define WS2812_SPI_DMA_WAIT_SPINS         6000000UL

#if !WS2812_SPI_DMA_HW_CLOCK_IS_VALID
#error "WS2812 SPI DMA clock configuration is invalid"
#endif

#if (DEV_RGB_MATRIX_PANEL_COUNT != 1U) && \
    (DEV_RGB_MATRIX_PANEL_COUNT != 4U)
#error "WS2812 DMA supports the configured 1-panel or 4-panel layouts"
#endif

#if WS2812_SPI_DMA_PANEL_DATA_BYTES > WS2812_SPI_DMA_HW_MAX_COUNT
#error "One RGB matrix panel exceeds the CH59x SPI DMA counter"
#endif

static uint8_t ws2812_spi_dma_buffer[2][WS2812_SPI_DMA_PANEL_DATA_BYTES]
    __attribute__((aligned(4)));
static bool ws2812_spi_dma_initialized;

static const uint16_t ws2812_spi_dma_nibble_lut[16] = {
    0x924U, 0x926U, 0x934U, 0x936U,
    0x9A4U, 0x9A6U, 0x9B4U, 0x9B6U,
    0xD24U, 0xD26U, 0xD34U, 0xD36U,
    0xDA4U, 0xDA6U, 0xDB4U, 0xDB6U
};

static uint8_t ws2812_spi_dma_power_scale(
    const dev_rgb_matrix_t *dev,
    const uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                        [DEV_RGB_MATRIX_WIDTH]
                        [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
#if DEV_RGB_MATRIX_POWER_LIMIT_MA > 0U
    uint16_t x;
    uint16_t y;
    uint32_t weighted_sum = 0UL;
    uint32_t corrected_sum;
    uint32_t channel_sum;
    const uint32_t pixel_count = DEV_RGB_MATRIX_PANEL_COUNT *
        DEV_RGB_MATRIX_PANEL_WIDTH * DEV_RGB_MATRIX_PANEL_HEIGHT;
    const uint32_t idle_current =
        pixel_count * DEV_RGB_MATRIX_PIXEL_IDLE_MA;
    const uint32_t channel_budget =
        (DEV_RGB_MATRIX_POWER_LIMIT_MA > idle_current)
            ? (((DEV_RGB_MATRIX_POWER_LIMIT_MA - idle_current) * 255UL) /
               DEV_RGB_MATRIX_CHANNEL_FULL_MA)
            : 0UL;

    for(y = 0U; y < DEV_RGB_MATRIX_HEIGHT; y++)
    {
        for(x = 0U; x < DEV_RGB_MATRIX_WIDTH; x++)
        {
            weighted_sum +=
                (uint32_t)pixels[y][x][0] * dev->correction_r;
            weighted_sum +=
                (uint32_t)pixels[y][x][1] * dev->correction_g;
            weighted_sum +=
                (uint32_t)pixels[y][x][2] * dev->correction_b;
        }
    }

    corrected_sum = (weighted_sum + 127UL) / 255UL;
    channel_sum =
        (corrected_sum * (uint32_t)dev->brightness + 127UL) / 255UL;

    if((channel_sum > channel_budget) && (channel_sum != 0UL))
    {
        return (uint8_t)((channel_budget * 255UL) / channel_sum);
    }
#else
    (void)dev;
    (void)pixels;
#endif

    return 255U;
}

static void ws2812_spi_dma_encode_channel(uint8_t value, uint8_t **output)
{
    const uint16_t high = ws2812_spi_dma_nibble_lut[value >> 4U];
    const uint16_t low = ws2812_spi_dma_nibble_lut[value & 0x0FU];

    *(*output)++ = (uint8_t)(high >> 4U);
    *(*output)++ = (uint8_t)((high << 4U) | (low >> 8U));
    *(*output)++ = (uint8_t)low;
}

static void ws2812_spi_dma_encode_panel(
    const dev_rgb_matrix_t *dev,
    const uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                        [DEV_RGB_MATRIX_WIDTH]
                        [DEV_RGB_MATRIX_RGB888_CHANNELS],
    uint8_t panel,
    uint8_t effective_brightness,
    uint8_t *output)
{
    uint16_t serial_strip;
    uint16_t serial_offset;
    uint16_t physical_x;
    uint16_t physical_y;
    uint16_t logical_x;
    uint16_t logical_y;

#if DEV_RGB_MATRIX_SERPENTINE_BY_COLUMN == 1U
    const uint16_t strip_count = DEV_RGB_MATRIX_PANEL_WIDTH;
    const uint16_t strip_length = DEV_RGB_MATRIX_PANEL_HEIGHT;
#else
    const uint16_t strip_count = DEV_RGB_MATRIX_PANEL_HEIGHT;
    const uint16_t strip_length = DEV_RGB_MATRIX_PANEL_WIDTH;
#endif

    for(serial_strip = 0U; serial_strip < strip_count; serial_strip++)
    {
        for(serial_offset = 0U; serial_offset < strip_length; serial_offset++)
        {
            drv_ws2812_serial_to_physical(serial_strip, serial_offset,
                                           &physical_x, &physical_y);
            drv_ws2812_map_pixel(panel, physical_x, physical_y,
                                 &logical_x, &logical_y);

            ws2812_spi_dma_encode_channel(
                drv_ws2812_output_channel(
                    pixels[logical_y][logical_x][1],
                    dev->correction_g, effective_brightness),
                &output);
            ws2812_spi_dma_encode_channel(
                drv_ws2812_output_channel(
                    pixels[logical_y][logical_x][0],
                    dev->correction_r, effective_brightness),
                &output);
            ws2812_spi_dma_encode_channel(
                drv_ws2812_output_channel(
                    pixels[logical_y][logical_x][2],
                    dev->correction_b, effective_brightness),
                &output);
        }
    }
}

static bool ws2812_spi_dma_handover(const uint8_t *next)
{
    uint32_t timeout = WS2812_SPI_DMA_WAIT_SPINS;
    uint32_t irq_state;

    while((WS2812_SPI_DMA_HW_REMAINING_COUNT() >
           WS2812_SPI_DMA_HANDOVER_BYTES) &&
          !WS2812_SPI_DMA_HW_COUNT_END_PENDING() &&
          (timeout != 0UL))
    {
        timeout--;
    }

    if((timeout == 0UL) || WS2812_SPI_DMA_HW_COUNT_END_PENDING())
    {
        return false;
    }

    WS2812_SPI_DMA_HW_ENTER_CRITICAL(irq_state);
    timeout = WS2812_SPI_DMA_WAIT_SPINS;
    while(!WS2812_SPI_DMA_HW_COUNT_END_PENDING() && (timeout != 0UL))
    {
        timeout--;
    }

    if(timeout == 0UL)
    {
        WS2812_SPI_DMA_HW_EXIT_CRITICAL(irq_state);
        return false;
    }

    WS2812_SPI_DMA_HW_STOP();
    WS2812_SPI_DMA_HW_START(next, WS2812_SPI_DMA_PANEL_DATA_BYTES);
    WS2812_SPI_DMA_HW_EXIT_CRITICAL(irq_state);
    return true;
}

static bool ws2812_spi_dma_wait_final(void)
{
    uint32_t timeout = WS2812_SPI_DMA_WAIT_SPINS;

    while(!WS2812_SPI_DMA_HW_COUNT_END_PENDING() && (timeout != 0UL))
    {
        timeout--;
    }

    if(timeout == 0UL)
    {
        return false;
    }

    WS2812_SPI_DMA_HW_STOP();
    WS2812_SPI_DMA_HW_WAIT_IDLE();
    return true;
}

static bool ws2812_spi_dma_fail(void)
{
    WS2812_SPI_DMA_HW_ABORT();
    return false;
}

bool drv_ws2812_spi_dma_init(dev_rgb_matrix_t *dev)
{
    if((dev == NULL) ||
       (dev->width != DEV_RGB_MATRIX_WIDTH) ||
       (dev->height != DEV_RGB_MATRIX_HEIGHT))
    {
        return false;
    }

    if(!WS2812_SPI_DMA_HW_INIT())
    {
        return false;
    }

    WS2812_SPI_DMA_HW_LATCH();
    ws2812_spi_dma_initialized = true;
    return true;
}

bool drv_ws2812_spi_dma_write(
    dev_rgb_matrix_t *dev,
    const uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                        [DEV_RGB_MATRIX_WIDTH]
                        [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    uint8_t power_scale;
    uint8_t effective_brightness;

    if((dev == NULL) || (pixels == NULL) ||
       !ws2812_spi_dma_initialized ||
       (dev->width != DEV_RGB_MATRIX_WIDTH) ||
       (dev->height != DEV_RGB_MATRIX_HEIGHT))
    {
        return false;
    }

    power_scale = ws2812_spi_dma_power_scale(dev, pixels);
    effective_brightness = (uint8_t)(
        (((uint32_t)dev->brightness * power_scale) + 127UL) / 255UL);

    ws2812_spi_dma_encode_panel(dev, pixels, 0U,
                                effective_brightness,
                                ws2812_spi_dma_buffer[0]);

#if DEV_RGB_MATRIX_PANEL_COUNT == 4U
    ws2812_spi_dma_encode_panel(dev, pixels, 1U,
                                effective_brightness,
                                ws2812_spi_dma_buffer[1]);
#endif

    WS2812_SPI_DMA_HW_LATCH();
    WS2812_SPI_DMA_HW_START(ws2812_spi_dma_buffer[0],
                            WS2812_SPI_DMA_PANEL_DATA_BYTES);

#if DEV_RGB_MATRIX_PANEL_COUNT == 4U
    if(!ws2812_spi_dma_handover(ws2812_spi_dma_buffer[1]))
    {
        return ws2812_spi_dma_fail();
    }

    ws2812_spi_dma_encode_panel(dev, pixels, 2U,
                                effective_brightness,
                                ws2812_spi_dma_buffer[0]);
    if(!ws2812_spi_dma_handover(ws2812_spi_dma_buffer[0]))
    {
        return ws2812_spi_dma_fail();
    }

    ws2812_spi_dma_encode_panel(dev, pixels, 3U,
                                effective_brightness,
                                ws2812_spi_dma_buffer[1]);
    if(!ws2812_spi_dma_handover(ws2812_spi_dma_buffer[1]))
    {
        return ws2812_spi_dma_fail();
    }
#endif

    if(!ws2812_spi_dma_wait_final())
    {
        return ws2812_spi_dma_fail();
    }

    WS2812_SPI_DMA_HW_LATCH();
    return true;
}

#endif /* DEV_RGB_MATRIX_USE_WS2812_SPI_DMA */
