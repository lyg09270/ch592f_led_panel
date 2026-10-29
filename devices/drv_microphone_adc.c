#include "dev_config.h"

#if DEV_MICROPHONE_USE_ADC == 1

#include "drv_microphone_adc.h"

#include <stddef.h>

#if MICROPHONE_ADC_SAMPLE_COUNT < 2U
#error "Microphone ADC sample count must be at least two"
#endif

#if MICROPHONE_ADC_VOLUME_FULL_SCALE == 0U
#error "Microphone ADC volume full scale must be nonzero"
#endif

static bool microphone_adc_initialized;
static bool microphone_adc_hardware_initialized;
static uint16_t microphone_adc_samples[MICROPHONE_ADC_SAMPLE_COUNT];
static uint16_t microphone_adc_envelope_q8;

bool drv_microphone_adc_init(dev_microphone_t *dev)
{
    if(dev == NULL)
    {
        return false;
    }

    dev->volume = 0U;
    microphone_adc_envelope_q8 = 0U;
    microphone_adc_hardware_initialized = false;
    microphone_adc_initialized = true;
    return true;
}

bool drv_microphone_adc_read(dev_microphone_t *dev)
{
    uint16_t sample_index;
    uint16_t mean;
    uint16_t amplitude;
    uint16_t target_volume;
    uint16_t target_q8;
    uint32_t sum = 0UL;
    uint32_t deviation_sum = 0UL;
    uint32_t adjusted;

    if((dev == NULL) || !microphone_adc_initialized)
    {
        return false;
    }

    if(!microphone_adc_hardware_initialized)
    {
        if(!MICROPHONE_ADC_HW_INIT())
        {
            return false;
        }
        microphone_adc_hardware_initialized = true;
    }

    MICROPHONE_ADC_HW_PREPARE();
    for(sample_index = 0U;
        sample_index < MICROPHONE_ADC_SAMPLE_COUNT;
        sample_index++)
    {
        uint16_t sample = MICROPHONE_ADC_HW_SAMPLE();

        microphone_adc_samples[sample_index] = sample;
        sum += sample;

        if((sample_index + 1U) < MICROPHONE_ADC_SAMPLE_COUNT)
        {
            MICROPHONE_ADC_HW_WAIT_US(
                MICROPHONE_ADC_SAMPLE_INTERVAL_US);
        }
    }

    mean = (uint16_t)(sum / MICROPHONE_ADC_SAMPLE_COUNT);
    for(sample_index = 0U;
        sample_index < MICROPHONE_ADC_SAMPLE_COUNT;
        sample_index++)
    {
        uint16_t sample = microphone_adc_samples[sample_index];

        deviation_sum += sample > mean
                             ? (uint32_t)(sample - mean)
                             : (uint32_t)(mean - sample);
    }
    amplitude = (uint16_t)(deviation_sum / MICROPHONE_ADC_SAMPLE_COUNT);

    if(amplitude <= MICROPHONE_ADC_SQUELCH)
    {
        target_volume = 0U;
    }
    else
    {
        adjusted = (uint32_t)(amplitude - MICROPHONE_ADC_SQUELCH);
        if(adjusted >= MICROPHONE_ADC_VOLUME_FULL_SCALE)
        {
            target_volume = 255U;
        }
        else
        {
            target_volume = (uint16_t)(
                (adjusted * 255UL +
                 (MICROPHONE_ADC_VOLUME_FULL_SCALE / 2U)) /
                MICROPHONE_ADC_VOLUME_FULL_SCALE);
        }
    }

    target_q8 = (uint16_t)(target_volume << 8U);
    if(target_q8 > microphone_adc_envelope_q8)
    {
        /* Fast attack: close half the distance on every 15 Hz reading. */
        microphone_adc_envelope_q8 +=
            (uint16_t)((target_q8 - microphone_adc_envelope_q8 + 1U) >> 1U);
    }
    else if(target_volume == 0U)
    {
        /* WLED-style fast path to silence once the squelch closes. */
        microphone_adc_envelope_q8 >>= 1U;
    }
    else
    {
        /* Slower release keeps real sounds natural without holding noise. */
        microphone_adc_envelope_q8 -=
            (uint16_t)((microphone_adc_envelope_q8 - target_q8 + 3U) >> 2U);
    }

    dev->volume = (uint8_t)((microphone_adc_envelope_q8 + 128U) >> 8U);

    return true;
}

#endif /* DEV_MICROPHONE_USE_ADC */
