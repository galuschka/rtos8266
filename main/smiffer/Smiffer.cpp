/*
 * Smiffer.cpp - smart meter sniffer
 */
//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "Smiffer.h"

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#include "Infrared.h"
#include "Indicator.h"
#include "WebServer.h"
#include "HttpHelper.h"
#include "HttpTable.h"
#include "HttpParser.h"

#include <esp_http_server.h>
#include <esp_log.h>    // ESP_LOGI()


namespace {
const char * const TAG       = "Smiffer";
Smiffer            s_smiffer {};
}

extern "C" {

esp_err_t get_smiffer_dump( httpd_req_t * req )
{
    s_smiffer.Dump( req );
    return ESP_OK;
}

esp_err_t post_smiffer_dump( httpd_req_t * req )
{
    s_smiffer.Dump( req, true );
    return ESP_OK;
}

}

namespace {
const char * const s_pathDump  = "/dump";
const httpd_uri_t  s_dump_get  = { .uri = s_pathDump, .method = HTTP_GET,  .handler = get_smiffer_dump,  .user_ctx = 0 };
const httpd_uri_t  s_dump_post = { .uri = s_pathDump, .method = HTTP_POST, .handler = post_smiffer_dump, .user_ctx = 0 };
const WebServer::Page s_pageDump { s_dump_get, "Dump" };
}

namespace
{
TickType_t now()
{
    return xTaskGetTickCount();
}
/*
unsigned long expiration( TickType_t ticks )
{
    TickType_t exp = xTaskGetTickCount() + ticks;
    if (!exp)
        --exp;
    return exp;
}
bool expired( TickType_t exp )
{
    if (! exp)
        return false;
    long diff = now() - exp;
    return (diff >= 0);
}
*/
}

void Ringbuf::loop( void (*callback)( uint8_t, void *, void * ), void * arg1, void * arg2 )
{
    ESP_LOGD( TAG, "buf: %p, wp = %p, wrap = %d", buf, wp, wrap );
    if (wrap < 0)
        return;  // no data

    uint8_t const * rp;
    if (wrap)
        rp = wp;        // we first do ++rp, so we start at wp + 1
    else
        rp = end - 1;   // we first do ++rp, so we start at buf

    do {
        if (++rp >= end)
            rp = buf;
        callback( *rp, arg1, arg2 );
    } while (rp != wp);
}
/*
Sml & Sml::Instance()
{
    return s_smiffer;
}
*/
Smiffer & Smiffer::Instance()
{
    return s_smiffer;
}

Smiffer::Smiffer()
{
}

Smiffer::~Smiffer()
{
}

bool Smiffer::Init()
{
    if (! (mSemaphore = xSemaphoreCreateBinary()))  // where we get waked up
        return false;
    WebServer::Instance().AddPage( s_pageDump, & s_dump_post );
    return true;
}

void Smiffer::read( uint8_t ch, bool ovfl )
{
    mRingbuf.fill( ch );
    if (ovfl)
        ++mOvflCnt;
    parse( ch );

    mRxTime = now();
    if (! mReceiving) {
        mReceiving = true;
        Indicator::Instance().Steady(1);
        xSemaphoreGive( mSemaphore );  // start fast timo check
    }
}

void Smiffer::onReady( u8 err, u8 byte )
{
    mObjCntOnReady = objCnt();
    mOffsetOnReady = offset();

    if (err == Err::NoError) {
        mFrameComplete = true;
        xSemaphoreGive( mSemaphore );
    }
}

extern "C" void smiffer_add_dump_byte( uint8_t ch, void * arg1, void * arg2 )
{
    char buf[4];
    buf[1] = (ch & 0xf) + '0' + ((((ch & 0xf) / 10) * ('A' - '0' - 10)));
    ch >>= 4;
    buf[0] = (ch & 0xf) + '0' + ((((ch & 0xf) / 10) * ('A' - '0' - 10)));
    buf[2] = ' ';
    buf[3] = 0;
    HttpHelper * hh  = (HttpHelper *) arg1;
    int        * cnt = (int *) arg2;
    if (*cnt && ! (*cnt & 0x1f))
        hh->Add( "<br />\n" );
    ++(*cnt);
    hh->Add( buf, 3 );
}

namespace {

std::string InputField( const char * key, long min, long max, long val )
{
    std::string str {"<input type=\"number\" step=\"1\" name=\"" }; str += key;
    str += "\" min=\"";   str += HttpHelper::String( min );
    str += "\" max=\"";   str += HttpHelper::String( max );
    str += "\" value=\""; str += HttpHelper::String( val );
    str += "\" />";
    return str;
}

}

