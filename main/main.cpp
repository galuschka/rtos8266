/*
 * main.cpp
 *
 *  Created on: 05.05.2020
 *      Author: galuschka
 */

#include "Wifi.h"
#include "WebServer.h"

#include "AnalogReader.h"
#include "Monitor.h"

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
    main_nvs_init();  // initialize non-volatile file system

    Wifi::Instance().Init( 60 /*secs timeout to try connect*/);

    // Wifi::Init blocks until success (or access point mode)
    // now we can initialize web server:
    WebServer::Instance().Init();

    // LED on GPIO2:
    {
        gpio_config_t io_conf;

        io_conf.pin_bit_mask = (1 << GPIO_NUM_2);      // blue LED onchip
        io_conf.mode         = GPIO_MODE_OUTPUT;       // set as output mode
        io_conf.pull_up_en   = GPIO_PULLUP_DISABLE;    // disable pull-up mode
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;  // disable pull-down mode
        io_conf.intr_type    = GPIO_INTR_DISABLE;      // disable interrupt

        gpio_config( &io_conf );    // configure GPIO with the given settings
    }

    AnalogReader reader;
    if (! reader.Init( 10/*Hz*/, 100/*values to store*/, GPIO_NUM_15/*power supply to sensor*/ )) {
        gpio_set_level( GPIO_NUM_2, 0 );        // low active -> steady on indicates error
        while (true)
            vTaskDelay( portMAX_DELAY );
    }
    Monitor monitor{reader};

    // Switch switch{monitor};

    while (true) {
        gpio_set_level( GPIO_NUM_2, 0 );        // low active -> flashing 1/10th second each second
        vTaskDelay( configTICK_RATE_HZ / 10 );
        gpio_set_level( GPIO_NUM_2, 1 );
        vTaskDelay( (configTICK_RATE_HZ * 9) / 10 );
    }
}
