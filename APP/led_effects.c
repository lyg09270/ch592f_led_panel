/*
 * Minimal MCU LED effect set.
 *
 * Effects are renumbered in the product order:
 *   0 Random Colors, 1 Twinkle, 2 Dissolve, 3 Chase Random,
 *   4 Fireworks, 5 microphone volume meter, 6 blue rolling ocean,
 *   7 white-crested rolling ocean, 8 golden sunset sun,
 *   9 deep orange-red sunset sun, 10 gradient sunrise,
 *   11 fireflies, 12 centered pulse, 13 prism, 14 liquid colors.
 * Effect 5 uses the PA8 ADC microphone as a simple volume meter.
 */

#include "led_effects.h"
#include "dev_microphone.h"

#include <string.h>

#define LED_EFFECT_FRAME_MS       66UL
#define LED_EFFECT_AUDIO_SAMPLE_FRAME_DIVISOR  2UL
#define LED_EFFECT_PIXEL_COUNT    \
    (DEV_RGB_MATRIX_WIDTH * DEV_RGB_MATRIX_HEIGHT)
#define LED_EFFECT_DESIGN_WIDTH                 16U
#define LED_EFFECT_DESIGN_HEIGHT                16U
#define LED_EFFECT_DESIGN_WIDTH_Q8              \
    (LED_EFFECT_DESIGN_WIDTH * 256UL)
#define LED_EFFECT_DESIGN_HEIGHT_Q8             \
    (LED_EFFECT_DESIGN_HEIGHT * 256UL)

typedef struct
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} led_rgb_t;

typedef struct
{
    bool running;
    bool initialized;
    bool dissolve_on;
    bool frame_clear_pending;
    uint8_t effect;
    uint8_t speed;
    uint8_t intensity;
    uint8_t hue_a;
    uint8_t hue_b;
    uint16_t aux;
    uint32_t now_ms;
    uint32_t step;
    uint32_t random_state;
    uint8_t music_level[DEV_RGB_MATRIX_WIDTH];
    uint8_t music_target[DEV_RGB_MATRIX_WIDTH];
    uint8_t music_peak[DEV_RGB_MATRIX_WIDTH];
} led_effect_state_t;

static const led_rgb_t led_color_primary = {255U, 128U, 0U};
static const led_rgb_t led_color_black = {0U, 0U, 0U};
static const led_rgb_t led_color_white = {255U, 255U, 255U};
static const led_rgb_t led_color_blue = {0U, 0U, 255U};
static led_effect_state_t led_effect_state;

static uint32_t led_random(void)
{
    uint32_t value = led_effect_state.random_state;

    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    led_effect_state.random_state = value;
    return value;
}

static uint8_t led_random8(void)
{
    return (uint8_t)(led_random() >> 24U);
}

static uint16_t led_random_pixel(void)
{
    return (uint16_t)(led_random() % LED_EFFECT_PIXEL_COUNT);
}

static led_rgb_t led_color_scale(led_rgb_t color, uint8_t scale)
{
    color.red = (uint8_t)(((uint16_t)color.red * scale) / 255U);
    color.green = (uint8_t)(((uint16_t)color.green * scale) / 255U);
    color.blue = (uint8_t)(((uint16_t)color.blue * scale) / 255U);
    return color;
}

static led_rgb_t led_color_saturation(led_rgb_t color, uint8_t saturation)
{
    uint8_t luminance = (uint8_t)(((uint16_t)color.red * 54U +
                                   (uint16_t)color.green * 183U +
                                   (uint16_t)color.blue * 19U) >> 8U);
    int16_t red = (int16_t)luminance +
                  (((int16_t)color.red - luminance) * saturation) / 255;
    int16_t green = (int16_t)luminance +
                    (((int16_t)color.green - luminance) * saturation) / 255;
    int16_t blue = (int16_t)luminance +
                   (((int16_t)color.blue - luminance) * saturation) / 255;
    led_rgb_t result = {
        (uint8_t)(red < 0 ? 0 : (red > 255 ? 255 : red)),
        (uint8_t)(green < 0 ? 0 : (green > 255 ? 255 : green)),
        (uint8_t)(blue < 0 ? 0 : (blue > 255 ? 255 : blue))
    };

    return result;
}

static led_rgb_t led_color_boost(led_rgb_t color, uint16_t scale)
{
    uint16_t value;

    value = ((uint16_t)color.red * scale) / 255U;
    color.red = (uint8_t)(value > 255U ? 255U : value);
    value = ((uint16_t)color.green * scale) / 255U;
    color.green = (uint8_t)(value > 255U ? 255U : value);
    value = ((uint16_t)color.blue * scale) / 255U;
    color.blue = (uint8_t)(value > 255U ? 255U : value);
    return color;
}

/* Convert webpage sRGB values to approximately linear WS2812 PWM values. */
static uint8_t led_web_channel_to_pwm(uint8_t value)
{
    static const uint8_t srgb_to_pwm[17] = {
        0U, 1U, 4U, 8U, 13U, 20U, 30U, 41U, 55U,
        71U, 90U, 111U, 134U, 161U, 190U, 222U, 255U
    };
    uint8_t index;
    uint8_t fraction;

    if(value == 255U)
    {
        return 255U;
    }

    index = value >> 4U;
    fraction = value & 0x0FU;
    return (uint8_t)(((uint16_t)srgb_to_pwm[index] *
                      (uint16_t)(16U - fraction) +
                      (uint16_t)srgb_to_pwm[index + 1U] * fraction +
                      8U) >> 4U);
}

static led_rgb_t led_color_web_to_pwm(led_rgb_t color)
{
    color.red = led_web_channel_to_pwm(color.red);
    color.green = led_web_channel_to_pwm(color.green);
    color.blue = led_web_channel_to_pwm(color.blue);
    return color;
}

static led_rgb_t led_color_apply_html_defaults(led_rgb_t color,
                                                uint8_t effect)
{
    if(effect < 10U)
    {
        return color;
    }

    if(effect == 10U)
    {
        return led_color_web_to_pwm(color);
    }

    if(effect == 11U)
    {
        return color;
    }

    if(effect == 13U)
    {
        return color;
    }

    color = led_color_saturation(color, 229U);
    return led_color_boost(color, 286U);
}

static led_rgb_t led_color_blend(led_rgb_t from, led_rgb_t to,
                                 uint8_t amount)
{
    uint16_t inverse = (uint16_t)(255U - amount);
    led_rgb_t result;

    result.red = (uint8_t)(((uint16_t)from.red * inverse +
                            (uint16_t)to.red * amount) / 255U);
    result.green = (uint8_t)(((uint16_t)from.green * inverse +
                              (uint16_t)to.green * amount) / 255U);
    result.blue = (uint8_t)(((uint16_t)from.blue * inverse +
                             (uint16_t)to.blue * amount) / 255U);
    return result;
}

static led_rgb_t led_color_wheel(uint8_t position)
{
    led_rgb_t color;

    position = (uint8_t)(255U - position);
    if(position < 85U)
    {
        color.red = (uint8_t)(255U - position * 3U);
        color.green = 0U;
        color.blue = (uint8_t)(position * 3U);
    }
    else if(position < 170U)
    {
        position = (uint8_t)(position - 85U);
        color.red = 0U;
        color.green = (uint8_t)(position * 3U);
        color.blue = (uint8_t)(255U - position * 3U);
    }
    else
    {
        position = (uint8_t)(position - 170U);
        color.red = (uint8_t)(position * 3U);
        color.green = (uint8_t)(255U - position * 3U);
        color.blue = 0U;
    }
    return color;
}

static uint8_t led_triwave8(uint8_t value)
{
    return (value & 0x80U) != 0U
               ? (uint8_t)((255U - value) << 1U)
               : (uint8_t)(value << 1U);
}

static uint8_t led_smoothstep8(uint8_t value)
{
    uint32_t square = (uint32_t)value * value;

    return (uint8_t)((square * (765UL - (uint32_t)value * 2UL) +
                      32512UL) / 65025UL);
}

/* 8.8 phase input and 8.8 smoothly interpolated wave output. */
static uint16_t led_smooth_wave_q8(uint16_t phase)
{
    uint8_t integer = (uint8_t)(phase >> 8U);
    uint8_t fraction = (uint8_t)phase;
    uint8_t first = led_smoothstep8(led_triwave8(integer));
    uint8_t second = led_smoothstep8(
        led_triwave8((uint8_t)(integer + 1U)));
    int16_t delta = (int16_t)second - first;

    return (uint16_t)((int32_t)first * 256L +
                      ((int32_t)delta * fraction));
}

static int16_t led_wave_offset_q8(uint16_t phase, uint16_t amplitude_q8)
{
    return (int16_t)((((int32_t)led_smooth_wave_q8(phase) - 32768L) *
                      amplitude_q8) >> 15U);
}

static uint8_t led_band_falloff_q8(int32_t distance_q8,
                                   uint16_t width_q8)
{
    uint32_t distance = distance_q8 < 0L
                            ? (uint32_t)(-distance_q8)
                            : (uint32_t)distance_q8;

    if(distance >= width_q8)
    {
        return 0U;
    }
    return led_smoothstep8((uint8_t)(255U -
        (distance * 255UL) / width_q8));
}

