/* IR Rx handler
*/

#pragma once

#include <stdint.h>
#include <FreeRTOS.h>
#include <semphr.h>

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

    void Run();     // to be called e.g. in main - will return on error

    void RxStartISR();   // ISR on start event (GPIO interrupt on start bit)
    void RxTimerISR();   // ISR on each bit timeout

  private:
    uint32_t        mBitUsec;   // µsecs per bit
    struct {
        gpio_num_t          pin;
        uint8_t             mode { Mode::Idle };   // idle - wait - read - wait - ...
        uint8_t             byte { 0 };   // contructed while bits received
        uint8_t             bits { 0 };   // number of bits remaining 7..0
        uint8_t             full { 0 };   // Wptr == Rptr and all filled
        uint8_t             buf[16];
        uint8_t * const     end { & buf[sizeof(buf)] };
        uint8_t *           wp  { buf };  // ISR writes here
        uint8_t *           rp  { buf };  // thread reads from here
        SemaphoreHandle_t   semaphore { 0 };
    }               rx;
};
