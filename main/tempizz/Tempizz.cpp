/* Tempizz.cpp - temperature reading implementation
*/

//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "Tempizz.h"
#include "WebServer.h"
#include "HttpHelper.h"
#include "HttpParser.h"
#include "HttpTable.h"

#include <math.h>               // isnanf()
#include <driver/gpio.h>        // gpio_config(), gpio_set_level()
#include <ds18b20/ds18b20.h>

#include <esp_log.h>
#include <nvs.h>

#if (LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG)
#define EXPRD(expr) do { expr; } while(0);
#else
#define EXPRD(expr)
#endif

namespace {
const char *const s_subpage         = "/config";
const char *const s_nvsNamespace    = "temperature";
const char *const s_keyDevMask      = "mask";   // u16 value as a bit mask to found devices
const char        s_keyAddrPrefix[] = "addr_";  // device index (0..(N-1)) is appended to keys
const char        s_keyNamePrefix[] = "name_";  // device index (0..(N-1)) is appended to keys
                                                // number of stored devices: first non-existant addr/name pair
const char *      s_keyInterval[Tempizz::INTERVAL::COUNT];
const char      * TAG = "Tempizz";
Tempizz         * s_tempizz = 0;
}


extern "C" esp_err_t get_tempizz_config( httpd_req_t * req )
{
    if (s_tempizz)
        s_tempizz->Setup( req );
    return ESP_OK;
}

extern "C" esp_err_t post_tempizz_config( httpd_req_t * req )
{
    if (s_tempizz)
        s_tempizz->Setup( req, true );
    return ESP_OK;
}

namespace {
const httpd_uri_t s_get_uri   = { .uri = s_subpage, .method = HTTP_GET,  .handler = get_tempizz_config,  .user_ctx = 0 };
const httpd_uri_t s_post_uri  = { .uri = s_subpage, .method = HTTP_POST, .handler = post_tempizz_config, .user_ctx = 0 };
const WebServer::Page s_page    { s_get_uri, "Configure tempizz settings" };
}

Tempizz::Tempizz( gpio_num_t pin ) : mPin{ pin }
{
    s_tempizz = this;
    WebServer::Instance().AddPage( s_page, & s_post_uri );
    s_keyInterval[INTERVAL::FAST] = "fast";
    s_keyInterval[INTERVAL::SLOW] = "slow";
    s_keyInterval[INTERVAL::ERROR] = "error";
}

void Tempizz::Rescan()
{
    mMode = MODE::SCAN;
    xSemaphoreGive( mSemaphore );
}

