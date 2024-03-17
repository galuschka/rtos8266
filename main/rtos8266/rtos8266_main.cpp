/*
 * rtos8266_main.cpp - just basic features: setup wifi paramter and update
 */

#include "Init.h"

#include "FreeRTOS.h"
#include "task.h"

extern "C" void app_main()
{
    Init::Init();

    while (true) {
        vTaskDelay( portMAX_DELAY );
    }
}
