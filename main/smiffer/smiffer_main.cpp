/*
 * smarter_main.cpp - smart meter sniffer
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
 *               GPIO13 - D7   1kΩ   D4 - GPIO2  (onboard LED)
 *               GPIO15 - D8    |    G  - GND     <----- input IR-LED -
 *                      - 3V3 --+    5V - power supply
 */

#define IRLED_INPUT_PIN    GPIO_NUM_4

//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "Init.h"
#include "Indicator.h"
#include "Smiffer.h"
#include "Infrared.h"

#include <esp_log.h>    // ESP_LOGI()

#if (LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG)
#define EXPRD(expr) do { expr; } while(0);
#else
#define EXPRD(expr)
#endif

namespace {
const char *TAG = "smiffer";
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
    Init::Init();

    Smiffer & smiffer = Smiffer::Instance();
    do {
        if (! smiffer.Init()) {
            ESP_LOGE( TAG, "smiffer.Init failed" );
            break;
        }
        SmifferIR smifferIR( smiffer, IRLED_INPUT_PIN );
        smiffer.SetInfrared( smifferIR );

        if (! smifferIR.Init()) {
            ESP_LOGE( TAG, "smifferIR.Init failed" );
            break;
        }

        smiffer.Run();

        ESP_LOGE( TAG, "smiffer.Run returned" );

    } while (false);

    while (1)
        vTaskDelay( portMAX_DELAY );
}
