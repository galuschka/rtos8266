/*
 * Wifi.cpp
 */

//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "Wifi.h"
#include "Indicator.h"
#include "WebServer.h"
#include "HttpParser.h"
#include "HttpHelper.h"
#include "HttpTable.h"

#include <string.h>         // strncpy()

#include <esp_wifi.h>       // esp_wifi_init(), ...
#include <nvs.h>            // nvs_open(), ...
#include <esp_ota_ops.h>    // esp_ota_get_app_description()
#include <esp_log.h>        // ESP_LOGI()
#include <esp_http_server.h>
#include <string>

#if (LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG)
#define EXPRD(expr) do { expr; } while(0);
#else
#define EXPRD(expr)
#endif

namespace
{
const char * const TAG               = "Wifi";

Wifi               s_wifi{};

const char * const s_nvsNamespace    = "wifi";
const char * const s_keyHost         = "host";
const char * const s_keyBgCol        = "bgcol";
const char * const s_keySsid[2]      = { "ssid",      "ssid1" };
const char * const s_keyPassword[2]  = { "password",  "password1" };
const char * const s_keyNoStation[2] = { "nostation", "nostation1" };
}

Wifi & Wifi::Instance()
{
    return s_wifi;
}

void Wifi::ReadParam()
{
    ESP_LOGI( TAG, "Reading Wifi configuration" );

    nvs_handle my_handle;
    if (nvs_open( s_nvsNamespace, NVS_READONLY, &my_handle ) == ESP_OK) {
        ESP_LOGD( TAG, "Reading Host" );
        size_t len = sizeof(mHost);
        nvs_get_str( my_handle, s_keyHost, mHost, &len );
        ESP_LOGD( TAG, "Reading BgCol" );
        len = sizeof(mBgCol);
        nvs_get_str( my_handle, s_keyBgCol, mBgCol, &len );
        ESP_LOGD( TAG, "Reading SSID" );
        for (int i =0; i < 2; ++i) {
            len = sizeof(mSsid[0]);
            nvs_get_str( my_handle, s_keySsid[i], mSsid[i], &len );
        }
        ESP_LOGD( TAG, "Reading password" );
        for (int i =0; i < 2; ++i) {
            len = sizeof(mPasswd[0]);
            nvs_get_str( my_handle, s_keyPassword[i], mPasswd[i], &len );
        }
        ESP_LOGD( TAG, "Reading no station counter" );
        for (int i =0; i < 2; ++i) {
            uint16_t no_station;
            if (nvs_get_u16( my_handle, s_keyNoStation[i], & no_station ) == ESP_OK)
                mNoStation[i] = no_station;
        }
        nvs_close( my_handle );
    }
    if (!mHost[0]) {
        uint8_t mac[6];
        esp_read_mac( mac, ESP_MAC_WIFI_SOFTAP );
        const esp_app_desc_t *const app_description =
                                    esp_ota_get_app_description();
        snprintf( mHost, sizeof(mHost) - 1, "%.*s-%02x-%02x-%02x",
                    sizeof(mHost) - 11, app_description->project_name,
                    mac[3], mac[4], mac[5] );
        mHost[ sizeof(mHost) - 1 ] = 0;
    }
}

void Wifi::SaveNoStation() const
{
    ESP_LOGI( TAG, "Saving no station after reboot counters" );
    nvs_handle my_handle;
    if (nvs_open( s_nvsNamespace, NVS_READWRITE, &my_handle ) == ESP_OK) {
        for (int i = 0; i < 2; ++i) {
            if (nvs_set_u16( my_handle, s_keyNoStation[i], mNoStation[i] ) != ESP_OK) {
                ESP_LOGE( TAG, "could not save no station counter %d: %d - nvs_set_u16 failed", i, mNoStation[i] );
            }
        }
        nvs_commit( my_handle );
        nvs_close( my_handle );
    } else {
        ESP_LOGE( TAG, "could not save no station counters - nvs_open failed" );
    }
}

