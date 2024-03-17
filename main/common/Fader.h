/*
 * Fader.h
 */
#pragma once

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>

#include <stdint.h>
#include <vector>

#include <driver/gpio.h>

//include "Relay.h"

class Relay;

class Fader
{
    enum STATUS {
        STATUS_IDLE = 0,
        STATUS_FADING,
    };
    enum NOTIFY {
        NOTIFY_NONE    = 0,
        NOTIFY_TRIGGER,
        NOTIFY_FADEEND,  // fading done -> make GPIO open drain / opt. switch off relay
        NOTIFY_HALT,
    };

    Fader();
public:
    Fader( gpio_num_t gpionumRed, gpio_num_t gpionumGreen, gpio_num_t gpionumBlue );
    ~Fader();

    bool    Start();

    void    Run();
    void    TimerIntr();    // hw timer interrupt handler

    void    Fade( float time, const float * level = nullptr );  // time: duration in seconds to set level[r,g,b]

    void    Fade( float time, float red, float green, float blue ) {
                float level[3] = { red, green, blue };
                Fade( time, level );
    }

private:
    void    Trigger( uint8_t notify );
    bool    PhaseCalc( uint8_t setToggle = 0 );  // return false when no pwm to serve

    static uint8_t highestbit( int x ) {
        return 31 - __builtin_clz( x );
    }
    uint16_t pinsOfCols( uint8_t colmask ) const {
        uint16_t pins = 0;
        while (colmask) {
            uint8_t col = highestbit( colmask );
            pins |= 1 << mGpioNum[col];
            colmask ^= 1 << col;
        }
        return pins;
    }

    // one HW timer for RGB -> PWM devided into up to 4 phases:
    //
    //                   |           |       |       |                       |
    //         ...       ^           ^       ^       ^                       ^ ...
    //                   on         off1    off2    off3                     on
    //
    // mPinOnOff   w1ts [0]    w1tc [1]                                w1ts [0]
    // mLoad             |<---[1]--->|<----------------[0]------------------>|
    //
    // mPinOnOff        [0]         [1]     [2]                             [0]
    // mLoad             |<---[1]--->|<-[2]->|<------------[3]-------------->|
    //
    // mPinOnOff        [0]         [1]     [2]     [3]                     [0]
    // mLoad             |<---[1]--->|<-[2]->|<-[3]->|<---------[4]--------->|

    uint16_t const      mPwmGpioMask;             // all pwm GPIOs as mask
    uint8_t  const      mGpioNum[3];              // RGB PWM pins

    uint8_t mStatus { STATUS_IDLE };  // fading task status
    uint8_t mPhase      { 0 };        // current phase
    uint8_t mActiveSet  { 0 };        // to toggle by a single bit flip
    uint8_t mIntrRunning{ 0 };        // interrupt handler should run
    ulong   mIntrCnt    { 0 };        // for debugging
    float   mCurrLvl[3] { 0,0,0 };    // current level (changing while fading)

    TaskHandle_t mTaskHandle {};

    struct Set {
        uint16_t  mPinOnOff[4] { 0,0,0,0 }; // GPIOs as mask to switch on, off1, off2, off3
        ulong     tmrLoad[4] {99,99,99,99}; // timeout load value for each phase
        ulong     mClocks       { 0 };      // number of cpu clks to fade in or out
        ulong     mStart        { 0 };      // g_esp_os_cpu_clk when fading started
        float     mStartLvl[3]  { 0,0,0 };  // level when fading started
        float     mTargetLvl[3] { 1,1,1 };  // target dimm level (1.0 = 100%)
    } mSet[2];
};
