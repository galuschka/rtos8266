/*
 * Relay.cpp
 *
 *  Created on: 19.05.2020
 *      Author: galuschka
 */

#include "Relay.h"

#include "esp_log.h"        // ESP_LOGI()

#include "FreeRTOSConfig.h"
#include "FreeRTOS.h"
#include "task.h"

const char *const TAG = "Relay";

namespace
{
TickType_t now()
{
    return xTaskGetTickCount();
}
}

//@formatter:off
Relay::Relay( gpio_num_t pin, bool openDrain, bool lowActive )
              : Pin         { pin },
                Mode        { MODE_AUTO },
                Active      { false },
                AutoActive  { false },
                LowActive   { lowActive },
                Ticks       { 0 },
                SwitchTime  { 0 }
{
//@formatter:on

gpio_config_t io_conf;

io_conf.pin_bit_mask = (1 << Pin);// the pin
if (openDrain)
io_conf.mode = GPIO_MODE_OUTPUT_OD;// open drain for single relay
else
io_conf.mode = GPIO_MODE_OUTPUT;// set as output mode
io_conf.pull_up_en = GPIO_PULLUP_DISABLE;// disable pull-up mode
io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;// disable pull-down mode
io_conf.intr_type = GPIO_INTR_DISABLE;// disable interrupt

gpio_config( &io_conf );// configure GPIO with the given settings

gpio_set_level( Pin, LowActive );
}

void Relay::SetMode( Relay::GenMode newMode )
{
    Mode = newMode;
    switch (Mode) {
    case MODE_OFF:
        RealOn( false );
        break;
    case MODE_AUTO:
        RealOn( AutoActive );
        break;
    case MODE_ON:
        RealOn( true );
        break;
    }
}

void Relay::AutoOn( bool on )
{
    AutoActive = on;
    if (Mode == MODE_AUTO)
        RealOn( AutoActive );
    else
        ESP_LOGI( TAG, "would switch %s", on ? "on" : "off" );
}

void Relay::RealOn( bool on )
{
    if (Active == on)
        return;

    gpio_set_level( Pin, on ^ LowActive );
    TickType_t yet = now();
    if (on) {
        SwitchTime = yet;
        Active = true;
        ESP_LOGI( TAG, "switched on" );
    } else {
        Ticks += (yet - SwitchTime);
        Active = false;
        SwitchTime = yet;
        ESP_LOGI( TAG, "switched off" );
    }
}

unsigned long Relay::TotalOn()
{
    if (Active)
        return Ticks + (now() - SwitchTime);
    return Ticks;
}

unsigned long Relay::TotalOnSecs()
{
    return (TotalOn() / configTICK_RATE_HZ);
}
