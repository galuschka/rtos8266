/*
 * Input.h
 */
#pragma once

#include <FreeRTOS.h>
#include <task.h>           // TaskHandle_t
#include <driver/gpio.h>    // gpio_num_t

class Input
{
public:
    typedef void (*callback_t)( void * userarg );

    Input( gpio_num_t pin ) : mPin{pin} {}

    bool Init();
    void Run();

    void SetCallback( callback_t callback, void * userarg ) {
        mUserArg = userarg;
        mCallback = callback;
    }

    bool Active() { return mActive; };

private:
    gpio_num_t   mPin;
    bool         mActive    { false };
    TaskHandle_t mTaskHandle{ nullptr };
    callback_t   mCallback  { nullptr };
    void       * mUserArg   { nullptr };
};