void Tempizz::Setup( httpd_req_t * req, bool post )
{
    HttpHelper hh{ req, "Configuration" };

    uint8_t const n = mDevInfo.size() < MaxDevStored ? mDevInfo.size() : MaxDevStored;

    //ESP_LOGD( TAG, "have %d device infos -> n set to %d", mDevInfo.size(), n );

    if (post) {
        // ESP_LOGD( TAG, "got POST data" );
        if (n) {
            HttpParser::Input in[n + INTERVAL::COUNT];
            char key[n][8];
            char buf[n][32];
            for (uint8_t i = 0; i < n; ++i) {
                strcpy( key[i], "name_" );
                key[i][5] = i + 'A';
                key[i][6] = 0;
                in[i] = HttpParser::Input{ key[i], buf[i], sizeof(buf[i]) };
            }
            char bufInterval[INTERVAL::COUNT][8];
            for (uint8_t i = 0; i < INTERVAL::COUNT; ++i)
                in[n+i] = HttpParser::Input{ s_keyInterval[i], bufInterval[i], sizeof(bufInterval[i]) };

            HttpParser parser{ in, (uint8_t) (sizeof(in)/sizeof(in[0])) };

            if (! parser.ParsePostData( req )) {
                hh.Add( "unexpected end of data while parsing post data" );
                return;
            }
            for (uint8_t i = 0; i < n; ++i)
                if (in[i].len) {
                    std::string name{ buf[i] };
                    if (mDevInfo[i].name != name) {
                        mDevInfo[i].name = name;
                        WriteDevInfo( i );
                    }
                }
            for (uint8_t i = 0; i < INTERVAL::COUNT; ++i)
                if (in[n+i].len) {
                    unsigned long interval = strtoul( bufInterval[i], 0, 0 );
                    if (interval) {
                        interval *= configTICK_RATE_HZ;
                        if (mInterval[i] != interval) {
                            mInterval[i] = interval;
                            WriteInterval( i );
                        }
                    }
                }
        }
    } else {
        // ESP_LOGD( TAG, "got GET data" );
        HttpParser::Input in[] = { { "rescan", 0, 0 } };
        HttpParser parser{ in, sizeof(in) / sizeof(in[0]) };
        if (! parser.ParseUriParam( req )) {
            hh.Add( "unexpected end of data while parsing get query" );
            return;
        }
        if (parser.Fields() & 1) {
            std::string meta{ std::string("<meta http-equiv=\"refresh\" content=\"1; URL=") + s_subpage + "\">" };
            hh.Head( meta.c_str() );
            Rescan();
        }
    }
    if (n) {
        ESP_LOGD( TAG, "constructing device table" );
        hh.Add( " <form method=\"post\">\n"
                "  <table>\n" );
        {
            Table<1, 7> table;
            table.Center( 0 );
            table.Center( 4 );
            table.Right( 5 );
            table.Right( 6 );
            table[0][0] = "Device";
            table[0][1] = "&nbsp;";
            table[0][2] = "OneWire ROM address";
            table[0][3] = "Device name";
            table[0][4] = "Used?";
            table[0][5] = "Temperature";
            table[0][6] = "Age";
            table.AddTo( hh, /*headrows*/ 1 );

            table[0][1].clear();
            for (uint8_t i = 0; i < n; ++i) {
                table[0][0] = (char) (i + 'A');
                table[0][2] = HttpHelper::HexString( mDevInfo.at(i).addr );
                table[0][3] = std::string("<input type=\"text\""
                                                " maxlength=31"
                                                " name=\"name_") + (char) (i + 'A') + "\""
                                                " value=\"" + mDevInfo[i].name + "\""
                                                " />";
                if (mDevMask & (1 << i))
                    table[0][4] = "&#x2713;";  // ☑ 9745 x2611  ✓ x2713
                else
                    table[0][4] = "&mdash;";  // ☐ 9744 x2610

                if (isnanf( mDevInfo[i].value )) {
                    table[0][5] = "-";
                    table[0][6] = "-";
                } else {
                    table[0][5] = HttpHelper::String( mDevInfo[i].value ) + "°C";
                    long x = (xTaskGetTickCount() - mDevInfo[i].time + configTICK_RATE_HZ/2) / configTICK_RATE_HZ;
                    if (x < 60)
                        table[0][6] = HttpHelper::String( x ) + " s";
                    else {
                        long y = x % 60;
                        x /= 60;
                        if (x < 60)
                            table[0][6] = HttpHelper::String( x ) + ":" + HttpHelper::String( y, 2 ) + " m:ss";
                        else {
                            x += (y + 30) / 60;
                            y = x % 60;
                            x /= 60;
                            table[0][6] = HttpHelper::String( x ) + ":" + HttpHelper::String( y, 2 ) + " h:mm";
                        }
                    }
                }
                table.AddTo( hh, 0, /*headcols*/ 1 );
            }

            hh.Add( "   <tr><td>&nbsp;</td></tr>\n" );  // vertical space: empty line

            table.ResetAlign();
            table.Right( 0 );
            table[0][0] = "Interval";
            table[0][2] = "Interval type";
            table[0][3] = "Interval time";
            table[0][4] = "Unit";
            table[0][5].clear();
            table[0][6].clear();
            table.AddTo( hh, /*headrows*/ 1 );

            table[0][4] = "s";
            table[0][5].clear();
            table[0][6].clear();

            for (uint8_t i = 0; i < INTERVAL::COUNT; ++i) {
                switch (i) {
                    case INTERVAL::FAST:  table[0][0] = "Fast";
                                          table[0][2] = "measurement interval"; break;
                    case INTERVAL::SLOW:  table[0][0] = "Slow";
                                          table[0][2] = "measurement interval"; break;
                    case INTERVAL::ERROR: table[0][0] = "Error";
                                          table[0][2] = "retry interval"; break;
                }
                long value = ((mInterval[i] + configTICK_RATE_HZ/2) / configTICK_RATE_HZ);
                table[0][3] = "<input type=\"number\""
                                    " name=\"" + std::string( s_keyInterval[i] ) + "\""
                                    " min=\"1\""
                                    " max=\"3600\""
                                    " value=\"" + HttpHelper::String( value ) + "\""
                                    " />";
                table.AddTo( hh, 0, /*headcols*/ 1);
            }

            hh.Add( "   <tr><td>&nbsp;</td></tr>\n" );  // vertical space: empty line

            table[0][0].clear();
            table[0][2].clear();
            table[0][3].clear();
            table[0][4].clear();
            table[0][5].clear();
            table[0][6] = "<button type=\"submit\" title=\"set device names and interval times\">submit</button>";
            table.AddTo( hh );
        }
        hh.Add( "  </table>\n"
                " </form>\n" );
    }

    hh.Add( " <br /><br /><br />\n"
            " <form>\n"
            "  <input type=\"hidden\" name=\"rescan\" />\n"
            "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;"
                "<button type=\"submit\""
                        " title=\"Scan for currently connected devices."
                        " The page will refresh after 1 second to show the new list."
                        " Just devices marked as 'used' will be used for measurement."
                        " To update the 'used' devices, you have to 'rescan'."
                        "\">"
                            "rescan"
                        "</button></center>\n"
            " </form>\n" );
}