static uint8_t led_blob_falloff_q8(int32_t dx_q8, int32_t dy_q8,
                                   uint16_t radius_q8)
{
    uint32_t distance_squared = (uint32_t)(dx_q8 * dx_q8 +
                                           dy_q8 * dy_q8);
    uint32_t radius_squared = (uint32_t)radius_q8 * radius_q8;

    if(distance_squared >= radius_squared)
    {
        return 0U;
    }
    return led_smoothstep8((uint8_t)(255U -
        (distance_squared * 255UL) / radius_squared));
}

static uint8_t led_ellipse_falloff_q8(int32_t dx_q8, int32_t dy_q8,
                                      uint16_t radius_x_q8,
                                      uint16_t radius_y_q8)
{
    int32_t nx = (dx_q8 * 256L) / radius_x_q8;
    int32_t ny = (dy_q8 * 256L) / radius_y_q8;
    uint32_t distance_squared = (uint32_t)(nx * nx + ny * ny);

    if(distance_squared >= 65536UL)
    {
        return 0U;
    }
    return led_smoothstep8((uint8_t)(255U -
        (distance_squared * 255UL) / 65536UL));
}

static uint8_t led_soft_box_q8(int32_t dx_q8, int32_t dy_q8,
                               uint16_t half_width_q8,
                               uint16_t half_height_q8)
{
    uint32_t x = dx_q8 < 0L ? (uint32_t)(-dx_q8) : (uint32_t)dx_q8;
    uint32_t y = dy_q8 < 0L ? (uint32_t)(-dy_q8) : (uint32_t)dy_q8;
    uint8_t wx;
    uint8_t wy;

    x = x > half_width_q8 ? x - half_width_q8 : 0UL;
    y = y > half_height_q8 ? y - half_height_q8 : 0UL;
    wx = x >= 512UL ? 0U : led_smoothstep8((uint8_t)(255U - x / 2U));
    wy = y >= 512UL ? 0U : led_smoothstep8((uint8_t)(255U - y / 2U));
    return (uint8_t)(((uint16_t)wx * wy) / 255U);
}

static uint16_t led_approx_distance_q8(int32_t dx_q8, int32_t dy_q8)
{
    uint32_t x = dx_q8 < 0L ? (uint32_t)(-dx_q8) : (uint32_t)dx_q8;
    uint32_t y = dy_q8 < 0L ? (uint32_t)(-dy_q8) : (uint32_t)dy_q8;
    uint32_t maximum = x > y ? x : y;
    uint32_t minimum = x > y ? y : x;

    return (uint16_t)(maximum + (minimum >> 1U));
}

/* Map any logical matrix size onto the original 16x16 design space. */
static int32_t led_design_coordinate_q8(uint16_t coordinate,
                                        uint16_t logical_size,
                                        uint16_t design_size)
{
    return (int32_t)(((uint32_t)(coordinate * 2U + 1U) *
                      design_size * 128UL) / logical_size);
}

static uint16_t led_design_phase_offset(uint16_t coordinate,
                                         uint16_t logical_size,
                                         uint16_t design_size,
                                         uint16_t phase_per_pixel)
{
    return (uint16_t)(((uint32_t)coordinate * design_size *
                       phase_per_pixel) / logical_size);
}

static uint8_t led_scene_strength(uint8_t weight)
{
    uint16_t strength = (uint16_t)(128U +
                        (led_effect_state.intensity >> 1U));
    return (uint8_t)(((uint16_t)weight * strength) / 255U);
}

static void led_set_pixel(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS],
    uint16_t index,
    led_rgb_t color)
{
    uint16_t row = index / DEV_RGB_MATRIX_WIDTH;
    uint16_t column = index % DEV_RGB_MATRIX_WIDTH;

    pixels[row][column][0] = color.red;
    pixels[row][column][1] = color.green;
    pixels[row][column][2] = color.blue;
}

static led_rgb_t led_get_pixel(
    const uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                        [DEV_RGB_MATRIX_WIDTH]
                        [DEV_RGB_MATRIX_RGB888_CHANNELS],
    uint16_t index)
{
    uint16_t row = index / DEV_RGB_MATRIX_WIDTH;
    uint16_t column = index % DEV_RGB_MATRIX_WIDTH;
    led_rgb_t color = {
        pixels[row][column][0],
        pixels[row][column][1],
        pixels[row][column][2]
    };

    return color;
}

static void led_fill(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS],
    led_rgb_t color)
{
    uint16_t index;

    for(index = 0U; index < LED_EFFECT_PIXEL_COUNT; index++)
    {
        led_set_pixel(pixels, index, color);
    }
}

static void led_fade_frame(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS],
    uint8_t scale)
{
    uint16_t index;

    for(index = 0U; index < LED_EFFECT_PIXEL_COUNT; index++)
    {
        led_set_pixel(pixels, index,
                      led_color_scale(led_get_pixel(pixels, index), scale));
    }
}

/* ID 0: Random Colors (formerly 005). */
static void led_effect_random_color(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    uint32_t cycle = 200UL +
                     (uint32_t)(255U - led_effect_state.speed) * 50UL;
    uint32_t iteration = led_effect_state.now_ms / cycle;
    uint32_t elapsed = led_effect_state.now_ms % cycle;
    uint32_t fade_time = (cycle * led_effect_state.intensity) >> 8U;
    uint8_t blend = 255U;

    if(iteration != led_effect_state.step)
    {
        led_effect_state.hue_b = led_effect_state.hue_a;
        led_effect_state.hue_a = led_random8();
        led_effect_state.step = iteration;
    }
    if(fade_time != 0UL && elapsed < fade_time)
    {
        blend = (uint8_t)((elapsed * 255UL) / fade_time);
    }

    led_fill(pixels,
             led_color_blend(led_color_wheel(led_effect_state.hue_b),
                             led_color_wheel(led_effect_state.hue_a),
                             blend));
}

/* ID 1: Twinkle (formerly 017). */
static void led_effect_twinkle(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    uint32_t cycle = 20UL +
                     (uint32_t)(255U - led_effect_state.speed) * 5UL;
    uint32_t iteration = led_effect_state.now_ms / cycle;
    uint16_t count = (uint16_t)(1U +
                     ((uint32_t)led_effect_state.intensity *
                      LED_EFFECT_PIXEL_COUNT >> 11U));

    led_fade_frame(pixels, 224U);
    if(iteration == led_effect_state.step && led_effect_state.now_ms != 0UL)
    {
        return;
    }
    led_effect_state.step = iteration;

    while(count-- != 0U)
    {
        uint16_t pixel = led_random_pixel();
        led_set_pixel(pixels, pixel,
                      led_color_wheel((uint8_t)(pixel + led_random8())));
    }
}

/* ID 2: Dissolve (formerly 018). */
static void led_effect_dissolve(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    uint16_t attempts = (uint16_t)(1U + LED_EFFECT_PIXEL_COUNT / 15U);
    uint16_t changed = 0U;
    uint16_t index;

    if(!led_effect_state.initialized)
    {
        led_fill(pixels, led_color_black);
        led_effect_state.dissolve_on = true;
        led_effect_state.initialized = true;
    }

    while(attempts-- != 0U)
    {
        if(led_random8() <= led_effect_state.intensity)
        {
            uint8_t retry;
            for(retry = 0U; retry < 10U; retry++)
            {
                uint16_t pixel = led_random_pixel();
                led_rgb_t current = led_get_pixel(pixels, pixel);
                bool is_off = (current.red | current.green | current.blue) == 0U;

                if(is_off == led_effect_state.dissolve_on)
                {
                    led_set_pixel(pixels, pixel,
                                  led_effect_state.dissolve_on
                                      ? led_color_primary
                                      : led_color_black);
                    break;
                }
            }
        }
    }

    for(index = 0U; index < LED_EFFECT_PIXEL_COUNT; index++)
    {
        led_rgb_t current = led_get_pixel(pixels, index);
        bool is_off = (current.red | current.green | current.blue) == 0U;
        if(is_off != led_effect_state.dissolve_on)
        {
            changed++;
        }
    }

    if(changed == LED_EFFECT_PIXEL_COUNT)
    {
        uint16_t hold_frames = (uint16_t)(1U +
                               (255U - led_effect_state.speed) / 8U);
        if(++led_effect_state.aux >= hold_frames)
        {
            led_effect_state.dissolve_on = !led_effect_state.dissolve_on;
            led_effect_state.aux = 0U;
        }
    }
}

/* ID 3: Chase Random (formerly 029). */
static void led_effect_chase_random(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    uint32_t cycle = 15UL +
                     (uint32_t)(255U - led_effect_state.speed) * 3UL;
    uint32_t iteration = led_effect_state.now_ms / cycle;
    uint16_t head = (uint16_t)(iteration % LED_EFFECT_PIXEL_COUNT);
    uint16_t previous_head = led_effect_state.aux;
    uint16_t size = (uint16_t)(1U +
                    ((uint32_t)led_effect_state.intensity *
                     LED_EFFECT_PIXEL_COUNT >> 10U));
    uint16_t index;

    if(head < previous_head)
    {
        led_effect_state.hue_b = led_effect_state.hue_a;
        led_effect_state.hue_a = led_random8();
    }

    led_fill(pixels, led_color_wheel(led_effect_state.hue_a));
    led_effect_state.aux = head;
    for(index = 0U; index < size; index++)
    {
        led_set_pixel(pixels,
                      (uint16_t)((head + index) % LED_EFFECT_PIXEL_COUNT),
                      led_color_primary);
        led_set_pixel(pixels,
                      (uint16_t)((head + size + index) %
                                 LED_EFFECT_PIXEL_COUNT),
                      led_color_blue);
    }
}

