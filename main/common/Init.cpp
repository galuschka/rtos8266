/*
 * Init.cpp
 *
 * Common initialization routine
 *
 * D1 mini's usable GPIOs: 4,5, 12,13,14
 *                         ___   _   _
 *                        | | |_| |_| |
 *                        | |        o|           <- wifi antenna and LED
 *
 *               /RST   - RST      X TX - GPIO1
 *               ADC0   - A0       X RX - GPIO3
 *               GPIO16 - D0 !    ok D1 - GPIO5
 *               GPIO14 - D5 ok   ok D2 - GPIO4
 *               GPIO12 - D6 ok    ! D3 - GPIO0
 *               GPIO13 - D7 ok    ! D4 - GPIO2  (onboard LED)
 *               GPIO15 - D8 !        G  - GND
 *               Aref   - 3V3        5V - power supply
 */
//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "BootCnt.h"
#include "Indicator.h"
#include "Wifi.h"
#include "WebServer.h"
#include "Updator.h"
#include "Mqtinator.h"

#include <nvs_flash.h>  // nvs_flash_init()
#include <esp_event.h>  // esp_event_loop_create_default()
#include <esp_netif.h>  // esp_netif_init()
#include <esp_log.h>    // ESP_LOGD()...

#include <driver/gpio.h>    // gpio_num_t

namespace {

const char * TAG = "Init";

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

} // namespace

namespace Init {

void Init( gpio_num_t secIndLED )
{
    BootCnt::Instance().Init();

    ESP_LOGD( TAG, "Indicator..." );
    Indicator::Instance().Init( GPIO_NUM_2, secIndLED );  // primary indicator LED always on GPIO2

    ESP_LOGD( TAG, "nvs_flash_init..." );
    main_nvs_init();  // initialize non-volatile file system

    ESP_LOGD( TAG, "Wifi..." );
    Wifi::Instance().Init( 60 );

    // Wifi::Init blocks until success (or access point mode)

    ESP_LOGD( TAG, "WebServer..." );
    WebServer::Instance().Init();

    ESP_LOGD( TAG, "Mqtinator..." );
    Mqtinator::Instance().Init();

    ESP_LOGD( TAG, "Updator..." );
    Updator::Instance().Init();

    ESP_LOGD( TAG, "init basic web pages..." );
    WebServer::Instance().InitPages();  // default pages Home, Wifi, MQTT, Update

    ESP_LOGD( TAG, "Init done." );
}

} // namespace Init
