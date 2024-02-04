/*
 * main.cpp
 *
 *  Created on: 05.05.2020
 *      Author: galuschka
 *
 * D1 mini's usable GPIOs: 4,5, 12,13,14
 *                         ___   _   _
 *                        | | |_| |_| |
 *                        | |        o|           <- wifi antenna and LED
 *
 *               /RST   - RST        TX - GPIO1
 * sensor <---   ADC0   - A0         RX - GPIO3
 *               GPIO16 - D0         D1 - GPIO5   ---> sensor power supply
 * unlock <---   GPIO14 - D5         D2 - GPIO4   ---> relay
 *               GPIO12 - D6         D3 - GPIO0
 *               GPIO13 - D7         D4 - GPIO2  (onboard LED)
 *               GPIO15 - D8         G  - GND
 *                      - 3V3        5V - power supply
 */

#include "Wifi.h"
#include "WebServer.h"
#include "Indicator.h"
#include "Updator.h"
//include "Mqtinator.h"
#include "Relay.h"

#include "AnalogReader.h"
#include "Monitor.h"
#include "Control.h"

#include <esp_event.h>  // esp_event_loop_create_default()
#include <esp_netif.h>  // esp_netif_init()
#include <esp_log.h>    // ESP_LOGI()
#include <nvs_flash.h>  // nvs_flash_init()

#include <driver/gpio.h>    // gpio_config(), gpio_set_level()

static const char *TAG = "threswizz";

void main_nvs_init()
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        if ((err == ESP_ERR_NVS_NO_FREE_PAGES)/* || (err == ESP_ERR_NVS_NEW_VERSION_FOUND)*/) {
            // NVS partition was truncated and needs to be erased
            // Retry nvs_flash_init
            ESP_LOGW( TAG, "no free pages - erasing NVS partition" );
            ESP_ERROR_CHECK( nvs_flash_erase() );
            ESP_ERROR_CHECK( nvs_flash_init() );
        } else {
            _esp_error_check_failed( err, __ESP_FILE__, __LINE__, __ASSERT_FUNC,
                    "nvs-flash-init()" );
        }
    }

    ESP_ERROR_CHECK( esp_netif_init() );
    ESP_ERROR_CHECK( esp_event_loop_create_default() );
}

extern "C" void app_main()
{
    ESP_LOGD( TAG, "Indicator..." );
    // LED on GPIO2:
    Indicator::Instance().Init( GPIO_NUM_2 );

    ESP_LOGD( TAG, "nvs_flash_init..." );
    main_nvs_init();  // initialize non-volatile file system

    ESP_LOGD( TAG, "Wifi..." );
    Wifi::Instance().Init( 60 );

    // Wifi::Init blocks until success (or access point mode)

    ESP_LOGD( TAG, "Updator..." );
    Updator::Instance().Init();

    // ESP_LOGD( TAG, "Mqtinator..." );
    // Mqtinator & mqtinator = Mqtinator::Instance();
    // mqtinator.Init();

    ESP_LOGD( TAG, "WebServer..." );
    WebServer::Instance().Init();

    Relay        relay  { GPIO_NUM_4, true, true };  // open drain mode and low active
    AnalogReader reader { GPIO_NUM_5, relay };       // power supply to sensor

    if (!reader.Init( 10/*Hz*/, 100 /*values to store*/ )) {
        Indicator::Instance().Indicate( Indicator::STATUS_ERROR );
        while (true)
            vTaskDelay( portMAX_DELAY );
    }

    Monitor monitor { reader };
    Control control { reader, relay, 0x200, 0x80 }; // off: reaching 1/2 FS / on: falling below 1/8 FS

    control.Run( Indicator::Instance() );
}
