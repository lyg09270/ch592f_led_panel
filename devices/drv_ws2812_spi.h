#ifndef DRV_WS2812_SPI_H
#define DRV_WS2812_SPI_H

#include "CH59x_common.h"
#include "dev_rgb_matrix.h"

/*
 * Blocking SPI hardware adaptation. Porting this implementation only requires
 * replacing the WS2812_SPI_HW_* definitions below.
 *
 *   WS2812 DIN -> CH592 PA14 (SPI0 MOSI)
 *   SPI0 SCK is PA13 and does not connect to the WS2812 panel.
 */
#define WS2812_SPI_HW_TARGET_BIT_RATE_HZ   3200000UL
#define WS2812_SPI_HW_CLOCK_DIVIDER        \
    ((FREQ_SYS + (WS2812_SPI_HW_TARGET_BIT_RATE_HZ / 2U)) / \
     WS2812_SPI_HW_TARGET_BIT_RATE_HZ)
#define WS2812_SPI_HW_BIT_RATE_HZ          \
    (FREQ_SYS / WS2812_SPI_HW_CLOCK_DIVIDER)
#define WS2812_SPI_HW_MAX_WRITE_BYTES      4095UL
#define WS2812_SPI_HW_CLOCK_IS_VALID                                  \
    ((WS2812_SPI_HW_CLOCK_DIVIDER >= 2U) &&                           \
     (WS2812_SPI_HW_CLOCK_DIVIDER <= 255U) &&                         \
     (WS2812_SPI_HW_BIT_RATE_HZ >= 3100000UL) &&                      \
     (WS2812_SPI_HW_BIT_RATE_HZ <= 3300000UL))

#ifndef WS2812_SPI_SERPENTINE_LAYOUT
#define WS2812_SPI_SERPENTINE_LAYOUT       1U
#endif

#define WS2812_SPI_HW_INIT()                                          \
    (GPIOPinRemap(DISABLE, RB_PIN_SPI0),                              \
     GPIOA_ResetBits(GPIO_Pin_13 | GPIO_Pin_14),                      \
     GPIOA_ModeCfg(GPIO_Pin_13, GPIO_ModeOut_PP_5mA),                 \
     GPIOA_ModeCfg(GPIO_Pin_14, GPIO_ModeOut_PP_20mA),                \
     SPI0_MasterDefInit(),                                            \
     SPI0_CLKCfg((uint8_t)WS2812_SPI_HW_CLOCK_DIVIDER),               \
     SPI0_DataMode(Mode0_HighBitINFront),                             \
     true)

#define WS2812_SPI_HW_WRITE(DATA, SIZE)                               \
    (SPI0_MasterTrans((DATA), (uint16_t)(SIZE)), true)
#define WS2812_SPI_HW_LATCH()                                         \
    do                                                               \
    {                                                                \
        while(R8_SPI0_FIFO_COUNT != 0U)                              \
        {                                                            \
        }                                                            \
        while((R8_SPI0_INT_FLAG & RB_SPI_FREE) == 0U)                \
        {                                                            \
        }                                                            \
        DelayUs(300U);                                               \
    } while(0)
#define WS2812_SPI_HW_STREAM_BEGIN(SIZE)                              \
    (R8_SPI0_CTRL_MOD &= (uint8_t)~RB_SPI_FIFO_DIR,                  \
     R16_SPI0_TOTAL_CNT = (uint16_t)(SIZE),                          \
     R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END)
#define WS2812_SPI_HW_STREAM_CONTINUE(SIZE)                          \
    (R16_SPI0_TOTAL_CNT = (uint16_t)((SIZE) +                        \
                                      R8_SPI0_FIFO_COUNT),           \
     R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END)
#define WS2812_SPI_HW_STREAM_PUT(BYTE)                               \
    do                                                               \
    {                                                                \
        while(R8_SPI0_FIFO_COUNT >= SPI_FIFO_SIZE)                   \
        {                                                            \
        }                                                            \
        R8_SPI0_FIFO = (uint8_t)(BYTE);                              \
    } while(0)
#define WS2812_SPI_HW_STREAM_PUT4(B0, B1, B2, B3)                   \
    do                                                               \
    {                                                                \
        while(R8_SPI0_FIFO_COUNT > (SPI_FIFO_SIZE - 4U))             \
        {                                                            \
        }                                                            \
        R8_SPI0_FIFO = (uint8_t)(B0);                                \
        R8_SPI0_FIFO = (uint8_t)(B1);                                \
        R8_SPI0_FIFO = (uint8_t)(B2);                                \
        R8_SPI0_FIFO = (uint8_t)(B3);                                \
    } while(0)
#define WS2812_SPI_HW_STREAM_PUT3(B0, B1, B2)                       \
    do                                                               \
    {                                                                \
        while(R8_SPI0_FIFO_COUNT > (SPI_FIFO_SIZE - 3U))             \
        {                                                            \
        }                                                            \
        R8_SPI0_FIFO = (uint8_t)(B0);                                \
        R8_SPI0_FIFO = (uint8_t)(B1);                                \
        R8_SPI0_FIFO = (uint8_t)(B2);                                \
    } while(0)
#define WS2812_SPI_HW_STREAM_WAIT_COMPLETE()                         \
    do                                                               \
    {                                                                \
        while(R8_SPI0_FIFO_COUNT != 0U)                              \
        {                                                            \
        }                                                            \
        while((R8_SPI0_INT_FLAG & RB_SPI_FREE) == 0U)                \
        {                                                            \
        }                                                            \
    } while(0)
#define WS2812_SPI_HW_ENTER_CRITICAL(STATE)                          \
    SYS_DisableAllIrq(&(STATE))
#define WS2812_SPI_HW_EXIT_CRITICAL(STATE)                           \
    SYS_RecoverIrq(STATE)

bool drv_ws2812_spi_init(dev_rgb_matrix_t *dev);
bool drv_ws2812_spi_write(
    dev_rgb_matrix_t *dev,
    const uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                        [DEV_RGB_MATRIX_WIDTH]
                        [DEV_RGB_MATRIX_RGB888_CHANNELS]);

#endif /* DRV_WS2812_SPI_H */
