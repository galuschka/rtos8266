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
    }

    ESP_LOGD( TAG, "Keypad..." ); EXPRD(vTaskDelay(1))
    Keypad keypad{ s_col, NELEMENTS(s_col), s_row, NELEMENTS(s_row) };

    ESP_LOGD( TAG, "keypad.Run()" ); EXPRD(vTaskDelay(1))
    keypad.Run();
}
