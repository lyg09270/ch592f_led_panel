#ifndef DRV_WS2812_GPIO_H
#define DRV_WS2812_GPIO_H

#include "CH59x_common.h"
#include "dev_rgb_matrix.h"

/*
 * GPIO hardware adaptation. Porting this implementation only requires
 * replacing the WS2812_GPIO_HW_* definitions below.
 *
 *   WS2812 DIN -> CH592 PA14
 *   SysTick must count at FREQ_SYS.
 */
#define WS2812_GPIO_HW_PIN                 GPIO_Pin_14
#define WS2812_GPIO_HW_RESET_TIME_US       300U
#define WS2812_GPIO_HW_T0H_CYCLES          24U
#define WS2812_GPIO_HW_T1H_CYCLES          48U
#define WS2812_GPIO_HW_BIT_CYCLES          75U

#ifndef WS2812_GPIO_SERPENTINE_LAYOUT
#define WS2812_GPIO_SERPENTINE_LAYOUT      1U
#endif

#if FREQ_SYS != 60000000UL
#error "Update WS2812 GPIO timing cycles for the selected FREQ_SYS"
#endif

#define WS2812_GPIO_HW_INIT()                                      \
    (GPIOPinRemap(DISABLE, RB_PIN_SPI0),                           \
     GPIOA_ResetBits(WS2812_GPIO_HW_PIN),                          \
     GPIOA_ModeCfg(WS2812_GPIO_HW_PIN, GPIO_ModeOut_PP_20mA),      \
     true)

#define WS2812_GPIO_HW_HIGH()                                      \
    (R32_PA_OUT |= WS2812_GPIO_HW_PIN)
#define WS2812_GPIO_HW_LOW()                                       \
    (R32_PA_CLR = WS2812_GPIO_HW_PIN)
#define WS2812_GPIO_HW_TIME_NOW()                                  \
    (*((volatile uint32_t *)&SysTick->CNT))
#define WS2812_GPIO_HW_WAIT_UNTIL(START, CYCLES)                   \
    do                                                             \
    {                                                              \
        while((uint32_t)(WS2812_GPIO_HW_TIME_NOW() - (START)) <    \
              (uint32_t)(CYCLES))                                  \
        {                                                          \
        }                                                          \
    } while(0)
#define WS2812_GPIO_HW_ENTER_CRITICAL(STATE)                       \
    SYS_DisableAllIrq(&(STATE))
#define WS2812_GPIO_HW_EXIT_CRITICAL(STATE)                        \
    SYS_RecoverIrq(STATE)
#define WS2812_GPIO_HW_RESET()                                     \
    do                                                             \
    {                                                              \
        WS2812_GPIO_HW_LOW();                                      \
        DelayUs(WS2812_GPIO_HW_RESET_TIME_US);                     \
    } while(0)

bool drv_ws2812_gpio_init(dev_rgb_matrix_t *dev);
bool drv_ws2812_gpio_write(
    dev_rgb_matrix_t *dev,
    const uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                        [DEV_RGB_MATRIX_WIDTH]
                        [DEV_RGB_MATRIX_RGB888_CHANNELS]);

#endif /* DRV_WS2812_GPIO_H */
