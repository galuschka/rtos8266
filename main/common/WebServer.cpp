/*
 * WebServer.h
 *
 *  Created on: 29.04.2020
 *      Author: galuschka
 */

//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "WebServer.h"
#include "Wifi.h"
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
const char * const s_subReboot  = "Restart device";
WebServer          s_WebServer{};

} // namespace

extern "C" esp_err_t handler_get_main( httpd_req_t * req )
{
    s_WebServer.MainPage( req );
    return ESP_OK;
}

extern "C" esp_err_t handler_get_favicon( httpd_req_t * req )
{
    do { // while(0)
        nvs_handle my_handle;
        if (nvs_open( "images", NVS_READONLY, &my_handle ) != ESP_OK)
            break;
        do { // while(0)
            char type[16];
            size_t len = sizeof(type) - 1;
            if (nvs_get_str( my_handle, "favicon-type", type, & len ) != ESP_OK) {
                ESP_LOGD( TAG, "favicon-type not in nvs" );
                break;
            }
            type[sizeof(type)-1] = 0;

            len = 0;
            if (nvs_get_blob( my_handle, "favicon", 0, & len ) != ESP_OK) {
                ESP_LOGI( TAG, "favicon-type known (\"%s\"), but favicon not stored", type );
                break;
            }
            if ((len < 64) || (len > 5 * 4096)) {  // max. 5 of 6 flash pages each 4KB
                ESP_LOGE( TAG, "invalid favicon size %d (no in %d..%d)", len, 64, 5 * 4096 );
                break;
            }
            void * data = malloc( len );
            if (! data) {
                ESP_LOGE( TAG, "cannot allocate favicon buffer of %d bytes", len );
                break;
            }
            if (nvs_get_blob( my_handle, "favicon", data, & len ) != ESP_OK) {
                free( data );
                ESP_LOGE( TAG, "reading favicon failed" );
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

    httpd_resp_set_type( req, "image/gif" /*or x-icon*/ );
    httpd_resp_send( req, favicon_ico, sizeof(favicon_ico) );
    return ESP_OK;
}

extern "C" esp_err_t handler_post_favicon( httpd_req_t * req )
{
    char type[16];
    char uri[80];
    HttpParser::Input in[] = { { "type", type, sizeof(type) },
                               { "img",  uri,  sizeof(uri)  } };
    HttpParser parser{ in, sizeof(in) / sizeof(in[0]) };

    HttpHelper hh{ req, "favicon" };
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
        if (esp == ESP_OK) {
            esp = nvs_set_blob( my_handle, "favicon", buf, totalLen );
            if (esp == ESP_OK) {
                nvs_commit( my_handle );
            }
        }
        nvs_close( my_handle );

        if (esp == ESP_OK)
            hh.Add( "success" );
        else
            hh.Add( "failed to store type and image" );
    } while (0);

    if (buf)
        free( buf );
    esp_http_client_cleanup( client );

    return ESP_OK;
}

extern "C" esp_err_t handler_get_wifi( httpd_req_t * req )
{
    HttpHelper hh{ req, s_subWifi };
    hh.Add( " <form method=\"post\">\n"
            "  <table border=0>\n"
    );

    {
        Table<4,4> table;
        table.Right( 0 );
        table[0][1] = "&nbsp;";

        table[0][0] = "Hostname:";
        table[0][2] = "<input type=\"text\" name=\"host\" maxlength=15 value=\"";
        table[0][3] = "(used also as SSID in AP mode)";

        table[1][0] = "SSID:";
        table[1][2] = "<input type=\"text\" name=\"id\" maxlength=15 value=\"";
        table[1][3] = "(remote SSID in station mode)";

        table[2][0] = "Password:";
        table[2][2] = "<input type=\"password\" name=\"pw\" maxlength=31>";

        table[3][2] = "<button type=\"submit\">set</button>";

        const char * const host = Wifi::Instance().GetHost();
        if (host && *host)
            table[0][2] += host;
        table[0][2] += "\">";

        const char * const id = Wifi::Instance().GetSsid();
        if (id && *id)
            table[1][2] += id;
        table[1][2] += "\">";

        const char * const pw = Wifi::Instance().GetPassword();
        if (pw && *pw)
            table[2][3] = "(keep empty to not change)";

        table.AddTo( hh );
    }

    hh.Add( "  </table>\n"
            " </form>" );
    return ESP_OK;
}


extern "C" esp_err_t handler_post_wifi( httpd_req_t * req )
{
    HttpHelper hh{ req, s_subWifi };

    if (!req->content_len) {
        hh.Add( "no data - nothing to be done" );
        return ESP_OK;
    }

    char host[16];
    char id[16];
    char pw[32];
    HttpParser::Input in[] = { { "host", host, sizeof(host) },
                               { "id",   id,   sizeof(id)   },
                               { "pw",   pw,   sizeof(pw)   } };
    HttpParser parser{ in, sizeof(in) / sizeof(in[0]) };

    if (! parser.ParsePostData( req )) {
        hh.Add( "unexpected end of data while parsing data" );
        return ESP_OK;
    }

    if (! strcmp( host, Wifi::Instance().GetHost() ))
        host[0] = 0;
    if (! strcmp( id, Wifi::Instance().GetSsid() ))
        id[0] = 0;
    if (! strcmp( pw, Wifi::Instance().GetPassword() ))
        pw[0] = 0;
    if (! (host[0] || id[0] || pw[0])) {
        hh.Add( "data unchanged" );
        return ESP_OK;
    }

	// ESP_LOGI( TAG, "received host \"%s\", ssid \"%s\", password \"%s\"", host, id, pw );
	if (! Wifi::Instance().SetParam( host, id, pw )) {
		hh.Add( "setting wifi parameter failed - try again" );
        return ESP_OK;
	}

    hh.Add( "configuration has been set" );
    return ESP_OK;
}


extern "C" esp_err_t handler_get_mqtt( httpd_req_t * req )
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

    HttpHelper hh{ req, s_subMqtt };
    hh.Add( "  <form method=\"post\">\n"
            "   <table border=0>\n" );
    table.AddTo( hh );
    hh.Add( "\n   </table>\n"
            "  </form>" );
    return ESP_OK;
}

extern "C" esp_err_t handler_post_mqtt( httpd_req_t * req )
{
    HttpHelper hh{ req, s_subMqtt };
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


extern "C" esp_err_t handler_get_update( httpd_req_t * req )
{
    Updator &updator = Updator::Instance();
    uint8_t progress = updator.Progress();

    HttpHelper hh{ req, s_subUpdate };

    bool const editable = ((progress == 0) || (progress >= 99));
    bool const postable = (editable || (progress == 90) || (progress == 95));
    bool const showmsg  = ((! postable) || (progress == 99));

    if (postable) {
        hh.Add( "  <form method=\"post\">\n" );
    } else {
        hh.Head( "<meta http-equiv=\"refresh\" content=\"1; URL=/update\">" );
    }
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
    return ESP_OK;
}

extern "C" esp_err_t handler_post_update( httpd_req_t * req )
{
    ESP_LOGD( TAG, "handler_post_update enter" ); EXPRD(vTaskDelay(1))

    Updator &updator = Updator::Instance();
    uint8_t progress = updator.Progress();

    if (progress == 90) {
        {
            HttpHelper hh{ req, s_subUpdate };
            hh.Head( "<meta http-equiv=\"refresh\" content=\"10; URL=/update\">" );
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

    HttpHelper hh{ req, s_subUpdate };

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

extern "C" esp_err_t handler_get_reboot( httpd_req_t * req )
{
    HttpHelper hh{ req, s_subReboot };
    hh.Add( "<form method=\"post\">"
            "<button type=\"submit\">reboot</button>"
            "</form>" );
    return ESP_OK;
}

extern "C" esp_err_t handler_post_reboot( httpd_req_t * req )
{
    {
        HttpHelper hh{ req, s_subReboot };
        hh.Head( "<meta http-equiv=\"refresh\" content=\"7; URL=/\">" );
        hh.Add( "This device will reboot - you will get redirected soon" );
    }
    vTaskDelay( configTICK_RATE_HZ / 10 );

    esp_restart();

    // no return - on error:
    ESP_LOGE( TAG, "esp_restart returned" );
    return ESP_OK;
}

extern "C" esp_err_t handler_get_readflash( httpd_req_t * req )
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
            HttpHelper hh{ req, "readflash" };
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
            HttpHelper hh{ req, "readflash" };
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

//@formatter:off

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
const httpd_uri_t uri_post_reboot   = { .uri = "/reboot",      .method = HTTP_POST, .handler = handler_post_reboot,   .user_ctx = 0 };

const WebServer::Page page_wifi    { uri_get_wifi,   s_subWifi };
const WebServer::Page page_mqtt    { uri_get_mqtt,   s_subMqtt };
const WebServer::Page page_update  { uri_get_update, s_subUpdate };
const WebServer::Page page_reboot  { uri_get_reboot, s_subReboot };

//@formatter:on

extern "C" httpd_handle_t start_webserver( void )
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

extern "C" void stop_webserver( httpd_handle_t server )
{
    // Stop the httpd server
    httpd_stop( server );
}

extern "C" void disconnect_handler( void * arg, esp_event_base_t event_base,
        int32_t event_id, void * event_data )
{
    httpd_handle_t *server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI( TAG, "Stopping web server" );
        stop_webserver( *server );
        *server = NULL;
    }
}

extern "C" void connect_handler( void * arg, esp_event_base_t event_base,
        int32_t event_id, void * event_data )
{
    httpd_handle_t *server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI( TAG, "Starting web server" );
        *server = start_webserver();
    }
}

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
    HttpHelper hh{ req };

    const esp_app_desc_t *const desc = esp_ota_get_app_description();
    hh.Add( "  <table border=0>\n"
            "   <tr><td>Project version:</td>" "<td>&nbsp;</td>" "<td>" );
    hh.Add( desc->version );
    hh.Add( "</td></tr>\n"
            "   <tr><td>IDF version:</td>"     "<td />" "<td>" );
    hh.Add( desc->idf_ver );
    hh.Add( "</td></tr>\n"
            "  </table>\n" );
    hh.Add( "  <br />\n" );
    for (PageList *elem = mAnchor; elem; elem = elem->Next) {
        hh.Add( "  <br />\n" );
        hh.Add( "  <a href=\"" );
        hh.Add( elem->Page.Uri.uri );
        hh.Add( "\">" );
        hh.Add( elem->Page.LinkText );
        hh.Add( "</a>\n" );
    }
}

void WebServer::Init()
{
    ESP_LOGI( TAG, "Start web server" );
    mServer = start_webserver();
    if (! mServer)
        return;

    ESP_LOGI( TAG, "Registering URI handlers" ); EXPRD( vTaskDelay(1) )
    httpd_register_uri_handler( mServer, &uri_main );
    httpd_register_uri_handler( mServer, &uri_get_favicon );
    httpd_register_uri_handler( mServer, &uri_post_favicon );
    httpd_register_uri_handler( mServer, &uri_readflash );

    ESP_LOGD( TAG, "Add sub pages" ); EXPRD( vTaskDelay(1) )
    AddPage( page_wifi,   &uri_post_wifi );
    AddPage( page_mqtt,   &uri_post_mqtt );
    AddPage( page_update, &uri_post_update );
    AddPage( page_reboot, &uri_post_reboot );
}
