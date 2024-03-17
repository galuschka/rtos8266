/*
 * Swizz.h
 */

#pragma once

#include <FreeRTOS.h>
#include <task.h>

#include <vector>

#include "nvs.h"  // nvs_handle

class Relay;
class Indicator;

class Swizz
{
public:
    Swizz( Relay relay[], size_t nofRelays );

    void Run();  // the thread function (call in main)

    void ReadParam();
    void Setup( struct httpd_req * req, bool post = false );

    void SwitchRelay( const char * topic, const char * data );
    void WdRequest(   const char * topic, const char * data );

private:
    void WriteParam();

    Relay                 * mRelay;     // array of N elements
    std::vector<uint16_t>   mDzIdx;     // domoticz device index for each relay (0: not used by dz)
    uint8_t const           mNofRelays; // array size of mRelay and size of mDzIdx
    TaskHandle_t            mTaskHandle; // to be used to notify
};
