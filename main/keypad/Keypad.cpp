/*
 * Keypad.cpp
 *
 *  Created on: 07.01.2022
 *      Author: holger
 */

//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "Keypad.h"
#include "Indicator.h"
#include "Mqtinator.h"

#include "esp_log.h"        // ESP_LOGI()

#include "FreeRTOSConfig.h"
#include "FreeRTOS.h"
#include "task.h"

#include "portmacro.h"
#include "esp8266/gpio_struct.h"

const char *const TAG = "Keypad";

                          // col: 0,1,2,     3
static const u8 s_num[4][4] = { { 1,2,3,   0xA },   // row 0
                                { 4,5,6,   0xB },   // row 1
                                { 7,8,9,   0xC },   // row 2
                              { 0xF,0,0xE, 0xD } }; // row 3

//@formatter:off
Keypad::Keypad( const u8 * col, u8 nofCols,
                const u8 * row, u8 nofRows )
              : mCol        { col },
                mRow        { row },
                mNofCols    { nofCols },
                mNofRows    { nofRows }
//@formatter:on
{
    mAllCols = 0;
    for (u8 i = 0; i < mNofCols; ++i)
        mAllCols |= 1 << mCol[i];
    mAllRows = 0;
    for (u8 i = 0; i < mNofRows; ++i)
        mAllRows |= 1 << mRow[i];

    // The GPIO of esp8266 can not be pulled down except RTC GPIO (16) which can not be pulled up.

    mOutConf.mode         = GPIO_MODE_DISABLE;      // disable when idle
    mOutConf.pull_up_en   = GPIO_PULLUP_DISABLE;    // pull-up mode
    mOutConf.pull_down_en = GPIO_PULLDOWN_DISABLE;  // pull-down mode
    mOutConf.intr_type    = GPIO_INTR_DISABLE;      // disable interrupt

    mInConf.mode         = GPIO_MODE_INPUT;         // input
    mInConf.pull_up_en   = GPIO_PULLUP_ENABLE;      // pull-up mode
    mInConf.pull_down_en = GPIO_PULLDOWN_DISABLE;   // pull-down mode
    mInConf.intr_type    = GPIO_INTR_DISABLE;       // disable interrupt

    RowsOutColsIn();
}

void Keypad::RowsOutColsIn()
{
    mInConf.pin_bit_mask = mAllCols;
    gpio_config( &mInConf ); // cols are input
    mOutConf.pin_bit_mask = mAllRows;
    gpio_config( &mOutConf ); // and rows are output
}

void Keypad::OnSequence( const char * seq )   // pause after sequence / seq is hex string
{
    ESP_LOGI( TAG, "seq %s", seq );

    Mqtinator::Instance().Pub( "sequence", seq );
}

void Keypad::OnKeyPress( u8 num )
{
    char c = num + '0';
    switch (num) {
        case 10: c = 'A'; break;
        case 11: c = 'B'; break;
        case 12: c = 'C'; break;
        case 13: c = 'D'; break;
        case 14: c = '#'; break;
        case 15: c = '*'; break;
    }
    ESP_LOGI( TAG, "key %c - mask = %#x", c, 1 << num );

    static char s_buf[2] = "x";
    s_buf[0] = c;
    Mqtinator::Instance().Pub( "key", s_buf );
}

void Keypad::OnMultiKey( u16 mask )
{
    ESP_LOGI( TAG, "key ev - mask = %#x", mask );

    static char s_buf[8];
    char * bp = & s_buf[sizeof(s_buf) - 1];
    *bp = 0;
    do {
        *--bp = (mask & 0xf) + '0' + ((((mask & 0xf) / 10) * ('A' - '0' - 10)));
        mask >>= 4;
    } while (mask && (bp > s_buf));
    if (bp >= &s_buf[2]) {
        *--bp = 'x';  // leading 0x
        *--bp = '0';
    }
    Mqtinator::Instance().Pub( "mask", bp );
}

void Keypad::OnRelease()
{
    ESP_LOGD( TAG, "key rel. (mask = 0)" );
    Mqtinator::Instance().Pub( "mask", "0" );
}

void Keypad::Run()
{
    uint8_t const longDelay  = configTICK_RATE_HZ / 10;
    uint8_t const shortDelay = configTICK_RATE_HZ / 50;
    uint8_t delay = longDelay;
    TickType_t lastRelease = 0;

    char seq[16];
    char * const seqend = & seq[sizeof(seq) - 1];
    char * sp = &seq[-1];

    while (true) {
        vTaskDelay( delay );

        u16 numMask = 0;
        u8 num = 0;  // any (not checked when numMask == 0)

        GPIO.enable_w1ts = mAllRows;
        {
            u16 colMatch;
            u16 inPrev;
            u16 in;

            GPIO.out_w1tc = mAllRows;   // low active
            ets_delay_us(300);
            in = GPIO.in & 0xffff;
            ets_delay_us(100);
            in &= (GPIO.in & 0xffff);  // just need to know: is any pin pulled down
            colMatch = ~in & mAllCols;

            if (colMatch) {  // at least one key pressed
                GPIO.out_w1ts = mAllRows & ~(1 << mRow[0]);   // high inactive
                for (u8 r = 0; (r < mNofRows) /*&& (r < 1)*/; ++r) {
                    GPIO.out_w1tc = 1 << mRow[r];   // low active
                    ets_delay_us(300);
                    in = GPIO.in & 0xffff;
                    do {
                        ets_delay_us(100);
                        inPrev = in;
                        in = GPIO.in & 0xffff;
                    } while ((in ^ inPrev) & mAllCols);
                    GPIO.out_w1ts = 1 << mRow[r];   // high inactive

                    colMatch = ~in & mAllCols;

                    if (! colMatch)
                        continue;

                    ESP_LOGD( TAG, "out 0%06o: ~0%06o & 0%02o == 0%02o", 1 << mRow[r], in, mAllCols, colMatch );

                    for (u8 c = 0; (c < mNofCols) && colMatch; ++c)
                        if (colMatch & (1 << mCol[c])) {
                            num = s_num[r][c];
                            numMask |= 1 << num;
                        } // for c
                } // for r
                if (! numMask) {
                    ESP_LOGW( TAG, "fast detection said 'at least one key pressed' - fix fast detection" );
                }
            } // if colMatch
            else
                GPIO.out_w1ts = mAllRows;   // all inactive
        }
        GPIO.enable_w1tc = mAllRows;

        if (numMask == mNumMask) {
            if (lastRelease) {
                TickType_t now = xTaskGetTickCount();
                TickType_t diff = now - lastRelease;
                if (diff >= (configTICK_RATE_HZ / 1)) {
                    // one second pause: send sequence
                    if ((sp > seq) && (sp < seqend)) {
                        *++sp = 0;
                        OnSequence( seq );
                    }
                    lastRelease = 0;
                    sp = &seq[-1];
                    delay = longDelay;
                }
            }
            continue;
        }

        if (! numMask) {
            lastRelease = xTaskGetTickCount();
            if (! lastRelease)
                lastRelease = 1;
            Indicator::Instance().Steady( 0 );
            OnRelease();
        } else {
            lastRelease = 0;
            if (! mNumMask) {
                Indicator::Instance().Steady( 1 );
                if (numMask == (1 << num)) {
                    if (sp < seqend) {
                        *++sp = num + '0' + ((num / 10) * ('A' - '0' - 10));
                    }
                    OnKeyPress( num );
                } else
                    OnMultiKey( numMask );
            } else {
                OnMultiKey( numMask );
            }
            delay = shortDelay;
        }

        mNumMask = numMask;
    }
}
