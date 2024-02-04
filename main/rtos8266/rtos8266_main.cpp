/*
 * main.cpp - just basic features: setup wifi paramter and update
 */

//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "Indicator.h"
#include "Wifi.h"
#include "Updator.h"
#include "WebServer.h"

#include "esp_event.h"  // esp_event_loop_create_default()
#include "esp_netif.h"  // esp_netif_init()
#include "esp_log.h"    // ESP_LOGI()
#include "nvs_flash.h"  // nvs_flash_init()

#if (LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG)
#define EXPRD(expr) do { expr; } while(0);
#else
#define EXPRD(expr)
#endif

static const char *TAG = "rtos8266";

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
    ESP_LOGD( TAG, "Indicator..." ); EXPRD(vTaskDelay(1))
    // red LED on GPIO2, green LED on GPIO16
    Indicator::Instance().Init( GPIO_NUM_2, GPIO_NUM_16 ); // red/green

    ESP_LOGD( TAG, "nvs_flash_init..." ); EXPRD(vTaskDelay(1))
    main_nvs_init();  // initialize non-volatile file system

    ESP_LOGD( TAG, "Wifi()" ); EXPRD(vTaskDelay(1))
    Wifi::Instance().Init( 60 );

    // Wifi::Init blocks until success (or access point mode)
    // now we can initialize web server:

    ESP_LOGD( TAG, "Updator..." ); EXPRD(vTaskDelay(1))
    Updator::Instance().Init();

    ESP_LOGD( TAG, "WebServer..." ); EXPRD(vTaskDelay(1))
    WebServer::Instance().Init();

    while (true) {
        vTaskDelay( portMAX_DELAY );
    }
}