void Smiffer::Dump( httpd_req_t * req, bool isPost )
{
    static const char s_keyTimerDiv[]  = "div";
    static const char s_keyLoadStart[] = "start";
    static const char s_keyLoadData[]  = "data";

    HttpHelper hh{ req, "dump latest Rx data", "Dump" };

    if (isPost) {
        char bufTimerDiv[4];
        char bufLoadStart[12];
        char bufLoadData[12];
        HttpParser::Input in[] = { { s_keyTimerDiv,  bufTimerDiv,  sizeof(bufTimerDiv)  },
                                   { s_keyLoadStart, bufLoadStart, sizeof(bufLoadStart) },
                                   { s_keyLoadData,  bufLoadData,  sizeof(bufLoadData)  } };
        HttpParser parser{ in, sizeof(in) / sizeof(in[0]) };

        if (! parser.ParsePostData( req )) {
            hh.Add( "unexpected end of data while parsing data" );
            return;
        }

        mInfrared->SetTimerDiv( (uint8_t) strtoul( bufTimerDiv,  0, 10 ) );
        mInfrared->SetTimerLoadStart(     strtoul( bufLoadStart, 0, 10 ) );
        mInfrared->SetTimerLoadData(      strtoul( bufLoadData,  0, 10 ) );
    }
    if (mOvflCnt) {
        hh.Add( "Overflow counter: " );
        hh.Add( mOvflCnt );
        hh.Add( "<br /><br />\n" );
    }
    {   // hh.Add( "SML counter: " );

        Table<Err::Unknown,11> table;
        hh.Add( " <form method=\"post\">\n" );
        hh.Add( "  <table caption=\"SML counter\">\n" );
        table[0][1] = "&nbsp;";
        table[0][3] = "&nbsp;&nbsp;&nbsp;";
        table[0][5] = "&nbsp;";
        table[0][7] = "&nbsp;&nbsp;&nbsp;";
        table[0][9] = "&nbsp;";

        table[Err::OutOfMemory - 1][0] = "out of memory";
        table[Err::InvalidType - 1][0] = "invalid type";
        table[Err::CrcError    - 1][0] = "CRC errors";
        table[Err::Timeout     - 1][0] = "timeout errors";
        table[Err::Unknown     - 1][0] = "other errors";

        table.Right(2);
        const u32 * cnt = getErrCntArray();
        u32 badframes = 0;
        for (u8 cnttype = 1; cnttype <= Err::Unknown; ++cnttype) {
            table[cnttype - 1][2] = HttpHelper::String( cnt[ cnttype ] );
            badframes += cnt[ cnttype ];
        }

        table[0][4] = "good frames";
        table[1][4] = "bad frames";
        table[2][4] = "frames total";
        table[3][4] = "bytes received";
        table[4][4] = "objects parsed";

        table.Right(6);
        table[0][6] = HttpHelper::String( cnt[ Err::NoError ] );
        table[1][6] = HttpHelper::String( badframes );
        table[2][6] = HttpHelper::String( badframes + cnt[ Err::NoError ] );
        table[3][6] = HttpHelper::String( (u32) mOffsetOnReady );
        table[4][6] = HttpHelper::String( (u32) mObjCntOnReady );

        table[0][8] = "timer clk div";
        table[1][8] = "timer load start";
        table[2][8] = "timer load data";

        table.Right(10);
        table[0][10] = InputField( s_keyTimerDiv,   0,      8, mInfrared->GetTimerDiv() );
        table[1][10] = InputField( s_keyLoadStart, 50, 100000, mInfrared->GetTimerLoadStart() );
        table[2][10] = InputField( s_keyLoadData,  50, 100000, mInfrared->GetTimerLoadData() );
        table[4][10] = "<center><button type=\"submit\">adjust timing</button></center>";

        table.AddTo( hh );
        hh.Add( "  </table>\n" );
        hh.Add( " </form>\n" );
    }
    hh.Add( " <br />\n" );
    int cnt = 0;
    {
        const char s_font[] = "<font style=\"font-family: monospace;\">\n";
        hh.Add( s_font, sizeof(s_font) - 1 );
    }
    mRingbuf.loop( smiffer_add_dump_byte, & hh, & cnt );
    {
        const char s_endfont[] = "\n</font>\n";
        hh.Add( s_endfont, sizeof(s_endfont) - 1 );
    }
}

void Smiffer::Run()
{
    // with 9600 baud, next byte should be available after 1 msec -> timo 10 msec?
    // configTICK_RATE_HZ is 100 -> min. 4 ticks seems to be more reasonable
    static constexpr TickType_t s_timo = configTICK_RATE_HZ <= 400 ? 4 : configTICK_RATE_HZ / 100;

    bool blinkOnTimo = false;
    while (true) {
        // vTaskDelay( 2 );
        TickType_t delay = portMAX_DELAY;  // configTICK_RATE_HZ ...
        if (mReceiving) {
            delay = s_timo;
        }
        xSemaphoreTake( mSemaphore, delay );
        if (mReceiving) {
            long diff = now() - mRxTime;
            if (diff > s_timo) {
                mReceiving = false;
                Indicator::Instance().Steady(0);
                timeout();  // abort parsing in case in the middle of frame

                if (blinkOnTimo) {
                    blinkOnTimo = false;
                    vTaskDelay( 10 );
                    Indicator::Instance().Blink(1);
                }
            }
        }
        if (mFrameComplete) {
            mFrameComplete = false;
            // todo...

            if (mReceiving)
                blinkOnTimo = true;
            else
                Indicator::Instance().Blink(1);
        }
    }
}
