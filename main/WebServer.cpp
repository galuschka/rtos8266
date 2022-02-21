/*
 * WebServer.h
 *
 *  Created on: 29.04.2020
 *      Author: galuschka
 */

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "WebServer.h"

#include "Wifi.h"
#include "Updator.h"

#include "esp_log.h"   			// ESP_LOGI()
#include "esp_event_base.h"   	// esp_event_base_t
#include "esp_ota_ops.h"        // esp_ota_get_app_description()
#include "../favicon.i"         // favicon_ico

// include "esp_task_wdt.h"     // esp_task_wdt_reset(), ...
#undef  WDT_FEED
#define WDT_FEED()  // esp_task_wdt_reset()  // reset WDT

#if (LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG)
#define EXPRD(expr) do { expr; } while(0);
#else
#define EXPRD(expr)
#endif

static const char *TAG = "WebServer";
static WebServer * s_WebServer;

#define min(a,b) ((a) < (b) ? a : b)

#define SendCharsChunk( req, chararray )  httpd_resp_send_chunk( req, chararray, sizeof(chararray) - 1 )

#define SendConstChunk( req, text ) do { static const char s_text[] = text; \
                                         SendCharsChunk( req, s_text ); \
                                    } while (0)

namespace
{
void SendStringChunk( httpd_req_t * req, const char * string )
{
    httpd_resp_send_chunk( req, string, strlen( string ) );
}

void SendInitialChunk( httpd_req_t * req, bool link2parent = true, const char * meta = 0 )
{
    SendConstChunk( req, "<!DOCTYPE html>\n"
                         "<html>\n"
                         " <head><meta charset=\"utf-8\"/>" );
    if (meta)
        SendStringChunk( req, meta );

    SendConstChunk( req, "\n </head>\n"
                           " <body>\n" );
    if (link2parent) {
        const esp_app_desc_t *const desc = esp_ota_get_app_description();
        SendConstChunk( req, "  <h1><a href=\"/\">" );
        SendStringChunk( req, desc->project_name );
        SendConstChunk( req, "</a></h1>\n" );
    }
}

void SendFinalChunk( httpd_req_t * req, bool link2parent = true )
{
    /*
    if (link2parent) {
        SendConstChunk( req, "<br /><br /><a href=\"/\">Home</a>\n" );
    }
    */
    SendStringChunk( req, " </body>\n" "</html>\n" );
    httpd_resp_send_chunk( req, 0, 0 );
}
}

extern "C" esp_err_t handler_get_main( httpd_req_t * req )
{
    s_WebServer->MainPage( req );
    return ESP_OK;
}

extern "C" esp_err_t handler_get_favicon( httpd_req_t * req )
{
    httpd_resp_set_type( req, "image/x-icon" );
    httpd_resp_send( req, favicon_ico, sizeof(favicon_ico) );
    return ESP_OK;
}

extern "C" esp_err_t handler_get_wifi( httpd_req_t * req )
{
    SendInitialChunk( req );
    SendConstChunk( req, "<form method=\"post\">"
                             "<table border=0>"
                              "<tr>"
                               "<td>SSID:</td>"
                               "<td><input type=\"text\" name=\"id\" value=\"" );
    const char * const id = s_WebServer->wifi().GetSsid();
    if (id && *id) {
        SendStringChunk( req, id );
    }
    SendConstChunk( req, "\" maxlength=31></td>"
                              "</tr>\n   "
                              "<tr>"
                               "<td>Password:</td>"
                               "<td><input type=\"password\" name=\"pw\" maxlength=31></td>"
                              "</tr>\n   "
                              "<tr>"
                               "<td />"
                               "<td><button type=\"submit\">set</button></td>"
                              "</tr>\n  "
                             "</table>\n "
                            "</form>" );
    SendFinalChunk( req );
    return ESP_OK;
}

