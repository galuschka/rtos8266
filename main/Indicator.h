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
    Indicator( gpio_num_t pin );
    void Indicate( STATUS status );
    bool Init();
    void Run();  // internal thread routine
private:
    gpio_num_t Pin;
    STATUS Status;
    long SigMask;
    TaskHandle_t TaskHandle;
    SemaphoreHandle_t Semaphore;
};

#endif /* MAIN_INDICATOR_H_ */
