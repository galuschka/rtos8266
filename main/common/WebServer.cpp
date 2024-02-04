/*
 * WebServer.h
 */
//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "WebServer.h"
#include "Wifi.h"
#include "Indicator.h"
#include "Mqtinator.h"
#include "Updator.h"
#include "HttpHelper.h"
#include "HttpTable.h"
#include "HttpParser.h"
#include "favicon.i"            // favicon_ico (when no image in nvs)

#include <string.h>     // memmove()
#include <string>       // std::string

#include <esp_log.h>   			// ESP_LOGI()
#include <esp_event_base.h>   	// esp_event_base_t
#include <esp_ota_ops.h>        // esp_ota_get_app_description()
#include <esp_flash_data_types.h> // esp_ota_select_entry_t, OTA_TEST_STAGE
#include <esp_http_client.h>   	// esp_http_client_config_t
#include <nvs.h>

#if (LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG)
#define EXPRD(expr) do { expr; } while(0);
#else
#define EXPRD(expr)
#endif

namespace
{
const char * const TAG          = "WebServer";
const char * const s_subWifi    = "WLAN parameter";
const char * const s_subMqtt    = "MQTT parameter";
const char * const s_subUpdate  = "Firmware update";
const char * const s_subFavicon = "Favicon update";
const char * const s_subReboot  = "Restart device";
WebServer          s_WebServer{};

} // namespace

