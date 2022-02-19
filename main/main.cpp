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
 *               ADC0   - A0         RX - GPIO3
 *               GPIO16 - D0         D1 - GPIO5   ---> col0
 *   row0 <---   GPIO14 - D5         D2 - GPIO4   ---> col1
 *   row1 <---   GPIO12 - D6         D3 - GPIO0   ---> col2
 *   row2 <---   GPIO13 - D7         D4 - GPIO2  (onboard LED)
 *   row3 <---   GPIO15 - D8         G  - GND
 *                      - 3V3        5V - power supply
 */
static const unsigned char s_row[] = { 14,12,13,15 };
static const unsigned char s_col[] = {  5, 4, 0 };
#define NELEMENTS(x) (sizeof(x)/sizeof(x[0]))

#include "Wifi.h"
#include "WebServer.h"

#include "Indicator.h"
#include "Pinpad.h"

#include "esp_event.h"  // esp_event_loop_create_default()
#include "esp_netif.h"  // esp_netif_init()
#include "esp_log.h"    // ESP_LOGI()
#include "nvs_flash.h"  // nvs_flash_init()

static const char *TAG = "main";

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
    ESP_LOGI( TAG, "enter" ); sys_delay_ms(5000);

    // LED on GPIO2:
    Indicator indicator { GPIO_NUM_2 };         // blue onboard LED
    indicator.Init();

    ESP_LOGI( TAG, "vor main_nvs_init()" ); sys_delay_ms(100);

    main_nvs_init();  // initialize non-volatile file system
/*
    ESP_LOGI( TAG, "vor Wifi::Instance().Init()" ); sys_delay_ms(100);

    Wifi::Instance().Init( indicator, 60 );  // secs timeout to try connect / 0:AP only

    ESP_LOGI( TAG, "vor WebServer::Instance().Init" ); sys_delay_ms(100);

    // Wifi::Init blocks until success (or access point mode)
    // now we can initialize web server:
    WebServer::Instance().Init();
*/
    ESP_LOGI( TAG, "vor Pinpad()" ); sys_delay_ms(100);

    Pinpad pinpad{ s_col, NELEMENTS(s_col), s_row, NELEMENTS(s_row) };

    ESP_LOGI( TAG, "vor pinpad.Run()" ); sys_delay_ms(100);

    pinpad.Run( indicator );
}
