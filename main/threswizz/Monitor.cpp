/*
 * Monitor.cpp
 *
 *  Created on: 06.05.2020
 *      Author: holger
 */

#include "Monitor.h"

#include <math.h>

#include "HttpHelper.h"
#include "WebServer.h"
#include "Wifi.h"

extern "C" esp_err_t monitor_get( httpd_req_t * req );

//@formatter:off
const httpd_uri_t s_uri =       { .uri = "/monitor", .method = HTTP_GET, .handler = monitor_get, .user_ctx = 0 };
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
    if (1 || Wifi::Instance().StationMode()) {
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
        HEIGHT = 500,   // graph height in pixel
        FONT_SIZE = 12,  // assumed font size
        Y0 = HEIGHT + (FONT_SIZE / 2) + 2,
        DIM_Y = HEIGHT + FONT_SIZE + 4,

        N =  60,         // show last N values
        FACTOR_X  = 10,  // graph width: N * FACTOR_X pixel
        X_LABEL   = 75,  // points reserved for 1st label text
        GROUP_IND = 75,  // label indent per group
        X0 = X_LABEL + ((GROUP_IND * 5) / 2),
        DIM_X = X0 + ((N - 1) * FACTOR_X) + 1,
    };
    // groups: 0 = overall (blue), 1 = off (black), 2 = on (red)
    static const char *const color[] = { "#44c", "#111", "#c44" };

    char buf[80];
    value_t val[N];
    Reader.GetValues( val, N );
    value_t minmax[3][2];
    unsigned long sum[3] = { 0 };
    unsigned char cnt[3] = { 0 };
    int group;
    for (int i = 0; i < N; ++i) {
        value_t const v = val[i] & AnalogReader::MASK_VALUE;
        group = (val[i] & 0x8000) ? 2 : 1;
        if (!cnt[group]) {
            minmax[group][0] = minmax[group][1] = v;
        } else if (minmax[group][0] > v)
            minmax[group][0] = v;
        else if (minmax[group][1] < v)
            minmax[group][1] = v;

        ++cnt[group];
        sum[group] += v;
    }
    for (group = 1; group < 3; ++group) {
        if (cnt[group]) {
            if (!cnt[0]) {
                minmax[0][0] = minmax[group][0];
                minmax[0][1] = minmax[group][1];
            } else {
                if (minmax[0][0] > minmax[group][0])
                    minmax[0][0] = minmax[group][0];
                if (minmax[0][1] < minmax[group][1])
                    minmax[0][1] = minmax[group][1];
            }
            cnt[0] += cnt[group];
            sum[0] += sum[group];
        }
    }
    // minmax[0][0] = 0;
    // minmax[0][1] = AnalogReader::NOF_VALUES;
    {
        double const f = floor( minmax[0][0] * 10.0 / AnalogReader::NOF_VALUES );
        double const c = ceil( minmax[0][1] * 10.0 / AnalogReader::NOF_VALUES );

        minmax[0][0] = (value_t) (int) (f / 10.0 * AnalogReader::NOF_VALUES);
        minmax[0][1] = (value_t) (int) (c / 10.0 * AnalogReader::NOF_VALUES);
    }
    float const scaleY = HEIGHT * 1.0 / (minmax[0][1] - minmax[0][0]);

