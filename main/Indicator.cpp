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

Indicator::Indicator( gpio_num_t pin ) :
        mPin { pin }, mStatus { STATUS_ERROR }, mSigMask { 0xf0 }, mTaskHandle { 0 }, mSemaphore {
                0 }
{
    gpio_config_t io_conf;

    io_conf.pin_bit_mask = (1 << mPin);
    io_conf.mode = GPIO_MODE_OUTPUT;       // set as output mode
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;    // disable pull-up mode
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;  // disable pull-down mode
    io_conf.intr_type = GPIO_INTR_DISABLE;      // disable interrupt

    gpio_config( &io_conf );    // configure GPIO with the given settings
    gpio_set_level( mPin, 0 );   // low active - switch on
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
    if (mStatus == status)
        return;
    mStatus = status;
    switch (mStatus) {
    case STATUS_ERROR:      // |##################################...
        mSigMask = 0;
        break;
    case STATUS_AP:         // |###########################|_|#|_|
        mSigMask = 0x111d;
        break;
    case STATUS_CONNECT:    // |###############################|_|
        mSigMask = 0x1f;
        break;
    case STATUS_IDLE:       // |_______________________________|#|
        mSigMask = 0xf1;
        break;
    case STATUS_ACTIVE:     // |################|________________|
        mSigMask = 0x88;
        break;
    }
    xSemaphoreGive( mSemaphore );
}

void Indicator::Blink( uint8_t num )
{
    mBlink = num;
    xSemaphoreGive( mSemaphore );
}

void Indicator::Run()
{
    int phase = 0;
    while (true) {
        if (mBlink) {
            do {
                gpio_set_level( mPin, 0 );
                xSemaphoreTake( mSemaphore, configTICK_RATE_HZ / 16 );
                gpio_set_level( mPin, 1 );
                xSemaphoreTake( mSemaphore, configTICK_RATE_HZ / 16 );
            } while (--mBlink);
        }

        if (!mSigMask) {
            gpio_set_level( mPin, 0 );   // low active - switch on
            xSemaphoreTake( mSemaphore, portMAX_DELAY );
        }

        if (++phase > 3)
            phase = 0;

        int duration = (mSigMask >> (phase * 4)) & 0xf;
        if (!duration)
            continue;

        gpio_set_level( mPin, phase & 1 );
        xSemaphoreTake( mSemaphore, (duration * configTICK_RATE_HZ) / 8 );
    }
}