bool Wifi::SetParam( const char * host, const char * bgcol, const char * ssid0, const char * password0, const char * ssid1, const char * password1 )
{
    nvs_handle my_handle;
    esp_err_t esp;
    esp = nvs_open( s_nvsNamespace, NVS_READWRITE, &my_handle );
    if (esp != ESP_OK) {
        ESP_LOGE( TAG, "nvs_open( \"%s\", ... ) failed: %d", s_nvsNamespace, esp );
        return false;
    }

    if (host && *host) {
        // ESP_LOGI( TAG, "set hostname to \"%s\"", host );
        esp = nvs_set_str( my_handle, s_keyHost, host );
        if (esp == ESP_OK) {
            strncpy( mHost, host, sizeof(mHost) );
            // ESP_LOGI( TAG, "hostname set to \"%s\"", mHost );
        // } else {
            // ESP_LOGE( TAG, "attempt to set hostname failed (error %d)", esp );
        }
    }
    if (bgcol && *bgcol) {
        esp = nvs_set_str( my_handle, s_keyBgCol, bgcol );
        if (esp == ESP_OK) {
            strncpy( mBgCol, bgcol, sizeof(mBgCol) );
        }
    }
    const char * ssid[2] = { ssid0, ssid1 };
    const char * pass[2] = { password0, password1 };
    for (int i = 0; i < 2; ++i) {
        if ((esp == ESP_OK) && ssid[i]) {
            esp = nvs_set_str( my_handle, s_keySsid[i], ssid[i] );
            if (esp == ESP_OK)
                strncpy( mSsid[i], ssid[i], sizeof(mSsid[0]) );
        }
        if ((esp == ESP_OK) && pass[i]) {
            esp = nvs_set_str( my_handle, s_keyPassword[i], pass[i] );
            if (esp == ESP_OK)
                strncpy( mPasswd[i], pass[i], sizeof(mPasswd[0]) );
        }
        if ((esp == ESP_OK) && ssid[i] && ! ssid[i][0]) {
            esp = nvs_set_str( my_handle, s_keyPassword[i], "" ); // clear on ssid clear to have a chance to unset
        }
        if (mNoStation[i]) {
            mNoStation[i] = 0;
            nvs_set_u16( my_handle, s_keyNoStation[i], mNoStation[i] );
        }
    }
    nvs_commit( my_handle );
    nvs_close( my_handle );
    return esp == ESP_OK;
}

extern "C" void wifi_event( void * wifi, esp_event_base_t event_base, int32_t event_id, void * event_data )
{
    ((Wifi*) wifi)->Event( event_base, event_id, event_data );
}

void Wifi::Event( esp_event_base_t event_base, int32_t event_id, void * event_data )
{
    if (event_id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t *event =
                (wifi_event_sta_connected_t*) event_data;
        ESP_LOGI( TAG, "joined to \"%s\"", event->ssid );
        mMode = MODE_WAITDHCP;
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW( TAG, "left WLAN" );
        if (mMode == MODE_RECONNECTING) {
            ESP_LOGW( TAG, "disconnected during re-connect -> reboot in 1 second" );
            ++mNoStation[mStaIdx];
            SaveNoStation();
            vTaskDelay( configTICK_RATE_HZ );
            esp_restart();
        }
        mMode = MODE_RECONNECTING;
        ESP_LOGW( TAG, "re-connecting..." );
        // esp_wifi_start();
        esp_wifi_connect();
    } else {
        ESP_LOGE( TAG, "unhandled wifi event %d", event_id );
    }
}

extern "C" void ip_event( void * wifi, esp_event_base_t event_base,
        int32_t event_id, void * event_data )
{
    switch (event_id) {
    case IP_EVENT_STA_GOT_IP:
        ESP_LOGI( TAG, "ip event \"got ip address\"" );
        ((Wifi*) wifi)->GotIp( (ip_event_got_ip_t*) event_data );
        break;
    case IP_EVENT_STA_LOST_IP:
        ESP_LOGW( TAG, "ip event \"lost ip address\"" );
        ((Wifi*) wifi)->LostIp();
        break;
    case IP_EVENT_AP_STAIPASSIGNED:
        ESP_LOGI( TAG, "ip event \"new client ip address assignment\"" );
        ((Wifi*) wifi)->NewClient( (ip_event_ap_staipassigned_t*) event_data );
        break;
    default:
        ESP_LOGI( TAG, "unhandled ip event %d", event_id );
        break;
    }
}