extern "C" esp_err_t handler_post_wifi( httpd_req_t * req )
{
    SendInitialChunk( req );
    if (!req->content_len) {
        SendConstChunk( req, "no data - nothing done" );
        SendFinalChunk( req );
        return ESP_OK;
    }
    enum STATUS
    {
        KEY, VALUE, HEX1, HEX2,
    };

    char key[3];
    char id[33];
    char pw[33];
    char x;
    char *val = 0;
    char *vp = key;
    char *end = &key[sizeof(key) - 1];

    id[0] = 0;
    pw[0] = 0;
    int status = KEY;

    char buf[64];
    char *bp = 0;
    int readlen = 0;
    int remaining = req->content_len;
    while (remaining) {
        if (!readlen) {
            if ((readlen = httpd_req_recv( req, buf,
                    min( remaining, sizeof(buf) ) )) <= 0) {
                if (readlen == HTTPD_SOCK_ERR_TIMEOUT) {
                    continue;  // Retry receiving if timeout occurred
                }
                return ESP_FAIL;
            }
            bp = buf;
            remaining -= readlen;
        }
        while (readlen) {
            // ESP_LOGI( TAG, "loop status=%d *bp=%c", status, *bp );

            switch (status) {
            case KEY:
                if (*bp == '=') {
                    *vp = 0;
                    if (!strcmp( key, "id" )) {
                        val = id;
                        end = val + sizeof(id) - 1;
                    } else if (!strcmp( key, "pw" )) {
                        val = pw;
                        end = val + sizeof(pw) - 1;
                    } else {
                        snprintf( buf, sizeof(buf), "unknown key \"%s\"", key );
                        buf[sizeof(buf)-1] = 0;
                        SendStringChunk( req, buf );
                        SendFinalChunk( req );
                        return ESP_OK;
                    }
                    status = VALUE;
                    vp = val;
                } else if (vp >= end) {
                    snprintf( buf, sizeof(buf), "key too long (\"%.*s%c...\")",
                            sizeof(key) - 1, key, *bp );
                    buf[sizeof(buf)-1] = 0;
                    SendStringChunk( req, buf );
                    SendFinalChunk( req );
                    return ESP_OK;
                } else {
                    *vp++ = *bp;
                }
                break;
            case VALUE:
                if (*bp == '&') {
                    *vp = 0;
                    status = KEY;
                    val = 0;
                    vp = key;
                    end = vp + sizeof(key) - 1;
                } else if (vp >= end) {
                    snprintf( buf, sizeof(buf),
                            "\"%s\" value too long (max %d)", key, end - val );
                    buf[sizeof(buf)-1] = 0;
                    SendStringChunk( req, buf );
                    SendFinalChunk( req );
                    return ESP_OK;
                } else if (*bp == '%') {
                    status = HEX1;
                } else {
                    *vp++ = *bp;
                }
                break;
            case HEX1:
            case HEX2:
                if (*bp <= '9') {
                    x = *bp - '0';
                } else {
                    x = (*bp + 10 - 'A') & 0xf;
                }
                switch (status) {
                case HEX1:
                    *vp = (x << 4);
                    status = HEX2;
                    break;
                case HEX2:
                    *vp |= x;
                    ++vp;
                    status = VALUE;
                    break;
                }
            }
            ++bp;
            --readlen;
        }
    }

    if (status != VALUE) {
        SendConstChunk( req, "unexpected end of data while parsing key" );
        SendFinalChunk( req );
        return ESP_OK;
    }
    *vp = 0;

    x = 3;
    if (id[0])
        x ^= 1;
    if (pw[0])
        x ^= 2;
    if (x) {
        snprintf( buf, sizeof(buf), "%s%s%s not set", x & 1 ? "SSID" : "",
                x == 3 ? " and " : "", x & 2 ? "Password" : "" );
        buf[sizeof(buf)-1] = 0;
        SendStringChunk( req, buf );
        SendFinalChunk( req );
        return ESP_OK;
    }

    if (strcmp( id, s_WebServer->wifi().GetSsid() ))
        x |= 1;
    if (strcmp( pw, s_WebServer->wifi().GetPassword() ))
        x |= 2;
    if (!x) {
        SendConstChunk( req, "data unchanged" );
        SendFinalChunk( req );
        return ESP_OK;
    }
#if 1
	// ESP_LOGI( TAG, "received ssid \"%s\", password \"%s\"", id, pw );
	if (! s_WebServer->wifi().SetParam( id, pw )) {
		SendConstChunk( req, "setting wifi parameter failed - try again" );
        SendFinalChunk( req );
		return ESP_OK;

	}
#endif
    SendConstChunk( req, "configuration has been set" );
    SendFinalChunk( req );
    return ESP_OK;
}

extern "C" esp_err_t handler_get_update( httpd_req_t * req )
{
    SendInitialChunk( req );
    SendConstChunk( req, "  <form method=\"post\">\n" // enctype=\"multipart/form-data\"
                         "   <table>\n"
                      // "    <tr><td>choose binary file:</td>\n"
                      // "     <td><input name=\"file\" type=\"file\" accept=\"application/octet-stream\"></td></tr>\n"
                         "    <tr><td /><td><button type=\"submit\">update</button></td></tr>\n"
                         "   </table>\n"
                         "  </form>\n"  );
    SendFinalChunk( req );
    return ESP_OK;
}

extern "C" esp_err_t handler_post_update( httpd_req_t * req )
{
    ESP_LOGD( TAG, "handler_post_update enter" ); EXPRD(vTaskDelay(1))

    SendInitialChunk( req );
                            // enctype=\"multipart/form-data\"
    SendConstChunk( req, "  <form method=\"post\">\n"
                         "   <table>\n"
                         "    <tr><td>url:</td>\n"
                         "     <td>" );
    SendStringChunk( req, Updator::Instance().Url() );
    SendConstChunk( req, "</td></tr>\n"
                         "    <tr><td /><td><button type=\"submit\">update</button></td></tr>\n"
                         "    <tr><td>progress:</td><td></td></tr>\n"
                         "   </table>\n"
                         "  </form>\n"  );
    SendFinalChunk( req );

    Updator::Instance().Go();
    return ESP_OK;
}