extern "C" {

uint64_t g_esp_os_cpu_clk;

esp_err_t handler_get_main( httpd_req_t * req )
{
    s_WebServer.MainPage( req );
    return ESP_OK;
}

esp_err_t handler_get_favicon( httpd_req_t * req )
{
    do { // while(0)
        nvs_handle my_handle;
        if (nvs_open( "images", NVS_READONLY, &my_handle ) != ESP_OK) {
            ESP_LOGI( TAG, "handler_get_favicon: nvs \"images\" does not exist" );
            break;
        }
        do { // while(0)
            char type[16];
            size_t len = sizeof(type) - 1;
            if (nvs_get_str( my_handle, "favicon-type", type, & len ) != ESP_OK) {
                ESP_LOGI( TAG, "handler_get_favicon: cannot read favicon-type in nvs \"images\"" );
                break;
            }
            type[sizeof(type)-1] = 0;

            len = 0;
            if (nvs_get_blob( my_handle, "favicon", 0, & len ) != ESP_OK) {
                ESP_LOGI( TAG, "handler_get_favicon: favicon-type known (\"%s\"), but favicon not stored", type );
                break;
            }
            if ((len < 64) || (len > 5 * 4096)) {  // max. 5 of 6 flash pages each 4KB
                ESP_LOGE( TAG, "handler_get_favicon: invalid favicon size %d (no in %d..%d)", len, 64, 5 * 4096 );
                break;
            }
            void * data = malloc( len );
            if (! data) {
                ESP_LOGE( TAG, "handler_get_favicon: cannot allocate favicon buffer of %d bytes", len );
                break;
            }
            if (nvs_get_blob( my_handle, "favicon", data, & len ) != ESP_OK) {
                free( data );
                ESP_LOGE( TAG, "handler_get_favicon: reading favicon failed" );
                break;
            }
            nvs_close( my_handle );

            httpd_resp_set_type( req, type );
            httpd_resp_send( req, (char *) data, len );
            free( data );
            return ESP_OK;

        } while (0);
        nvs_close( my_handle );
    } while (0);

    ESP_LOGI( TAG, "handler_get_favicon: sending hard-coded favicon" );

    httpd_resp_set_type( req, "image/gif" /*or x-icon*/ );
    httpd_resp_send( req, favicon_ico, sizeof(favicon_ico) );
    return ESP_OK;
}

esp_err_t handler_post_favicon( httpd_req_t * req )
{
    char type[16];
    char uri[80];
    HttpParser::Input in[] = { { "type", type, sizeof(type) },
                               { "img",  uri,  sizeof(uri)  } };
    HttpParser parser{ in, sizeof(in) / sizeof(in[0]) };

    HttpHelper hh{ req, s_subFavicon, "Update" };
    if (! parser.ParsePostData( req )) {
        hh.Add( "unexpected end of data while parsing data" );
        return ESP_OK;
    }
    if (! (type[0] && uri[0])) {
        hh.Add( "incomplete form" );
        return ESP_OK;
    }

    esp_http_client_config_t config;
    memset( & config, 0, sizeof(config) );
    config.url = uri;
    esp_http_client_handle_t client = esp_http_client_init( & config );
    if (client == NULL) {
        hh.Add( "Failed to initialize HTTP connection" );
        return ESP_OK;
    }
    char * buf = 0;
    do { // while (0)
        esp_err_t e = esp_http_client_open( client, 0 );
        if (e != ESP_OK) {
            hh.Add( "Failed to open HTTP connection" );
            break;
        }
        int const totalLen = esp_http_client_fetch_headers( client );
        if (totalLen <= 0) {
            hh.Add( "Failed to fetch headers" );
            break;
        }
        int const status_code = esp_http_client_get_status_code( client );
        if (status_code != 200) {
            hh.Add( "Server response other than 200" );
            break;
        }
        if ((totalLen < 64) || (totalLen > 5 * 4096)) {
            hh.Add( "size not in 64B..20KB" );
            break;
        }
        buf = (char *) malloc( totalLen );
        if (! buf) {
            hh.Add( "Failed to allocate buffer" );
            break;
        }
        int remain = totalLen;
        do {
            int readLen = esp_http_client_read( client, &buf[totalLen - remain], remain);
            if (! readLen) {
                hh.Add( "no more data but uncomplete" );
                break;
            }
            remain -= readLen;
        } while (remain);
        if (remain) {
            break;
        }

        nvs_handle my_handle;
        if (nvs_open( "images", NVS_READWRITE, &my_handle ) != ESP_OK) {
            hh.Add( "cannot open NVS namespace" );
            break;
        }
        esp_err_t esp = nvs_set_str(  my_handle, "favicon-type", type );
        if (esp != ESP_OK) {
            hh.Add( "failed to store type" );
        } else {
            esp = nvs_set_blob( my_handle, "favicon", buf, totalLen );
            if (esp == ESP_OK)
                hh.Add( "failed to store image (size %d)", totalLen );
            else {
                nvs_commit( my_handle );
                hh.Add( "success" );
            }
        }
        nvs_close( my_handle );
    } while (0);

    if (buf)
        free( buf );

    esp_http_client_cleanup( client );

    return ESP_OK;
}

esp_err_t handler_get_wifi( httpd_req_t * req )
{
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
        table[r0+8]        [4] = "<br />(set and reset fail counter)";

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
    return ESP_OK;
}


esp_err_t handler_post_wifi( httpd_req_t * req )
{
    HttpHelper hh{ req, s_subWifi, "Wifi" };

    if (!req->content_len) {
        hh.Add( "no data - nothing to be done" );
        return ESP_OK;
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

    if (! parser.ParsePostData( req )) {
        hh.Add( "unexpected end of data while parsing data" );
        return ESP_OK;
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
        hh.Add( "data unchanged" );
        return ESP_OK;
    }

    // ESP_LOGD( TAG, "before SetParam" ); EXPRD( vTaskDelay( 5 ) )

	if (! Wifi::Instance().SetParam( host, bgcol, id[0], pw[0], id[1], pw[1] )) {
		hh.Add( "setting wifi parameter failed - try again" );
        return ESP_OK;
	}

    // ESP_LOGD( TAG, "after SetParam" ); EXPRD( vTaskDelay( 5 ) )

    hh.Add( "configuration has been set" );
    return ESP_OK;
}


esp_err_t handler_get_mqtt( httpd_req_t * req )
{
    Table<5,4> table;
    table.Right( 0 );
    table[0][1] = "&nbsp;";

    table[0][0] = "Host:";
    table[0][2] = "<input type=\"text\" name=\"host\" maxlength=15 value=\"";
    table[0][3] = "(IPv4 address of MQTT broker)";

    table[1][0] = "Port:";
    table[1][2] = "<input type=\"text\" name=\"port\" maxlength=5 value=\"";
    table[1][3] = "(listening port of MQTT broker)";

    table[2][0] = "Publish topic:";
    table[2][2] = "<input type=\"text\" name=\"pub\" maxlength=15 value=\"";
    table[2][3] = "(top level topic for publish)";

    table[3][0] = "Subscribe topic:";
    table[3][2] = "<input type=\"text\" name=\"sub\" maxlength=15 value=\"";
    table[3][3] = "(subscription topic)";

    table[4][2] = "<button type=\"submit\">set</button>";

    {
        ip4_addr_t host = Mqtinator::Instance().GetHost();
        if (host.addr) {
            char buf[16];
            ip4addr_ntoa_r( & host, buf, sizeof(buf) );
            table[0][2] += buf;
        }
        table[0][2] += "\">";
    }
    {
        uint16_t port = Mqtinator::Instance().GetPort();
        if (port) {
            char buf[8];
            char * bp = & buf[sizeof(buf)-1];
            *bp = 0;
            do {
                *--bp = (port % 10) + '0';
                port /= 10;
            } while (port && (bp > buf));
            table[1][2] += bp;
        }
        table[1][2] += "\">";
    }
    table[2][2] += Mqtinator::Instance().GetPubTopic();
    table[2][2] += "\">";

    table[3][2] += Mqtinator::Instance().GetSubTopic();
    table[3][2] += "\">";

    HttpHelper hh{ req, s_subMqtt, "MQTT" };
    hh.Add( "  <form method=\"post\">\n"
            "   <table border=0>\n" );
    table.AddTo( hh );
    hh.Add( "\n   </table>\n"
            "  </form>" );
    return ESP_OK;
}

esp_err_t handler_post_mqtt( httpd_req_t * req )
{
    HttpHelper hh{ req, s_subMqtt, "MQTT" };
    if (!req->content_len) {
        hh.Add( "no data - nothing to be done" );
        return ESP_OK;
    }

    char hostBuf[16];
    char portBuf[8];
    char pub[16];
    char sub[16];
    HttpParser::Input in[] = { { "host", hostBuf, sizeof(hostBuf) },
                               { "port", portBuf, sizeof(portBuf) },
                               { "pub",  pub,     sizeof(pub)  },
                               { "sub",  sub,     sizeof(sub)  } };
    HttpParser parser{ in, sizeof(in) / sizeof(in[0]) };

    if (! parser.ParsePostData( req )) {
        hh.Add( "unexpected end of data while parsing data" );
        return ESP_OK;
    }

    uint8_t changes = 0;
    ip_addr_t host;
    host.addr = ipaddr_addr( hostBuf );
    char *end;
    uint16_t port = (uint16_t) strtoul( portBuf, & end, 0 );
    if (host.addr != Mqtinator::Instance().GetHost().addr)
        changes |= 1 << 0;
    if (port != Mqtinator::Instance().GetPort())
        changes |= 1 << 1;
    if (strcmp( pub, Mqtinator::Instance().GetPubTopic() ))
        changes |= 1 << 2;
    if (strcmp( sub, Mqtinator::Instance().GetSubTopic() ))
        changes |= 1 << 3;

    if (! changes) {
        hh.Add( "data unchanged" );
        return ESP_OK;
    }

	// ESP_LOGI( TAG, "received host \"%s\", ssid \"%s\", password \"%s\"", host, id, pw );
	if (! Mqtinator::Instance().SetParam( host, port, pub, sub )) {
		hh.Add( "setting MQTT parameter failed - try again" );
		return ESP_OK;
	}

    hh.Add( "configuration has been set" );
    return ESP_OK;
}


esp_err_t handler_get_update( httpd_req_t * req )
{
    Updator &updator = Updator::Instance();
    uint8_t const progress = updator.Progress();

    HttpHelper hh{ req, s_subUpdate, "Update" };

    bool const editable = ((progress == 0) || (progress >= 99));
    bool const postable = (editable || (progress == 90) || (progress == 95));
    bool const showmsg  = ((! postable) || (progress == 99));

    if (! postable) {
        hh.Head( "<meta http-equiv=\"refresh\" content=\"1; URL=/update\">" );
    }

    if (editable) {
        Table<4,8> table;
        table[0][0] = "Idx";
        table[0][1] = "Partition";
        table[0][2] = "&nbsp;";
        table[0][3] = "Label";
        table[0][4] = "Seq.";
        table[0][5] = "Test stage";
        table[0][6] = "Description";
        table[0][7] = "Address";

        table.Right(0);
        table.Right(1);
        table.Right(4);
        table.Right(5);
        table.Right(7);

        const esp_partition_t* running = esp_ota_get_running_partition();
        const esp_partition_t* otadata = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
        const uint8_t n = get_ota_partition_count();

        uint8_t                  actOtaIdx = 2;
        esp_ota_select_entry_t   s[2];
        esp_ota_select_entry_t * sel[2] = { 0, 0 };

        for (uint8_t i = 0; i < 2; ++i) {
            esp_err_t ret = spi_flash_read(otadata->address + (i * SPI_FLASH_SEC_SIZE), &s[i], sizeof(esp_ota_select_entry_t));
            if (ret != ESP_OK)
                memset( &s[i], 0xff, sizeof(esp_ota_select_entry_t) );
            else if ((s[i].ota_seq % n) == (running->subtype & PART_SUBTYPE_OTA_MASK)) {
                actOtaIdx = i;
                sel[0] = &s[i];
                sel[1] = &s[i^1];
            }
        }
        if (sel[0]) {
            table[1][0] = "[" + std::to_string((int)  actOtaIdx     ) + "]";
            table[2][0] = "[" + std::to_string((int) (actOtaIdx ^ 1)) + "]";
            table[1][1] = "active";
            table[2][1] = "inactive";
            table[1][7] = "0x" + HttpHelper::HexString( running->address, 1 );
        } else {
            table[1][0] = "[0]";
            table[2][0] = "[1]";
        }
        uint8_t switchable = 0;
        uint8_t test_stage[2] = {OTA_TEST_STAGE_LZ_MOD4_FAILED,OTA_TEST_STAGE_LZ_MOD4_FAILED};
        for (uint8_t i = 0; i < 2; ++i) {
            esp_ota_select_entry_t * sp = sel[i];
            if (! sp) {
                sp = & s[i];
                char x[2];
                x[1] = 0;
                x[0] = 'A' + (s[i].ota_seq % n);
                table[1+i][1] = x;
            }
            if (sp->seq_label[0] != 0xff)
                table[1+i][3] = (char *) sp->seq_label;
            table[1+i][4] = std::to_string( sp->ota_seq );

            table[1+i][5] = "0x" + HttpHelper::HexString( sp->test_stage, 1 );
            uint8_t const lz = __builtin_clz(sp->test_stage);
            bool const isMask = ((sp->test_stage + 1) == (0x80000000 >> (lz - 1)));
            if (isMask) {
                if (sp->test_stage > 0xf)
                    switchable |= 1 << i;
                test_stage[i] = (uint8_t) (lz & 3);
                switch (lz & 3)
                {
                    case OTA_TEST_STAGE_LZ_MOD4_TO_TEST: table[1+i][6] = "to test"; break;
                    case OTA_TEST_STAGE_LZ_MOD4_TESTING: table[1+i][6] = "testing"; break;
                    case OTA_TEST_STAGE_LZ_MOD4_FAILED:  table[1+i][6] = "failed";  break;
                    case OTA_TEST_STAGE_LZ_MOD4_PASSED:  table[1+i][6] = "passed";  break;
                }
            }
        }

        uint8_t idx = 2;
        uint8_t val = OTA_TEST_STAGE_LZ_MOD4_PASSED;
        // toggle active partition?
        if (sel[0] && switchable) {
            if (sel[0]->ota_seq > sel[1]->ota_seq) {  // active partition is newer
                // switchable, when test for other partition passed
                if ((switchable & 1) && (test_stage[1] == OTA_TEST_STAGE_LZ_MOD4_PASSED)) {
                    table[3][4] = "<button type=\"submit\">";
                    table[3][4] += "mark active as failed<br>to reactivate old image";
                    table[3][4] += "</button>";
                    table[3].Unite( 4 );
                    idx = actOtaIdx;
                    val = OTA_TEST_STAGE_LZ_MOD4_FAILED;
                }
            } else {  // active partition is older
                if (switchable & 2) {
                    table[3][4] = "<button type=\"submit\">";
                    table[3][4] += "retest newer image";
                    table[3][4] += "</button>";
                    table[3].Unite( 4 );
                    idx = actOtaIdx ^ 1;
                    val = OTA_TEST_STAGE_LZ_MOD4_TO_TEST;
                }
            }
        }

        hh.Add( "  <div style=\"float: right;\">\n" );
        if (val != OTA_TEST_STAGE_LZ_MOD4_PASSED) {
            hh.Add( "   <form method=\"get\" action=\"/reboot\">\n" );
            hh.Add( "    <input type=\"hidden\" name=\"idx\" value=\"" + std::to_string((int) idx) + "\" />" );
            hh.Add( "    <input type=\"hidden\" name=\"val\" value=\"" + std::to_string((int) val) + "\" />" );
        }
        hh.Add( "    <table>\n" );

        table.AddTo( hh, 1 );

        hh.Add( "    </table>\n" );
        if (val != OTA_TEST_STAGE_LZ_MOD4_PASSED)
            hh.Add( "   </form>\n" );
        hh.Add( "  </div>\n" );
    }

    hh.Add( "  <div>\n" );
    if (postable)
        hh.Add( "  <form method=\"post\">\n" );
    hh.Add( "  <table>\n" );
    {
        Table<4,4> table;
        table.Right( 0 );       // 1st column: right aligned
        table[0][1] = "&nbsp;"; // some padding

        table[0][0] = "uri:";
        if (editable)
            table[0][2] = "<input type=\"text\" name=\"uri\" maxlength=79"
                          " alt=\"setup a web server for download to the device\""
                          " value=\"";
        table[0][2] += updator.GetUri();
        if (editable)
            table[0][2] += "\">";
 
        table[1][0] = "status:";
        switch (progress)
        {
            case   0: table[1][2] = "ready to update";    break;
            case  90: table[1][2] = "wait for reboot";    break;
            case  95: table[1][2] = "test booting";       break;
            case  99: table[1][2] = "update failed";      break;
            case 100: table[1][2] = "update succeeded";   break;
            default:  table[1][2] = "update in progress"; break;
        }

        if (postable) {
            table[2][2] = "<button type=\"submit\">";
            switch (progress)
            {
                case   0: table[2][2] += "start update";         break;
                case  90: table[2][2] += "reboot";               break;
                case  95: table[2][2] += "confirm well booting"; break;
                case  99: table[2][2] += "retry update";         break;
                case 100: table[2][2] += "update again";         break;
            }
            table[2][2] += "</button>";
        } else {
            table[2][0] = "progress:";
            table[2][2] = "<progress max=\"100\" value=\"";
            char buf[8];
            sprintf( buf, "%d", progress );
            table[2][2] += buf;
            table[2][2] += "\" />";
        }

        if (showmsg) {
            const char * msg = updator.GetMsg();
            if (msg && *msg) {
                if (progress == 99)
                    table[3][0] = "last status message:";
                else
                    table[3][0] = "status message:";
                table[3][2] = msg;
            }
        }
        table.AddTo( hh );
    }
    hh.Add( "   </table>\n" );
    if (postable)
        hh.Add( "  </form>\n" );
    hh.Add( "  </div>\n" );
    hh.Add( "  <div style=\"clear: both\"></div>\n" );

    if (editable) {
        hh.Add( "  <br /><br />\n"
                " <h2>Favicon update</h2>\n"
                "  <form method=\"post\" action=\"/favicon\">\n"
                "   <table>\n" );
        {
            Table<3,4> table;
            table.Right( 0 );
            table[0][1] = "&nbsp;";

            table[0][0] = "type:";
            table[0][2] = "<input type=\"text\" name=\"type\" maxlength=16 value=\"image/x-icon\">";
            table[0][3] = "(... or image/gif etc.)";

            table[1][0] = "image URI:";
            table[1][2] = "<input type=\"text\" name=\"img\" maxlength=80";
            table[1][3] = "(where to get the image)";

            table[2][2] = "<button type=\"submit\">favicon update</button>";

            char const * strFw = updator.GetUri();
            if (*strFw) {
                table[1][2] += " value=\"";
                int lenFw = strlen( strFw );
                if (strcmp( strFw + lenFw - 4, ".bin" ) == 0)
                    lenFw -= 4;
                char const * build = strstr( strFw, "/build/" );
                if (build) {
                    const int len0 = build + 1 - strFw;  // incl. leading /
                    const int off  = len0 + 5;           // incl. trailing /
                    const int len1 = lenFw - off;
                    table[1][2].append( strFw, len0 );
                    table[1][2].append( "images" );
                    table[1][2].append( strFw + off, len1 );
                } else {
                    table[1][2].append( strFw, lenFw );
                }
                table[1][2] += ".ico\"";
            }
            table[1][2] += ">";

            table.AddTo( hh );
        }
        hh.Add( "   </table>\n"
                "  </form>\n" );
    }

    if (progress != 90) {
        hh.Add( "  <br /><br />\n"
                " <h2>Restart device</h2>\n"
                "  <form method=\"get\" action=\"/reboot\">\n"
                "   <button type=\"submit\">reboot</button>\n"
                "  </form>\n" );
        if (progress == 95)
            hh.Add( " (when reboot in another way than by \"confirm well booting\","
                    " the system will fallback to the previous partition)\n" );
    }
    return ESP_OK;
}

esp_err_t handler_post_update( httpd_req_t * req )
{
    ESP_LOGD( TAG, "handler_post_update enter" ); EXPRD(vTaskDelay(1))

    Updator &updator = Updator::Instance();
    uint8_t progress = updator.Progress();

    if (progress == 90) {
        {
            HttpHelper hh{ req, s_subUpdate, "Update" };
            hh.Head( "<meta http-equiv=\"refresh\" content=\"5; URL=/update\">" );
            hh.Add( "  <h3>system will reboot...</h3>\n"
                    "  <br />\n"
                    "  <br />This page should become refreshed automatically, when system is up again.\n"
                    "  <br />\n"
                    "  <br />When the device does not boot properly, power off and on.\n"
                    "  <br />This will again activate the old image as fallback image.\n"
                    "  <br />\n"
                    "  <br />On proper boot up, you have to confirm this by the button\n"
                    "  <br />shown in this sub page after the page has been refreshed.\n" );
        }
        vTaskDelay( configTICK_RATE_HZ / 4 );  // give http stack a chance to send out
        esp_restart();
    }

    HttpHelper hh{ req, s_subUpdate, "Update" };

    if ((progress > 0) && (progress < 99)) {
        if (progress == 95)
            updator.Confirm();

        hh.Head( "<meta http-equiv=\"refresh\" content=\"1; URL=/update\">" );
        return ESP_OK;
    }

    char uri[80];
    HttpParser::Input in[] = { {"uri",uri,sizeof(uri)} };
    HttpParser parser{ in, sizeof(in) / sizeof(in[0]) };

    if (! parser.ParsePostData( req )) {
        hh.Add( "unexpected end of data while parsing data" );
        return ESP_OK;
    }
    const char * err = 0;
    if (! uri[0])
        err = "no uri specified";
    else if (strcmp( updator.GetUri(), uri )) {
        if (! updator.SetUri( uri ))
            err = "failed to store new uri";
    }
    if (err) {
        hh.Add( "  <form method=\"post\">\n" // enctype=\"multipart/form-data\"
                "   <table>\n"
                "    <tr><td align=\"right\">uri:</td><td>&nbsp;</td>\n"
                "     <td><input type=\"text\" name=\"uri\" value=\"" );
        hh.Add( uri );
        hh.Add( "\" maxlength=79 alt=\"setup a web server for download to the device\"></td></tr>\n"
                "    <tr><td /><td /><td><button type=\"submit\">try again</button></td></tr>\n"
                "    <tr><td align=\"right\">status:</td><td /><td>" );
        hh.Add( err );
        hh.Add( "</td></tr>\n"
                "   </table>\n" );
        return ESP_OK;
    }

    hh.Head( "<meta http-equiv=\"refresh\" content=\"1; URL=/update\">" );
    hh.Add( "   <table>\n"
            "    <tr><td align=\"right\">uri:</td><td>&nbsp;</td>\n"
            "     <td>" );
    hh.Add( updator.GetUri() );
    hh.Add( "</td></tr>\n"
            "    <tr><td align=\"right\">status:</td><td /><td>triggering update</td></tr>\n"
            "    <tr><td align=\"right\">progress:</td><td /><td><progress value=\"0\" max=\"100\"></progress></td></tr>\n"
            "   </table>\n" );

    updator.Go();
    return ESP_OK;
}

esp_err_t handler_get_reboot( httpd_req_t * req )
{
    {   // will send when hh will loose scope

        HttpHelper hh{ req, s_subReboot, "Update" };
        hh.Head( "<meta http-equiv=\"refresh\" content=\"5; URL=/\">" );
        hh.Add( "This device will reboot - you will get redirected in 5 secs." );

        {  // re-test or fallback?
            char idx[4];
            char val[4];
            HttpParser::Input in[] = { {"idx",idx,sizeof(idx)},
                                       {"val",val,sizeof(val)} };
            HttpParser parser{ in, sizeof(in) / sizeof(in[0]) };

            if (! parser.ParseUriParam( req )) {
                hh.Add( "<br /><br />\n" );
                hh.Add( "unexpected end of data while parsing data\n" );
                return ESP_OK;
            }

            if (idx[0] && val[0]) {
                hh.Add( "<br /><br />\n" );
                const esp_partition_t * otadata = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
                const uint8_t i = idx[0] & 1;
                const uint8_t newstage = val[0] & 3;
                const uint32_t addr = otadata->address + ((int) i * SPI_FLASH_SEC_SIZE) + offsetof(esp_ota_select_entry_t,test_stage);
                uint32_t test_stage;
                if (spi_flash_read( addr, &test_stage, sizeof(test_stage) ) == ESP_OK) {
                    const uint8_t lz = __builtin_clz(test_stage);
                    const uint8_t shift = ((32 + newstage) - lz) & 3;
                    if (shift) {
                        if ((lz+shift)<=32) {
                            test_stage >>= shift;
                            spi_flash_write( addr, &test_stage, sizeof(test_stage) );
                            hh.Add( "test stage adopted\n" );
                        } else
                            hh.Add( "cannot adopt test stage - too less free bits\n" );
                    } else
                        hh.Add( "test stage already set as requested\n" );
                } else
                    hh.Add( "read error while reading test stage at address %#x\n", addr );
            }
        }
    }

    vTaskDelay( configTICK_RATE_HZ / 10 );

    esp_restart();

    // no return - on error:
    ESP_LOGE( TAG, "esp_restart returned" );
    return ESP_OK;
}

esp_err_t handler_get_readflash( httpd_req_t * req )
{
    unsigned long addr = 0;
    unsigned long size = 0;
    {
        char addrBuf[16];
        char sizeBuf[16];
        HttpParser::Input in[] = { { "addr", addrBuf, sizeof(addrBuf) },
                                   { "size", sizeBuf, sizeof(sizeBuf) } };
        HttpParser parser{ in, sizeof(in) / sizeof(in[0]) };

        if (! parser.ParseUriParam( req )) {
            HttpHelper hh{ req, "read flash" };
            hh.Add( "unexpected end of data while parsing data" );
            return ESP_FAIL;
        }
        char * end = 0;
        const char * addrErr = 0;
        const char * sizeErr = 0;
        if (addrBuf[0]) {
            addr = strtoul( addrBuf, & end, 0 );
            if (*end)
                addrErr = "invalid address";
        }
        if (! sizeBuf[0])
            sizeErr = "no size specified";
        else {
            size = strtoul( sizeBuf, & end, 0 );
            if (*end)
                sizeErr = "invalid size";
            else if (! size)
                sizeErr = "size 0 not allowed";
        }
        if (addrErr || sizeErr) {
            HttpHelper hh{ req, "read flash" };
            if (addrErr)
                hh.Add( addrErr );
            if (addrErr && sizeErr)
                hh.Add( " and " );
            if (sizeErr)
                hh.Add( sizeErr );
            return ESP_FAIL;
        }
    }
    char buf[0x400];
    while (size) {
        unsigned long len = size < sizeof(buf) ? size : sizeof(buf);
        spi_flash_read(       addr, buf, len );
        httpd_resp_send_chunk( req, buf, len );
        addr += len;
        size -= len;
    }
    httpd_resp_send_chunk( req, 0, 0 );
    return ESP_OK;
}

httpd_handle_t start_webserver( void )
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 20;

    // Start the httpd server
    ESP_LOGI( TAG, "Starting web server on port: '%d'", config.server_port );
    if (httpd_start( &server, &config ) == ESP_OK) {
        return server;
    }

    ESP_LOGI( TAG, "Error starting web server!" );
    return NULL;
}

void stop_webserver( httpd_handle_t server )
{
    // Stop the httpd server
    httpd_stop( server );
}

void disconnect_handler( void * arg, esp_event_base_t event_base,
        int32_t event_id, void * event_data )
{
    httpd_handle_t *server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI( TAG, "Stopping web server" );
        stop_webserver( *server );
        *server = NULL;
    }
}

