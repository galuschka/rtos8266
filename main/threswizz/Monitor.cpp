/*
 * Monitor.cpp
 */
//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "Monitor.h"

#include <esp_log.h>
#include <math.h>
#include <string.h>

#include "HttpHelper.h"
#include "HttpParser.h"
#include "WebServer.h"
#include "Wifi.h"

namespace {
const char * const TAG = "Monitor";
}

extern "C" esp_err_t monitor_get( httpd_req_t * req );

const httpd_uri_t     s_uri = { .uri = "/monitor", .method = HTTP_GET, .handler = monitor_get, .user_ctx = 0 };
const WebServer::Page s_page  { s_uri, "Monitor" };

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
        HEIGHT      = 512,  // graph height in pixel
        FONT_SIZE   =  16,  // assumed font size
        Y0          = HEIGHT + FONT_SIZE * 2 + (FONT_SIZE / 2) + 2,
        DIM_Y       = HEIGHT + FONT_SIZE * 3 + 4,

        N           = 600,  // show last N values
        FACTOR_X    =   2,  // graph width: N * FACTOR_X pixel
        X_LABEL     =  75,  // points reserved for 1st label text
        GROUP_IND   =  75,  // label indent per group
        X0          = X_LABEL + ((GROUP_IND * 5) / 2),
        DIM_X       = X0 + ((N - 1) * FACTOR_X) + 1,
    };
    // groups: 0 = overall (blue), 1 = off (black), 2 = on (red)
    static const char *const color[] = { "#44c", "#111", "#c44" };

    char buf[80];
    value_t val[N];
    Reader.GetValues( val, N );
    value_t minmax[3][2];
    unsigned long  sum[3] = { 0 };
    unsigned short cnt[3] = { 0 };
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
    value_t min = 0;
    value_t max = AnalogReader::NOF_VALUES;
    for (group = 1; group < 3; ++group) {
        if (cnt[group]) {
            if (!cnt[0]) {
                min = minmax[group][0];
                max = minmax[group][1];
            } else {
                if (min > minmax[group][0])
                    min = minmax[group][0];
                if (max < minmax[group][1])
                    max = minmax[group][1];
            }
            cnt[0] += cnt[group];
            sum[0] += sum[group];
        }
    }
    ESP_LOGD( TAG, "min/max of all values: %d/%d", min, max );

    const char * color0[2] = { color[1], color[2] };
    minmax[0][0] = ThresOff;
    minmax[0][1] = ThresOn;
    if (minmax[0][0] > minmax[0][1]) {
        minmax[0][1] = ThresOff;
        minmax[0][0] = ThresOn;
        color0[0] = color[2];
        color0[1] = color[1];
    }

    for (int kind = 0; kind < 2; ++kind) {
        value_t const v = minmax[0][kind];
        if (! (v & 0x8000)) {
            if (min > v)
                min = v;
            else if (max < v)
                max = v;
        }
    }
    ESP_LOGD( TAG, "min/max incl. thresholds: %d/%d", min, max );

    {  // scale to next 5% (twentieth) boundaries
        enum {  
            NOF_VALUES  = AnalogReader::NOF_VALUES,  // 1024
            TENTH       = (NOF_VALUES + 9) / 10,     //  103
            TWENTIETH   = (TENTH + 1) / 2            //   52
        };
        double f =  0;
        double c = 20; // corresponds 100%
        if (min > TENTH)
            f = floor( (min - TWENTIETH) * 20.0 / NOF_VALUES );
        if (max < (NOF_VALUES - TENTH))
            c = ceil(  (max + TWENTIETH) * 20.0 / NOF_VALUES );

        min = (value_t) (int) (f / 20.0 * NOF_VALUES);
        max = (value_t) (int) (c / 20.0 * NOF_VALUES);
    }
    ESP_LOGD( TAG, "min/max rounded down/up: %d/%d", min, max );

    float const scaleY = HEIGHT * 1.0 / (max - min);
    ESP_LOGD( TAG, "scaleY: %d %%", (int) ((scaleY + 0.5) * 100.0) );

