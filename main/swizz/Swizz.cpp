/*
 * Swizz.cpp
 *
 * Switch relays by mqtt messages
 */
//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "Swizz.h"

#include <esp_log.h>
#include <nvs.h>            // nvs_open(), ...

#include "Relay.h"
#include "Indicator.h"
#include "Mqtinator.h"

#include "HttpHelper.h"
#include "HttpTable.h"
#include "HttpParser.h"
#include "WebServer.h"
//include "Wifi.h"

const char * const TAG = "Swizz";

extern "C" esp_err_t get_swizz_config( httpd_req_t * req );
extern "C" esp_err_t post_swizz_config( httpd_req_t * req );

/* on:
 *   dz/cg/dn/338 {
 *   "Battery" : 255,
 *   "LastUpdate" : "2024-03-04 14:03:11",
 *   "RSSI" : 12,
 *   "description" : "",
 *   "dtype" : "Light/Switch",
 *   "hwid" : "7",
 *   "id" : "000141A2",
 **  "idx" : 338,
 *   "name" : "Teichfilter-UV-Lampe",
 **  "nvalue" : 1,
 *   "org_hwid" : "7",
 *   "stype" : "Switch",
 *   "svalue1" : "0",
 *   "switchType" : "On/Off",
 *   "unit" : 1
 *   }
 * off:
 *   dz/cg/dn/338 {
 *   "Battery" : 255,
 *   "LastUpdate" : "2024-03-04 14:04:43",
 *   "RSSI" : 12,
 *   "description" : "",
 *   "dtype" : "Light/Switch",
 *   "hwid" : "7",
 *   "id" : "000141A2",
 **  "idx" : 338,
 *   "name" : "Teichfilter-UV-Lampe",
 **  "nvalue" : 0,
 *   "org_hwid" : "7",
 *   "stype" : "Switch",
 *   "svalue1" : "0",
 *   "switchType" : "On/Off",
 *   "unit" : 1
 *   }
 */

const httpd_uri_t s_get_uri   = { .uri = "/switch", .method = HTTP_GET,  .handler = get_swizz_config,  .user_ctx = 0 };
const httpd_uri_t s_post_uri  = { .uri = "/switch", .method = HTTP_POST, .handler = post_swizz_config, .user_ctx = 0 };
const WebServer::Page s_page    { s_get_uri, "Switches" };

const char *s_keyIdx = "idx";  // domoticz device index to store

Swizz *s_swizz = 0;

extern "C" {

esp_err_t get_swizz_config( httpd_req_t * req )
{
    if (s_swizz)
        s_swizz->Setup( req );
    return ESP_OK;
}

esp_err_t post_swizz_config( httpd_req_t * req )
{
    if (s_swizz)
        s_swizz->Setup( req, true );
    return ESP_OK;
}

void on_swizz_relay( const char * topic, const char * data )
{
    ESP_LOGI( TAG, "got \"%s\" \"%.16s...\" (%d bytes)", topic, data, strlen(data) );

    if (s_swizz)
        s_swizz->SwitchRelay( topic, data );
}

void on_swizz_wd_request( const char * topic, const char * data )
{
    ESP_LOGI( TAG, "got \"%s\" \"%.16s...\" (%d bytes)", topic, data, strlen(data) );

    if (s_swizz)
        s_swizz->WdRequest( topic, data );
}

}

Swizz::Swizz( Relay relay[], size_t nofRelays )
            : mRelay { relay }
            , mNofRelays { (uint8_t) nofRelays }
{
    mDzIdx.resize( mNofRelays, 0 );

    s_swizz = this;
    WebServer::Instance().AddPage( s_page, & s_post_uri );
}

void Swizz::SwitchRelay( const char * topic, const char * data )
{
    const char * cp = strstr( data, "\"idx\"" );
    if (! cp) {
        ESP_LOGE( TAG, "\"idx\" not found" );
        return;
    }
    cp += 5;
    while ((*cp == ' ') || (*cp == ':'))
        ++cp;
    char * end;
    const char * const sidx = cp;
    unsigned long val = strtoul( sidx, &end, 10 );

    uint8_t r;
    for (r = 0; r < mNofRelays; ++r)
        if (mDzIdx[r] == val)
            break;
    if (r >= mNofRelays) {
        ESP_LOGI( TAG, "\"idx\" %lu does not match any relay", val );
        return;
    }
    cp = strstr( end, "\"nvalue\"" );
    if (! cp) {
        ESP_LOGE( TAG, "\"nvalue\" not found" );
        return;
    }
    cp += 8;
    while ((*cp == ' ') || (*cp == ':'))
        ++cp;
    val = strtoul( cp, &end, 10 );

    ESP_LOGI( TAG, "will set relay %d to mode %ld", r, val );
    mRelay[r].SetMode( val ? Relay::MODE_ON : Relay::MODE_OFF );

    xTaskNotify( mTaskHandle, 1 << r, eSetBits );  // confirm in task context
}

void Swizz::WdRequest( const char * topic, const char * data )
{
    xTaskNotify( mTaskHandle, (1 << mNofRelays) - 1, eSetBits );  // confirm in task context
}

