// define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "Smiffer.h"
#include "Indicator.h"
#include "WebServer.h"
#include "HttpHelper.h"

#include <esp_http_server.h>
#include <esp_log.h>    // ESP_LOGI()

namespace {
const char * const TAG       = "Smiffer";
Smiffer    *       s_smiffer = 0;
}

extern "C" esp_err_t smiffer_dump( httpd_req_t * req )
{
    if (s_smiffer)
        s_smiffer->Dump( req );
    return ESP_OK;
}

namespace {
const char * const s_pathDump  = "/dump";
const httpd_uri_t  s_dump_get  = { .uri = s_pathDump, .method = HTTP_GET,  .handler = smiffer_dump, .user_ctx = 0 };
const WebServer::Page s_pageDump { s_dump_get, "Dump latest Rx bytes" };
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

Smiffer::Smiffer()
{
    s_smiffer = this;
    WebServer::Instance().AddPage( s_pageDump );
}

Smiffer::~Smiffer()
{
    s_smiffer = 0;
}

void Smiffer::read( uint8_t ch, bool ovfl )
{
    ringbuf.fill( ch );
    if (ovfl)
        ++ovflCnt;
    Indicator::Instance().Blink(1);
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

void Smiffer::Dump( httpd_req_t * req )
{
    HttpHelper hh{ req, "Dump" };
    if (ovflCnt) {
        hh.Add( "Overflow counter: " );
        hh.Add( ovflCnt );
        hh.Add( "<br /><br />\n" );
    }
    int cnt = 0;
    {
        const char s_font[] = "<font style=\"font-family: monospace;\">\n";
        hh.Add( s_font, sizeof(s_font) - 1 );
    }
    ringbuf.loop( smiffer_add_dump_byte, & hh, & cnt );
    {
        const char s_endfont[] = "\n</font>\n";
        hh.Add( s_endfont, sizeof(s_endfont) - 1 );
    }
}
