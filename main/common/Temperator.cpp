/* Temperator.cpp - temperature reading implementation
*/

//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "Temperator.h"

#include "Mqtinator.h"
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
const char *const s_subpage         = "/temperature";
const char *const s_nvsNamespace    = "temperature";
const char *const s_keyDevMask      = "mask";   // u16 value as a bit mask to found devices
const char        s_keyAddrPrefix[] = "addr_";  // device index (0..(N-1)) is appended to keys
const char        s_keyNamePrefix[] = "name_";  // device index (0..(N-1)) is appended to keys
const char        s_keyIdxPrefix[]  = "idx_";   // device index (0..(N-1)) is appended to keys
                                                // number of stored devices: first non-existant addr/name pair
const char *      s_keyInterval[Temperator::INTERVAL::COUNT];
const char      * TAG = "Temperator";
Temperator      * s_temperator = 0;
}

extern "C" esp_err_t get_temperator_config( httpd_req_t * req )
{
    if (s_temperator)
        s_temperator->Setup( req );
    return ESP_OK;
}

extern "C" esp_err_t post_temperator_config( httpd_req_t * req )
{
    if (s_temperator)
        s_temperator->Setup( req, true );
    return ESP_OK;
}

namespace {
const httpd_uri_t s_get_uri   = { .uri = s_subpage, .method = HTTP_GET,  .handler = get_temperator_config,  .user_ctx = 0 };
const httpd_uri_t s_post_uri  = { .uri = s_subpage, .method = HTTP_POST, .handler = post_temperator_config, .user_ctx = 0 };
const WebServer::Page s_page    { s_get_uri, "Temperature" };
}

Temperator::Temperator( gpio_num_t pin ) : mPin{ pin }
{
    s_temperator = this;
    WebServer::Instance().AddPage( s_page, & s_post_uri );
    s_keyInterval[INTERVAL::FAST] = "fast";
    s_keyInterval[INTERVAL::SLOW] = "slow";
    s_keyInterval[INTERVAL::ERROR] = "error";
}

void Temperator::OnTempRead( callback_t callback, void * userarg )
{
    mUserArg = userarg;
    mCallback = callback;
}

void Temperator::Rescan()
{
    mMode = MODE::SCAN;
    xSemaphoreGive( mSemaphore );
}

