/*
 * smarter_main.cpp - smart meter sniffer
 *
 *  Created on: 04.12.2022
 *      Author: galuschka
 *
 * D1 mini's usable GPIOs: 4,5, 12,13,14
 *                         ___   _   _
 *                        | | |_| |_| |
 *                        | |        o|           <- wifi antenna and LED
 *
 *               /RST   - RST        TX - GPIO1
 *               ADC0   - A0         RX - GPIO3
 *               GPIO16 - D0         D1 - GPIO5
 *               GPIO14 - D5    +--- D2 - GPIO4   <----- input IR-LED +
 *               GPIO12 - D6    |    D3 - GPIO0  (Flash)
 *               GPIO13 - D7    R    D4 - GPIO2  (onboard LED)
 *               GPIO15 - D8    |    G  - GND     <----- input IR-LED -
 *                      - 3V3 --+    5V - power supply
 */

#define IRLED_INPUT_PIN    GPIO_NUM_4

//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "Indicator.h"
#include "Wifi.h"
#include "Updator.h"
#include "WebServer.h"
#include "Smiffer.h"
#include "Infrared.h"

#include <esp_event.h>  // esp_event_loop_create_default()
#include <esp_netif.h>  // esp_netif_init()
#include <esp_log.h>    // ESP_LOGI()
#include <nvs_flash.h>  // nvs_flash_init()

#if (LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG)
#define EXPRD(expr) do { expr; } while(0);
#else
#define EXPRD(expr)
#endif

static const char *TAG = "smiffer";

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

class SmifferIR : public Infrared
{
    SmifferIR();
  public:
    SmifferIR( Smiffer & smiffer, gpio_num_t rxPin, uint32_t baudrate = 9600 ) : Infrared( rxPin, baudrate ), mSmiffer {smiffer} {}
    void rxFunc( uint8_t byte, bool ovfl ) {
        mSmiffer.read( byte, ovfl );
    }
  private:
    Smiffer & mSmiffer;
};

extern "C" void app_main()
{
    ESP_LOGD( TAG, "Indicator..." ); EXPRD(vTaskDelay(1))
    // red LED on GPIO2, green LED on GPIO16
    Indicator::Instance().Init( GPIO_NUM_2 ); // red/green

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

    ESP_LOGD( TAG, "LED off" );
    Indicator::Instance().Steady( 0 );

    Smiffer smiffer{};
    SmifferIR smifferIR( smiffer, IRLED_INPUT_PIN );
    smifferIR.Run();
    ESP_LOGE( TAG, "smifferIR.Run failed" );
    while (1)
        vTaskDelay( portMAX_DELAY );
}