extern "C" esp_err_t handler_get_reboot( httpd_req_t * req )
{
    SendInitialChunk( req );
    static char s_fmt[] = "<form method=\"post\">"
            "<button type=\"submit\">reboot</button>"
            "</form>";
    SendCharsChunk( req, s_fmt );
    SendFinalChunk( req );
    return ESP_OK;
}

extern "C" esp_err_t handler_post_reboot( httpd_req_t * req )
{
    SendInitialChunk( req, true, "<meta http-equiv=\"refresh\" content=\"7; URL=/\">" );
    SendConstChunk( req, "This device will reboot - you will get redirected soon" );
    SendFinalChunk( req );
    vTaskDelay( configTICK_RATE_HZ / 10 );

    esp_restart();

    // no return - on error:
    ESP_LOGE( TAG, "esp_restart returned" );
    return ESP_OK;
}


//@formatter:off
const httpd_uri_t uri_main =        { .uri = "/",
                                      .method = HTTP_GET,
                                      .handler = handler_get_main,
                                      .user_ctx = 0 };
const httpd_uri_t uri_favicon =     { .uri = "/favicon.ico",
                                      .method = HTTP_GET,
                                      .handler = handler_get_favicon,
                                      .user_ctx = 0 };
const httpd_uri_t uri_get_wifi =    { .uri = "/wifi",
                                      .method = HTTP_GET,
                                      .handler = handler_get_wifi,
                                      .user_ctx = 0 };
const httpd_uri_t uri_post_wifi =   { .uri = "/wifi",
                                      .method = HTTP_POST,
                                      .handler = handler_post_wifi,
                                      .user_ctx = 0 };
const httpd_uri_t uri_get_update =  { .uri = "/update",
                                      .method = HTTP_GET,
                                      .handler = handler_get_update,
                                      .user_ctx = 0 };
const httpd_uri_t uri_post_update = { .uri = "/update",
                                      .method = HTTP_POST,
                                      .handler = handler_post_update,
                                      .user_ctx = 0 };
const httpd_uri_t uri_get_reboot =  { .uri = "/reboot",
                                      .method = HTTP_GET,
                                      .handler = handler_get_reboot,
                                      .user_ctx = 0 };
const httpd_uri_t uri_post_reboot = { .uri = "/reboot",
                                      .method = HTTP_POST,
                                      .handler = handler_post_reboot,
                                      .user_ctx = 0 };

const WebServer::Page page_wifi    { uri_get_wifi,   "WLAN parameter" };
const WebServer::Page page_update  { uri_get_update, "Firmware update" };
const WebServer::Page page_reboot  { uri_get_reboot, "Restart device" };

//@formatter:on

extern "C" httpd_handle_t start_webserver( void )
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

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

WebServer::WebServer(Wifi & wifi) : mWifi {wifi}
{
    s_WebServer = this;
    server = start_webserver();
}
/*
WebServer& WebServer::Instance()
{
    static WebServer webserver { };
    return webserver;
}
*/
void WebServer::AddPage( const WebServer::Page & page,
        const httpd_uri_t * postUri )
{
    PageList *elem = new PageList { page };

    if (LastElem) {
        LastElem->Next = elem;
    } else {
        Anchor = elem;
    }
    LastElem = elem;

    httpd_register_uri_handler( server, &page.Uri );
    if (postUri)
        httpd_register_uri_handler( server, postUri );
}

void WebServer::MainPage( httpd_req_t * req )
{
    SendInitialChunk( req, false );

    const esp_app_desc_t *const desc = esp_ota_get_app_description();
    SendConstChunk( req, "  <h1>" );
    SendStringChunk( req, desc->project_name );
    SendConstChunk( req, "</h1>\n"
                         "  <table border=0>\n"
                         "   <tr><td>Project version:</td>" "<td>" );
    SendStringChunk( req, desc->version );
    SendConstChunk( req, "</td></tr>\n"
                         "   <tr><td>IDF version:</td>"     "<td>" );
    SendStringChunk( req, desc->idf_ver );
    SendConstChunk( req, "</td></tr>\n"
                         "  </table>\n" );

    SendConstChunk( req, "  <br />\n" );

    for (PageList *elem = Anchor; elem; elem = elem->Next) {
        SendConstChunk(  req, "  <br />\n" );
        SendConstChunk(  req, "  <a href=\"" );
        SendStringChunk( req, elem->Page.Uri.uri );
        SendConstChunk(  req, "\">" );
        SendStringChunk( req, elem->Page.LinkText );
        SendConstChunk(  req, "</a>\n" );
    }
    SendFinalChunk( req, false );
}

void WebServer::Init()
{
    ESP_LOGI( TAG, "Registering URI handlers" );
    httpd_register_uri_handler( server, &uri_main );
    httpd_register_uri_handler( server, &uri_favicon );

    ESP_LOGD( TAG, "AddPage wifi" );
    AddPage( page_wifi, &uri_post_wifi );

    ESP_LOGD( TAG, "AddPage update" );
    AddPage( page_update, &uri_post_update );

    AddPage( page_reboot, &uri_post_reboot );
}
