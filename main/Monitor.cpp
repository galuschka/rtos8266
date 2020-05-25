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
    enum
    {
        HEIGHT = 512,   // graph height
        GRID_Y = 10,   // autoscale grid factor
        HALF_FONT = 8,   // to add at top and bottom for text
        Y0 = HEIGHT + HALF_FONT,
        DIM_Y = HEIGHT + (HALF_FONT * 2),

        N = 100,   // show last N values
        FACTOR_X = 10,   // graph width: N * FACTOR_X points
        X_LABEL = 50,   // points reserved for label text
        GROUP_IND = 40,   // label indent per group
        X0 = X_LABEL + (GROUP_IND * 4),
        DIM_X = X0 + (N * FACTOR_X),
    };
    static const char *const color[] = { "#777", "#4c4", "#c44" };

    char buf[80];
    value_t val[N];
    Reader.GetValues( val, N );
    value_t minmax[3][2];
    int set = 0;
    int group;
    for (group = 1; group < 3; ++group)
        minmax[group][0] = minmax[group][1] = AnalogReader::MAX_VALUE + 1; // invalidate
    for (int i = 0; i < N; ++i) {
        value_t const v = val[i] & AnalogReader::MAX_VALUE;
        group = (val[i] & 0x8000) ? 2 : 1;
        if (!(set & group)) {
            set |= group;
            minmax[group][0] = minmax[group][1] = v;
        } else if (minmax[group][0] > v)
            minmax[group][0] = v;
        else if (minmax[group][1] < v)
            minmax[group][1] = v;
    }
    for (group = 1; group < 3; ++group) {
        if (set & group) {
            if (!(set & 4)) {
                minmax[0][0] = minmax[group][0];
                minmax[0][1] = minmax[group][1];
                set |= 4;
            } else {
                if (minmax[0][0] > minmax[group][0])
                    minmax[0][0] = minmax[group][0];
                if (minmax[0][1] < minmax[group][1])
                    minmax[0][1] = minmax[group][1];
            }
        }
    }
    // minmax[0][0] = 0;
    // minmax[0][1] = AnalogReader::MAX_VALUE;
    minmax[0][0] = (minmax[0][0] / GRID_Y) * GRID_Y;
    minmax[0][1] = ((minmax[0][1] / GRID_Y) + 1) * GRID_Y;
    float const scaleY = HEIGHT * 1.0 / (minmax[0][1] - minmax[0][0]);

#define Y(v)    (Y0 - (int) (((v) - minmax[0][0]) * scaleY))

    static char s_data1[] =
            "<head><meta http-equiv=\"refresh\" content=\"1\"></head>\n"
                    "<body>\n";
    static char s_data9[] = "</body>\n";

    SendCharsChunk( req, s_data1 );
    snprintf( buf, sizeof(buf), "<svg viewBox=\"0 0 %d %d\" class=\"chart\">\n",
            DIM_X, DIM_Y );
    SendStringChunk( req, buf );

    SendStringChunk( req,
            " <text text-anchor=\"end\" dominant-baseline=\"central\">\n" );
    set = 0;
    for (group = 0; group < 3; ++group) {
        if (minmax[group][0] > AnalogReader::MAX_VALUE)
            continue;
        for (int kind = 0; kind < 2; ++kind) {
            if ((!kind)
                    || (((minmax[group][1] - minmax[group][0]) * scaleY)
                            > HALF_FONT)) {
                snprintf( buf, sizeof(buf),
                        "  <tspan style=\"fill: %s;\" x=\"%d\" y=\"%d\">%d</tspan>\n",
                        color[group], (X_LABEL - 1) + (group * GROUP_IND),
                        Y( minmax[group][kind] ), minmax[group][kind] );
                SendStringChunk( req, buf );
            }
        }
    }
    SendStringChunk( req, " </text>\n" );

    snprintf( buf, sizeof(buf),
            "  <line stroke=\"%s\" stroke-width=\"1\" x1=\"%d\" x2=\"%d\" y1=\"%d\" y2=\"%d\" />\n",
            color[0], X0 - 1, X0 - 1, Y( minmax[0][1] ), Y( minmax[0][0] ) );
    SendStringChunk( req, buf );
    for (group = 0; group < 3; ++group) {
        if (minmax[group][0] > AnalogReader::MAX_VALUE)
            continue;
        for (int kind = 0; kind < 2; ++kind) {
            snprintf( buf, sizeof(buf),
                    "  <line stroke=\"%s\" stroke-width=\"1\" x1=\"%d\" x2=\"%d\" y1=\"%d\" y2=\"%d\" />\n",
                    color[group], X_LABEL + (group * GROUP_IND), X0 - 1,
                    Y( minmax[group][kind] ), Y( minmax[group][kind] ) );
            SendStringChunk( req, buf );
        }
    }

    set = 0;
    for (int i = 0; i < N; ++i) {
        value_t const v = val[i] & AnalogReader::MAX_VALUE;
        value_t const y = Y( v );
        int const x = X0 + (i * FACTOR_X);

        if (set) {
            snprintf( buf, sizeof(buf), " %d,%d", x, y );
            SendStringChunk( req, buf );
        }
        group = (val[i] & 0x8000) ? 2 : 1;
        if (set != group) {
            if (set)
                SendStringChunk( req, "\" />\n" );
            set = group;
            snprintf( buf, sizeof(buf), " <polyline fill=\"none\""
                    " stroke=\"%s\""
                    " stroke-width=\"3\""
                    " points=\""
                    "%d,%d", color[group], x, y );
            SendStringChunk( req, buf );
        }
    }
    if (set)
        SendStringChunk( req, "\" />\n" );

    SendStringChunk( req, " </svg>\n" );
    snprintf( buf, sizeof(buf), " <br /><br /><br />"
            "free heap: %d, %d\n", esp_get_free_heap_size(),
            heap_caps_get_free_size( MALLOC_CAP_8BIT ) );
    SendStringChunk( req, buf );

    SendCharsChunk( req, s_data9 );

    httpd_resp_send_chunk( req, 0, 0 );
}
