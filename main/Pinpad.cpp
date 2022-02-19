/*
 * Pinpad.cpp
 *
 *  Created on: 07.01.2022
 *      Author: galuschka
 */

//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "Pinpad.h"
#include "Indicator.h"

#include "esp_log.h"        // ESP_LOGI()

#include "FreeRTOSConfig.h"
#include "FreeRTOS.h"
#include "task.h"

#include "portmacro.h"
#include "esp8266/gpio_struct.h"

const char *const TAG = "Pinpad";

                          // col: 0,1,2,     3
static const u8 s_num[4][4] = { { 1,2,3,   0xA },   // row 0
                                { 4,5,6,   0xB },   // row 1
                                { 7,8,9,   0xC },   // row 2
                              { 0xF,0,0xE, 0xD } }; // row 3

//@formatter:off
Pinpad::Pinpad( const u8 * col, u8 nofCols,
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

void Pinpad::RowsOutColsIn()
{
    mInConf.pin_bit_mask = mAllCols;
    gpio_config( &mInConf ); // cols are input
    mOutConf.pin_bit_mask = mAllRows;
    gpio_config( &mOutConf ); // and rows are output
}

void Pinpad::OnKeyPress( u8 num )
{
    ESP_LOGI( TAG, "key %2d - mask = 0%o", num, 1 << num );
}

void Pinpad::OnMultiKey( u16 mask )
{
    ESP_LOGI( TAG, "key ev - mask = 0%o", mask );
}

void Pinpad::OnRelease()
{
    ESP_LOGD( TAG, "key rel. (mask = 0)" );
}

void Pinpad::Run( Indicator & indicator )
{
    while (true) {
        vTaskDelay( configTICK_RATE_HZ / 100 ); // / 100

        u16 numMask = 0;
        u8 num = 0;  // any (not checked when numMask == 0)

        u16 colMatch;
        u16 inPrev;
        u16 in;

        GPIO.enable_w1ts = mAllRows;
        for (u8 r = 0; (r < mNofRows) /*&& (r < 1)*/; ++r) {
            // GPIO.enable_w1ts = 1 << mRow[r];
            GPIO.out_w1tc = 1 << mRow[r];   // low active
            ets_delay_us(700);
            in = GPIO.in & 0xffff;
            do {
                ets_delay_us(100);
                inPrev = in;
                in = GPIO.in & 0xffff;
            } while (in != inPrev);
            GPIO.out_w1ts = 1 << mRow[r];   // high inactive
            // GPIO.enable_w1tc = 1 << mRow[r];
            colMatch = ~in & mAllCols;

            if (! colMatch)
                continue;

            ESP_LOGD( TAG, "out 0%06o: ~0%06o & 0%02o == 0%02o", 1 << mRow[r], in, mAllCols, colMatch );

            for (u8 c = 0; (c < mNofCols) && colMatch; ++c)
                if (colMatch & (1 << mCol[c])) {
                    num = s_num[r][c];
                    numMask |= 1 << num;
                }
        }
        GPIO.enable_w1tc = mAllRows;

        if (numMask == mNumMask)
            continue;

        if (! numMask) {
            indicator.Steady( 0 );
            OnRelease();
        } else {
            if (! mNumMask) {
                indicator.Steady( 1 );
                if (numMask == (1 << num))
                    OnKeyPress( num );
                else
                    OnMultiKey( numMask );
            } else {
                OnMultiKey( numMask );
            }
        }

        mNumMask = numMask;
    }
}
