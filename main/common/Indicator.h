/*
 * Indicator.h
 */

#ifndef MAIN_INDICATOR_H_
#define MAIN_INDICATOR_H_

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>

#include <driver/gpio.h>    // gpio_num_t

class Indicator
{
public:
    enum STATUS
    {
        STATUS_ERROR,       // any serious error
        STATUS_AP,          // access point mode - awaiting config
        STATUS_CONNECT,     // trying to connect in station mode
        STATUS_IDLE,        // station mode and relay is switched off
        STATUS_ACTIVE,      // station mode and relay is switched on
    };
    bool Init( gpio_num_t pinPrimary, gpio_num_t pinSecondary = GPIO_NUM_MAX );
    void Indicate( STATUS status );
    // SigMask: 0x89abcdef -> f on, e off, d on, ...
    //                   0 -> steady off
    //                   1 -> steady on
    void SigMask( unsigned long priSigMask, unsigned long secSigMask = 0 );
    void SigDelay( unsigned long ticksDelay );   // delay signalling for some time
    void Blink( uint8_t numPri, uint8_t numSec = 0 );  // num = number of times on
    void Access( uint8_t ok );  // false: 2x primary / true: 1x secondary
    void Steady( uint8_t on );  // steady on/off
    void Pause( bool pause );   // pause on/off for performing SW update
    bool Init();
    void Run();  // internal thread routine
    static Indicator& Instance();

    uint8_t               NofLEDs(  void ) const { return mPin[1] < GPIO_NUM_MAX ? 2 : 1; }
    const unsigned long * SigMask(  void ) const { return mSigMask; }
    const uint8_t       * SigSlots( void ) const { return mSigSlots; }

    Indicator();
private:
    gpio_num_t        mPin[2]       { GPIO_NUM_MAX, GPIO_NUM_MAX };
    uint8_t           mBlink[2]     { 0, 0 };  // 0xff: steady on
    unsigned long     mSigMask[2]   { 0, 0 };
    uint8_t           mSigSlots[2]  { 1, 1 };  // just to avoid div 0
    TickType_t        mSigStart     { 0 };  // phase 0 start
    TickType_t        mSigDelay     { 0 };  // when to resume signalling (0 = not resume)
    bool              mPause        { false };
    TaskHandle_t      mTaskHandle   { 0 };
    SemaphoreHandle_t mSemaphore    { 0 };
};

#endif /* MAIN_INDICATOR_H_ */
