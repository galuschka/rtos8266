/*
 * Indicator.cpp
 *
 *  Created on: 19.05.2020
 *      Author: holger
 */

#include "Indicator.h"

#include "esp_log.h"

const char *const TAG = "Indicator";

extern "C" void IndicatorTask( void * indicator )
{
    ((Indicator*) indicator)->Run();
}

Indicator::Indicator( gpio_num_t pinRed, gpio_num_t pinGreen ) :
        mPinRed { pinRed },
        mPinGreen{ pinGreen },
        mBlink { 0 },
        mBlinkOk { 0 },
        mSigMask { -1 },
        mTaskHandle { 0 },
        mSemaphore { 0 }
{
    gpio_config_t io_conf;

    io_conf.pin_bit_mask = (1 << mPinRed) | (1 << mPinGreen);
    io_conf.mode = GPIO_MODE_OUTPUT;       // set as output mode
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;    // disable pull-up mode
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;  // disable pull-down mode
    io_conf.intr_type = GPIO_INTR_DISABLE;      // disable interrupt

    gpio_config( &io_conf );    // configure GPIO with the given settings
    gpio_set_level( mPinRed, 0 );   // low active - switch on
    gpio_set_level( mPinGreen, 0 );   // low active - switch on
}

bool Indicator::Init()
{
    mSemaphore = xSemaphoreCreateBinary( );
    xTaskCreate( IndicatorTask, "Indicator", /*stack size*/1024, this, /*prio*/
            1, &mTaskHandle );
    if (!mTaskHandle) {
        ESP_LOGE( TAG, "xTaskCreate failed" );
        return false;
    }

    if (!mSemaphore) {
        ESP_LOGE( TAG, "xSemaphoreCreateBinary failed" );
        return false;
    }

    return true;
}

void Indicator::Indicate( Indicator::STATUS status )
{
    long sigMask = 0;
    switch (status) {
    case STATUS_ERROR:      // |#########|__|###|__|###|__|###|__|
        sigMask = 0x23232328;
        break;
    case STATUS_AP:         // |###########################|_|#|_|
        sigMask = 0x111d;
        break;
    case STATUS_CONNECT:    // |###############################|_|
        sigMask = 0x1f;
        break;
    case STATUS_IDLE:       // |_______________________________|#|
        sigMask = 0xf1;
        break;
    case STATUS_ACTIVE:     // |################|________________|
        sigMask = 0x88;
        break;
    }
    mBlink = 0;
    if (mSigMask == sigMask)
        return;
    mSigMask = sigMask;
    xSemaphoreGive( mSemaphore );
}

void Indicator::Blink( uint8_t num )
{
    mBlink = num;
    xSemaphoreGive( mSemaphore );
}

void Indicator::Steady( uint8_t on )
{
    mBlink = 0;
    mSigMask = on ? 0 : -1;
    xSemaphoreGive( mSemaphore );
}

void Indicator::Access( uint8_t ok )
{
    if (ok) {
        mBlinkOk = 1;
    } else {
        mBlink = 2;
    }
    xSemaphoreGive( mSemaphore );
}

void Indicator::Run()
{
    gpio_set_level( mPinGreen, 1 );  // low active - switch off
    int phase = 0;
    while (true) {
        if (mBlinkOk) {
            gpio_set_level( mPinGreen, 0 );
            vTaskDelay( configTICK_RATE_HZ / 4 );
            gpio_set_level( mPinGreen, 1 );
            while (--mBlinkOk) {
                vTaskDelay( configTICK_RATE_HZ / 8 );
                gpio_set_level( mPinGreen, 0 );
                vTaskDelay( configTICK_RATE_HZ / 4 );
                gpio_set_level( mPinGreen, 1 );
            }
        }
        if (mBlink) {
            do {
                gpio_set_level( mPinRed, 0 );
                vTaskDelay( configTICK_RATE_HZ / 16 );
                gpio_set_level( mPinRed, 1 );
                vTaskDelay( configTICK_RATE_HZ / 16 );
            } while (--mBlink);
            vTaskDelay( configTICK_RATE_HZ / 16 );
            phase = 0;  // restart old modus
        }

        if (mSigMask == 0) {
            gpio_set_level( mPinRed, 0 );   // low active - switch on
            xSemaphoreTake( mSemaphore, portMAX_DELAY );
            continue;
        }
        if (mSigMask == -1) {
            gpio_set_level( mPinRed, 1 );   // low active - switch off
            xSemaphoreTake( mSemaphore, portMAX_DELAY );
            continue;
        }

        if (++phase >= 8)
            phase = 0;

        int duration = (mSigMask >> (phase * 4)) & 0xf;
        if (!duration) {
            phase = 0;
            duration = mSigMask & 0xf;
        }

        gpio_set_level( mPinRed, phase & 1 );
        xSemaphoreTake( mSemaphore, (duration * configTICK_RATE_HZ) / 8 );
    }
}