/* ID 4: Fireworks (formerly 042). */
static void led_effect_fireworks(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    uint16_t count;

    led_fade_frame(pixels, 210U);
    count = (uint16_t)(((uint32_t)(1U +
                       (led_effect_state.intensity >> 6U)) *
                       LED_EFFECT_PIXEL_COUNT) /
                       (LED_EFFECT_DESIGN_WIDTH *
                        LED_EFFECT_DESIGN_HEIGHT));
    if(count == 0U)
    {
        count = 1U;
    }
    while(count-- != 0U)
    {
        if(led_random8() < (uint8_t)(16U +
                                     (led_effect_state.speed >> 1U)))
        {
            led_set_pixel(pixels, led_random_pixel(),
                          led_color_wheel(led_random8()));
        }
    }
}

/*
 * ID 5: microphone volume meter (formerly 123).
 * speed controls launch/fall response, intensity controls height, and
 * brightness remains the common final output scale.
 */
static void led_effect_music_volume(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    uint16_t column;
    const uint8_t idle_height = (uint8_t)(
        DEV_RGB_MATRIX_HEIGHT / LED_EFFECT_DESIGN_HEIGHT);

    /* Measure at about 7.5 Hz while animation output runs near 15 fps. */
    if((((led_effect_state.now_ms / LED_EFFECT_FRAME_MS) %
         LED_EFFECT_AUDIO_SAMPLE_FRAME_DIVISOR) == 0UL) &&
       dev_microphone.read(&dev_microphone))
    {
        uint8_t volume = dev_microphone.volume;
        uint8_t amplitude = (uint8_t)(
            ((uint32_t)volume *
             (DEV_RGB_MATRIX_HEIGHT - idle_height) *
             led_effect_state.intensity + 32512UL) / 65025UL);
        uint8_t variation = (uint8_t)((amplitude + 3U) >> 2U);

        for(column = 0U; column < DEV_RGB_MATRIX_WIDTH; column++)
        {
            uint8_t target = (uint8_t)(idle_height + amplitude);

            if(variation != 0U)
            {
                target = (uint8_t)(target -
                         (led_random8() % (variation + 1U)));
            }
            led_effect_state.music_target[column] = target;
        }
        led_effect_state.step = volume;
        led_effect_state.initialized = true;
    }

    led_fill(pixels, led_color_black);
    for(column = 0U; column < DEV_RGB_MATRIX_WIDTH; column++)
    {
        uint8_t level = led_effect_state.music_level[column];
        uint8_t target = led_effect_state.music_target[column];
        uint8_t rise = (uint8_t)(
            ((uint16_t)(4U + (led_effect_state.speed >> 6U)) *
             DEV_RGB_MATRIX_HEIGHT) / LED_EFFECT_DESIGN_HEIGHT);
        uint8_t fall_period = (uint8_t)(1U +
                              ((255U - led_effect_state.speed) >> 7U));

        if(level < target)
        {
            uint16_t next = (uint16_t)level + rise;
            level = (uint8_t)(next > target ? target : next);
        }
        else if((level > target) &&
                (((led_effect_state.now_ms / LED_EFFECT_FRAME_MS) %
                  fall_period) == 0UL))
        {
            level--;
        }
        led_effect_state.music_level[column] = level;

        if(level != 0U)
        {
            uint16_t row = (uint16_t)(DEV_RGB_MATRIX_HEIGHT - level);
            uint8_t hue = (uint8_t)(column * 17U + level * 7U +
                                    (led_effect_state.now_ms >> 4U));
            led_rgb_t color = led_color_wheel(hue);
            uint16_t tail_row;

            /* Continuous fading tail from the flying point to the bottom. */
            for(tail_row = row;
                tail_row < DEV_RGB_MATRIX_HEIGHT;
                tail_row++)
            {
                uint16_t distance = (uint16_t)(tail_row - row);
                uint8_t scale = (uint8_t)(
                    ((uint32_t)(level - distance) * 255UL) / level);

                led_set_pixel(pixels,
                              (uint16_t)(tail_row * DEV_RGB_MATRIX_WIDTH +
                                         column),
                              led_color_scale(color, scale));
            }
        }
    }
}

/*
 * IDs 6 and 7: slow rolling ocean, with blue or white-blue crests.
 * The sea occupies roughly the lower two thirds of the matrix. Two low-speed
 * integer waves move in opposite directions; restrained blue-grey colors keep
 * saturation and the depth gradient deliberately subtle.
 */
static void led_effect_ocean_roll(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS],
    bool blue_crest)
{
    static const led_rgb_t sky = {1U, 2U, 4U};
    static const led_rgb_t blue_crest_color = {52U, 118U, 178U};
    static const led_rgb_t blue_shallow_color = {25U, 66U, 108U};
    static const led_rgb_t white_crest_color = {158U, 184U, 201U};
    static const led_rgb_t white_shallow_color = {48U, 78U, 102U};
    static const led_rgb_t deep_color = {1U, 3U, 7U};
    led_rgb_t crest_color = blue_crest
                                ? blue_crest_color
                                : white_crest_color;
    led_rgb_t shallow_color = blue_crest
                                  ? blue_shallow_color
                                  : white_shallow_color;
    uint32_t rate = 8UL + (uint32_t)(led_effect_state.speed >> 5U);
    uint16_t phase_q8 = (uint16_t)((led_effect_state.now_ms * rate) >> 1U);
    uint8_t amplitude = (uint8_t)(2U +
                        ((uint16_t)led_effect_state.intensity * 4U) / 255U);
    uint16_t crest_width_q8 = (uint16_t)(1024U +
                              (uint16_t)led_effect_state.intensity * 3U);
    uint8_t crest_max = (uint8_t)(88U +
                        (led_effect_state.intensity >> 1U));
    uint16_t column;

    led_fill(pixels, sky);
    for(column = 0U; column < DEV_RGB_MATRIX_WIDTH; column++)
    {
        uint16_t wave_a = led_smooth_wave_q8((uint16_t)(
            phase_q8 + led_design_phase_offset(
                column, DEV_RGB_MATRIX_WIDTH,
                LED_EFFECT_DESIGN_WIDTH, 17U * 256U)));
        uint16_t wave_b = led_smooth_wave_q8((uint16_t)(
            96U * 256U - (phase_q8 >> 1U) +
            led_design_phase_offset(column, DEV_RGB_MATRIX_WIDTH,
                                    LED_EFFECT_DESIGN_WIDTH,
                                    29U * 256U)));
        uint16_t combined = (uint16_t)(((uint32_t)wave_a * 3UL +
                                        wave_b) >> 2U);
        int32_t surface_q8 =
            (int32_t)(LED_EFFECT_DESIGN_HEIGHT / 3U) * 256L +
            (((int32_t)combined - 32768L) * amplitude) / 128L;
        uint16_t row;

        if(surface_q8 < 128L)
        {
            surface_q8 = 128L;
        }
        else if(surface_q8 >
                ((int32_t)LED_EFFECT_DESIGN_HEIGHT_Q8 - 384L))
        {
            surface_q8 = (int32_t)LED_EFFECT_DESIGN_HEIGHT_Q8 - 384L;
        }

        for(row = 0U; row < DEV_RGB_MATRIX_HEIGHT; row++)
        {
            int32_t center_q8 = led_design_coordinate_q8(
                row, DEV_RGB_MATRIX_HEIGHT, LED_EFFECT_DESIGN_HEIGHT);
            int32_t distance_q8 = center_q8 - surface_q8;
            int32_t coverage_value = distance_q8 + 128L;
            uint8_t coverage;
            uint32_t water_depth_q8;
            uint32_t available_q8;
            uint8_t depth;
            uint32_t crest_distance;
            uint8_t crest_amount = 0U;
            uint8_t shimmer;
            led_rgb_t color = led_color_blend(shallow_color, deep_color,
                                              0U);

            if(coverage_value <= 0L)
            {
                continue;
            }
            coverage = coverage_value >= 255L
                           ? 255U
                           : (uint8_t)coverage_value;

            water_depth_q8 = distance_q8 > 0L
                                 ? (uint32_t)distance_q8
                                 : 0UL;
            available_q8 = (uint32_t)(LED_EFFECT_DESIGN_HEIGHT_Q8 -
                                      surface_q8);
            depth = available_q8 == 0UL
                        ? 255U
                        : (uint8_t)((water_depth_q8 * 255UL) /
                                    available_q8);
            color = led_color_blend(shallow_color, deep_color, depth);

            crest_distance = distance_q8 < 0L
                                 ? (uint32_t)(-distance_q8)
                                 : (uint32_t)distance_q8;
            if(crest_distance < crest_width_q8)
            {
                crest_amount = (uint8_t)(
                    ((uint32_t)(crest_width_q8 - crest_distance) *
                     crest_max) / crest_width_q8);
            }
            shimmer = (uint8_t)(led_smooth_wave_q8((uint16_t)(
                phase_q8 * 2U + led_design_phase_offset(
                    column, DEV_RGB_MATRIX_WIDTH,
                    LED_EFFECT_DESIGN_WIDTH, 13U * 256U) +
                led_design_phase_offset(row, DEV_RGB_MATRIX_HEIGHT,
                    LED_EFFECT_DESIGN_HEIGHT, 19U * 256U))) >> 13U);

            color = led_color_blend(color, crest_color,
                                    (uint8_t)(crest_amount + shimmer));
            if(coverage != 255U)
            {
                color = led_color_blend(sky, color, coverage);
            }
            led_set_pixel(pixels,
                          (uint16_t)(row * DEV_RGB_MATRIX_WIDTH + column),
                          color);
        }
    }
}

