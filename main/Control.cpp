/*
 * Control.cpp
 *
 *  Created on: 19.05.2020
 *      Author: holger
 */

#include "Control.h"

#include "math.h"   // pow()

#include "FreeRTOSConfig.h"
#include "esp_log.h"
#include "nvs.h"            // nvs_open(), ...

#include "AnalogReader.h"
#include "Relay.h"
#include "Indicator.h"

#include "WebServer.h"
#include "Wifi.h"


const char * const TAG = "Control";

extern "C" esp_err_t config_get( httpd_req_t * req );

#define SendCharsChunk( req, chararray )  httpd_resp_send_chunk( req, chararray, sizeof(chararray) - 1 )

namespace
{
void SendStringChunk( httpd_req_t * req, const char * string )
{
    httpd_resp_send_chunk( req, string, strlen( string ) );
}

TickType_t now()
{
    return xTaskGetTickCount();
}

unsigned long expiration( TickType_t ticks )
{
    TickType_t exp = xTaskGetTickCount() + ticks;
    if (!exp)
        --exp;
    return exp;
}
}

//@formatter:off
const httpd_uri_t s_uri =       { .uri = "/config",
                                  .method = HTTP_GET,
                                  .handler = config_get,
                                  .user_ctx = 0 };
const WebServer::Page s_page    { s_uri, "Configure switching thresholds" };
//@formatter:on

const char *s_keyThresOff = "thresOff";
const char *s_keyThresOn  = "thresOn";
const char *s_keyMinOff   = "minOff";
const char *s_keyMinOn    = "minOn";
const char *s_keyMaxOn    = "maxOn";

Control *s_control = 0;

extern "C" esp_err_t config_get( httpd_req_t * req )
{
    if (s_control)
        s_control->Setup( req );
    return ESP_OK;
}

//@formatter:off
Control::Control( AnalogReader & reader, Relay & relay, thres_t thresOff, thres_t thresOn )
                : mReader       { reader },
                  mRelay        { relay },
                  ThresOff      { thresOff },
                  ThresOn       { thresOn },
                  MinOffTicks   { configTICK_RATE_HZ },
                  MinOnTicks    { configTICK_RATE_HZ * 15 },
                  MaxOnTicks    { configTICK_RATE_HZ * 60 * 60 }
//@formatter:on
{
    mRelay.AutoOn( false );
    mRelay.SetMode( Relay::MODE_AUTO );

    if (Wifi::Instance().StationMode()) {
        s_control = this;
        WebServer::Instance().AddPage( s_page, 0 );
    }
}

void Control::Run( Indicator & indicator )
{
    indicator.Indicate( Indicator::STATUS_IDLE );
    ReadParam();
    TickType_t exp = 0; // set, when AutoOn(true) called -> expiration to switch off
    while (true) {
        bool toSwitch = false;

        if (exp) {
            signed long diff = exp - now();
            toSwitch = (diff <= 0);
            if (!toSwitch) {
                if (ThresOff > ThresOn)
                    toSwitch = mReader.Average( 10 ) >= ThresOff;
                else
                    toSwitch = mReader.Average( 10 ) <= ThresOff;
            }
            if (toSwitch) {
                mRelay.AutoOn( false );
                indicator.Indicate( Indicator::STATUS_IDLE );
                exp = 0;
                if (MinOffTicks) {
                    vTaskDelay( MinOffTicks );
                    continue;
                }
            }
        } else {
            if (ThresOff > ThresOn)
                toSwitch = mReader.Average( 10 ) <= ThresOn;
            else
                toSwitch = mReader.Average( 10 ) >= ThresOn;
            if (toSwitch) {
                mRelay.AutoOn( true );
                indicator.Indicate( Indicator::STATUS_ACTIVE );
                exp = expiration( MaxOnTicks );
                if (MinOnTicks) {
                    vTaskDelay( MinOnTicks );
                    continue;
                }
            }
        }

        vTaskDelay( configTICK_RATE_HZ / 10 );
    }
}

#if 0
void Control::SetThreshold( Control::thres_t thresOff,
        Control::thres_t thresOn )
{
    ThresOff = thresOff;
    ThresOn = thresOn;
}

void Control::SetTimeLimits( int minOffTicks, int minOnTicks, int maxOnTicks )
{
    MinOffTicks = minOffTicks;
    MinOnTicks = minOnTicks;
    MaxOnTicks = maxOnTicks;
}
#endif

void Control::ReadParam()
{
    ESP_LOGI( TAG, "Reading control configuration" );

    nvs_handle my_handle;
    if (nvs_open( "control", NVS_READONLY, &my_handle ) == ESP_OK) {
        {
            uint16_t val;
            if (nvs_get_u16( my_handle, s_keyThresOff, & val ) == ESP_OK)
                ThresOff = val;
            if (nvs_get_u16( my_handle, s_keyThresOn, & val ) == ESP_OK)
                ThresOn = val;
        }
        {
            uint32_t val;
            if (nvs_get_u32( my_handle, s_keyMinOff, & val ) == ESP_OK)
                MinOffTicks = val;
            if (nvs_get_u32( my_handle, s_keyMinOn, & val ) == ESP_OK)
                MinOnTicks = val;
            if (nvs_get_u32( my_handle, s_keyMaxOn, & val ) == ESP_OK)
                MaxOnTicks = val;
        }

        nvs_close( my_handle );
    }
}