void Tempizz::ReadConfig()
{
    nvs_handle my_handle;
    if (nvs_open( s_nvsNamespace, NVS_READONLY, &my_handle ) != ESP_OK)
        return;

    ESP_LOGD( TAG, "Reading addr/name pairs" );
    char     keyAddr[sizeof(s_keyAddrPrefix) + 1];
    char     keyName[sizeof(s_keyNamePrefix) + 1];
    memcpy( keyAddr, s_keyAddrPrefix, sizeof(s_keyAddrPrefix) );
    memcpy( keyName, s_keyNamePrefix, sizeof(s_keyNamePrefix) );
    keyAddr[sizeof(s_keyAddrPrefix)] = 0;
    keyName[sizeof(s_keyNamePrefix)] = 0;

    mDevInfo.clear();
    uint8_t n = 0;
    do {
        keyAddr[sizeof(s_keyAddrPrefix) - 1] = n + 'A';
        keyName[sizeof(s_keyNamePrefix) - 1] = n + 'A';
        uint64_t addr;
        // ESP_LOGD( TAG, "nvs_get_u64( ... \"%s\" ...)", keyAddr );
        if (nvs_get_u64( my_handle, keyAddr, &addr ) != ESP_OK)
            break;
        char   name[32];
        size_t len = sizeof(name);
        // ESP_LOGD( TAG, "nvs_get_str( ... \"%s\" ...)", keyName );
        if (nvs_get_str( my_handle, keyName, name, & len ) != ESP_OK)
            name[0] = 0; // empty in case name not set
        mDevInfo.push_back( DevInfo( addr, name ) );
        ++n;
    } while (n < MaxDevStored);

    nvs_get_u16( my_handle, s_keyDevMask, &mDevMask );

    for (uint8_t i = 0; i < INTERVAL::COUNT; ++i) {
        uint32_t value;
        if (nvs_get_u32( my_handle, s_keyInterval[i], & value ) == ESP_OK)
            if (value)
                mInterval[i] = value;
    }

    nvs_close( my_handle );
}

