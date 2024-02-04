/*
 * Updator.h
 *
 *  Created on: 19.05.2020
 *      Author: holger
 */

#ifndef MAIN_UPDATOR_H_
#define MAIN_UPDATOR_H_

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "driver/gpio.h"    // gpio_num_t

class Updator
{
public:
    void Run();  // internal thread routine
    void Go();   // trigger update

    const char * Url() { return mUrl; };  // from where we will download the binary

    static Updator & Instance();
    bool Init( const char * url );

    Updator();
private:
    void Update();

    bool         mGo;
    const char * mUrl;
    TaskHandle_t mTaskHandle;
    SemaphoreHandle_t mSemaphore;
};

#endif /* MAIN_UPDATOR_H_ */


