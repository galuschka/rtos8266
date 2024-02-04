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
    bool Go();      // trigger update
    bool Confirm(); // trigger continue: testing -> stable

    bool         SetUri( const char * uri );      // set in vfs and for next download
    const char * GetUri() { return mUri; };       // get uri to be used for download
    const char * GetMsg() { return mMsg; };       // status message
    uint8_t      Progress() { return mProgress; };  // to be used for progress bar

    static Updator & Instance();
    bool Init();

    Updator() {};
    void Run();  // internal thread routine, but must be public
private:
    void Update();
    void ReadUri();

    uint8_t           mProgress   {0};   // 0: idle / 1:..94,96..98: progress / 95: confirm / 99: failed / 100: success
    char              mUri[80]    {""};  // http://my.really.long.uri:8888/to/firmware/location/is/67/in/length
    const char      * mMsg        {0};   // status information
    TaskHandle_t      mTaskHandle {0};
    SemaphoreHandle_t mSemaphore  {0};
};

#endif /* MAIN_UPDATOR_H_ */