#define Y(v)    (Y0 - (int) (((v) - min) * scaleY))

    HttpHelper hh{ req, 0/*"Monitor analog pin values"*/, "Monitor" };
    {
        char refresh[8];
        HttpParser::Input in[] = { { "refresh", refresh, sizeof(refresh) } };
        HttpParser parser{ in, sizeof(in) / sizeof(in[0]) };

        parser.ParseUriParam( req );
        if (refresh[0]) {
            std::string head { ("<meta http-equiv=\"refresh\" content=\"" + std::string(refresh) + "\">") };
            hh.Head( head.c_str() );
        }
    }
    /*
     * hh.Add( "<b>"           "<font color=#44c>" "thresholds/average" "</font>"
     *          "&nbsp;&nbsp;" "<font color=#111>" "relay off"          "</font>"
     *          "&nbsp;&nbsp;" "<font color=#c44>" "relay on"           "</font>"
     *         "</b>\n" );
     */
    snprintf( buf, sizeof(buf), "<svg viewBox=\"0 0 %d %d\" class=\"chart\">\n",
                                                 DIM_X, DIM_Y );
    hh.Add( buf );
    hh.Add( " <text text-anchor=\"end\" dominant-baseline=\"central\">\n" );

    hh.Add( "  <tspan style=\"fill: #44c;\" x=\"100\"  y=\"8\">thresholds /</tspan>\n" );
    hh.Add( "  <tspan style=\"fill: #44c;\"  x=\"70\" y=\"24\">average</tspan>\n" );
    hh.Add( "  <tspan style=\"fill: #111;\" x=\"150\" y=\"24\">relay off</tspan>\n" );
    hh.Add( "  <tspan style=\"fill: #c44;\" x=\"225\" y=\"24\">relay on</tspan>\n" );
    int set = 0;
    for (group = 0; group < 3; ++group) {
        if (!cnt[group])
            continue;
        for (int kind = 0; kind < 2; ++kind) {
            if (minmax[group][kind] & 0x8000)
                continue;
            if ((!kind) ||
                (((minmax[group][1] - minmax[group][0]) * scaleY) > FONT_SIZE)) {
                snprintf( buf, sizeof(buf),
                          "  <tspan style=\"fill: %s;\" x=\"%d\" y=\"%d\">",
                          color[group],
                          (X_LABEL - 4) + (group * GROUP_IND),
                          Y( minmax[group][kind] ));
                hh.Add( buf ); // split cause compiler warning
                hh.Add( minmax[group][kind] * 100.0 / AnalogReader::NOF_VALUES, group ? 1 : 0 );
                // if (group) hh.Add( "&puncsp;&nbsp;" );  // unknown purpose...
                hh.Add( "&nbsp;&percnt;</tspan>\n" );
            } else {
                ESP_LOGI( TAG, "max.[%d] does not fit: calculated %d pixel for max-min=%d",
                                    group,
                                    (int) ((minmax[group][1] - minmax[group][0]) * scaleY),
                                            minmax[group][1] - minmax[group][0] );
            }
        }
        {
            float const avg = (sum[group] * 1.0 / cnt[group]);
            if (((minmax[group][0] | minmax[group][1]) & 0x8000) ||
                (((abs(avg - minmax[group][0]) * scaleY) > FONT_SIZE) &&
                 ((abs(minmax[group][1] - avg) * scaleY) > FONT_SIZE))) {
                snprintf( buf, sizeof(buf),
                          "  <tspan style=\"fill: %s;\" x=\"%d\" y=\"%d\">",
                          color[group],
                          (X_LABEL - 4) + (group * GROUP_IND),
                          Y( avg ) );
                hh.Add( buf ); // split cause compiler warning
                hh.Add( avg * 100.0 / AnalogReader::NOF_VALUES, 1 );
                hh.Add( "&nbsp;&percnt;</tspan>\n" );
            } else {
                ESP_LOGI( TAG, "avg.[%d] does not fit: calculated %d/%d pixel for min,avg,max=%d,%d,%d",
                                    group,
                                    (int) (abs(avg - minmax[group][0]) * scaleY),
                                    (int) (abs(minmax[group][1] - avg) * scaleY),
                                               minmax[group][0], (int) avg,
                                               minmax[group][1] );
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
              X0 - 1, X0 - 1, Y( max ), Y( min ) );
    hh.Add( buf );
    for (group = 0; group < 3; ++group) {
        if (!cnt[group])
            continue;
        for (int kind = 0; kind < 2; ++kind) {
            if (minmax[group][kind] & 0x8000)
                continue;
            // stroke-dasharray=\"5,10\" does not work...
            snprintf( buf, sizeof(buf),
                      "  <line stroke=\"%s\" stroke-width=\"1\" ",
                       group ? color[group] : color0[kind] );
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
