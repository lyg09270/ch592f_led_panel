#ifndef DRV_WS2812_SPI_DMA_H
#define DRV_WS2812_SPI_DMA_H

#include "CH59x_common.h"
#include "dev_rgb_matrix.h"

/*
 * CH59x SPI0 DMA adaptation.
 *
 * One WS2812 bit is encoded as three SPI bits at 2.4 MHz:
 *   0 -> 100  (0.417 us high, 0.833 us low)
 *   1 -> 110  (0.833 us high, 0.417 us low)
 *
 * All HAL/register semantics stay behind these macros. The .c file only
 * implements the portable ping-pong transport sequence.
 */
#define WS2812_SPI_DMA_HW_TARGET_BIT_RATE_HZ   2400000UL
#define WS2812_SPI_DMA_HW_CLOCK_DIVIDER                             \
    ((FREQ_SYS + (WS2812_SPI_DMA_HW_TARGET_BIT_RATE_HZ / 2U)) /    \
     WS2812_SPI_DMA_HW_TARGET_BIT_RATE_HZ)
#define WS2812_SPI_DMA_HW_BIT_RATE_HZ                              \
    (FREQ_SYS / WS2812_SPI_DMA_HW_CLOCK_DIVIDER)
#define WS2812_SPI_DMA_HW_MAX_COUNT             4095U
#define WS2812_SPI_DMA_HW_CLOCK_IS_VALID                            \
    ((WS2812_SPI_DMA_HW_CLOCK_DIVIDER >= 2U) &&                    \
     (WS2812_SPI_DMA_HW_CLOCK_DIVIDER <= 255U) &&                  \
     (WS2812_SPI_DMA_HW_BIT_RATE_HZ >= 2350000UL) &&               \
     (WS2812_SPI_DMA_HW_BIT_RATE_HZ <= 2450000UL))

#define WS2812_SPI_DMA_HW_INIT()                                    \
    (GPIOPinRemap(DISABLE, RB_PIN_SPI0),                            \
     GPIOA_ResetBits(GPIO_Pin_13 | GPIO_Pin_14),                    \
     GPIOA_ModeCfg(GPIO_Pin_13, GPIO_ModeOut_PP_5mA),               \
     GPIOA_ModeCfg(GPIO_Pin_14, GPIO_ModeOut_PP_20mA),              \
     SPI0_MasterDefInit(),                                          \
     SPI0_CLKCfg((uint8_t)WS2812_SPI_DMA_HW_CLOCK_DIVIDER),         \
     SPI0_DataMode(Mode0_HighBitINFront),                           \
     R8_SPI0_INTER_EN = 0U,                                         \
     true)

#define WS2812_SPI_DMA_HW_START(DATA, SIZE)                         \
    do                                                              \
    {                                                               \
        R8_SPI0_CTRL_CFG &= (uint8_t)~(RB_SPI_DMA_ENABLE |          \
                                        RB_SPI_DMA_LOOP);            \
        R8_SPI0_CTRL_MOD &= (uint8_t)~RB_SPI_FIFO_DIR;               \
        R16_SPI0_DMA_BEG = (uint16_t)(uint32_t)(DATA);               \
        R16_SPI0_DMA_NOW = (uint16_t)(uint32_t)(DATA);               \
        R16_SPI0_DMA_END = (uint16_t)(uint32_t)((DATA) + (SIZE));    \
        R16_SPI0_TOTAL_CNT = (uint16_t)(SIZE);                       \
        R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END | RB_SPI_IF_DMA_END;   \
        R8_SPI0_CTRL_CFG |= RB_SPI_DMA_ENABLE;                      \
    } while(0)

#define WS2812_SPI_DMA_HW_REMAINING_COUNT()     R16_SPI0_TOTAL_CNT
#define WS2812_SPI_DMA_HW_COUNT_END_PENDING()                       \
    ((R8_SPI0_INT_FLAG & RB_SPI_IF_CNT_END) != 0U)
#define WS2812_SPI_DMA_HW_STOP()                                    \
    (R8_SPI0_CTRL_CFG &= (uint8_t)~(RB_SPI_DMA_ENABLE |             \
                                     RB_SPI_DMA_LOOP))
#define WS2812_SPI_DMA_HW_WAIT_IDLE()                               \
    do                                                              \
    {                                                               \
        while(R8_SPI0_FIFO_COUNT != 0U)                             \
        {                                                           \
        }                                                           \
        while((R8_SPI0_INT_FLAG & RB_SPI_FREE) == 0U)               \
        {                                                           \
        }                                                           \
    } while(0)
#define WS2812_SPI_DMA_HW_LATCH()                                   \
    do                                                              \
    {                                                               \
        WS2812_SPI_DMA_HW_WAIT_IDLE();                              \
        DelayUs(300U);                                              \
    } while(0)
#define WS2812_SPI_DMA_HW_ABORT()                                   \
    do                                                              \
    {                                                               \
        WS2812_SPI_DMA_HW_STOP();                                   \
        R8_SPI0_CTRL_MOD |= RB_SPI_ALL_CLEAR;                       \
        R8_SPI0_CTRL_MOD &= (uint8_t)~RB_SPI_ALL_CLEAR;             \
        R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END | RB_SPI_IF_DMA_END;   \
        DelayUs(300U);                                              \
    } while(0)

#define WS2812_SPI_DMA_HW_ENTER_CRITICAL(STATE) SYS_DisableAllIrq(&(STATE))
#define WS2812_SPI_DMA_HW_EXIT_CRITICAL(STATE)  SYS_RecoverIrq(STATE)

bool drv_ws2812_spi_dma_init(dev_rgb_matrix_t *dev);
bool drv_ws2812_spi_dma_write(
    dev_rgb_matrix_t *dev,
    const uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                        [DEV_RGB_MATRIX_WIDTH]
                        [DEV_RGB_MATRIX_RGB888_CHANNELS]);

#endif /* DRV_WS2812_SPI_DMA_H */
