/*
 * Control.cpp
 *
 *  Created on: 19.05.2020
 *      Author: holger
 */

#include "Control.h"

#include "FreeRTOSConfig.h"

#include "AnalogReader.h"
#include "Relay.h"
#include "Indicator.h"

namespace
{
TickType_t now()
{
    return xTaskGetTickCount();
}

unsigned long expiration( TickType_t ticks )
{
    TickType_t exp = xTaskGetTickCount() + ticks;
    if (!exp)
        --exp;
    return exp;
}
}

//@formatter:off
Control::Control( AnalogReader & reader, Relay & relay, value_t thresOff, value_t thresOn )
                : mReader       { reader },
                  mRelay        { relay },
                  ThresOff      { thresOff },
                  ThresOn       { thresOn },
                  MinOffTicks   { configTICK_RATE_HZ },
                  MinOnTicks    { configTICK_RATE_HZ * 15 },
                  MaxOnTicks    { configTICK_RATE_HZ * 60 * 60 }
{
//@formatter:on

mRelay.AutoOn( false );
mRelay.SetMode( Relay::MODE_AUTO );
}

void Control::Run( Indicator & indicator )
{
    indicator.Indicate( Indicator::STATUS_IDLE );
    TickType_t exp = 0; // set, when AutoOn(true) called -> expiration to switch off
    while (true) {
        bool toSwitch = false;

        if (exp) {
            signed long diff = exp - now();
            toSwitch = (diff <= 0);
            if (!toSwitch) {
                if (ThresOff > ThresOn)
                    toSwitch = mReader.Average( 10 ) >= ThresOff;
                else
                    toSwitch = mReader.Average( 10 ) <= ThresOff;
            }
            if (toSwitch) {
                mRelay.AutoOn( false );
                indicator.Indicate( Indicator::STATUS_IDLE );
                exp = 0;
                if (MinOffTicks) {
                    vTaskDelay( MinOffTicks );
                    continue;
                }
            }
        } else {
            if (ThresOff > ThresOn)
                toSwitch = mReader.Average( 10 ) <= ThresOn;
            else
                toSwitch = mReader.Average( 10 ) >= ThresOn;
            if (toSwitch) {
                mRelay.AutoOn( true );
                indicator.Indicate( Indicator::STATUS_ACTIVE );
                exp = expiration( MaxOnTicks );
                if (MinOnTicks) {
                    vTaskDelay( MinOnTicks );
                    continue;
                }
            }
        }

        vTaskDelay( configTICK_RATE_HZ / 10 );
    }
}

void Control::SetThreshold( Control::value_t thresOff,
        Control::value_t thresOn )
{
    ThresOff = thresOff;
    ThresOn = thresOn;
}

void Control::SetTimeLimits( int minOffTicks, int minOnTicks, int maxOnTicks )
{
    MinOffTicks = minOffTicks;
    MinOnTicks = minOnTicks;
    MaxOnTicks = maxOnTicks;
}