/*
 * IDs 8 and 9: centered sunset sun in golden or deep orange-red palettes.
 * A symmetric radial gradient runs from a bright sunset yellow core through
 * orange and orange-red to an almost black rim. The whole disc breathes as a
 * single surface so the animation remains even and free of local flicker.
 */
static void led_effect_sunset_sun(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS],
    bool deep_sunset)
{
    static const led_rgb_t golden_center = {255U, 188U, 48U};
    static const led_rgb_t golden_orange = {242U, 91U, 14U};
    static const led_rgb_t golden_rim = {105U, 17U, 3U};
    static const led_rgb_t sunset_center = {255U, 126U, 12U};
    static const led_rgb_t sunset_orange = {232U, 55U, 5U};
    static const led_rgb_t sunset_rim = {82U, 7U, 1U};
    static const led_rgb_t outer_color = {2U, 0U, 0U};
    led_rgb_t center_color = deep_sunset ? sunset_center : golden_center;
    led_rgb_t orange_color = deep_sunset ? sunset_orange : golden_orange;
    led_rgb_t rim_color = deep_sunset ? sunset_rim : golden_rim;
    uint32_t rate = 4UL + (uint32_t)(led_effect_state.speed >> 6U);
    uint16_t phase_q8 = (uint16_t)((led_effect_state.now_ms * rate) >> 1U);
    uint8_t pulse = (uint8_t)(led_smooth_wave_q8(phase_q8) >> 8U);
    int32_t radius_q8 =
                        (int32_t)(LED_EFFECT_DESIGN_WIDTH - 4U) * 256L +
                        (int32_t)led_effect_state.intensity * 2L +
                        ((int32_t)pulse - 128L);
    uint32_t radius_squared;
    uint8_t pulse_level = (uint8_t)(220U +
                          ((uint16_t)pulse * 35U) / 255U);
    uint16_t row;
    uint16_t column;

    if(radius_q8 < 256L)
    {
        radius_q8 = 256L;
    }
    radius_squared = (uint32_t)((radius_q8 * radius_q8) >> 8U);
    led_fill(pixels, led_color_black);

    for(row = 0U; row < DEV_RGB_MATRIX_HEIGHT; row++)
    {
        int32_t y_q8 = (led_design_coordinate_q8(
            row, DEV_RGB_MATRIX_HEIGHT, LED_EFFECT_DESIGN_HEIGHT) -
            (int32_t)(LED_EFFECT_DESIGN_HEIGHT_Q8 / 2UL)) * 2L;

        for(column = 0U; column < DEV_RGB_MATRIX_WIDTH; column++)
        {
            int32_t x_q8 = (led_design_coordinate_q8(
                column, DEV_RGB_MATRIX_WIDTH, LED_EFFECT_DESIGN_WIDTH) -
                (int32_t)(LED_EFFECT_DESIGN_WIDTH_Q8 / 2UL)) * 2L;
            uint32_t distance_squared = (uint32_t)(
                (x_q8 * x_q8 + y_q8 * y_q8) >> 8U);
            uint16_t radial = (uint16_t)((distance_squared * 255UL) /
                                         radius_squared);
            led_rgb_t color;

            if(radial < 96U)
            {
                color = led_color_blend(center_color, orange_color,
                    (uint8_t)(((uint32_t)radial * 255UL) / 96UL));
            }
            else if(radial < 224U)
            {
                color = led_color_blend(orange_color, rim_color,
                    (uint8_t)(((uint32_t)(radial - 96U) * 255UL) /
                              128UL));
            }
            else if(radial < 320U)
            {
                color = led_color_blend(rim_color, outer_color,
                    (uint8_t)(((uint32_t)(radial - 224U) * 255UL) /
                              96UL));
            }
            else
            {
                color = led_color_black;
            }

            led_set_pixel(pixels,
                          (uint16_t)(row * DEV_RGB_MATRIX_WIDTH + column),
                          led_color_scale(color, pulse_level));
        }
    }
}

/* Integer ports of the scenes in 16x16-light-board.html. */
static void led_effect_board_sunrise(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    static const led_rgb_t sky = {46U, 71U, 82U};
    static const led_rgb_t horizon_color = {194U, 140U, 125U};
    static const led_rgb_t sea = {52U, 101U, 108U};
    static const led_rgb_t deep = {31U, 74U, 85U};
    static const led_rgb_t sun = {255U, 217U, 152U};
    static const led_rgb_t halo = {246U, 165U, 111U};
    static const led_rgb_t reflection = {232U, 156U, 112U};
    uint32_t rate = 4UL + (uint32_t)(led_effect_state.speed >> 5U);
    uint16_t phase = (uint16_t)((led_effect_state.now_ms * rate) >> 1U);
    int32_t horizon_q8 = 2342L + led_wave_offset_q8(phase, 44U);
    int32_t sun_x_q8 = (int32_t)DEV_RGB_MATRIX_WIDTH * 128L +
                       led_wave_offset_q8((uint16_t)(phase + 5000U), 602U);
    int32_t sun_y_q8 = 1971L -
                       led_wave_offset_q8((uint16_t)(phase + 17000U), 525U);
    uint16_t row;
    uint16_t column;

    for(row = 0U; row < DEV_RGB_MATRIX_HEIGHT; row++)
    {
        int32_t y_q8 = (int32_t)row * 256L + 128L;
        for(column = 0U; column < DEV_RGB_MATRIX_WIDTH; column++)
        {
            int32_t x_q8 = (int32_t)column * 256L + 128L;
            led_rgb_t color;

            if(y_q8 < horizon_q8)
            {
                uint8_t amount = (uint8_t)((y_q8 * 230L) / horizon_q8);
                color = led_color_blend(sky, horizon_color, amount);
            }
            else
            {
                uint32_t depth_q8 = (uint32_t)(y_q8 - horizon_q8);
                uint32_t sea_height_q8 = (uint32_t)DEV_RGB_MATRIX_HEIGHT *
                                         256UL - (uint32_t)horizon_q8;
                uint8_t amount = (uint8_t)((depth_q8 * 255UL) /
                                           sea_height_q8);
                color = led_color_blend(sea, deep, amount);
            }

            {
                int32_t dx = x_q8 - sun_x_q8;
                int32_t dy = y_q8 - sun_y_q8;
                uint8_t glow = led_blob_falloff_q8(dx, dy, 896U);
                uint8_t core = led_blob_falloff_q8(dx, dy, 430U);
                color = led_color_blend(color, halo,
                                        led_scene_strength(glow >> 1U));
                if(y_q8 < horizon_q8)
                {
                    color = led_color_blend(color, sun,
                                            led_scene_strength(core));
                }
            }

            if(y_q8 >= horizon_q8)
            {
                int32_t ripple = led_wave_offset_q8((uint16_t)(
                    phase * 3U + row * 4700U), 190U);
                uint16_t depth = (uint16_t)((y_q8 - horizon_q8) >> 8U);
                uint16_t width = (uint16_t)(180U + depth * 52U);
                uint8_t stripe = led_band_falloff_q8(
                    x_q8 - sun_x_q8 + ripple, width);
                uint8_t modulation = (uint8_t)(
                    led_smooth_wave_q8((uint16_t)(phase * 4U +
                    column * 6100U + row * 9800U)) >> 8U);
                stripe = (uint8_t)(((uint16_t)stripe * modulation) / 255U);
                color = led_color_blend(color, reflection,
                                        led_scene_strength(stripe));
            }
            led_set_pixel(pixels,
                          (uint16_t)(row * DEV_RGB_MATRIX_WIDTH + column),
                          color);
        }
    }
}

