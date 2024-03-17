/*
 * rgb.h - interface to listen for mqtt message
 */
#pragma once

#include <FreeRTOS.h>
#include <task.h>

#include <stdint.h>
#include <string>

class Fader;

class RGB
{
    RGB();
public:
    RGB( Fader & fader, uint16_t dzDevIdx );

    bool Start();
    void Run();
    void Subscription( const char * topic, const char * data );
private:
    void HandleInput();

    Fader     & mFader;
    uint16_t    mDevIdx;

    std::string mTopic{};
    std::string mData{};

    TaskHandle_t mTaskHandle{};
};