void Temperator::Setup( httpd_req_t * req, bool post )
{
    HttpHelper hh{ req, "Configure temperature sensors settings", "Temperature" };

    uint8_t const n = mDevInfo.size() < MaxDevStored ? mDevInfo.size() : MaxDevStored;

    ESP_LOGD( TAG, "have %d device infos -> n set to %d", mDevInfo.size(), n );

    std::string postError{};

    while (post) {
        ESP_LOGD( TAG, "got POST data" );
        if (n) {
            HttpParser::Input in[(2 * n) + INTERVAL::COUNT];
            char namekey[n][8];
            char namebuf[n][32];
            for (uint8_t i = 0; i < n; ++i) {
                strcpy( namekey[i], "name_" );
                namekey[i][5] = i + 'A';
                namekey[i][6] = 0;
                in[i] = HttpParser::Input{ namekey[i], namebuf[i], sizeof(namebuf[i]) };
            }
            char idxkey[n][8];
            char idxbuf[n][8];
            for (uint8_t i = 0; i < n; ++i) {
                strcpy( idxkey[i], "idx_" );
                idxkey[i][4] = i + 'A';
                idxkey[i][5] = 0;
                in[n+i] = HttpParser::Input{ idxkey[i], idxbuf[i], sizeof(idxbuf[i]) };
            }
            char bufInterval[INTERVAL::COUNT][8];
            for (uint8_t i = 0; i < INTERVAL::COUNT; ++i)
                in[(2*n)+i] = HttpParser::Input{ s_keyInterval[i], bufInterval[i], sizeof(bufInterval[i]) };

            HttpParser parser{ in, (uint8_t) (sizeof(in)/sizeof(in[0])) };

            const char * parseError = parser.ParsePostData( req );
            if (parseError) {
                postError = "parser error: ";
                postError += parseError;
                break;
            }

            for (uint8_t i = 0; i < n; ++i) {
                bool mod = false;
                if (in[i].len) {
                    std::string name{ namebuf[i] };
                    if (mDevInfo[i].name != name) {
                        mDevInfo[i].name = name;
                        mod = true;
                    }
                }
                if (in[n+i].len) {
                    uint16_t idx = (uint16_t) strtoul( idxbuf[i], 0, 0 );
                    if (mDevInfo[i].idx != idx) {
                        mDevInfo[i].idx = idx;
                        mod = true;
                    }
                }
                if (mod) {
                    WriteDevInfo( i );
                }
            }
            for (uint8_t i = 0; i < INTERVAL::COUNT; ++i)
                if (in[(2*n)+i].len) {
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
        break;
    } // end ot pseudo while
    if (! post) {
        // ESP_LOGD( TAG, "got GET data" );
        HttpParser::Input in[] = { { "rescan", 0, 0 } };
        HttpParser parser{ in, sizeof(in) / sizeof(in[0]) };

        const char * parseError = parser.ParseUriParam( req );
        if (parseError) {
            hh.Add( "parser error: " );
            hh.Add( parseError );
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
            Table<1, 8> table;
            table.Center( 0 );
            table.Center( 4 );
            table.Right( 5 );
            table.Right( 6 );
            table[0][0] = "Device";
            table[0][1] = "&nbsp;";
            table[0][2] = "OneWire ROM address";
            table[0][3] = "Name";
            table[0][4] = "Idx";
            table[0][5] = "Used?";
            table[0][6] = "Temp.";
            table[0][7] = "Age";
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
                table[0][4] = std::string("<input type=\"number\""
                                                " maxlength=5"
                                                " name=\"idx_") + (char) (i + 'A') + "\""
                                                " value=\"" + std::to_string( mDevInfo[i].idx ) + "\""
                                                " />";
                if (mDevMask & (1 << i))
                    table[0][5] = "&#x2713;";  // ☑ 9745 x2611  ✓ x2713
                else
                    table[0][5] = "&mdash;";  // ☐ 9744 x2610

                if (isnanf( mDevInfo[i].value )) {
                    table[0][6] = "-";
                    table[0][7] = "-";
                } else {
                    table[0][6] = HttpHelper::String( mDevInfo[i].value, 1 ) + "°C";
                    long x = (xTaskGetTickCount() - mDevInfo[i].time + configTICK_RATE_HZ/2) / configTICK_RATE_HZ;
                    if (x < 60)
                        table[0][7] = HttpHelper::String( x ) + " s";
                    else {
                        long y = x % 60;
                        x /= 60;
                        if (x < 60)
                            table[0][7] = HttpHelper::String( x ) + ":" + HttpHelper::String( y, 2 ) + " m:ss";
                        else {
                            x += (y + 30) / 60;
                            y = x % 60;
                            x /= 60;
                            table[0][7] = HttpHelper::String( x ) + ":" + HttpHelper::String( y, 2 ) + " h:mm";
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
            table[0][7].clear();
            table.AddTo( hh, /*headrows*/ 1 );

            table[0][4] = "s";
            table[0][5].clear();
            table[0][6].clear();
            table[0][7].clear();

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
            if (post) {
                if (postError.empty())
                    table[0][7] = "setup succeeded";
                else {
                    table[0][7] = "setup failed: ";
                    table[0][7] += postError;
                }
            }
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

void Temperator::ReadConfig()
{
    nvs_handle my_handle;
    if (nvs_open( s_nvsNamespace, NVS_READONLY, &my_handle ) != ESP_OK)
        return;

    ESP_LOGD( TAG, "Reading addr/name/idx tuples" );
    char keyAddr[sizeof(s_keyAddrPrefix) + 1];
    char keyName[sizeof(s_keyNamePrefix) + 1];
    char keyIdx[ sizeof(s_keyIdxPrefix)  + 1];
    memcpy( keyAddr, s_keyAddrPrefix, sizeof(s_keyAddrPrefix) );
    memcpy( keyName, s_keyNamePrefix, sizeof(s_keyNamePrefix) );
    memcpy( keyIdx,  s_keyIdxPrefix,  sizeof(s_keyIdxPrefix) );
    keyAddr[sizeof(s_keyAddrPrefix)] = 0;
    keyName[sizeof(s_keyNamePrefix)] = 0;
    keyIdx[ sizeof(s_keyIdxPrefix) ] = 0;

    mDevInfo.clear();
    uint8_t n = 0;
    do {
        keyAddr[sizeof(s_keyAddrPrefix) - 1] = n + 'A';
        keyName[sizeof(s_keyNamePrefix) - 1] = n + 'A';
        keyIdx[ sizeof(s_keyIdxPrefix)  - 1] = n + 'A';

        uint64_t addr = 0;
        esp_err_t err = nvs_get_u64( my_handle, keyAddr, &addr );
        if (err != ESP_OK) {
            ESP_LOGD( TAG, "nvs_get_u64( \"%s\" ) -> %#x ==> break loop", keyAddr, err );
            break;
        }
        ESP_LOGD( TAG, "nvs_get_u64( \"%s\" ) -> %#x, %s", keyAddr, err, HttpHelper::HexString( addr ).c_str() );

        char   name[32];
        size_t len = sizeof(name);
        uint16_t idx = 0;
        err = nvs_get_str( my_handle, keyName, name, & len );
        if (err != ESP_OK)
            name[0] = 0; // empty in case name not set
        ESP_LOGD( TAG, "nvs_get_str( \"%s\" ) -> %#x, \"%s\"", keyName, err, name );
        err = nvs_get_u16( my_handle, keyIdx, & idx );
        ESP_LOGD( TAG, "nvs_get_u16( \"%s\" ) -> %#x, %d", keyIdx, err, idx );
        if (err != ESP_OK)
            idx = 0; // empty in case name not set
        mDevInfo.push_back( DevInfo( addr, name, idx ) );
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

void Temperator::WriteDevInfo( uint8_t idx )
{
    nvs_handle my_handle;
    if (nvs_open( s_nvsNamespace, NVS_READWRITE, &my_handle ) != ESP_OK)
        return;

    char keyAddr[sizeof(s_keyAddrPrefix) + 1];
    char keyName[sizeof(s_keyNamePrefix) + 1];
    char keyIdx[ sizeof(s_keyIdxPrefix)  + 1];
    memcpy( keyAddr, s_keyAddrPrefix, sizeof(s_keyAddrPrefix) );
    memcpy( keyName, s_keyNamePrefix, sizeof(s_keyNamePrefix) );
    memcpy( keyIdx,  s_keyIdxPrefix,  sizeof(s_keyIdxPrefix) );
    keyAddr[sizeof(s_keyAddrPrefix) - 1] = idx + 'A';
    keyName[sizeof(s_keyNamePrefix) - 1] = idx + 'A';
    keyIdx[ sizeof(s_keyIdxPrefix)  - 1] = idx + 'A';
    keyAddr[sizeof(s_keyAddrPrefix)] = 0;
    keyName[sizeof(s_keyNamePrefix)] = 0;
    keyIdx[ sizeof(s_keyIdxPrefix) ] = 0;

    ESP_LOGD( TAG, "nvs_set_u64( \"%s\", %s )", keyAddr, HttpHelper::HexString( mDevInfo[idx].addr ).c_str() );
    nvs_set_u64( my_handle, keyAddr, mDevInfo[idx].addr );

    if (mDevInfo[idx].name.length()) {
        ESP_LOGD( TAG, "nvs_set_str( \"%s\", \"%s\" )", keyName, mDevInfo[idx].name.c_str() );
        nvs_set_str( my_handle, keyName, mDevInfo[idx].name.c_str() );
    } else {
        ESP_LOGD( TAG, "nvs_erase_key( \"%s\" )", keyName );
        nvs_erase_key( my_handle, keyName );
    }
    if (mDevInfo[idx].idx) {
        ESP_LOGD( TAG, "nvs_set_u16( \"%s\", %d )", keyIdx, mDevInfo[idx].idx );
        nvs_set_u16( my_handle, keyIdx, mDevInfo[idx].idx );
    } else {
        ESP_LOGD( TAG, "nvs_erase_key( \"%s\" )", keyIdx );
        nvs_erase_key( my_handle, keyIdx );
    }
    nvs_commit( my_handle );
    nvs_close( my_handle );
}

void Temperator::WriteDevMask()
{
    nvs_handle my_handle;
    if (nvs_open( s_nvsNamespace, NVS_READWRITE, &my_handle ) != ESP_OK)
        return;

    nvs_set_u16( my_handle, s_keyDevMask, mDevMask );
    nvs_commit( my_handle );
    nvs_close( my_handle );
}

void Temperator::WriteInterval( uint8_t idx )
{
    nvs_handle my_handle;
    if (nvs_open( s_nvsNamespace, NVS_READWRITE, &my_handle ) != ESP_OK)
        return;

    nvs_set_u32( my_handle, s_keyInterval[idx], mInterval[idx] );
    nvs_commit( my_handle );
    nvs_close( my_handle );
}

extern "C" void TemperatorTask( void * temperator )
{
    ((Temperator *) temperator)->Run();
}
bool Temperator::Start()
{
    xTaskCreate( TemperatorTask, "Temperator", /*stack size*/2048, this,
                 /*prio*/ 1, &mTaskHandle );
    if (!mTaskHandle) {
        ESP_LOGE( TAG, "xTaskCreate failed" );
        return false;
    }
    return true;
}
void Temperator::Run()
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
#if 1
            int nFound = ds18b20_scan_devices( mPin, addr, sizeof( addr ) / sizeof( addr[0] ) );
#else
            int nFound = 5;
            for (int i = 0; i < nFound; ++i) {
                addr[i] = 0xaa01 + i;
            }
#endif
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
                DevInfo devInfo{ addr[j], 0, 0 };
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
        TickType_t now = xTaskGetTickCount();
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
                TickType_t deltaTicks = now - mDevInfo[i].time;
                float deltaTemp = temperature - mDevInfo[i].value;
                mDevInfo[i].time = now;
                mDevInfo[i].value = temperature;

                if (mCallback) {
                    mCallback( mUserArg, mDevInfo[i].idx, temperature );
                }
                // last value read between slow and fast interval time -> usual behavior
                // if ((deltaTicks >= (mInterval[ INTERVAL::SLOW ] - 5)) && (deltaTicks <= (mInterval[ INTERVAL::SLOW ] + 5)))
                if ((abs(deltaTemp)/deltaTicks) >= (1.0/(60.0*configTICK_RATE_HZ))) {  // >= 1°C/min
                    sleep = mInterval[ INTERVAL::FAST ];
                }

#if (LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG)
                if (mDevInfo[i].name.length()) {
                    ESP_LOGD( TAG, "temperature %s: %.1g°C", mDevInfo[i].name.c_str(), temperature );
                } else {
                    ESP_LOGD( TAG, "temperature %08x-%08x: %.1g°C", (uint32_t) (addr >> 32), (uint32_t) addr, temperature );
                }
#endif
                if (mDevInfo[i].idx) {
                    Mqtinator::Instance().Pub( mDevInfo[i].idx, HttpHelper::String( temperature, 1 ) );
                }
            }
        }
    }
}
