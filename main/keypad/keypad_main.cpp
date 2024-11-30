/*
 * keypad_main.cpp
 *
 * D1 mini's usable GPIOs: 4,5, 12,13,14
 * https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/
 *      10k pull-up   on GPIO  0,2
 *      10k pull-down on GPIO 15 (and 16)
 *
 * https://espeasy.readthedocs.io/en/latest/Reference/GPIO.html#best-pins-to-use-on-esp8266
 *
 * !!!note!!! d1mini pro not working: GPIO 0 as pull down output won't pull down to 0V
 *
 * io - can be used as inout or output (ioI: used as input / ioO: used as output)
 * ↑↓ - 40k..60k internal pull up/down
 *                         ___   _   _
 * o↑ - 10k pull up       | | |_| |_| |
 * o↓ - 10k pull down     | |        o|           <- wifi antenna and LED
 *
 *                 /RST   - RST    TX - GPIO1   
 *                 ADC0   - A0     RX - GPIO3   
 *  green <--- o↓O GPIO16 - D0     D1 - GPIO5 ioO ---> row3
 *  row0  <--- ioO GPIO14 - D5     D2 - GPIO4 ioI <--- col1
 *  row1  <--- ioO GPIO12 - D6     D3 - GPIO0 o↑I <--- col2
 *  row2  <--- ioO GPIO13 - D7     D4 - GPIO2 o↑O ---> red (+ onboard LED)
 *  col0  ---> o↓I GPIO15 - D8     G  - GND
 *                        - 3V3    5V - power supply
 */
static const unsigned char s_row[] = { 14,12,13, 5 };
static const unsigned char s_col[] = { 15, 4, 0    };
#define NELEMENTS(x) (sizeof(x)/sizeof(x[0]))

// ESP8166 internal hard wired pull up/down pins:
static const unsigned short s_pullup   = 1 <<  0;  // and  2 but not keypad
static const unsigned short s_pulldown = 1 << 15;  // and 16 but not keypad and not a u16

//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "Init.h"
#include "Indicator.h"
#include "Mqtinator.h"
#include "Keypad.h"

#include "esp_log.h"    // ESP_LOGI()

#if (LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG)
#define EXPRD(expr) do { expr; } while(0);
#else
#define EXPRD(expr)
#endif

namespace {
const char *TAG = "keypad";
}

extern "C" {

unsigned long atoul( const char * str, const char ** end )
{
    unsigned long ret = 0;
    while (*str == ' ') ++str;
    if (*str != '0') {
        while ((*str >= '0') && (*str <= '9')) {
            ret *= 10;
            ret += *str++ & 0xf;
        }
    } else if (*++str == 'x') {
        for (char x = *++str | 0x20;  // upper case -> lower case
                ((x >= '0') && (x <= '9')) || ((x >= 'a') && (x <= 'f'));
                    x = *++str | 0x20) {
            ret <<= 4;
            ret |= (x + (((x >> 6) & 1) * (0x6a - 'a'))) & 0xf;
        }
    } else {
        while ((*str >= '0') && (*str <= '7')) {
            ret <<= 3;
            ret |= *str++ & 7;
        }
    }
    if (end)
        *end = str;
    return ret;
}

void OnFlash( const char * topic, const char * data )
{
    ESP_LOGD( TAG, "got \"%s\" \"%s\"", topic, data );
    Indicator::Instance().Access( *data & 1 );
}

void OnBlink( const char * topic, const char * data )
{
    ESP_LOGD( TAG, "got \"%s\" \"%s\"", topic, data );
    const char * end = 0;
    unsigned long pri, sec = 0;
    pri = atoul( data, & end );
    if (end)
        sec = atoul( end, 0 );
    Indicator::Instance().Blink( (uint8_t) (pri & 0xff), (uint8_t) (sec & 0xff) );
}

void OnSignal( const char * topic, const char * data )
{
    ESP_LOGD( TAG, "got \"%s\" \"%s\"", topic, data );
    const char * end = 0;
    unsigned long priSigMask, secSigMask = 0;
    priSigMask = atoul( data, & end );
    if (end)
        secSigMask = atoul( end, 0 );
    Indicator::Instance().SigMask( priSigMask, secSigMask );
}

void OnConnected( Mqtinator & mqtinator )
{
    mqtinator.Pub( "init", "1" );
}

}

extern "C" void app_main()
{
    Init::Init( GPIO_NUM_16 );  // red LED on GPIO2, green LED on GPIO16

    ESP_LOGD( TAG, "subscribe to flash, blink, signal..." ); EXPRD(vTaskDelay(1))
    {
        Mqtinator & mqtinator = Mqtinator::Instance();
        mqtinator.Sub( "flash", & OnFlash );
        mqtinator.Sub( "blink", & OnBlink );
        mqtinator.Sub( "signal", & OnSignal );
        mqtinator.OnConnected( & OnConnected );
    }

    ESP_LOGD( TAG, "Keypad..." ); EXPRD(vTaskDelay(1))
    Keypad keypad{ s_pullup, s_pulldown, s_col, NELEMENTS(s_col), s_row, NELEMENTS(s_row) };

    ESP_LOGD( TAG, "keypad.Run()" ); EXPRD(vTaskDelay(1))
    keypad.Run();
}
