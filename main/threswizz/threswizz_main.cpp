/*
 * threswizz_main.cpp
 *
 * last working version:
 *  Project version:	 	0.3-14-ga50f6
 *  IDF version:		   v3.4-56-g6ea4e2af
 *
 * D1 mini's usable GPIOs: 4,5, 12,13,14
 *                         ___   _   _
 *                        | | |_| |_| |
 *                        | |        o|           <- wifi antenna and LED
 *
 *               /RST   - RST        TX - GPIO1
 * sensor  <---  ADC0   - A0         RX - GPIO3
 *               GPIO16 - D0         D1 - GPIO5   ---> sensor power supply
 * unlock  <---  GPIO14 - D5         D2 - GPIO4   ---> relay 1
 * relay 2 <---  GPIO12 - D6         D3 - GPIO0   ---> onewire
 *               GPIO13 - D7         D4 - GPIO2  (onboard LED)
 *               GPIO15 - D8         G  - GND
 *                      - 3V3        5V - power supply
 */

#include "Init.h"
#include "Indicator.h"
#include "Temperator.h"
#include "Relay.h"
#include "Input.h"

#include "AnalogReader.h"
#include "Monitor.h"
#include "Control.h"

#include <esp_log.h>    // ESP_LOGI()

#include <driver/gpio.h>    // gpio_config(), gpio_set_level()

namespace {
const char *TAG = "threswizz";
}

extern "C" void tempCallback( void * control, uint16_t idx, float temperature )
{
    ((Control *)control)->Temperature( idx, temperature );
}

extern "C" void app_main()
{
    Init::Init();

    Relay        relay1 { GPIO_NUM_4,  true, true };  // open drain mode and low active
    Relay        relay2 { GPIO_NUM_12, true, true };  // open drain mode and low active
    AnalogReader reader { GPIO_NUM_5, relay1, relay2 };       // power supply to sensor
    Monitor      monitor{ reader };
    Input        input  { GPIO_NUM_14 };                    // switch to manual control
    Temperator   temperator{ GPIO_NUM_0 };                        // one wire on GPIO 0
    Control      control{ reader, relay1, relay2, input, monitor }; // off: reaching 1/2 FS / on: falling below 1/8 FS

    const char * err = nullptr;
    if (! input.Init())
        err = "input";
    else if (! reader.Init( 1/*sec store/report interval*/, 8/*meas. avg. calc.*/, 10/*Hz avg. meas. freq.*/, 600/*values to store*/ ))
        err = "reader";
    else if (! temperator.Start())
        err = "temperator";
    else {
        temperator.OnTempRead( tempCallback, & control );
        control.Run( Indicator::Instance() );
    }

    if (err) {
        ESP_LOGE( TAG, "Initialization error: %s", err );
    }
    Indicator::Instance().Indicate( Indicator::STATUS_ERROR );
    while (true)
        vTaskDelay( portMAX_DELAY );
}