static void led_effect_board_waves(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    static const led_rgb_t base = {47U, 91U, 99U};
    static const led_rgb_t deep = {29U, 70U, 79U};
    static const led_rgb_t wave = {72U, 140U, 148U};
    static const led_rgb_t middle = {93U, 159U, 157U};
    static const led_rgb_t crest = {180U, 216U, 198U};
    static const led_rgb_t glint = {210U, 223U, 202U};
    static const uint16_t band_y_q8[3] = {870U, 2048U, 3200U};
    uint32_t rate = 5UL + (uint32_t)(led_effect_state.speed >> 5U);
    uint16_t phase = (uint16_t)((led_effect_state.now_ms * rate) >> 1U);
    uint16_t row;
    uint16_t column;

    for(row = 0U; row < DEV_RGB_MATRIX_HEIGHT; row++)
    {
        uint8_t depth = (uint8_t)(((uint32_t)row * 255UL) /
                                  (DEV_RGB_MATRIX_HEIGHT - 1U));
        for(column = 0U; column < DEV_RGB_MATRIX_WIDTH; column++)
        {
            int32_t y_q8 = (int32_t)row * 256L + 128L;
            int16_t sway = (int16_t)(
                led_wave_offset_q8((uint16_t)(column * 4700U - phase), 190U) +
                led_wave_offset_q8((uint16_t)(column * 2300U +
                                             (phase >> 1U)), 97U));
            led_rgb_t color = led_color_blend(base, deep, depth);
            uint8_t band;

            for(band = 0U; band < 3U; band++)
            {
                int32_t center = (int32_t)band_y_q8[band] + sway +
                    led_wave_offset_q8((uint16_t)(phase + band * 14000U), 72U);
                uint8_t body = led_band_falloff_q8(y_q8 - center - 235L,
                                                   430U);
                uint8_t tip = led_band_falloff_q8(y_q8 - center, 125U);
                color = led_color_blend(color,
                                        band == 1U ? middle : wave,
                                        led_scene_strength(body >> 1U));
                color = led_color_blend(color, crest,
                                        led_scene_strength(tip >> 1U));
            }
            {
                uint8_t sparkle = (uint8_t)(led_smooth_wave_q8((uint16_t)(
                    column * 11200U + row * 4600U + phase)) >> 8U);
                if(sparkle > 238U)
                {
                    color = led_color_blend(color, glint,
                                            (uint8_t)(sparkle - 238U));
                }
            }
            led_set_pixel(pixels,
                          (uint16_t)(row * DEV_RGB_MATRIX_WIDTH + column),
                          color);
        }
    }
}

static void led_effect_board_fireflies(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    static const led_rgb_t dusk = {42U, 63U, 58U};
    static const led_rgb_t deep = {27U, 49U, 49U};
    static const led_rgb_t foliage = {69U, 91U, 69U};
    static const led_rgb_t cool = {175U, 222U, 141U};
    static const led_rgb_t warm = {246U, 191U, 107U};
    static const led_rgb_t cool_core = {218U, 242U, 165U};
    static const led_rgb_t warm_core = {255U, 221U, 148U};
    uint32_t rate = 4UL + (uint32_t)(led_effect_state.speed >> 5U);
    uint16_t phase = (uint16_t)((led_effect_state.now_ms * rate) >> 1U);
    uint16_t row;
    uint16_t column;

    for(row = 0U; row < DEV_RGB_MATRIX_HEIGHT; row++)
    {
        uint8_t depth = (uint8_t)(((uint32_t)row * 255UL) /
                                  (DEV_RGB_MATRIX_HEIGHT - 1U));
        for(column = 0U; column < DEV_RGB_MATRIX_WIDTH; column++)
        {
            int32_t x_q8 = (int32_t)column * 256L + 128L;
            int32_t y_q8 = (int32_t)row * 256L + 128L;
            uint8_t ground = led_band_falloff_q8(y_q8 - 3400L, 900U);
            led_rgb_t color = led_color_blend(dusk, deep, depth);
            uint8_t light;

            color = led_color_blend(color, foliage, ground >> 2U);
            for(light = 0U; light < 11U; light++)
            {
                int32_t fx = (int32_t)(1U + (light * 5U) % 14U) * 256L +
                    led_wave_offset_q8((uint16_t)(phase + light * 5700U),
                                       270U);
                int32_t fy = (int32_t)(1U + (light * 7U) % 13U) * 256L +
                    led_wave_offset_q8((uint16_t)(phase + light * 9100U),
                                       220U);
                uint8_t pulse = (uint8_t)(led_smooth_wave_q8((uint16_t)(
                    phase * 3U + light * 6000U)) >> 8U);
                uint8_t glow = led_blob_falloff_q8(x_q8 - fx, y_q8 - fy,
                                                   390U);
                uint8_t core = led_blob_falloff_q8(x_q8 - fx, y_q8 - fy,
                                                   170U);
                uint8_t strength = (uint8_t)(((uint16_t)glow *
                                              (48U + (pulse * 3U) / 4U)) /
                                             255U);
                bool is_warm = (light & 3U) == 0U;

                color = led_color_blend(color, is_warm ? warm : cool,
                                        led_scene_strength(strength));
                color = led_color_blend(color,
                                        is_warm ? warm_core : cool_core,
                                        led_scene_strength((uint8_t)(
                                            ((uint16_t)core * pulse) / 255U)));
            }
            led_set_pixel(pixels,
                          (uint16_t)(row * DEV_RGB_MATRIX_WIDTH + column),
                          color);
        }
    }
}

static void led_effect_board_colorblocks(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    static const led_rgb_t base = {65U, 78U, 91U};
    static const led_rgb_t lower = {87U, 84U, 104U};
    static const led_rgb_t coral = {208U, 147U, 139U};
    static const led_rgb_t mint = {122U, 181U, 157U};
    static const led_rgb_t violet = {165U, 156U, 186U};
    static const led_rgb_t accent = {242U, 190U, 121U};
    uint32_t rate = 4UL + (uint32_t)(led_effect_state.speed >> 5U);
    uint16_t phase = (uint16_t)((led_effect_state.now_ms * rate) >> 1U);
    int32_t c1x = 870L + led_wave_offset_q8(phase, 704U);
    int32_t c1y = 1101L + led_wave_offset_q8((uint16_t)(phase + 15000U), 576U);
    int32_t c2x = 2970L + led_wave_offset_q8((uint16_t)(phase + 9000U), 653U);
    int32_t c2y = 2688L + led_wave_offset_q8((uint16_t)(phase + 24000U), 563U);
    int32_t c3x = 2074L + led_wave_offset_q8((uint16_t)(phase + 22000U), 717U);
    int32_t c3y = 1869L + led_wave_offset_q8((uint16_t)(phase + 36000U), 576U);
    int32_t c4x = 2816L + led_wave_offset_q8((uint16_t)(phase + 41000U), 717U);
    int32_t c4y = 819L + led_wave_offset_q8((uint16_t)(phase + 52000U), 435U);
    uint16_t row;
    uint16_t column;

    for(row = 0U; row < DEV_RGB_MATRIX_HEIGHT; row++)
    {
        uint8_t depth = (uint8_t)(((uint32_t)row * 255UL) /
                                  DEV_RGB_MATRIX_HEIGHT);
        for(column = 0U; column < DEV_RGB_MATRIX_WIDTH; column++)
        {
            int32_t x = (int32_t)column * 256L + 128L;
            int32_t y = (int32_t)row * 256L + 128L;
            led_rgb_t color = led_color_blend(base, lower, depth);

            color = led_color_blend(color, coral, led_scene_strength(
                led_soft_box_q8(x - c1x, y - c1y, 845U, 870U)));
            color = led_color_blend(color, mint, led_scene_strength(
                led_soft_box_q8(x - c2x, y - c2y, 1024U, 922U)));
            color = led_color_blend(color, violet,
                led_scene_strength((uint8_t)(led_soft_box_q8(
                    x - c3x, y - c3y, 947U, 1024U) * 3U / 4U)));
            color = led_color_blend(color, accent,
                led_scene_strength((uint8_t)(led_soft_box_q8(
                    x - c4x, y - c4y, 294U, 307U) * 3U / 4U)));
            led_set_pixel(pixels,
                          (uint16_t)(row * DEV_RGB_MATRIX_WIDTH + column),
                          color);
        }
    }
}

static void led_effect_board_breathing(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    static const led_rgb_t base = {56U, 78U, 86U};
    static const led_rgb_t jade = {144U, 181U, 165U};
    static const led_rgb_t rose = {196U, 156U, 162U};
    static const led_rgb_t violet = {174U, 169U, 191U};
    static const led_rgb_t gold = {239U, 201U, 153U};
    uint32_t rate = 3UL + (uint32_t)(led_effect_state.speed >> 6U);
    uint16_t phase = (uint16_t)((led_effect_state.now_ms * rate) >> 1U);
    uint8_t breath = (uint8_t)(led_smooth_wave_q8(phase) >> 8U);
    uint16_t row;
    uint16_t column;

    for(row = 0U; row < DEV_RGB_MATRIX_HEIGHT; row++)
    {
        for(column = 0U; column < DEV_RGB_MATRIX_WIDTH; column++)
        {
            int32_t x = (int32_t)column * 256L + 128L;
            int32_t y = (int32_t)row * 256L + 128L;
            led_rgb_t color = base;
            uint8_t weight;

            weight = led_ellipse_falloff_q8(x - 1050L, y - 2099L,
                (uint16_t)(1101U + breath * 2U), 1357U);
            color = led_color_blend(color, jade,
                                    led_scene_strength(weight));
            weight = led_ellipse_falloff_q8(x - 2918L, y - 1587L,
                1306U, (uint16_t)(1075U + (255U - breath) * 2U));
            color = led_color_blend(color, rose,
                                    led_scene_strength(weight));
            weight = led_ellipse_falloff_q8(x - 1971L, y - 3098L,
                                            1459U, 896U);
            color = led_color_blend(color, violet,
                                    led_scene_strength(weight >> 1U));
            weight = led_ellipse_falloff_q8(
                x - (2125L + led_wave_offset_q8((uint16_t)(phase + 9000U),
                                                384U)),
                y - 1997L, (uint16_t)(410U + breath),
                (uint16_t)(461U + breath));
            color = led_color_blend(color, gold,
                                    led_scene_strength(weight >> 1U));
            led_set_pixel(pixels,
                          (uint16_t)(row * DEV_RGB_MATRIX_WIDTH + column),
                          color);
        }
    }
}

