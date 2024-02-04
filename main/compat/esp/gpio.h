/* compatibility header to map gpio of esp-sdk to esp8266-rtos-sdk
*/
#ifndef _ESP_GPIO_H
#define _ESP_GPIO_H

#include <driver/gpio.h>
#include <esp8266/gpio_struct.h>    // GPIO

#ifdef	__cplusplus
extern "C" {
#endif

typedef enum {
    GPIO_INPUT          = GPIO_MODE_INPUT,
    GPIO_OUTPUT         = GPIO_MODE_OUTPUT,
    GPIO_OUT_OPEN_DRAIN = GPIO_MODE_OUTPUT_OD,
} gpio_direction_t;

static inline void gpio_enable( const uint8_t gpio_num, const gpio_direction_t direction )
{
    gpio_set_direction( gpio_num, direction );
}

static inline bool gpio_read( const uint8_t gpio_num )
{
    return gpio_get_level( gpio_num );
}

static inline bool gpio_write( const uint8_t gpio_num, const bool set )
{
    return gpio_set_level( gpio_num, set ) == ESP_OK;
}

#ifdef	__cplusplus
}
#endif

#endif
