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
        Pin { pin }, Status { STATUS_ERROR }, SigMask { 0xf0 }, TaskHandle { 0 }, Semaphore {
                0 }
{
    gpio_config_t io_conf;

    io_conf.pin_bit_mask = (1 << Pin);
    io_conf.mode = GPIO_MODE_OUTPUT;       // set as output mode
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;    // disable pull-up mode
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;  // disable pull-down mode
    io_conf.intr_type = GPIO_INTR_DISABLE;      // disable interrupt

    gpio_config( &io_conf );    // configure GPIO with the given settings
    gpio_set_level( Pin, 0 );   // low active - switch on
}

bool Indicator::Init()
{
    xTaskCreate( IndicatorTask, "Indicator", /*stack size*/2048, this, /*prio*/
            1, &TaskHandle );
    if (!TaskHandle) {
        ESP_LOGE( TAG, "xTaskCreate failed" );
        return false;
    }

    Semaphore = xSemaphoreCreateBinary( );

    if (!Semaphore) {
        ESP_LOGE( TAG, "xSemaphoreCreateBinary failed" );
        return false;
    }

    return true;
}

void Indicator::Indicate( Indicator::STATUS status )
{
    if (Status == status)
        return;
    Status = status;
    switch (Status) {
    case STATUS_ERROR:      // |##################################...
        SigMask = 0;
        break;
    case STATUS_AP:         // |###########################|_|#|_|
        SigMask = 0x111d;
        break;
    case STATUS_CONNECT:    // |###############################|_|
        SigMask = 0x1f;
        break;
    case STATUS_IDLE:       // |_______________________________|#|
        SigMask = 0xf1;
        break;
    case STATUS_ACTIVE:     // |################|________________|
        SigMask = 0x88;
        break;
    }
    xSemaphoreGive( Semaphore );
}

void Indicator::Run()
{
    int phase = 0;
    while (true) {
        if (!SigMask) {
            gpio_set_level( Pin, 0 );   // low active - switch on
            xSemaphoreTake( Semaphore, portMAX_DELAY );
        }

        if (++phase > 3)
            phase = 0;

        int duration = (SigMask >> (phase * 4)) & 0xf;
        if (!duration)
            continue;

        gpio_set_level( Pin, phase & 1 );
        xSemaphoreTake( Semaphore, (duration * configTICK_RATE_HZ) / 8 );
    }
}
