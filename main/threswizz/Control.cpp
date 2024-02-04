/*
 * Control.cpp
 *
 *  Created on: 19.05.2020
 *      Author: holger
 */

//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "Control.h"

#include <string>   // std::string

#include <FreeRTOSConfig.h>
#include <esp_log.h>
#include <nvs.h>            // nvs_open(), ...

#include "AnalogReader.h"
#include "Relay.h"
#include "Indicator.h"

#include "HttpHelper.h"
#include "HttpTable.h"
#include "HttpParser.h"
#include "WebServer.h"
#include "Wifi.h"

const char * const TAG = "Control";

extern "C" esp_err_t get_config( httpd_req_t * req );
extern "C" esp_err_t post_config( httpd_req_t * req );

namespace
{
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
const httpd_uri_t s_get_uri   = { .uri = "/config", .method = HTTP_GET,  .handler = get_config,  .user_ctx = 0 };
const httpd_uri_t s_post_uri  = { .uri = "/config", .method = HTTP_POST, .handler = post_config, .user_ctx = 0 };
const WebServer::Page s_page    { s_get_uri, "Configure switching thresholds" };
//@formatter:on

const char *s_keyThresOff = "thresOff";
const char *s_keyThresOn  = "thresOn";
const char *s_keyMinOff   = "minOff";
const char *s_keyMinOn    = "minOn";
const char *s_keyMaxOn    = "maxOn";

Control *s_control = 0;

extern "C" esp_err_t get_config( httpd_req_t * req )
{
    if (s_control)
        s_control->Setup( req );
    return ESP_OK;
}

extern "C" esp_err_t post_config( httpd_req_t * req )
{
    if (s_control)
        s_control->Setup( req, true );
    return ESP_OK;
}

//@formatter:off
Control::Control( AnalogReader & reader, Relay & relay, thres_t thresOff, thres_t thresOn )
                : mReader        { reader },
                  mRelay         { relay },
                  mThresOff      { thresOff },
                  mThresOn       { thresOn },
                  mMinOffTicks   { configTICK_RATE_HZ },
                  mMinOnTicks    { configTICK_RATE_HZ * 15 },
                  mMaxOnTicks    { configTICK_RATE_HZ * 60 * 60 }
//@formatter:on
{
    mRelay.AutoOn( false );
    mRelay.SetMode( Relay::MODE_AUTO );

    if (1 || Wifi::Instance().StationMode()) {
        s_control = this;
        WebServer::Instance().AddPage( s_page, & s_post_uri );
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
                if (mThresOff > mThresOn)
                    toSwitch = mReader.Average( 10 ) >= mThresOff;
                else
                    toSwitch = mReader.Average( 10 ) <= mThresOff;
            }
            if (toSwitch) {
                mRelay.AutoOn( false );
                indicator.Indicate( Indicator::STATUS_IDLE );
                exp = 0;
                if (mMinOffTicks) {
                    vTaskDelay( mMinOffTicks );
                    continue;
                }
            }
        } else {
            if (mThresOff > mThresOn)
                toSwitch = mReader.Average( 10 ) <= mThresOn;
            else
                toSwitch = mReader.Average( 10 ) >= mThresOn;
            if (toSwitch) {
                mRelay.AutoOn( true );
                indicator.Indicate( Indicator::STATUS_ACTIVE );
                exp = expiration( mMaxOnTicks );
                if (mMinOnTicks) {
                    vTaskDelay( mMinOnTicks );
                    continue;
                }
            }
        }

        vTaskDelay( configTICK_RATE_HZ / 10 );
    }
}

void Control::ReadParam()
{
    ESP_LOGI( TAG, "Reading control configuration" );

    nvs_handle my_handle;
    if (nvs_open( "control", NVS_READONLY, &my_handle ) == ESP_OK) {
        {
            uint16_t val;
            if (nvs_get_u16( my_handle, s_keyThresOff, & val ) == ESP_OK)
                mThresOff = val;
            if (nvs_get_u16( my_handle, s_keyThresOn, & val ) == ESP_OK)
                mThresOn = val;
        }
        {
            uint32_t val;
            if (nvs_get_u32( my_handle, s_keyMinOff, & val ) == ESP_OK)
                mMinOffTicks = val;
            if (nvs_get_u32( my_handle, s_keyMinOn, & val ) == ESP_OK)
                mMinOnTicks = val;
            if (nvs_get_u32( my_handle, s_keyMaxOn, & val ) == ESP_OK)
                mMaxOnTicks = val;
        }
        nvs_close( my_handle );

        ESP_LOGD( TAG, "thresOff = %6d   %%",   mThresOff );
        ESP_LOGD( TAG, "thresOn  = %6d   %%",   mThresOn  );
        ESP_LOGD( TAG, "minOff   = %8lu ticks", mMinOffTicks );
        ESP_LOGD( TAG, "minOn    = %8lu ticks", mMinOnTicks );
        ESP_LOGD( TAG, "maxOn    = %8lu ticks", mMaxOnTicks );
    }
}

