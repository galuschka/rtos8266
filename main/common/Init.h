/*
 * Init.h
 */

#include <driver/gpio.h>    // gpio_num_t

namespace Init {
    void Init( gpio_num_t secIndLED = GPIO_NUM_MAX );  // when secondary indicator LED used
}
