#include "dev_microphone.h"
#include "dev_config.h"

#if DEV_MICROPHONE_USE_ADC == 1
#include "drv_microphone_adc.h"

dev_microphone_t dev_microphone = {
    .volume = 0U,
    .init = drv_microphone_adc_init,
    .read = drv_microphone_adc_read
};

#endif /* DEV_MICROPHONE_USE_ADC */