#define Y(v)    (Y0 - (int) (((v) - minmax[0][0]) * scaleY))

    HttpHelper hh{ req, "Monitor" };
    hh.Head( "<meta http-equiv=\"refresh\" content=\"5\">" );
    hh.Add( "<center><b>"
                "<font color=#c44>" "relay on"    "</font>&nbsp;&nbsp;"
                "<font color=#111>" "relay off"   "</font>&nbsp;&nbsp;"
                "<font color=#44c>" "min/max/avg" "</font>"
            "</b></center>\n" );

    snprintf( buf, sizeof(buf), "<svg viewBox=\"0 0 %d %d\" class=\"chart\">\n",
                DIM_X, DIM_Y );
    hh.Add( buf );
    hh.Add( " <text text-anchor=\"end\" dominant-baseline=\"central\">\n" );
    int set = 0;
    for (group = 0; group < 3; ++group) {
        if (!cnt[group])
            continue;
        for (int kind = 0; kind < 2; ++kind) {
            if ((!kind)
                    || (((minmax[group][1] - minmax[group][0]) * scaleY)
                            > FONT_SIZE)) {
                snprintf( buf, sizeof(buf),
                          "  <tspan style=\"fill: %s;\" x=\"%d\" y=\"%d\">",
                          color[group], (X_LABEL - 4) + (group * GROUP_IND),
                          Y( minmax[group][kind] ));
                hh.Add( buf ); // split cause compiler warning
                hh.Add( minmax[group][kind] * 100.0 / AnalogReader::NOF_VALUES, group ? 1 : 0 );
                // if (group) hh.Add( "&puncsp;&nbsp;" );  // unknown purpose...
                hh.Add( "&nbsp;&percnt;</tspan>\n" );
            }
        }
        if (((minmax[group][1] - minmax[group][0]) * scaleY) > FONT_SIZE) {
            float const avg = (sum[group] * 1.0 / cnt[group]);
            if ((((avg - minmax[group][0]) * scaleY) > FONT_SIZE)
                    && (((minmax[group][1] - avg) * scaleY) > FONT_SIZE)) {
                snprintf( buf, sizeof(buf),
                          "  <tspan style=\"fill: %s;\" x=\"%d\" y=\"%d\">",
                          color[group], (X_LABEL - 4) + (group * GROUP_IND), Y( avg ) );
                hh.Add( buf ); // split cause compiler warning
                hh.Add( avg * 100.0 / AnalogReader::NOF_VALUES, 1 );
                hh.Add( "&nbsp;&percnt;</tspan>\n" );
            }
        }
    }
    hh.Add( " </text>\n" );

    snprintf( buf, sizeof(buf),
            "  <line stroke=\"%s\" stroke-width=\"1\" ",
            color[0] );
    hh.Add( buf );  // split cause compiler warning
    snprintf( buf, sizeof(buf),
              "x1=\"%d\" x2=\"%d\" y1=\"%d\" y2=\"%d\" />\n",
              X0 - 1, X0 - 1, Y( minmax[0][1] ), Y( minmax[0][0] ) );
    hh.Add( buf );
    for (group = 0; group < 3; ++group) {
        if (!cnt[group])
            continue;
        for (int kind = 0; kind < 2; ++kind) {
            // stroke-dasharray=\"5,10\" does not work...
            snprintf( buf, sizeof(buf),
                      "  <line stroke=\"%s\" stroke-width=\"1\" ",
                      color[group] );
            hh.Add( buf );  // split cause compiler warning
            snprintf( buf, sizeof(buf),
                      "x1=\"%d\" x2=\"%d\" y1=\"%d\" y2=\"%d\" />\n",
                      X_LABEL + (group * GROUP_IND), DIM_X,
                      Y( minmax[group][kind] ), Y( minmax[group][kind] ) );
            hh.Add( buf );
        }
    }

    set = 0;
    for (int i = 0; i < N; ++i) {
        value_t const v = val[i] & AnalogReader::MASK_VALUE;
        value_t const y = Y( v );
        int const x = X0 + (i * FACTOR_X);

        if (set) {
            snprintf( buf, sizeof(buf), " %d,%d", x, y );
            hh.Add( buf );
        }
        group = (val[i] & 0x8000) ? 2 : 1;
        if (set != group) {
            if (set)
                hh.Add( "\" />\n" );
            set = group;
            snprintf( buf, sizeof(buf), " <polyline fill=\"none\""
                      " stroke=\"%s\""
                      " stroke-width=\"3\""
                      " points=\""
                      "%d,%d", color[group], x, y );
            hh.Add( buf );
        }
    }
    if (set)
        hh.Add( "\" />\n" );

    hh.Add( " </svg>\n" );
    /*
     snprintf( buf, sizeof(buf), " <br /><br /><br />"
     "free heap: %d, %d\n", esp_get_free_heap_size(),
     heap_caps_get_free_size( MALLOC_CAP_8BIT ) );
     hh.Add( buf );
     */
}