static void led_effect_board_aurora(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    static const led_rgb_t base = {36U, 60U, 74U};
    static const led_rgb_t green = {108U, 206U, 181U};
    static const led_rgb_t violet = {173U, 149U, 208U};
    static const led_rgb_t rose = {216U, 156U, 174U};
    static const led_rgb_t highlight = {210U, 232U, 198U};
    uint32_t rate = 5UL + (uint32_t)(led_effect_state.speed >> 5U);
    uint16_t phase = (uint16_t)((led_effect_state.now_ms * rate) >> 1U);
    uint16_t row;
    uint16_t column;

    for(row = 0U; row < DEV_RGB_MATRIX_HEIGHT; row++)
    {
        int32_t y = (int32_t)row * 256L + 128L;
        for(column = 0U; column < DEV_RGB_MATRIX_WIDTH; column++)
        {
            int16_t sway = (int16_t)(
                led_wave_offset_q8((uint16_t)(column * 4500U - phase), 397U) +
                led_wave_offset_q8((uint16_t)(column * 2000U +
                                             (phase >> 1U)), 294U));
            uint8_t first = led_band_falloff_q8(y - 1126L - sway, 800U);
            uint8_t second = led_band_falloff_q8(y - 2253L +
                                                 (sway * 3L) / 4L, 870U);
            uint8_t third = led_band_falloff_q8(y - 3226L -
                led_wave_offset_q8((uint16_t)(column * 3200U + phase), 358U),
                900U);
            uint8_t edge = led_band_falloff_q8(y - 870L - sway, 230U);
            led_rgb_t color = base;

            color = led_color_blend(color, green,
                                    led_scene_strength(first));
            color = led_color_blend(color, violet,
                                    led_scene_strength(second >> 1U));
            color = led_color_blend(color, rose,
                                    led_scene_strength(third >> 2U));
            color = led_color_blend(color, highlight,
                                    led_scene_strength(edge >> 2U));
            led_set_pixel(pixels,
                          (uint16_t)(row * DEV_RGB_MATRIX_WIDTH + column),
                          color);
        }
    }
}

static void led_effect_board_pulse(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    static const led_rgb_t base = {20U, 22U, 45U};
    static const led_rgb_t pink = {250U, 33U, 123U};
    static const led_rgb_t cyan = {23U, 218U, 230U};
    static const led_rgb_t gold = {252U, 178U, 39U};
    uint32_t expansion = 2UL + (uint32_t)(led_effect_state.speed >> 6U);
    uint16_t cycle = (uint16_t)(((led_effect_state.now_ms * expansion) >> 2U) %
                                3072UL);
    uint16_t second_cycle = (uint16_t)((cycle + 1536U) % 3072U);
    uint16_t beat_phase = (uint16_t)(led_effect_state.now_ms *
                                     (8UL + (led_effect_state.speed >> 5U)));
    uint8_t beat = (uint8_t)(led_smooth_wave_q8(beat_phase) >> 8U);
    int32_t center_x = (int32_t)DEV_RGB_MATRIX_WIDTH * 128L +
        led_wave_offset_q8((uint16_t)(beat_phase >> 2U), 180U);
    int32_t center_y = (int32_t)DEV_RGB_MATRIX_HEIGHT * 128L +
        led_wave_offset_q8((uint16_t)((beat_phase >> 2U) + 16000U), 180U);
    uint16_t row;
    uint16_t column;

    for(row = 0U; row < DEV_RGB_MATRIX_HEIGHT; row++)
    {
        for(column = 0U; column < DEV_RGB_MATRIX_WIDTH; column++)
        {
            int32_t dx = (int32_t)column * 256L + 128L - center_x;
            int32_t dy = (int32_t)row * 256L + 128L - center_y;
            uint16_t distance = led_approx_distance_q8(dx, dy);
            uint8_t ring_a = led_band_falloff_q8((int32_t)distance - cycle,
                                                 230U);
            uint8_t ring_b = led_band_falloff_q8((int32_t)distance -
                                                 second_cycle, 245U);
            uint8_t core = led_blob_falloff_q8(dx, dy, 520U);
            led_rgb_t color = base;

            color = led_color_blend(color, pink,
                                    led_scene_strength(ring_a));
            color = led_color_blend(color, cyan,
                                    led_scene_strength(ring_b));
            core = (uint8_t)(((uint16_t)core * (56U +
                             ((uint16_t)beat * 3U) / 4U)) / 255U);
            color = led_color_blend(color, gold,
                                    led_scene_strength(core));
            led_set_pixel(pixels,
                          (uint16_t)(row * DEV_RGB_MATRIX_WIDTH + column),
                          color);
        }
    }
}

static void led_effect_board_prism(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    static const led_rgb_t base = {8U, 12U, 28U};
    static const led_rgb_t cyan = {0U, 220U, 255U};
    static const led_rgb_t rose = {255U, 25U, 145U};
    static const led_rgb_t gold = {255U, 150U, 0U};
    static const led_rgb_t highlight = {205U, 80U, 255U};
    uint32_t rate = 5UL + (uint32_t)(led_effect_state.speed >> 5U);
    uint16_t phase = (uint16_t)((led_effect_state.now_ms * rate) >> 1U);
    int32_t cyan_center = 333L + led_wave_offset_q8(phase, 1050U);
    int32_t rose_center = 1920L +
                          led_wave_offset_q8((uint16_t)(phase + 16000U), 896U);
    int32_t gold_center = 2100L +
                          led_wave_offset_q8((uint16_t)(phase + 33000U), 947U);
    uint16_t row;
    uint16_t column;

    for(row = 0U; row < DEV_RGB_MATRIX_HEIGHT; row++)
    {
        for(column = 0U; column < DEV_RGB_MATRIX_WIDTH; column++)
        {
            int32_t x = led_design_coordinate_q8(
                column, DEV_RGB_MATRIX_WIDTH, LED_EFFECT_DESIGN_WIDTH);
            int32_t y = led_design_coordinate_q8(
                row, DEV_RGB_MATRIX_HEIGHT, LED_EFFECT_DESIGN_HEIGHT);
            int32_t diagonal = (x * 200L - y * 113L) >> 8U;
            int32_t cross = ((x + y) * 141L) >> 8U;
            uint8_t a = led_band_falloff_q8(diagonal - cyan_center, 720U);
            uint8_t b = led_band_falloff_q8(diagonal - rose_center, 650U);
            uint8_t c = led_band_falloff_q8(cross - gold_center, 750U);
            uint8_t intersection = a < c ? a : c;
            led_rgb_t color = base;

            color = led_color_blend(color, cyan,
                                    led_scene_strength(a));
            color = led_color_blend(color, rose,
                                    led_scene_strength(b));
            color = led_color_blend(color, gold,
                                    led_scene_strength(c));
            if(b < c && b > intersection)
            {
                intersection = b;
            }
            color = led_color_blend(color, highlight,
                                    led_scene_strength(intersection >> 2U));
            led_set_pixel(pixels,
                          (uint16_t)(row * DEV_RGB_MATRIX_WIDTH + column),
                          color);
        }
    }
}

