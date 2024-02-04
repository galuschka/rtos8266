/* Smiffer.h
 */
#pragma once

#include <FreeRTOS.h>
#include <semphr.h>

#include <stdint.h>
#include <driver/gpio.h>

#include "sml/sml.h"

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

class Infrared;

class Smiffer : public Sml
{
    virtual void onReady( u8 err, u8 byte );
    virtual void dump( const char * name = nullptr ) {};

  public:
    static Smiffer & Instance();

    Smiffer();
    ~Smiffer();

    void read( uint8_t ch, bool ovfl );
 
    void Dump( httpd_req * req, bool isPost = false );
    bool Init();
    void SetInfrared( Infrared & infrared ) { mInfrared = & infrared; }
    void Run();

  private:
    Infrared        * mInfrared     { 0 };
    Ringbuf           mRingbuf      {};
    u16               mOffsetOnReady{ 0 };
    idx               mObjCntOnReady{ 0 };
    long              mOvflCnt      { 0 };
    bool              mReceiving    { false };
    bool              mFrameComplete{ false };
 // unsigned long     mRxExpiration { 0 };
    unsigned long     mRxTime       { 0 };   // time when received last byte
    SemaphoreHandle_t mSemaphore    { 0 };
};
