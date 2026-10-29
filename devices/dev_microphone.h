#ifndef DEV_MICROPHONE_H
#define DEV_MICROPHONE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct dev_microphone
{
    uint8_t volume;

    bool (*init)(struct dev_microphone *dev);
    bool (*read)(struct dev_microphone *dev);
} dev_microphone_t;

extern dev_microphone_t dev_microphone;

#endif /* DEV_MICROPHONE_H */
