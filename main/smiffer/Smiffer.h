/* Smiffer.h
 */
#pragma once

#include <stdint.h>
#include <driver/gpio.h>

struct httpd_req;

class Ringbuf
{
  public:
    Ringbuf() = default;
    void fill( uint8_t ch ) { if (++wp >= end) { wp = buf; if (wrap < 1) ++wrap; } *wp = ch; }
    void loop( void (*callback)( uint8_t, void *, void * ), void * arg1, void * arg2 );

  private:
    uint8_t               buf[1024] { 0 };
    int8_t                wrap      { -1 };
    uint8_t       *       wp        { & buf[sizeof(buf) - 1] };
    uint8_t const * const end       { & buf[sizeof(buf)] };
};

class Smiffer
{
  public:
    Smiffer();
    ~Smiffer();

    void read( uint8_t ch, bool ovfl );

    void Dump( httpd_req * req );

  private:
    Ringbuf ringbuf {};
    long ovflCnt { 0 };
};