static void led_effect_board_liquid(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    static const led_rgb_t base = {4U, 5U, 12U};
    static const led_rgb_t teal = {59U, 200U, 195U};
    static const led_rgb_t pink = {232U, 98U, 165U};
    static const led_rgb_t violet = {18U, 10U, 38U};
    static const led_rgb_t gold = {242U, 189U, 99U};
    uint32_t rate = 5UL + (uint32_t)(led_effect_state.speed >> 5U);
    uint16_t phase = (uint16_t)((led_effect_state.now_ms * rate) >> 1U);
    int32_t teal_x = 1126L + led_wave_offset_q8(phase, 768U);
    int32_t teal_y = 1331L +
                     led_wave_offset_q8((uint16_t)(phase + 15000U), 666U);
    int32_t pink_x = 3021L +
                     led_wave_offset_q8((uint16_t)(phase + 28000U), 717U);
    int32_t pink_y = 2739L +
                     led_wave_offset_q8((uint16_t)(phase + 43000U), 691U);
    int32_t gold_x = 2048L +
                     led_wave_offset_q8((uint16_t)(phase + 50000U), 768U);
    int32_t gold_y = 2048L +
                     led_wave_offset_q8((uint16_t)(phase + 8000U), 768U);
    uint16_t row;
    uint16_t column;

    for(row = 0U; row < DEV_RGB_MATRIX_HEIGHT; row++)
    {
        for(column = 0U; column < DEV_RGB_MATRIX_WIDTH; column++)
        {
            int32_t x = led_design_coordinate_q8(
                column, DEV_RGB_MATRIX_WIDTH, LED_EFFECT_DESIGN_WIDTH);
            int32_t y = led_design_coordinate_q8(
                row, DEV_RGB_MATRIX_HEIGHT, LED_EFFECT_DESIGN_HEIGHT);
            uint8_t swirl = (uint8_t)(led_smooth_wave_q8((uint16_t)(
                phase * 2U + led_design_phase_offset(
                    column, DEV_RGB_MATRIX_WIDTH,
                    LED_EFFECT_DESIGN_WIDTH, 3900U) +
                led_design_phase_offset(row, DEV_RGB_MATRIX_HEIGHT,
                    LED_EFFECT_DESIGN_HEIGHT, 6200U))) >> 8U);
            uint8_t teal_weight = led_blob_falloff_q8(x - teal_x,
                                                      y - teal_y, 1450U);
            uint8_t pink_weight = led_blob_falloff_q8(x - pink_x,
                                                      y - pink_y, 1380U);
            uint8_t gold_weight = led_blob_falloff_q8(x - gold_x,
                                                      y - gold_y, 580U);
            led_rgb_t color = led_color_blend(base, violet, swirl >> 2U);

            color = led_color_blend(color, teal,
                                    led_scene_strength(teal_weight));
            color = led_color_blend(color, pink,
                                    led_scene_strength(pink_weight));
            color = led_color_blend(color, gold,
                                    led_scene_strength(gold_weight >> 1U));
            led_set_pixel(pixels,
                          (uint16_t)(row * DEV_RGB_MATRIX_WIDTH + column),
                          color);
        }
    }
}

/* Updated raw-pixel scenes from 16x16-light-board(1).html. */
static void led_effect_board_sunrise_v2(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    static const led_rgb_t top = {125U, 125U, 125U};
    static const led_rgb_t middle = {130U, 90U, 70U};
    static const led_rgb_t bottom = {71U, 71U, 71U};
    static const led_rgb_t sun = {160U, 130U, 90U};
    uint32_t rate = 4UL + (uint32_t)(led_effect_state.speed >> 5U);
    uint16_t phase = (uint16_t)((led_effect_state.now_ms * rate) >> 1U);
    int32_t sun_x_q8 = 1920L + led_wave_offset_q8(phase, 602U);
    int32_t sun_y_q8 = 1600L +
        led_wave_offset_q8((uint16_t)(phase + 17000U), 307U);
    uint16_t sun_radius_q8 = (uint16_t)(512U +
                                       led_effect_state.intensity);
    uint16_t row;
    uint16_t column;

    for(row = 0U; row < DEV_RGB_MATRIX_HEIGHT; row++)
    {
        int32_t y_q8 = led_design_coordinate_q8(
            row, DEV_RGB_MATRIX_HEIGHT, LED_EFFECT_DESIGN_HEIGHT);
        led_rgb_t background;

        if(y_q8 <= 640L)
        {
            background = top;
        }
        else if(y_q8 <= 2176L)
        {
            uint8_t amount = led_smoothstep8((uint8_t)(
                ((uint32_t)(y_q8 - 640L) * 255UL) / 1536UL));
            background = led_color_blend(top, middle, amount);
        }
        else if(y_q8 <= 3456L)
        {
            uint8_t amount = led_smoothstep8((uint8_t)(
                ((uint32_t)(y_q8 - 2176L) * 255UL) / 1280UL));
            background = led_color_blend(middle, bottom, amount);
        }
        else
        {
            background = bottom;
        }

        for(column = 0U; column < DEV_RGB_MATRIX_WIDTH; column++)
        {
            int32_t x_q8 = led_design_coordinate_q8(
                column, DEV_RGB_MATRIX_WIDTH, LED_EFFECT_DESIGN_WIDTH);
            int32_t dx = x_q8 - sun_x_q8;
            int32_t dy = y_q8 - sun_y_q8;
            uint8_t sun_weight = led_blob_falloff_q8(dx, dy,
                                                     sun_radius_q8);
            led_rgb_t color = led_color_blend(background, sun,
                                              sun_weight);

            led_set_pixel(pixels,
                          (uint16_t)(row * DEV_RGB_MATRIX_WIDTH + column),
                          color);
        }
    }
}

static void led_effect_board_waves_v2(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    static const led_rgb_t base = {47U, 91U, 99U};
    static const led_rgb_t deep = {29U, 70U, 79U};
    static const led_rgb_t wave = {72U, 140U, 148U};
    static const led_rgb_t middle = {93U, 159U, 157U};
    static const led_rgb_t crest = {180U, 216U, 198U};
    static const led_rgb_t glint = {210U, 223U, 202U};
    static const uint16_t band_y_q8[3] = {870U, 2048U, 3200U};
    uint32_t rate = 5UL + (uint32_t)(led_effect_state.speed >> 5U);
    uint16_t phase = (uint16_t)((led_effect_state.now_ms * rate) >> 1U);
    uint16_t sway_scale = (uint16_t)(128U + led_effect_state.intensity);
    uint16_t row;
    uint16_t column;

    for(row = 0U; row < DEV_RGB_MATRIX_HEIGHT; row++)
    {
        int32_t y_q8 = (int32_t)row * 256L + 128L;
        for(column = 0U; column < DEV_RGB_MATRIX_WIDTH; column++)
        {
            int32_t raw_sway =
                led_wave_offset_q8((uint16_t)(column * 4700U - phase), 190U) +
                led_wave_offset_q8((uint16_t)(column * 2300U +
                                             (phase >> 1U)), 97U);
            int32_t sway = (raw_sway * sway_scale) >> 8U;
            led_rgb_t color = row < 8U ? base : deep;
            bool is_crest = false;
            uint8_t band;

            for(band = 0U; band < 3U; band++)
            {
                int32_t center = (int32_t)band_y_q8[band] + sway +
                    led_wave_offset_q8((uint16_t)(phase + band * 14000U), 72U);
                int32_t distance = y_q8 - center;

                if(distance > 115L && distance < 525L)
                {
                    color = band == 1U ? middle : wave;
                    is_crest = false;
                }
                if(distance >= -115L && distance <= 115L)
                {
                    color = crest;
                    is_crest = true;
                }
            }

            if(is_crest)
            {
                uint8_t sparkle = (uint8_t)(led_smooth_wave_q8((uint16_t)(
                    column * 11200U + row * 4600U + phase)) >> 8U);
                if(sparkle > 245U)
                {
                    color = glint;
                }
            }
            led_set_pixel(pixels,
                          (uint16_t)(row * DEV_RGB_MATRIX_WIDTH + column),
                          color);
        }
    }
}

static void led_effect_board_fireflies_v2(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    static const led_rgb_t background = {0U, 0U, 0U};
    static const led_rgb_t green = {10U, 38U, 14U};
    static const led_rgb_t yellow = {62U, 41U, 4U};
    static const led_rgb_t green_core = {30U, 82U, 34U};
    static const led_rgb_t yellow_core = {105U, 70U, 7U};
    static const uint16_t seed_x_q8[6] = {
        819U, 2406U, 1382U, 3123U, 717U, 2995U
    };
    static const uint16_t seed_y_q8[6] = {
        794U, 1203U, 2278U, 2662U, 3098U, 666U
    };
    static const uint16_t seed_phase[6] = {
        0U, 6000U, 12000U, 17700U, 23700U, 29400U
    };
    uint32_t rate = 6UL + (uint32_t)(led_effect_state.speed >> 5U);
    uint16_t phase = (uint16_t)((led_effect_state.now_ms * rate) >> 1U);
    uint16_t row;
    uint16_t column;

    for(row = 0U; row < DEV_RGB_MATRIX_HEIGHT; row++)
    {
        for(column = 0U; column < DEV_RGB_MATRIX_WIDTH; column++)
        {
            int32_t x_q8 = led_design_coordinate_q8(
                column, DEV_RGB_MATRIX_WIDTH, LED_EFFECT_DESIGN_WIDTH);
            int32_t y_q8 = led_design_coordinate_q8(
                row, DEV_RGB_MATRIX_HEIGHT, LED_EFFECT_DESIGN_HEIGHT);
            led_rgb_t color = background;
            uint8_t priority = 0U;
            uint8_t light;

            for(light = 0U; light < 6U; light++)
            {
                uint8_t blink = (uint8_t)(led_smooth_wave_q8((uint16_t)(
                    phase * 3U + seed_phase[light])) >> 8U);
                uint8_t trail;

                if(blink < 28U)
                {
                    continue;
                }
                for(trail = 3U; trail-- != 0U;)
                {
                    uint16_t trail_phase = (uint16_t)(phase +
                        seed_phase[light] - trail * 4400U);
                    int32_t fx = (int32_t)seed_x_q8[light] +
                        led_wave_offset_q8(trail_phase, 589U);
                    int32_t fy = (int32_t)seed_y_q8[light] +
                        led_wave_offset_q8((uint16_t)(trail_phase + 16000U),
                                           435U);
                    uint8_t level = trail == 0U ? 2U : 1U;
                    uint16_t radius = trail == 0U ? 215U : 159U;

                    if(level > priority && led_blob_falloff_q8(
                           x_q8 - fx, y_q8 - fy, radius) != 0U)
                    {
                        bool is_yellow = (light & 1U) != 0U;
                        color = level == 2U
                                    ? (is_yellow ? yellow_core : green_core)
                                    : (is_yellow ? yellow : green);
                        priority = level;
                    }
                }
            }
            led_set_pixel(pixels,
                          (uint16_t)(row * DEV_RGB_MATRIX_WIDTH + column),
                          color);
        }
    }
}