void Control::WriteParam()
{
    ESP_LOGI( TAG, "Writing control configuration" );

    ESP_LOGD( TAG, "thresOff = %6d/1023",   mThresOff );
    ESP_LOGD( TAG, "thresOn  = %6d/1023",   mThresOn  );
    ESP_LOGD( TAG, "minOff   = %8lu ticks", mMinOffTicks );
    ESP_LOGD( TAG, "minOn    = %8lu ticks", mMinOnTicks );
    ESP_LOGD( TAG, "maxOn    = %8lu ticks", mMaxOnTicks );

    nvs_handle my_handle;
    if (nvs_open( "control", NVS_READWRITE, &my_handle ) == ESP_OK) {
        SetU16( my_handle, s_keyThresOff, mThresOff );
        SetU16( my_handle, s_keyThresOn,  mThresOn );
        SetU32( my_handle, s_keyMinOff,   mMinOffTicks );
        SetU32( my_handle, s_keyMinOn,    mMinOnTicks );
        SetU32( my_handle, s_keyMaxOn,    mMaxOnTicks );
        nvs_commit( my_handle );
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

void Control::Setup( struct httpd_req * req, bool post )
{
    HttpHelper hh{ req, "Configuration" };

    if (post) {
        {
            char bufThresOff[4];
            char bufThresOn[4];
            char bufMinOff[4];
            char bufMinOn[4];
            char bufMaxOn[8];
            HttpParser::Input in[] = { { s_keyThresOff, bufThresOff, sizeof(bufThresOff) },
                                       { s_keyThresOn,  bufThresOn,  sizeof(bufThresOn)  },
                                       { s_keyMinOff,   bufMinOff,   sizeof(bufMinOff)   },
                                       { s_keyMinOn,    bufMinOn,    sizeof(bufMinOn)    },
                                       { s_keyMaxOn,    bufMaxOn,    sizeof(bufMaxOn)    } };
            HttpParser parser{ in, sizeof(in) / sizeof(in[0]) };

            if (! parser.ParsePostData( req )) {
                hh.Add( "unexpected end of data while parsing data" );
                return;
            }

            mThresOff    = (thres_t) ((strtoul( bufThresOff, 0, 10 ) * 1023 + 50) / 100);
            mThresOn     = (thres_t) ((strtoul( bufThresOn,  0, 10 ) * 1023 + 50) / 100);
            mMinOffTicks =            (strtoul( bufMinOff,   0, 10 ) * configTICK_RATE_HZ);
            mMinOnTicks  =            (strtoul( bufMinOn,    0, 10 ) * configTICK_RATE_HZ);
            mMaxOnTicks  =            (strtoul( bufMaxOn,    0, 10 ) * configTICK_RATE_HZ);
        }
        WriteParam();
    }
    hh.Add( " <form method=\"post\">\n"
            "  <table>\n" );
    {
        Table<5,4> table;
        table.Right( 0 );
        table.Right( 2 );
        table[0][1] = "&nbsp;";

        table[0][0] = "switching off threshold";
        table[1][0] = "switching on threshold";
        table[2][0] = "min. off time";
        table[3][0] = "min. on time";
        table[4][0] = "max. on time";

        table[0][3] = "&percnt;";
        table[1][3] = "&percnt;";
        table[2][3] = "secs";
        table[3][3] = "secs";
        table[4][3] = "secs";

        table[0][2] = InputField( s_keyThresOff, 0, 100, ((long) mThresOff * 100 + 511) / 1023 );
        table[1][2] = InputField( s_keyThresOn,  0, 100, ((long) mThresOn  * 100 + 511) / 1023 );
        table[2][2] = InputField( s_keyMinOff,   1,  60, ((long) mMinOffTicks + configTICK_RATE_HZ/2) / configTICK_RATE_HZ );
        table[3][2] = InputField( s_keyMinOn,   1,   60, ((long) mMinOnTicks  + configTICK_RATE_HZ/2) / configTICK_RATE_HZ );
        table[4][2] = InputField( s_keyMaxOn,  1, 86400, ((long) mMaxOnTicks  + configTICK_RATE_HZ/2) / configTICK_RATE_HZ );

        table.AddTo( hh );
    }
    hh.Add( "  </table>\n"
            "  <button type=\"submit\">submit</button>\n"
            " </form>\n" );
}