void Control::WriteParam()
{
    nvs_handle my_handle;
    if (nvs_open( "control", NVS_READWRITE, &my_handle ) == ESP_OK) {
        SetU16( my_handle, s_keyThresOff, ThresOff );
        SetU16( my_handle, s_keyThresOn,  ThresOn );
        SetU32( my_handle, s_keyMinOff,   MinOffTicks );
        SetU32( my_handle, s_keyMinOn,    MinOnTicks );
        SetU32( my_handle, s_keyMaxOn,    MaxOnTicks );
        nvs_close( my_handle );
    }
}

void Control::SetU16( nvs_handle nvs, const char * key, uint16_t val )
{
    uint16_t oldval;
    if (nvs_get_u16( nvs, key, & oldval ) == ESP_OK)
        if (oldval == val)
            return;
    nvs_set_u16( nvs, key, val );
    ESP_LOGI( TAG, "set u16 %s=%d", key, val );
}

void Control::SetU32( nvs_handle nvs, const char * key, uint32_t val )
{
    uint32_t oldval;
    if (nvs_get_u32( nvs, key, & oldval ) == ESP_OK)
        if (oldval == val)
            return;
    nvs_set_u32( nvs, key, val );
    ESP_LOGI( TAG, "set u32 %s=%d", key, val );
}

namespace {
void SendConfigRow( struct httpd_req * req, const char * key, float min, float max, int decimals, const char * unit, float val, const char * title )
{
    char str[10];

    SendStringChunk( req, "<tr><td>" );
    SendStringChunk( req, title );
    SendStringChunk( req, "</td><td align=\"right\"><input type=\"number" );
    SendStringChunk( req, "\" name=\""  ); SendStringChunk( req, key );
    SendStringChunk( req, "\" min=\""   ); snprintf( str, sizeof(str), "%.*f", decimals, min ); SendStringChunk( req, str );
    SendStringChunk( req, "\" max=\""   ); snprintf( str, sizeof(str), "%.*f", decimals, max ); SendStringChunk( req, str );
    SendStringChunk( req, "\" value=\"" ); snprintf( str, sizeof(str), "%.*f", decimals, val ); SendStringChunk( req, str );
    SendStringChunk( req, "\" step=\""  ); snprintf( str, sizeof(str), "%f", pow( 10,-decimals ) ); SendStringChunk( req, str );
    SendStringChunk( req, "\" /></td><td>" );
    SendStringChunk( req, unit );
    SendStringChunk( req, "</td></tr>" );
}
}

void Control::Setup( struct httpd_req * req )
{
    size_t const buf_len = httpd_req_get_url_query_len( req );
    if (buf_len) {
        ESP_LOGI( TAG, "query length: %d", buf_len );
        char * const buf = (char *) malloc( buf_len + 1 );
        if (httpd_req_get_url_query_str( req, buf, buf_len + 1 ) == ESP_OK) {
            ESP_LOGI( TAG, "query: %s", buf );
            char * bp = buf;
            while (bp && *bp) {
                char * key = bp;
                bp = strchr( key, '=' );
                if (! bp)
                    break;
                *bp++ = 0;
                char * val = bp;
                bp = strchr( val, '&' );
                if (bp)
                    *bp++ = 0;

                float x;
                sscanf( val, "%f", &x );
                ESP_LOGI( TAG, "get %s=%f", key, x );

                if (! strcmp( key, s_keyThresOff )) {
                    ThresOff = (thres_t) (x * 10.24);
                } else if (! strcmp( key, s_keyThresOn )) {
                    ThresOn = (thres_t) (x * 10.24);
                } else if (! strcmp( key, s_keyMinOff )) {
                    MinOffTicks = (timo_t) (x * configTICK_RATE_HZ);
                } else if (! strcmp( key, s_keyMinOn )) {
                    MinOnTicks = (timo_t) (x * configTICK_RATE_HZ);
                } else if (! strcmp( key, s_keyMaxOn )) {
                    MaxOnTicks = (timo_t) (x * configTICK_RATE_HZ);
                } else {
                    ESP_LOGE( TAG, "unknown key: %s=%f", key, x );
                }
            }
        }
        free( buf );
        WriteParam();
    }
    static char s_data1[] = "<body>"
                            "<form>"
                            "<table border=0>";
    static char s_data9[] = "</table>"
                            "<button type=\"submit\">submit</button>"
                            "</form>"
                            "</body>";

    SendCharsChunk( req, s_data1 );

    SendConfigRow( req, s_keyThresOff, 0, 100, 0, "%", ThresOff / 10.24, "switching off threshold" );
    SendConfigRow( req, s_keyThresOn,  0, 100, 0, "%", ThresOn  / 10.24, "switching on threshold" );
    SendConfigRow( req, s_keyMinOff, 0.01, 60, 2, "secs", MinOffTicks * 1.0 / configTICK_RATE_HZ, "min. off time" );
    SendConfigRow( req, s_keyMinOn,  0.01, 60, 2, "secs", MinOnTicks  * 1.0 / configTICK_RATE_HZ, "min. on time" );
    SendConfigRow( req, s_keyMaxOn,  1, 86400, 0, "secs", MaxOnTicks  * 1.0 / configTICK_RATE_HZ, "max. on time" );

    SendCharsChunk( req, s_data9 );
    httpd_resp_send_chunk( req, 0, 0 );
}
