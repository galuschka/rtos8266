/*
 * Relay.h
 *
 *  Created on: 19.05.2020
 *      Author: galuschka
 */

#ifndef MAIN_RELAY_H_
#define MAIN_RELAY_H_

#include "FreeRTOS.h"
#include "portmacro.h"
#include "driver/gpio.h"    // gpio_num_t

class Relay
{
public:
    enum GenMode
    {
        MODE_OFF,   // stay off (regardless on Switch() calls)
        MODE_AUTO,  // automatic control enabled -> Switch(x) will have effect
        MODE_ON,    // manual switch
    };
    Relay( gpio_num_t pin, bool openDrain = false, bool lowActive = false );

    void SetMode( GenMode newMode );
    void AutoOn( bool on );  // switch on/off by controlling module

    unsigned long TotalOn();      // total ON time in ticks
    unsigned long TotalOnSecs();  // total ON time in secs

private:
    void RealOn( bool on );  // internal switch method (-> sets Active)

    gpio_num_t Pin;            // pin, connected to the relay
    GenMode Mode;           // OFF/AUTO/ON
    bool Active;         // "active" status of the pin
    bool AutoActive;     // set by AutoOn()
    bool LowActive;      // inverse logic
    TickType_t Ticks;          // total running time in ticks
    TickType_t SwitchTime;     // tick value, when switched
};

#endif /* MAIN_RELAY_H_ */
