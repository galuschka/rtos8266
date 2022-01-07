/*
 * Pinpad.cpp
 *
 *  Created on: 07.01.2022
 *      Author: galuschka
 */

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

    mInConf.mode         = GPIO_MODE_INPUT;         // set as input mode
    mInConf.pull_up_en   = GPIO_PULLUP_DISABLE;     // disable pull-up mode
    mInConf.pull_down_en = GPIO_PULLDOWN_ENABLE;    // enable pull-down mode
    mInConf.intr_type    = GPIO_INTR_DISABLE;       // disable interrupt

    mOutConf.mode         = GPIO_MODE_OUTPUT;        // set as output mode
    mOutConf.pull_up_en   = GPIO_PULLUP_DISABLE;     // disable pull-up mode
    mOutConf.pull_down_en = GPIO_PULLDOWN_DISABLE;   // disable pull-down mode
    mOutConf.intr_type    = GPIO_INTR_DISABLE;       // disable interrupt

    RowsInColsOut();
}

void Pinpad::RowsInColsOut()
{
    mInConf.pin_bit_mask = mAllRows;
    gpio_config( &mInConf ); // rows are input
    mOutConf.pin_bit_mask = mAllCols;
    gpio_config( &mOutConf ); // and cols are output
}

void Pinpad::TempColsInRowsOut()
{
    mInConf.pin_bit_mask = mAllCols;
    gpio_config( &mInConf ); // cols are input
    mOutConf.pin_bit_mask = mAllRows;
    gpio_config( &mOutConf ); // and rows are output
}

void Pinpad::OnKeyPress( u8 num )
{
    ESP_LOGI( TAG, "key %2d - mask = %04x", num, 1 << num );
}

void Pinpad::OnMultiKey( u16 mask )
{
    ESP_LOGI( TAG, "key ev - mask = %04x", mask );
}

void Pinpad::OnRelease()
{
    ESP_LOGI( TAG, "key rel. (mask = 0)" );
}

void Pinpad::Run( Indicator & indicator )
{
    while (true) {
        vTaskDelay( configTICK_RATE_HZ / 100 );

        GPIO.out_w1ts |= mAllCols;
        vTaskDelay(1);
        u16 rowMatch = GPIO.in & mAllRows;
        if (! rowMatch) {
            GPIO.out_w1tc |= mAllCols;
            if (mNumMask) {
                mNumMask = 0;
                OnRelease();
            }
            continue;
        }

        GPIO.out_w1tc |= mAllCols;

        TempColsInRowsOut();

        u16 numMask = 0;
        u8 num = 0;  // any (not checked when numMask == 0)

        for (u8 r = 0; (r < mNofRows) && rowMatch; ++r) {
            if (! (rowMatch & (1 << r)))
                continue;
            rowMatch &= ~(1 << r);

            GPIO.out_w1ts |= 1 << mRow[r];
            vTaskDelay(1);
            u16 colMatch = GPIO.in & mAllCols;
            if (colMatch) {
                for (u8 c = 0; (c < mNofCols) && colMatch; ++c) {
                    if (! (colMatch & (1 << c)))
                        continue;
                    colMatch &= ~(1 << c);

                    num = s_num[r][c];
                    numMask |= 1 << num;
                }
            }
            GPIO.out_w1tc |= 1 << mRow[r];
        }

        RowsInColsOut();  // undo fine check

        if (numMask == mNumMask)
            continue;

        if (! numMask)  // should not happen (race condition)
            OnRelease();
        else if ((! mNumMask) && (numMask == (1 << num)))
            OnKeyPress( num );
        else
            OnMultiKey( numMask );
        mNumMask = numMask;
    }
}