void Tempizz::WriteDevInfo( uint8_t idx )
{
    nvs_handle my_handle;
    if (nvs_open( s_nvsNamespace, NVS_READWRITE, &my_handle ) != ESP_OK)
        return;

    char keyAddr[sizeof(s_keyAddrPrefix) + 1];
    char keyName[sizeof(s_keyNamePrefix) + 1];
    memcpy( keyAddr, s_keyAddrPrefix, sizeof(s_keyAddrPrefix) );
    memcpy( keyName, s_keyNamePrefix, sizeof(s_keyNamePrefix) );
    keyAddr[sizeof(s_keyAddrPrefix) - 1] = idx + 'A';
    keyName[sizeof(s_keyNamePrefix) - 1] = idx + 'A';
    keyAddr[sizeof(s_keyAddrPrefix)] = 0;
    keyName[sizeof(s_keyNamePrefix)] = 0;

    nvs_set_u64( my_handle, keyAddr, mDevInfo.at(idx).addr );
    if (mDevInfo[idx].name.length())
        nvs_set_str( my_handle, keyName, mDevInfo[idx].name.c_str() );
    else
        nvs_erase_key( my_handle, keyName );

    nvs_commit( my_handle );
    nvs_close( my_handle );
}

void Tempizz::WriteDevMask()
{
    nvs_handle my_handle;
    if (nvs_open( s_nvsNamespace, NVS_READWRITE, &my_handle ) != ESP_OK)
        return;

    nvs_set_u16( my_handle, s_keyDevMask, mDevMask );
    nvs_commit( my_handle );
    nvs_close( my_handle );
}

void Tempizz::WriteInterval( uint8_t idx )
{
    nvs_handle my_handle;
    if (nvs_open( s_nvsNamespace, NVS_READWRITE, &my_handle ) != ESP_OK)
        return;

    nvs_set_u32( my_handle, s_keyInterval[idx], mInterval[idx] );
    nvs_commit( my_handle );
    nvs_close( my_handle );
}

