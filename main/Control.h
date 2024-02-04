/*
 * Control.h
 *
 *  Created on: 19.05.2020
 *      Author: holger
 */

#ifndef MAIN_CONTROL_H_
#define MAIN_CONTROL_H_

#include "nvs.h"  // nvs_handle

class AnalogReader;
class Relay;
class Indicator;

class Control
{
public:
    typedef unsigned short thres_t;
    typedef unsigned long  timo_t;

    Control( AnalogReader & reader, Relay & relay, thres_t thresOff,
            thres_t thresOn );
    void Run( Indicator & indicator );  // the thread function (call in main)

    void ReadParam();
    void Setup( struct httpd_req * req );
    // void SetThreshold( thres_t thresOff, thres_t thresOn );
    // void SetTimeLimits( int minOffTicks, int minOnTicks, int maxOnTicks );
    //@formatter:off
    thres_t GetThresOff() { return ThresOff; }
    thres_t GetThresOn()  { return ThresOn; }
    timo_t GetMinOffTicks() { return MinOffTicks; }
    timo_t GetMinOnTicks()  { return MinOnTicks; }
    timo_t GetMaxOnTicks()  { return MaxOnTicks; }
    //@formatter:on

private:
    void WriteParam();
    void SetU16( nvs_handle nvs, const char * key, uint16_t val );
    void SetU32( nvs_handle nvs, const char * key, uint32_t val );

    AnalogReader &mReader;
    Relay &mRelay;
    thres_t ThresOff;       // switch off, when passing threshold
    thres_t ThresOn;        // switch on, when passing threshold
    timo_t MinOffTicks;    // stay off at least ... ticks
    timo_t MinOnTicks;     // stay on at least ... ticks
    timo_t MaxOnTicks;     // switch off at least after ... ticks
};

#endif /* MAIN_CONTROL_H_ */
