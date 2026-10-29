#ifndef DRV_MICROPHONE_ADC_H
#define DRV_MICROPHONE_ADC_H

#include "CH59x_common.h"
#include "dev_microphone.h"

/*
 * CH592 hardware adaptation.
 *
 *   Microphone analog output -> PA8 / ADC12
 *   Microphone GND           -> GND
 *   Microphone VCC           -> module-rated supply
 */
#define MICROPHONE_ADC_HW_PIN                  GPIO_Pin_8
#define MICROPHONE_ADC_HW_CHANNEL              CH_EXTIN_12
#define MICROPHONE_ADC_HW_SAMPLE_CLOCK         SampleFreq_3_2
#define MICROPHONE_ADC_HW_GAIN                 ADC_PGA_2

/* 32 samples at 8 kHz: one measurement takes about 4 ms. */
#define MICROPHONE_ADC_SAMPLE_COUNT            32U
#define MICROPHONE_ADC_SAMPLE_INTERVAL_US      125U
#define MICROPHONE_ADC_SQUELCH                  5U
#define MICROPHONE_ADC_VOLUME_FULL_SCALE       48U

#define MICROPHONE_ADC_HW_PREPARE()                                \
    (GPIOA_ModeCfg(MICROPHONE_ADC_HW_PIN, GPIO_ModeIN_Floating),   \
     ADC_ExtSingleChSampInit(MICROPHONE_ADC_HW_SAMPLE_CLOCK,       \
                             MICROPHONE_ADC_HW_GAIN),               \
     ADC_ChannelCfg(MICROPHONE_ADC_HW_CHANNEL))

#define MICROPHONE_ADC_HW_INIT()                                   \
    (MICROPHONE_ADC_HW_PREPARE(),                                  \
     (void)ADC_ExcutSingleConver(),                                \
     true)

#define MICROPHONE_ADC_HW_SAMPLE()          ADC_ExcutSingleConver()
#define MICROPHONE_ADC_HW_WAIT_US(TIME_US)  DelayUs((TIME_US))

bool drv_microphone_adc_init(dev_microphone_t *dev);
bool drv_microphone_adc_read(dev_microphone_t *dev);

#endif /* DRV_MICROPHONE_ADC_H */