void connect_handler( void * arg, esp_event_base_t event_base,
        int32_t event_id, void * event_data )
{
    httpd_handle_t *server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI( TAG, "Starting web server" );
        *server = start_webserver();
    }
}

} // extern "C"

namespace {

const httpd_uri_t uri_main          = { .uri = "/",            .method = HTTP_GET,  .handler = handler_get_main,      .user_ctx = 0 };
const httpd_uri_t uri_get_favicon   = { .uri = "/favicon.ico", .method = HTTP_GET,  .handler = handler_get_favicon,   .user_ctx = 0 };
const httpd_uri_t uri_post_favicon  = { .uri = "/favicon",     .method = HTTP_POST, .handler = handler_post_favicon,  .user_ctx = 0 };
const httpd_uri_t uri_readflash     = { .uri = "/readflash",   .method = HTTP_GET,  .handler = handler_get_readflash, .user_ctx = 0 };
const httpd_uri_t uri_get_wifi      = { .uri = "/wifi",        .method = HTTP_GET,  .handler = handler_get_wifi,      .user_ctx = 0 };
const httpd_uri_t uri_post_wifi     = { .uri = "/wifi",        .method = HTTP_POST, .handler = handler_post_wifi,     .user_ctx = 0 };
const httpd_uri_t uri_get_mqtt      = { .uri = "/mqtt",        .method = HTTP_GET,  .handler = handler_get_mqtt,      .user_ctx = 0 };
const httpd_uri_t uri_post_mqtt     = { .uri = "/mqtt",        .method = HTTP_POST, .handler = handler_post_mqtt,     .user_ctx = 0 };
const httpd_uri_t uri_get_update    = { .uri = "/update",      .method = HTTP_GET,  .handler = handler_get_update,    .user_ctx = 0 };
const httpd_uri_t uri_post_update   = { .uri = "/update",      .method = HTTP_POST, .handler = handler_post_update,   .user_ctx = 0 };
const httpd_uri_t uri_get_reboot    = { .uri = "/reboot",      .method = HTTP_GET,  .handler = handler_get_reboot,    .user_ctx = 0 };

const WebServer::Page page_home    { uri_main,       "Home" };
const WebServer::Page page_wifi    { uri_get_wifi,   "Wifi" };
const WebServer::Page page_mqtt    { uri_get_mqtt,   "MQTT" };
const WebServer::Page page_update  { uri_get_update, "Update" };

} // namespace