void Tempizz::Run()
{
    if (! mSemaphore) {
        if (! (mSemaphore = xSemaphoreCreateBinary())) {
            ESP_LOGE( TAG, "xSemaphoreCreateBinary failed" );
            return;
        }
    }

    {
        gpio_config_t   cfg;
        cfg.pin_bit_mask = 1 << mPin;
        cfg.mode         = GPIO_MODE_OUTPUT_OD;
        cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type    = GPIO_INTR_DISABLE;
        gpio_config( &cfg );
    }

    ReadConfig();

    if (! mDevMask || ! mDevInfo.size()) {
        mMode = MODE::SCAN;
    }

    TickType_t sleep = 0;
    while (true) {
        if (sleep)
            xSemaphoreTake( mSemaphore, sleep );

        if (mMode == MODE::SCAN) {
            mMode = MODE::NORMAL;
            ds18b20_addr_t addr[MaxNofDev];
            int nFound = ds18b20_scan_devices( mPin, addr, sizeof( addr ) / sizeof( addr[0] ) );
            ESP_LOGI( TAG, "found %d devices", nFound );
            uint16_t const oldDevMask = mDevMask;
            mDevMask = 0;
            uint16_t nonMatching = (1 << nFound) - 1;
            for (uint8_t i = 0; i < mDevInfo.size(); ++i) {
                for (uint8_t j = 0; j < nFound; ++j) {
                    if (mDevInfo[i].addr == addr[j]) {
                        mDevMask |= 1 << i;
                        nonMatching &= ~(1 << j);
                        break;
                    }
                }
            }
            while (nonMatching) {  // add new found devices to list
                uint8_t j = 31 - __builtin_clz(nonMatching);
                nonMatching &= ~(1 << j);
                assert( j < nFound );
                // ESP_LOGD( TAG, "non-matching device %d: %08x-%08x (next mask %#x)", j, (uint32_t) (addr[j] >> 32), (uint32_t) addr[j], nonMatching );

                uint8_t i = (uint8_t) mDevInfo.size();
                if (i >= MaxDevStored) {  // up to N known devices: recycle old dev
                    uint8_t fistUnused = 0xff;
                    uint8_t fistUnnamedUnused = 0xff;
                    for (i = 0; i < MaxDevStored; ++i) {
                        if (! (mDevMask & (1 << i))) {
                            if (fistUnused == 0xff)
                                fistUnused = i;
                            if (mDevInfo[i].name.length() == 0) {
                                if (fistUnnamedUnused == 0xff) {
                                    fistUnnamedUnused = i;
                                    break;
                                }
                            }
                        }
                    }
                    if (fistUnnamedUnused != 0xff) {
                        i = fistUnnamedUnused;
                        ESP_LOGD( TAG, "recycling unnamed device index %d", i );
                    } else {
                        i = fistUnused;
                        ESP_LOGD( TAG, "recycling unused device index %d", i );
                    }
                }
                assert( i < MaxDevStored );
                DevInfo devInfo{ addr[j], 0 };
                if (i < mDevInfo.size()) 
                    mDevInfo[i] = devInfo;
                else
                    mDevInfo.push_back( devInfo );
                ESP_LOGD( TAG, "store device index %d (%08x-%08x) to nvs", i, (uint32_t) (addr[j] >> 32), (uint32_t) addr[j] );
                WriteDevInfo( i );
                mDevMask |= 1 << i;
            }
            if (mDevMask != oldDevMask) {
                ESP_LOGD( TAG, "set device mask %#x to nvs", mDevMask );
                WriteDevMask();
            }
            ESP_LOGD( TAG, "search done" );
            // ESP_LOGD( TAG, "have %d device infos scan", mDevInfo.size() );
        }

        if (! mDevMask) {
            sleep = mInterval[ INTERVAL::ERROR ];
            mMode = MODE::SCAN;
            continue;
        }
        sleep = mInterval[ INTERVAL::SLOW ];

        // ESP_LOGD( TAG, "have %d device infos before measurement starts", mDevInfo.size() );

        if (! ds18b20_measure( mPin, DS18B20_ANY, true )) {
            ESP_LOGE( TAG, "ds18b20_measure( DS18B20_ANY ) failed" );
            // ESP_LOGD( TAG, "have %d device infos after measurement started", mDevInfo.size() );

            sleep = mInterval[ INTERVAL::ERROR ];
            continue;
        }
        uint16_t devMask = mDevMask;
        while (devMask) {
            uint8_t i = 31 - __builtin_clz(devMask);
            assert( i < mDevInfo.size() );
            devMask &= ~(1 << i);

            // ESP_LOGD( TAG, "have %d device infos in loop / devMask = %#x / i = %d", mDevInfo.size(), devMask, i );

            uint64_t addr = mDevInfo.at(i).addr;
            float temperature = ds18b20_read_temperature( mPin, addr );
            if (isnanf(temperature)) {
                if (mDevInfo[i].name.length()) {
                    ESP_LOGE( TAG, "temperature %s unavailable", mDevInfo[i].name.c_str() );
                } else {
                    ESP_LOGE( TAG, "temperature %08x-%08x unavailable", (uint32_t) (addr >> 32), (uint32_t) addr );
                }
            } else {
                mDevInfo[i].value = temperature;
                mDevInfo[i].time = xTaskGetTickCount();
                std::string val = HttpHelper::String( temperature, 2 );
                if (mDevInfo[i].name.length()) {
                    ESP_LOGI( TAG, "temperature %s: %s°C", mDevInfo[i].name.c_str(), val.c_str() );
                } else {
                    ESP_LOGI( TAG, "temperature %08x-%08x: %s°C", (uint32_t) (addr >> 32), (uint32_t) addr, val.c_str() );
                }
            }
        }
    }
}
