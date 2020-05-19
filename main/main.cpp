/*
 * main.cpp
 *
 *  Created on: 05.05.2020
 *      Author: galuschka
 */

#include "Wifi.h"
#include "WebServer.h"

#include "Indicator.h"
#include "AnalogReader.h"
#include "Monitor.h"
#include "Relay.h"
#include "Control.h"

#include "esp_event.h"  // esp_event_loop_create_default()
#include "esp_netif.h"  // esp_netif_init()
#include "esp_log.h"    // ESP_LOGI()
#include "nvs_flash.h"  // nvs_flash_init()

#include "driver/gpio.h"    // gpio_config(), gpio_set_level()

static const char *TAG = "MAIN";

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
    // LED on GPIO2:
    Indicator indicator { GPIO_NUM_2 };         // blue onchip LED
    indicator.Init();

    main_nvs_init();  // initialize non-volatile file system

    Wifi::Instance().Init( indicator, 60/*secs timeout to try connect*/);

    // Wifi::Init blocks until success (or access point mode)
    // now we can initialize web server:
    WebServer::Instance().Init();

    Relay relay { GPIO_NUM_12, true, true };  // open drain mode and low active
    AnalogReader reader { GPIO_NUM_15/*sensor pwrsup*/, 10/*Hz*/, 100 /*values to store*/};

    if (!reader.Init()) {
        indicator.Indicate( Indicator::STATUS_ERROR );
        while (true)
            vTaskDelay( portMAX_DELAY );
    }

    Monitor monitor { reader };
    Control control { reader, relay, 0x200, 0x80 }; // off: reaching 1/2 FS / on: falling below 1/8 FS

    control.Run( indicator );
}
