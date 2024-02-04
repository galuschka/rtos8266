/* IR Rx handler
*/

#pragma once

#include <stdint.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#include <driver/gpio.h>

namespace Mode {
  enum Mode {
    Idle,
    Wait, // wait for start bit (falling edge GPIO intr)
    Read, // read bits (timer interrupt)
    Stop, // skip stop bit slot
  };
}

class Infrared
{
  public:
    Infrared( gpio_num_t rxPin, uint32_t baudrate );
    ~Infrared();

    virtual void rxFunc( uint8_t byte, bool ovfl ) = 0;  // will be called in Run() context on each byte received

    bool Init();         // to be called to create task (will do Run inside that task)
    void Run();          // to be called when not using Init() in main - will return on error

    void RxStartISR();   // ISR on start event (GPIO interrupt on start bit)
    void RxTimerISR();   // ISR on each bit timeout

    uint32_t GetTimerLoadStart() const { return mTimerLoadStart; }
    uint32_t GetTimerLoadData()  const { return mTimerLoadData;  }
    uint8_t  GetTimerDiv()       const { return mTimerDiv; }
    void     SetTimerLoadStart( uint32_t ticks ) { mTimerLoadStart = ticks; }
    void     SetTimerLoadData(  uint32_t ticks ) { mTimerLoadData  = ticks; }
    void     SetTimerDiv( uint8_t div ) {
        if (mTimerDiv != div) {
            mTimerLoadStart <<= mTimerDiv;
            mTimerLoadData  <<= mTimerDiv;
            mTimerDiv = div;
            mTimerLoadStart >>= mTimerDiv;
            mTimerLoadData  >>= mTimerDiv;
        }
    }

  private:
    uint8_t         mTimerDiv;        // HW timer clock divisor bits (0, 4, or 8: TIMER_CLKDIV_1, _16, _256)
                                      // ticks to load in HW timer count down counter:
    uint32_t        mTimerLoadStart;  // on start bit edge interrupt (1.5 bits time in theory)
    uint32_t        mTimerLoadData;   // on first bit and reloaded      (1 bit time in theory)
    gpio_config_t   mConf;

    struct {
        gpio_num_t          pin;
        uint8_t             mode { Mode::Idle };   // idle - wait - read - wait - ...
        uint8_t             byte { 0 };   // contructed while bits received
        uint8_t             bits { 0 };   // number of bits remaining 7..0
        uint8_t             full { 0 };   // Wptr == Rptr and all filled
        uint8_t             buf[40];
        uint8_t * const     end { & buf[sizeof(buf)] };
        uint8_t *           wp  { buf };  // ISR writes here
        uint8_t *           rp  { buf };  // thread reads from here
        SemaphoreHandle_t   semaphore { 0 };
    }               rx;
    TaskHandle_t    mTaskHandle {0};
};