static void led_effect_board_colorblocks_v2(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    static const led_rgb_t base = {65U, 78U, 91U};
    static const led_rgb_t lower = {87U, 84U, 104U};
    static const led_rgb_t coral = {208U, 147U, 139U};
    static const led_rgb_t mint = {122U, 181U, 157U};
    static const led_rgb_t violet = {165U, 156U, 186U};
    static const led_rgb_t accent = {242U, 190U, 121U};
    uint32_t rate = 4UL + (uint32_t)(led_effect_state.speed >> 5U);
    uint16_t phase = (uint16_t)((led_effect_state.now_ms * rate) >> 1U);
    int32_t c1x = 870L + led_wave_offset_q8(phase, 704U);
    int32_t c1y = 1101L + led_wave_offset_q8((uint16_t)(phase + 15000U), 576U);
    int32_t c2x = 2970L + led_wave_offset_q8((uint16_t)(phase + 9000U), 653U);
    int32_t c2y = 2688L + led_wave_offset_q8((uint16_t)(phase + 24000U), 563U);
    int32_t c3x = 2074L + led_wave_offset_q8((uint16_t)(phase + 22000U), 717U);
    int32_t c3y = 1869L + led_wave_offset_q8((uint16_t)(phase + 36000U), 576U);
    int32_t c4x = 2816L + led_wave_offset_q8((uint16_t)(phase + 41000U), 717U);
    int32_t c4y = 819L + led_wave_offset_q8((uint16_t)(phase + 52000U), 435U);
    uint16_t row;
    uint16_t column;

    for(row = 0U; row < DEV_RGB_MATRIX_HEIGHT; row++)
    {
        for(column = 0U; column < DEV_RGB_MATRIX_WIDTH; column++)
        {
            int32_t x = (int32_t)column * 256L + 128L;
            int32_t y = (int32_t)row * 256L + 128L;
            led_rgb_t color = row < 8U ? base : lower;

            if((x - c1x >= -845L && x - c1x <= 845L) &&
               (y - c1y >= -870L && y - c1y <= 870L)) color = coral;
            if((x - c2x >= -1024L && x - c2x <= 1024L) &&
               (y - c2y >= -922L && y - c2y <= 922L)) color = mint;
            if((x - c3x >= -947L && x - c3x <= 947L) &&
               (y - c3y >= -1024L && y - c3y <= 1024L)) color = violet;
            if((x - c4x >= -294L && x - c4x <= 294L) &&
               (y - c4y >= -307L && y - c4y <= 307L)) color = accent;

            led_set_pixel(pixels,
                          (uint16_t)(row * DEV_RGB_MATRIX_WIDTH + column),
                          color);
        }
    }
}

static void led_effect_board_pulse_v2(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    static const led_rgb_t base = {20U, 22U, 45U};
    static const led_rgb_t pink = {250U, 33U, 123U};
    static const led_rgb_t cyan = {23U, 218U, 230U};
    static const led_rgb_t gold = {252U, 178U, 39U};
    uint32_t expansion = 2UL + (uint32_t)(led_effect_state.speed >> 6U);
    uint16_t cycle = (uint16_t)(((led_effect_state.now_ms * expansion) >> 2U) %
                                3072UL);
    uint16_t second_cycle = (uint16_t)((cycle + 1536U) % 3072U);
    uint16_t beat_phase = (uint16_t)(led_effect_state.now_ms *
                                     (8UL + (led_effect_state.speed >> 5U)));
    uint8_t beat = (uint8_t)(led_smooth_wave_q8(beat_phase) >> 8U);
    uint16_t ring_width = (uint16_t)(256U +
                          led_effect_state.intensity);
    int32_t center_x = (int32_t)(LED_EFFECT_DESIGN_WIDTH_Q8 / 2UL);
    int32_t center_y = (int32_t)(LED_EFFECT_DESIGN_HEIGHT_Q8 / 2UL);
    uint16_t row;
    uint16_t column;

    for(row = 0U; row < DEV_RGB_MATRIX_HEIGHT; row++)
    {
        for(column = 0U; column < DEV_RGB_MATRIX_WIDTH; column++)
        {
            int32_t dx = led_design_coordinate_q8(
                column, DEV_RGB_MATRIX_WIDTH,
                LED_EFFECT_DESIGN_WIDTH) - center_x;
            int32_t dy = led_design_coordinate_q8(
                row, DEV_RGB_MATRIX_HEIGHT,
                LED_EFFECT_DESIGN_HEIGHT) - center_y;
            uint16_t distance = led_approx_distance_q8(dx, dy);
            uint8_t ring_a = led_band_falloff_q8(
                (int32_t)distance - cycle, ring_width);
            uint8_t ring_b = led_band_falloff_q8(
                (int32_t)distance - second_cycle, ring_width);
            uint8_t core = led_blob_falloff_q8(dx, dy, 640U);
            led_rgb_t color = base;

            core = (uint8_t)(((uint16_t)core * beat) / 255U);
            color = led_color_blend(color, cyan, ring_b);
            color = led_color_blend(color, pink, ring_a);
            color = led_color_blend(color, gold, core);
            led_set_pixel(pixels,
                          (uint16_t)(row * DEV_RGB_MATRIX_WIDTH + column),
                          color);
        }
    }
}

bool led_effects_start(uint8_t effect, uint8_t speed, uint8_t intensity,
                       uint8_t brightness)
{
    if((effect >= LED_EFFECT_COUNT) ||
       (dev_rgb_matrix.set_brightness == NULL) ||
       !dev_rgb_matrix.set_brightness(&dev_rgb_matrix, brightness))
    {
        return false;
    }

    memset(&led_effect_state, 0, sizeof(led_effect_state));
    led_effect_state.running = true;
    led_effect_state.effect = effect;
    led_effect_state.speed = speed;
    led_effect_state.intensity = intensity;
    led_effect_state.frame_clear_pending = true;
    led_effect_state.hue_a = (uint8_t)(effect * 37U + speed);
    led_effect_state.hue_b = (uint8_t)(led_effect_state.hue_a + 85U);
    led_effect_state.random_state = 0x574C4544UL ^
                                    ((uint32_t)effect << 16U) ^
                                    ((uint32_t)speed << 8U) ^ intensity;
    return true;
}

void led_effects_stop(void)
{
    led_effect_state.running = false;
}

bool led_effects_is_running(void)
{
    return led_effect_state.running;
}

void led_effects_render(
    uint8_t pixels[DEV_RGB_MATRIX_HEIGHT]
                  [DEV_RGB_MATRIX_WIDTH]
                  [DEV_RGB_MATRIX_RGB888_CHANNELS])
{
    uint16_t column;
    uint16_t row;

    if(!led_effect_state.running || pixels == NULL)
    {
        return;
    }

    if(led_effect_state.frame_clear_pending)
    {
        led_fill(pixels, led_color_black);
        led_effect_state.frame_clear_pending = false;
    }

    switch(led_effect_state.effect)
    {
        case 0U:
            led_effect_random_color(pixels);
            break;
        case 1U:
            led_effect_twinkle(pixels);
            break;
        case 2U:
            led_effect_dissolve(pixels);
            break;
        case 3U:
            led_effect_chase_random(pixels);
            break;
        case 4U:
            led_effect_fireworks(pixels);
            break;
        case 5U:
            led_effect_music_volume(pixels);
            break;
        case 6U:
            led_effect_ocean_roll(pixels, true);
            break;
        case 7U:
            led_effect_ocean_roll(pixels, false);
            break;
        case 8U:
            led_effect_sunset_sun(pixels, false);
            break;
        case 9U:
            led_effect_sunset_sun(pixels, true);
            break;
        case 10U:
            led_effect_board_sunrise_v2(pixels);
            break;
        case 11U:
            led_effect_board_fireflies_v2(pixels);
            break;
        case 12U:
            led_effect_board_pulse_v2(pixels);
            break;
        case 13U:
            led_effect_board_prism(pixels);
            break;
        case 14U:
            led_effect_board_liquid(pixels);
            break;
        default:
            led_fill(pixels, led_color_black);
            break;
    }

    for(row = 0U; row < DEV_RGB_MATRIX_HEIGHT; row++)
    {
        for(column = 0U; column < DEV_RGB_MATRIX_WIDTH; column++)
        {
            led_rgb_t output = {
                pixels[row][column][0],
                pixels[row][column][1],
                pixels[row][column][2]
            };

            output = led_color_apply_html_defaults(
                output, led_effect_state.effect);
            pixels[row][column][0] = output.red;
            pixels[row][column][1] = output.green;
            pixels[row][column][2] = output.blue;
        }
    }

    led_effect_state.now_ms += LED_EFFECT_FRAME_MS;
}
