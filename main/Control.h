/*
 * Control.h
 *
 *  Created on: 19.05.2020
 *      Author: holger
 */

#ifndef MAIN_CONTROL_H_
#define MAIN_CONTROL_H_

class AnalogReader;
class Relay;
class Indicator;

class Control
{
public:
    typedef unsigned short value_t;

    Control( AnalogReader & reader, Relay & relay, value_t thresOff,
            value_t thresOn );
    void Run( Indicator & indicator );  // the thread function (call in main)

    void SetThreshold( value_t thresOff, value_t thresOn );
    void SetTimeLimits( int minOffTicks, int minOnTicks, int maxOnTicks );

    value_t GetThresOff()
    {
        return ThresOff;
    }
    value_t GetThresOn()
    {
        return ThresOn;
    }

    int GetMinOffTicks()
    {
        return MinOffTicks;
    }
    int GetMinOnTicks()
    {
        return MinOnTicks;
    }
    int GetMaxOnTicks()
    {
        return MaxOnTicks;
    }

private:
    AnalogReader &mReader;
    Relay &mRelay;
    value_t ThresOff;       // switch off, when passing threshold
    value_t ThresOn;        // switch on, when passing threshold
    int MinOffTicks;    // stay off at least ... ticks
    int MinOnTicks;     // stay on at least ... ticks
    int MaxOnTicks;     // switch off at least after ... ticks
};

#endif /* MAIN_CONTROL_H_ */
