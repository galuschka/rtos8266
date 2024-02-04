/*
 * Control.h
 *
 *  Created on: 19.05.2020
 *      Author: holger
 */

#ifndef MAIN_CONTROL_H_
#define MAIN_CONTROL_H_

#include <FreeRTOS.h>
#include <semphr.h>

#include "AnalogReader.h"  // AnalogReader::INV_VALUE

#include "nvs.h"  // nvs_handle

class AnalogReader;
class Relay;
class Indicator;
class Input;

class Control
{
public:
    enum MODE {
        MODE_AUTO_OFF   = 0,  // normal mode - auto off
        MODE_AUTO_ON    = 1,  // normal mode - auto on
        MODE_AUTO_PAUSE = 2,  // forced pause

        MODE_TEST1_END  = 3,  // test phase expired -> off
        MODE_TEST2_END  = 4,  // test phase expired -> off
        MODE_TEST1      = 5,  // test relais 1 (3 secs, then -> MODE_TEST1_END)
        MODE_TEST2      = 6,  // test relais 2 (3 secs, then -> MODE_TEST2_END)
        MODE_TEST3      = 7,  // test relais 1 + 2 (3 secs, then -> MODE_AUTO_OFF or ..._ON)
#define MODE_TEST_NUM(x) ((x) & 3)  // _TEST1 -> 1 / _TEST2 -> 2 / _TEST3 -> 3

        MODE_TESTOFF   =  8,  // manual off / start test sequence
        MODE_FASTON    =  9,  // switch on threshold reached faster than minimal expected time (stay off)
        MODE_FASTOFF   = 10,  // switch off threshold reached faster than minimal expected time
        MODE_OVERHEAT  = 11,  // safety off because of overheat
     // MODE_WET       = 12,  // free for further checks

        COUNT_MODES
    };
    enum MBIT {
        MBIT_AUTO_OFF   = 1 << MODE_AUTO_OFF,
        MBIT_AUTO_ON    = 1 << MODE_AUTO_ON,
        MBIT_AUTO_PAUSE = 1 << MODE_AUTO_PAUSE,
        MBIT_TEST1_END  = 1 << MODE_TEST1_END,
        MBIT_TEST2_END  = 1 << MODE_TEST2_END,
        MBIT_TEST1      = 1 << MODE_TEST1,
        MBIT_TEST2      = 1 << MODE_TEST2,
        MBIT_TEST3      = 1 << MODE_TEST3,
        MBIT_TESTOFF    = 1 << MODE_TESTOFF,
        MBIT_FASTON     = 1 << MODE_FASTON,
        MBIT_FASTOFF    = 1 << MODE_FASTOFF,
        MBIT_OVERHEAT   = 1 << MODE_OVERHEAT,
    };
    enum MMASK {
        MMASK_AUTO      = MBIT_AUTO_OFF | MBIT_AUTO_ON,          // MODE_AUTO_PAUSE is not an "auto" mode
        MMASK_TEST      = MBIT_TEST1 | MBIT_TEST2 | MBIT_TEST3,  // manual relay test
        MMASK_REL1      = MBIT_TEST1              | MBIT_TEST3,
        MMASK_REL2      =              MBIT_TEST2 | MBIT_TEST3,
        MMASK_MAYBE_ON  = MBIT_TEST1 | MBIT_TEST2,                              // on when one relay does not work
        MMASK_IS_ON     =                           MBIT_TEST3 | MBIT_AUTO_ON,  // should be on

        MMASK_SAFETY_OFF = 0xffff ^ (MBIT_TESTOFF | (MBIT_TESTOFF - 1))
    };

    enum EVENT {
        EV_NEWVALUE     = 1 << 0,  // AnalogReader read a new value
        EV_INPUT        = 1 << 1,  // button press event -> manual test relais 1 -> 2 -> both -> normal mode
        EV_REMOTE       = 1 << 2,  // remote mode change (MQTT subscribe)
        EV_MODECHANGED  = 1 << 3,  // mode changed - check PublichMode
        EV_EXPIRATION   = 1 << 4,  // timer expiration

        COUNT_EVENTS
    };

    typedef unsigned short value_t;
    typedef unsigned long  timo_t;

    Control( AnalogReader & reader, Relay & relay1, Relay & relay2, Input & input );

    void Run( Indicator & indicator );  // the thread function (call in main)
    void Temperature( uint16_t idx, float temperature );
    void AnalogValue( unsigned short value );
    void Subscription( const char * topic, const char * data );
    void NextTestStep();                // button pressed -> go to next test step

    void ReadParam();
    void SavePwrOnMode( uint8_t mode );
    void Setup( struct httpd_req * req, bool post = false );

private:
    void WriteParam();
    void SetU16( nvs_handle nvs, const char * key, uint16_t val );
    void SetU32( nvs_handle nvs, const char * key, uint32_t val );
    void SafetyOff( uint8_t newMode );
    void PublishMode();
    void PublishValue( bool force = false );
    void Notify( uint8_t ev );

    AnalogReader & mReader;
    Relay        & mRelay1;
    Relay        & mRelay2;
    Input        & mInput;

    value_t  mThresOff    { 0x200 };  // switch off, when passing threshold
    value_t  mThresOn     {  0x80 };  // switch on, when passing threshold
    timo_t   mMinOffTicks { configTICK_RATE_HZ *  5 };       // stay off at least ... ticks
    timo_t   mMinOnTicks  { configTICK_RATE_HZ * 10 };       // must stay on at least ... ticks (otherwise safety off)
    timo_t   mMaxOnTicks  { configTICK_RATE_HZ * 60 * 10 };  // switch off at least after ... ticks
    value_t  mValueTol    { 0 };  // threshold to publish changed value
    uint16_t mValueIdx    { 0 };  // device index to publish value
    uint16_t mModeIdx     { 0 };  // device index to publish mode
    uint16_t mTempIdx     { 0 };  // temperature sensor to check overheat
    uint8_t  mTempMax    { 99 };  // safety switch off on overheat

    uint8_t  mMode { MODE_TESTOFF };  // effective operational mode
    uint8_t  mModeRemote { 0 };      // mode set by MQTT subscription
    uint8_t  mEvents { 0 };          // bit-or-ed mask of events to be handled

    value_t  mValue { AnalogReader::INV_VALUE };

    SemaphoreHandle_t mSemaphore { 0 };
};

#endif /* MAIN_CONTROL_H_ */
