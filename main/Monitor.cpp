/*
 * Monitor.cpp
 *
 *  Created on: 06.05.2020
 *      Author: holger
 */

#include "Monitor.h"

#include "WebServer.h"
#include "Wifi.h"

#define SendCharsChunk( req, chararray )  httpd_resp_send_chunk( req, chararray, sizeof(chararray) - 1 )

namespace
{
void SendStringChunk( httpd_req_t * req, const char * string )
{
    httpd_resp_send_chunk( req, string, strlen( string ) );
}
}

extern "C" esp_err_t monitor_get( httpd_req_t * req );

//@formatter:off
const httpd_uri_t s_uri =       { .uri = "/monitor",
                                  .method = HTTP_GET,
                                  .handler = monitor_get,
                                  .user_ctx = 0 };
const WebServer::Page s_page    { s_uri, "Monitor analog pin values" };
//@formatter:on

Monitor *s_monitor = 0;

extern "C" esp_err_t monitor_get( httpd_req_t * req )
{
    if (s_monitor)
        s_monitor->Show( req );
    return ESP_OK;
}

Monitor::Monitor( AnalogReader & analog_reader ) :
        Reader { analog_reader }
{
    if (Wifi::Instance().StationMode()) {
        s_monitor = this;
        WebServer::Instance().AddPage( s_page, 0 );
    }
}

Monitor::~Monitor()
{
    s_monitor = 0;
}

void Monitor::Show( struct httpd_req * req ) const
{
    static const char * const color[] = { "#000", "#4c4", "#c48" };

    const int n = 100;
    AnalogReader::value_t val[n];
    Reader.GetValues( val, n );
    AnalogReader::value_t minmax[3][2];
    int set = 0;
    for (int i = 0; i < n; ++i) {
        AnalogReader::value_t const v = val[i] & 0x03ff;
        if (val[i] & 0x8000) {
            if (!(set & 2)) {
                set |= 2;
                minmax[2][0] = minmax[2][1] = v;
            } else if (minmax[2][0] > v)
                minmax[2][0] = v;
            else if (minmax[2][1] < v)
                minmax[2][1] = v;
        } else {
            if (!(set & 1)) {
                set |= 1;
                minmax[1][0] = minmax[1][1] = v;
            } else if (minmax[1][0] > v)
                minmax[1][0] = v;
            else if (minmax[1][1] < v)
                minmax[1][1] = v;
        }
    }
    // minmax[0][0] = minmax[1][0] < minmax[2][0] ? minmax[1][0] : minmax[2][0];
    // minmax[0][1] = minmax[1][1] > minmax[2][1] ? minmax[1][1] : minmax[2][1];
    minmax[0][0] = 0;
    minmax[0][1] = 0x03ff;

    static char s_data1[] =
            "<head><meta http-equiv=\"refresh\" content=\"1\"></head>\n"
                    "<body>\n"
                    "<svg viewBox=\"0 0 1200 530\" class=\"chart\">\n";
    static char s_data9[] = "\n</svg>\n"
            "</body>\n";

    SendCharsChunk( req, s_data1 );

    char buf[80];
    set = 0;

    SendStringChunk( req,
            " <text text-anchor=\"end\" dominant-baseline=\"central\">\n" );
    for (int group = 0; group < 3; ++group) {
        for (int kind = 0; kind < 2; ++kind) {
            // we have 32 bits to reserve ranges of values 0..1023
            // 3 bits will be set on each ->  1024 / 29 = 35,3
            if (!(set & (2 << (minmax[group][kind] / 36)))) {
                set |= (7 << (minmax[group][kind] / 36));
                snprintf( buf, sizeof(buf),
                        "  <tspan x=\"90\" y=\"%d\">%d</tspan>\n",
                        520 - (minmax[group][kind] >> 1), minmax[group][kind] );
                SendStringChunk( req, buf );
            }
        }
    }
    SendStringChunk( req, " </text>\n" );

    for (int group = 0; group < 3; ++group) {
        for (int kind = 0; kind < 2; ++kind) {
            snprintf( buf, sizeof(buf),
                    "  <line x1=\"100\" x2=\"190\" y1=\"%d\" y2=\"%d\" stroke=\"%s\" stroke-width=\"1\" />\n",
                    520 - (minmax[group][kind] >> 1),
                    520 - (minmax[group][kind] >> 1),
                    color[group] );
            SendStringChunk( req, buf );
        }
    }

    set = 0;
    for (int i = 0; i < n; ++i) {
        AnalogReader::value_t const v = val[i] & 0x03ff;
        if (set) {
            snprintf( buf, sizeof(buf), " %d,%d", 200 + i * 10,
                    520 - (v >> 1) );
            SendStringChunk( req, buf );
        }
        int group = (val[i] & 0x8000) ? 2 : 1;
        if (set != group) {
            if (set)
                SendStringChunk( req, "\" />\n" );
            set = group;
            snprintf( buf, sizeof(buf), " <polyline fill=\"none\""
                    " stroke=\"%s\""
                    " stroke-width=\"3\""
                    " points=\""
                    "%d,%d",
                    color[group],
                    200 + i * 10,
                    520 - (v >> 1) );
            SendStringChunk( req, buf );
        }
    }
    if (set)
        SendStringChunk( req, "\" />\n" );

    SendCharsChunk( req, s_data9 );

    httpd_resp_send_chunk( req, 0, 0 );
}
