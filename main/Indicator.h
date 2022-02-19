/*
 * Indicator.h
 *
 *  Created on: 19.05.2020
 *      Author: holger
 */

#ifndef MAIN_INDICATOR_H_
#define MAIN_INDICATOR_H_

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "driver/gpio.h"    // gpio_num_t

class Indicator
{
public:
    enum STATUS
    {
        STATUS_ERROR,       // any serious error
        STATUS_AP,          // access point mode - awaiting config
        STATUS_CONNECT,     // trying to connect in station mode
        STATUS_IDLE,        // station mode and relay is switched off
        STATUS_ACTIVE,      // station mode and relay is switched on
    };
    Indicator( gpio_num_t pinRed, gpio_num_t pinGreen );
    void Indicate( STATUS status );
    void Blink( uint8_t num );  // num = number of times on
    void Steady( uint8_t on );  // steady on/off
    void Access( uint8_t ok );  // false: 2x red / true: 1x green
    bool Init();
    void Run();  // internal thread routine
private:
    gpio_num_t mPinRed;
    gpio_num_t mPinGreen;
    uint8_t mBlink;
    uint8_t mBlinkOk;
    long mSigMask;
    TaskHandle_t mTaskHandle;
    SemaphoreHandle_t mSemaphore;
};

#endif /* MAIN_INDICATOR_H_ */