WebServer& WebServer::Instance()
{
    return s_WebServer;
}

void WebServer::AddPage( const WebServer::Page & page,
        const httpd_uri_t * postUri )
{
    PageList *elem = new PageList { page };

    if (mLastElem) {
        mLastElem->Next = elem;
    } else {
        mAnchor = elem;
    }
    mLastElem = elem;

    httpd_register_uri_handler( mServer, &page.Uri );
    if (postUri)
        httpd_register_uri_handler( mServer, postUri );
}

void WebServer::MainPage( httpd_req_t * req )
{
    HttpHelper hh{ req, 0, "Home" };
    Table<7,3> table;

    const esp_app_desc_t *const desc = esp_ota_get_app_description();
    table[0][1] = "&nbsp;";
    table[0][0] = "Project name:";     table[0][2] = desc->project_name;
    table[1][0] = "Project version:";  table[1][2] = desc->version;
    table[2][0] = "IDF version:";      table[2][2] = desc->idf_ver;
    table[3][0] = "RTOS version:";     table[3][2] = tskKERNEL_VERSION_NUMBER;
    table[4][0] = "signaling mask:";
    table[5][0] = "sec. sig. mask:";

    Indicator & indicator = Indicator::Instance();
    {
        const unsigned long * mask  = indicator.SigMask();
        const uint8_t       * slots = indicator.SigSlots();
        for (uint8_t led = 0; led < indicator.NofLEDs(); ++led) {
            table[4 + led][2] = "0x";
            table[4 + led][2] += HttpHelper::HexString( (uint32_t) mask[led] );
            if (mask[led]) {
                table[4 + led][2] += " (";
                table[4 + led][2] += HttpHelper::String( (uint32_t) slots[led] );
                table[4 + led][2] += " slots)";
            }
        }
    }

    const uint8_t upTimeRow = 4 + indicator.NofLEDs();
    table[upTimeRow][0] = "uptime:";
    {
        unsigned long uptime = (unsigned long) (g_esp_os_cpu_clk / (CPU_CLK_FREQ));
        unsigned int  secs   = (unsigned int) (uptime %  60); uptime /=  60;
        unsigned int  mins   = (unsigned int) (uptime %  60); uptime /=  60;
        unsigned int  hours  = (unsigned int) (uptime %  24); uptime /=  24;
        unsigned int  days   = (unsigned int) (uptime % 365); uptime /= 365;
        unsigned int  years  = (unsigned int)  uptime;
        uint8_t ydhms;
        if (years)      ydhms = 0;
        else if (days)  ydhms = 1;
        else if (hours) ydhms = 2;
        else if (mins)  ydhms = 3;
        else            ydhms = 4;

        std::string up{""};
        switch (ydhms) {
            case 0: up += std::to_string( years ) + " years, ";  // fallthru
            case 1: up += std::to_string( days  ) + " days, ";   // fallthru
            case 2: up += std::to_string( hours ) + ":";         // fallthru
            case 3: up += HttpHelper::String( (long) mins, ydhms < 3 ? 2 : 1 ) + ":";  // fallthru
            case 4: up += HttpHelper::String( (long) secs, ydhms < 4 ? 2 : 1 ) + " ";
        }
        const char * const s_hms = "h:mm:ss";
        up += & s_hms[ ydhms <= 2 ? 0 : (ydhms - 2) * 3 ];
        table[upTimeRow][2] = up;
    }

    hh.Add( "  <table>\n" );
    table.AddTo( hh );
    hh.Add( "  </table>\n" );
}

void WebServer::Init()
{
    ESP_LOGI( TAG, "Start web server" );
    mServer = start_webserver();
    if (! mServer)
        return;

    ESP_LOGI( TAG, "Registering URI handlers" ); EXPRD( vTaskDelay(1) )
    AddPage( page_home, &uri_main );

    ESP_LOGD( TAG, "Add sub pages" ); EXPRD( vTaskDelay(1) )
    AddPage( page_wifi,   &uri_post_wifi );
    AddPage( page_mqtt,   &uri_post_mqtt );
    AddPage( page_update, &uri_post_update );

    httpd_register_uri_handler( mServer, &uri_get_reboot );
    httpd_register_uri_handler( mServer, &uri_get_favicon );
    httpd_register_uri_handler( mServer, &uri_post_favicon );
    httpd_register_uri_handler( mServer, &uri_readflash );
}