void Swizz::Run()
{
    Indicator::Instance().Indicate( Indicator::STATUS_IDLE );

    ReadParam();
    mTaskHandle = xTaskGetCurrentTaskHandle();

    Mqtinator & mqtinator = Mqtinator::Instance();

    for (uint8_t r = 0; r < mNofRelays; ++r)
        if (mDzIdx[r]) {
            char idxbuf[8];
            char * sidx = HttpHelperI2A( idxbuf, mDzIdx[r] );
            mqtinator.Sub( sidx, & on_swizz_relay );
        }

    mqtinator.WdSub( "status", & on_swizz_wd_request );

    while (true)
    {
        uint32_t notification = 0;
        BaseType_t succ = xTaskNotifyWait( 0, 0xffffffff, & notification, portMAX_DELAY );
        if (! succ)
            continue;

        while (notification) {
            uint8_t r = 31 - __builtin_clz( notification );
            notification ^= 1 << r;
            if (! mDzIdx[r])
                continue;

            char idxbuf[8];
            char * sidx = HttpHelperI2A( idxbuf, mDzIdx[r] );
            std::string confirmData{ "{ \"idx\": " };
            confirmData += sidx;
            confirmData += ", \"nvalue\": ";
            confirmData += mRelay[r].GetMode() == Relay::MODE_OFF ? "0" : "1";
            confirmData += " }";

            mqtinator.WdPub( 0, confirmData.c_str() );
        }
    }
}

void Swizz::ReadParam()
{
    ESP_LOGI( TAG, "Reading swizz configuration" );

    nvs_handle my_handle;
    if (nvs_open( "swizz", NVS_READONLY, &my_handle ) == ESP_OK) {
        std::string keyPrefix = s_keyIdx;
        for (uint8_t r = 0; r < mNofRelays; ++r) {
            uint16_t val;
            if (nvs_get_u16( my_handle, (keyPrefix + std::to_string( (int) r )).c_str(), & val ) == ESP_OK)
                mDzIdx[r] = val;
        }
        nvs_close( my_handle );
    }
}

void Swizz::WriteParam()
{
    nvs_handle my_handle;
    if (nvs_open( "swizz", NVS_READWRITE, &my_handle ) == ESP_OK) {
        std::string keyPrefix = s_keyIdx;
        for (uint8_t r = 0; r < mNofRelays; ++r) {
            nvs_set_u16( my_handle, (keyPrefix + std::to_string( (int) r )).c_str(), mDzIdx[r] );
        }
        nvs_commit( my_handle );
        nvs_close( my_handle );
    }
}


namespace {

std::string InputField( const char * key, uint8_t num, long min, long max, long val )
{
    std::string str {"<input type=\"number\" name=\"" }; str += key; str += HttpHelper::String( (long) num );
    str += "\" min=\"";   str += HttpHelper::String( min );
    str += "\" max=\"";   str += HttpHelper::String( max );
    str += "\" value=\""; str += HttpHelper::String( val );
    str += "\" />";
    return str;
}

}

void Swizz::Setup( struct httpd_req * req, bool post )
{
    HttpHelper hh{ req, "Configure switching relays", "Swizz" };

    if (post) {
        {
            char bufVal[mNofRelays][6];
            char bufKey[mNofRelays][8];

            HttpParser::Input in[mNofRelays];
            uint8_t const strLen = (uint8_t) (sizeof(s_keyIdx) - 1);

            for (uint8_t r = 0; r < mNofRelays; ++r) {
                char * pKey = bufKey[r];
                char * bp = pKey + sizeof(bufKey[0]) - 1;
                *bp = 0;
                uint8_t x = r;
                do {
                    *--bp = (x % 10) + '0';
                    x /= 10;
                } while (x);
                uint8_t const numOffset = (uint8_t) (bp - pKey);
                uint8_t const strOffset = numOffset - strLen;
                pKey += strOffset;
                memcpy( pKey, s_keyIdx, strLen );
                in[r] = HttpParser::Input( pKey, bufVal[r], sizeof(bufVal[0]) );
            }
            HttpParser parser{ in, mNofRelays };

            const char * parseError = parser.ParsePostData( req );
            if (parseError) {
                hh.Add( "parser error: " );
                hh.Add( parseError );
                return;
            }

            for (uint8_t r = 0; r < mNofRelays; ++r) {
                mDzIdx[r] = (uint16_t) strtoul( bufVal[r], 0, 10 );
                ESP_LOGD( TAG, "parsed %s%d as \"%s\" - %d", s_keyIdx, r, bufVal[r], mDzIdx[r] );
            }
        }
        WriteParam();
    }

    hh.Add( " <form method=\"post\">\n"
            "  <table>\n" );
    {
        Table<8+1,4> table;
        table.Right( 0 );
        table.Right( 2 );
        table[0][1] = "&nbsp;";  // some space due to right adjust of Parameter
        table[0][0] = "Relay №";
        table[0][2] = "Device index";
        table[0][3] = "Domoticz device index";
        for (uint8_t r = 0; r < mNofRelays; ++r) {
            table[r+1][0] = std::to_string( (int) r + 1 );
            table[r+1][2] = InputField( s_keyIdx, r, 0, 65535, mDzIdx[r] );
        }
        table[mNofRelays+1][2] = "<br /><center><button type=\"submit\">submit</button></center>";
        table[mNofRelays+1][4] = "<br />submit the values to be stored on the device";
        table.AddTo( hh, 1 );
    }
    hh.Add( "  </table>\n"
            " </form>\n" );
}
