/*
 * Input.cpp
 */

//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "Input.h"

#include <driver/gpio.h>    // gpio_num_t
#include <esp_log.h>
#include "esp8266/gpio_struct.h"


const char * const TAG = "Input";


extern "C" void InputTask( void * input )
{
    ((Input*) input)->Run();
}

bool Input::Init()
{
    gpio_config_t conf;
    conf.pin_bit_mask = 1 << mPin;
    conf.mode         = GPIO_MODE_INPUT;        // input
    conf.pull_up_en   = GPIO_PULLUP_ENABLE;     // pull-up mode
    conf.pull_down_en = GPIO_PULLDOWN_DISABLE;  // pull-down off
    conf.intr_type    = GPIO_INTR_DISABLE;      // disable interrupt
    gpio_config( &conf ); 

    xTaskCreate( InputTask, "Input", /*stack size*/1024, this, /*prio*/1, &mTaskHandle );
    if (!mTaskHandle) {
        ESP_LOGE( TAG, "xTaskCreate failed" );
        return false;
    }
    return true;
}

void Input::Run()
{
    while (true)
    {
        vTaskDelay( configTICK_RATE_HZ / 10 );
        
        bool nowActive = ! ((GPIO.in >> mPin) & 1);
        if (mActive != nowActive) {
            mActive = nowActive;
            ESP_LOGE( TAG, "pin %d %sactivated", mPin, mActive ? "" : "de" );
            if (nowActive && mCallback)
                mCallback( mUserArg );
        }
    }
}
