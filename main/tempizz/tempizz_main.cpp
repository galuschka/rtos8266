/*
 * tempizz_main.cpp
 *
 * D1 mini's usable GPIOs: 4,5, 12,13,14
 *                         ___   _   _
 *                        | | |_| |_| |
 *                        | |        o|           <- wifi antenna and LED
 *
 *               /RST   - RST      X TX - GPIO1
 *               ADC0   - A0       X RX - GPIO3
 *               GPIO16 - D0 !    ok D1 - GPIO5  ---> relay 1
 *   red   <---  GPIO14 - D5 ok   ok D2 - GPIO4  ---> relay 2
 *   green <---  GPIO12 - D6 ok    ! D3 - GPIO0  ---> onewire
 *   blue  <---  GPIO13 - D7 ok    ! D4 - GPIO2  (onboard LED)
 *               GPIO15 - D8 !        G  - GND
 *                      - 3V3        5V - power supply
 */

#include "Init.h"
#include "Temperator.h"
#include "Relay.h"
#include "Fader.h"
#include "rgb.h"

#include <esp_log.h>    // ESP_LOGD()...

namespace {
const char *TAG = "tempizz";
}

extern "C" void app_main()
{
    Init::Init();

    Relay relay1 { GPIO_NUM_5, true, true };  // open drain mode and low active
    Relay relay2 { GPIO_NUM_4, true, true };  // open drain mode and low active

    Fader fader { GPIO_NUM_14, GPIO_NUM_12, GPIO_NUM_13 };  // pwm: R G B
    fader.Start();

    ESP_LOGD( TAG, "OneWire..." );
    Temperator temperator{ GPIO_NUM_0 };
    temperator.Start();

    RGB rgb{ fader, 344 };
    rgb.Start();

    while (1) {
        // vTaskDelay( configTICK_RATE_HZ * 4 );
        vTaskDelay( portMAX_DELAY );
    }
}