void Wifi::GotIp( ip_event_got_ip_t * event )
{
    memcpy( &mIpAddr, &event->ip_info.ip, sizeof(ip4_addr_t) );
    xEventGroupSetBits( mConnectEventGroup, GOT_IPV4_BIT );
}

void Wifi::LostIp( void )
{
    memset( &mIpAddr, 0, sizeof(ip4_addr_t) );
    xEventGroupSetBits( mConnectEventGroup, LOST_IPV4_BIT );
}

void Wifi::NewClient( ip_event_ap_staipassigned_t * event )
{
    xEventGroupSetBits( mConnectEventGroup, NEW_CLIENT_BIT );
}

bool Wifi::ModeSta( int connTimoInSecs )
{
    if (! mSsid[0][0]) {
        if (! mSsid[1][0]) {
            return false;   // neither 0 nor 1 has valid ssid -> AP mode
        }
        mStaIdx = 1;  // just 1 valid
    } else if (mSsid[1][0] && (mNoStation[0] > mNoStation[1]))
        mStaIdx = 1;  // both valid, but 1 has less errors
    else
        mStaIdx = 0;

    mMode = MODE_CONNECTING;

    ESP_LOGI( TAG, "Connecting to \"%s\" ...", mSsid[mStaIdx] );

    wifi_config_t wifi_config;
    memset( &wifi_config, 0, sizeof(wifi_config_t) );

    strncpy( (char*) wifi_config.sta.ssid,     mSsid[mStaIdx],   sizeof(wifi_config.sta.ssid) );
    strncpy( (char*) wifi_config.sta.password, mPasswd[mStaIdx], sizeof(wifi_config.sta.password) );

    ESP_ERROR_CHECK( esp_wifi_set_storage( WIFI_STORAGE_RAM ) );
    ESP_ERROR_CHECK( esp_wifi_set_mode( WIFI_MODE_STA ) );
    ESP_ERROR_CHECK( esp_wifi_set_config( ESP_IF_WIFI_STA, &wifi_config ) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    ESP_ERROR_CHECK( esp_wifi_connect() );

    if (!xEventGroupWaitBits( mConnectEventGroup, GOT_IPV4_BIT, true, true,
                            configTICK_RATE_HZ * connTimoInSecs )) {
        mMode = MODE_CONNECTFAILED;
        ESP_LOGW( TAG, "Connection to %s timed out - setup AP", mSsid[mStaIdx] );
        return false;
    }
    mMode = MODE_STATION;
    ESP_LOGI( TAG, "Connected to %s", mSsid[mStaIdx] );
    ESP_LOGI( TAG, "IPv4 address: " IPSTR, IP2STR( & mIpAddr ) );
    /*
    * ESP_LOGI( TAG, "reset no station after reboot counter" );
    * mNoStation = 0;
    * SaveNoStation();
    */
    return true;
}

void Wifi::ModeAp()
{
    wifi_config_t wifi_config;
    memset( &wifi_config, 0, sizeof(wifi_config_t) );

    strncpy( (char*) wifi_config.ap.ssid, mHost, sizeof(wifi_config.ap.ssid) - 1 );
    wifi_config.ap.ssid[ sizeof(wifi_config.ap.ssid) - 1 ] = 0;
    wifi_config.ap.ssid_len = strlen( (char*) wifi_config.ap.ssid );

    /* if (mPassword[0]) {
        wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
        strncpy( (char*) wifi_config.ap.password, mPassword, sizeof(wifi_config.ap.password) );
    } else */
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    wifi_config.ap.max_connection = 1;

    ESP_LOGI( TAG, "Setup AP \"%s\" ...", wifi_config.ap.ssid );
    if (LOG_LOCAL_LEVEL >= ESP_LOG_INFO)
        vTaskDelay( 1 );      // output collides with esp_wifi_set_mode() output
    ESP_ERROR_CHECK( esp_wifi_set_mode( WIFI_MODE_AP ) );
    ESP_ERROR_CHECK( esp_wifi_set_config( ESP_IF_WIFI_AP, & wifi_config ) );
    ESP_ERROR_CHECK( esp_wifi_start() );

    mMode = MODE_ACCESSPOINT;
}

void Wifi::Init( int connTimoInSecs )
{
    ESP_LOGD( TAG, "xEventGroupCreate()" ); EXPRD(vTaskDelay(1))
    mConnectEventGroup = xEventGroupCreate();

    ESP_LOGD( TAG, "ReadParam()" ); EXPRD(vTaskDelay(1))
    ReadParam();

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT()
    ;
    ESP_LOGD( TAG, "esp_wifi_init()" ); EXPRD(vTaskDelay(1))

    ESP_ERROR_CHECK( esp_wifi_init( &wifi_init_config ) );
    ESP_LOGD( TAG, "esp_event_handler_register(WIFI_EVENT)" ); EXPRD(vTaskDelay(1))
    ESP_ERROR_CHECK(
            esp_event_handler_register( WIFI_EVENT, ESP_EVENT_ANY_ID, & wifi_event, this ) );
    ESP_LOGD( TAG, "esp_event_handler_register(IP_EVENT)" ); EXPRD(vTaskDelay(1))
    ESP_ERROR_CHECK(
            esp_event_handler_register( IP_EVENT, ESP_EVENT_ANY_ID, & ip_event, this ) );

    if (connTimoInSecs && (mSsid[0][0] || mSsid[1][0])) {
        Indicator::Instance().Indicate( Indicator::STATUS_CONNECT );
        ESP_LOGD( TAG, "ModeSta()" );
        if (ModeSta( connTimoInSecs )) {
            Indicator::Instance().Indicate( Indicator::STATUS_IDLE );
            return;
        }
    }
    Indicator::Instance().Indicate( Indicator::STATUS_AP );
    ESP_LOGD( TAG, "ModeAp()" ); EXPRD(vTaskDelay(1))
    ModeAp();
}

///////////////////// web interface /////////////////////

extern "C" {

esp_err_t handler_get_wifi( httpd_req_t * req )
{
    s_wifi.Setup( req );
    return ESP_OK;
}

esp_err_t handler_post_wifi( httpd_req_t * req )
{
    s_wifi.Setup( req, true );
    return ESP_OK;
}

}

namespace
{
const char * const s_subWifi    = "WLAN parameter";
const httpd_uri_t  uri_get_wifi = { .uri = "/wifi", .method = HTTP_GET,  .handler = handler_get_wifi,  .user_ctx = 0 };
const httpd_uri_t uri_post_wifi = { .uri = "/wifi", .method = HTTP_POST, .handler = handler_post_wifi, .user_ctx = 0 };
const WebServer::Page page_wifi   { uri_get_wifi, "Wifi" };
}

void Wifi::AddPage( WebServer & webserver )
{
    webserver.AddPage( page_wifi, & uri_post_wifi );
}

void Wifi::Setup( httpd_req_t * req, bool post )
{
    std::string postError{};
    while (post)
    {
        if (! req->content_len) {
            postError = "no data - nothing to be done";
            break;
        }

        char * host  = 0;
        char * bgcol = 0;
        char * id[2] = { 0, 0 };
        char * pw[2] = { 0, 0 };
        char   bufhost[16];
        char   bufbgcol[16];
        char   bufid[2][16];
        char   bufpw[2][32];

        HttpParser::Input in[] = { { "host", bufhost,  sizeof(bufhost)  },
                                   { "bgcol",bufbgcol, sizeof(bufbgcol) },
                                   { "id0",  bufid[0], sizeof(bufid[0]) },
                                   { "pw0",  bufpw[0], sizeof(bufpw[0]) },
                                   { "id1",  bufid[1], sizeof(bufid[1]) },
                                   { "pw1",  bufpw[1], sizeof(bufpw[1]) }
                                 };
        HttpParser parser{ in, sizeof(in) / sizeof(in[0]) };

        const char * parseError = parser.ParsePostData( req );
        if (parseError) {
            postError = "parser error: ";
            postError += parseError;
            break;
        }

        if (strcmp( bufhost, Wifi::Instance().GetHost() ))
            host = bufhost;
        if (strcmp( bufbgcol, Wifi::Instance().GetBgCol() ))
            bgcol = bufbgcol;
        for (int i = 0; i < 2; ++i) {
            if (strcmp( bufid[i], Wifi::Instance().GetSsid(i) ))
                id[i] = bufid[i];
            if (bufpw[i][0])
                if (strcmp( bufpw[i], Wifi::Instance().GetPassword(i) ))
                    pw[i] = bufpw[i];
        }

        if (! (host || bgcol
                || id[0] || pw[0] || Wifi::Instance().NoStationCounter(0)
                || id[1] || pw[1] || Wifi::Instance().NoStationCounter(1))) {
            postError = "data unchanged";
            break;
        }

        // ESP_LOGD( TAG, "before SetParam" ); EXPRD( vTaskDelay( 5 ) )

        if (! Wifi::Instance().SetParam( host, bgcol, id[0], pw[0], id[1], pw[1] )) {
            postError = "setting wifi parameter failed - try again";
            break;
        }

        // ESP_LOGD( TAG, "after SetParam" ); EXPRD( vTaskDelay( 5 ) )
        break;
    } // pseudo while loop - just checked for post with some early break

    HttpHelper hh{ req, s_subWifi, "Wifi" };

    hh.Add( " <form method=\"post\">\n"
            "  <table border=0>\n"
    );

    {
        constexpr uint8_t r0 = 2;
        Table<r0+9,5> table;
        table.Unite(    0, 0 );  // unite with neighbor cell 0,1 -> Hostname
        table.Unite(    1, 0 );  // unite with neighbor cell 1,1 -> Background color
        table.Unite( r0+0, 0 );  // unite with neighbor cell x,1 -> Favorite WLAN
        table.Unite( r0+4, 0 );  // unite with neighbor cell x,1 -> Alternate WLAN

        table.Right( 1 );

        table[0][2] = "&nbsp;";

        table[0][0] = "Hostname:";
        table[0]        [4] = "(used also as SSID in AP mode)";

        table[1][0] = "Background color:";

        table[r0+0][0] = "Favorite WLAN";
        table[r0+1]  [1] = "SSID:";          table[r0+1][3] = "<input type=\"text\""  " name=\"id0\" maxlength=15 value=\"";
        table[r0+2]  [1] = "Password:";      table[r0+2][3] = "<input type=\"password\" name=\"pw0\" maxlength=31>";
        table[r0+3]  [1] = "Fail counter:";  table[r0+3][4] = "(number of connection errors)";

        table[r0+4][0] = "Alternate WLAN";
        table[r0+5]  [1] = "SSID:";          table[r0+5][3] = "<input type=\"text\""  " name=\"id1\" maxlength=15 value=\"";
        table[r0+6]  [1] = "Password:";      table[r0+6][3] = "<input type=\"password\" name=\"pw1\" maxlength=31>";
        table[r0+7]  [1] = "Fail counter:";  table[r0+7][4] = "(number of connection errors)";

        table.Center( r0+8, 3 );
        table[r0+8]    [3] = "<br /><button type=\"submit\">submit</button>";
        if (! post)
            table[r0+8] [4] = "<br />(set and reset fail counter)";
        else if (postError.empty())
            table[r0+8] [4] = "<br />(values successfully written)";
        else
            table[r0+8] [4] = "<br />error: " + postError;

        const char * const host = Wifi::Instance().GetHost();
        table[0]    [3] = "<input type=\"text\" name=\"host\" maxlength=15 value=\"";
        if (host && *host)
            table[0][3] += host;
        table[0][3] += "\">";

        const char * const color = Wifi::Instance().GetBgCol();
        table[1]    [3] = "<input type=\"color\" name=\"bgcol\" value=\"";
        if (color && *color)
            table[1][3] += color;
        else
            table[1][3] += "lightblue";
        table[1][3] += "\">";

        const char * const pw0 = Wifi::Instance().GetPassword(0);
        const char * const pw1 = Wifi::Instance().GetPassword(1);
        if (pw0 && pw0[0])
            table[r0+2]    [4] = "(keep empty to not change / clear SSID to clear)";
        if (pw1 && pw1[0])
            table[r0+6]    [4] = "(keep empty to not change / clear SSID to clear)";

        for (int i = 0; i < 2; ++i) {
            const char * const id = Wifi::Instance().GetSsid(i);
            if (id && *id)
                table[r0+1+(i*4)][3] += id;
            table[r0+1+(i*4)][3] += "\">";
            table[r0+3+(i*4)][3] = std::to_string( Wifi::Instance().NoStationCounter(i) );
            table.Right( r0+3+(i*4), 3 );
        }

        table.AddTo( hh, 0, 1 );
    }

    hh.Add( "  </table>\n"
            " </form>" );
}
