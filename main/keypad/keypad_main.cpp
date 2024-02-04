/*
 * keypad_main.cpp
 *
 * D1 mini's usable GPIOs: 4,5, 12,13,14
 * https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/
 *      10k pull-up   on GPIO  0,2
 *      10k pull-down on GPIO 15
 *                         ___   _   _
 *                        | | |_| |_| |
 *                        | |        o|           <- wifi antenna and LED
 *
 *               /RST   - RST        TX - GPIO1
 *               ADC0   - A0         RX - GPIO3
 *   green <---  GPIO16 - D0         D1 - GPIO5   ---> col0
 *   row0  --->  GPIO14 - D5         D2 - GPIO4   ---> col1
 *   row1  --->  GPIO12 - D6         D3 - GPIO0   ---> col2
 *   row2  --->  GPIO13 - D7         D4 - GPIO2   ---> red (+ onboard LED)
 *   row3  --->  GPIO15 - D8         G  - GND
 *                      - 3V3        5V - power supply
 */
static const unsigned char s_row[] = { 14,12,13,15 };
static const unsigned char s_col[] = {  5, 4, 0 };
#define NELEMENTS(x) (sizeof(x)/sizeof(x[0]))

//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "Indicator.h"
#include "Wifi.h"
#include "Updator.h"
#include "Mqtinator.h"
#include "WebServer.h"
#include "Keypad.h"

#include "esp_event.h"  // esp_event_loop_create_default()
#include "esp_netif.h"  // esp_netif_init()
#include "esp_log.h"    // ESP_LOGI()
#include "nvs_flash.h"  // nvs_flash_init()

#if (LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG)
#define EXPRD(expr) do { expr; } while(0);
#else
#define EXPRD(expr)
#endif

static const char *TAG = "keypad";

extern "C" void OnFlash( const char * topic, const char * data )
{
    ESP_LOGD( TAG, "got \"%s\" \"%s\"", topic, data );
    Indicator::Instance().Access( *data & 1 );
}

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

    ESP_LOGD( TAG, "Mqtinator..." ); EXPRD(vTaskDelay(1))
    Mqtinator & mqtinator = Mqtinator::Instance();
    mqtinator.Init();
    mqtinator.Sub( "flash", & OnFlash );


    ESP_LOGD( TAG, "WebServer..." ); EXPRD(vTaskDelay(1))
    WebServer::Instance().Init();

    ESP_LOGD( TAG, "Keypad..." ); EXPRD(vTaskDelay(1))
    Keypad keypad{ s_col, NELEMENTS(s_col), s_row, NELEMENTS(s_row) };

    ESP_LOGD( TAG, "keypad.Run()" ); EXPRD(vTaskDelay(1))
    keypad.Run();
}
