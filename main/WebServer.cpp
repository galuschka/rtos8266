/*
 * WebServer.h
 *
 *  Created on: 29.04.2020
 *      Author: galuschka
 */

//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "WebServer.h"

#include <string.h>     // memmove()

#include "Wifi.h"
#include "Updator.h"

#include "esp_log.h"   			// ESP_LOGI()
#include "esp_event_base.h"   	// esp_event_base_t
#include "esp_ota_ops.h"        // esp_ota_get_app_description()
#include "../favicon.i"         // favicon_ico

#if (LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG)
#define EXPRD(expr) do { expr; } while(0);
#else
#define EXPRD(expr)
#endif

#define min(a,b) ((a) < (b) ? a : b)

#define SendCharsChunk( req, chararray )  httpd_resp_send_chunk( req, chararray, sizeof(chararray) - 1 )

#define SendConstChunk( req, text ) do { static const char s_text[] = text; \
                                         SendCharsChunk( req, s_text ); \
                                    } while (0)

namespace
{
const char * const TAG          = "WebServer";
const char * const s_subWifi    = "WLAN parameter";
const char * const s_subUpdate  = "Firmware update";
const char * const s_subReboot  = "Restart device";
WebServer          s_WebServer{};


void SendStringChunk( httpd_req_t * req, const char * string )
{
    httpd_resp_send_chunk( req, string, strlen( string ) );
}

void SendInitialChunk( httpd_req_t * req, const char * subheader = 0, const char * meta = 0 )
{
    SendConstChunk( req, "<!DOCTYPE html>\n"
                         "<html>\n"
                         " <head><meta charset=\"utf-8\"/>" );
    if (meta) {
        SendStringChunk( req, meta );
    }
    SendConstChunk( req, "\n </head>\n"
                           " <body>\n" );
    const esp_app_desc_t *const desc = esp_ota_get_app_description();
    if (subheader) {
        SendConstChunk( req, "  <h1><a href=\"/\">" );
        SendStringChunk( req, desc->project_name );
        SendConstChunk( req, "</a></h1>\n  <h2>" );
        SendStringChunk( req, subheader );
        SendConstChunk( req, "  </h2>\n" );
    } else {
        SendConstChunk( req, "  <h1>" );
        SendStringChunk( req, desc->project_name );
        SendConstChunk( req, "</h1>\n" );
    }
}

class Parser
{
  public:
    struct Input {
        const char * key;  // field name
        char       * buf;  // buffer
        uint8_t      len;  // input to parse: buf size / output: length
        Input( const char * akey, char * abuf, uint8_t size ) : key{akey}, buf{abuf}, len{size} {};
    };
    Input * mInArray;
    uint8_t mNofFields;
    Parser( Input * inArray, uint8_t nofFields ) : mInArray{inArray}, mNofFields{nofFields} {};
    bool parse( httpd_req_t * req );
};

void SendFinalChunk( httpd_req_t * req, bool link2parent = true )
{
    /*
    if (link2parent) {
        SendConstChunk( req, "<br /><br /><a href=\"/\">Home</a>\n" );
    }
    */
    SendConstChunk( req, " </body>\n" "</html>\n" );
    httpd_resp_send_chunk( req, 0, 0 );
}
} // namespace

extern "C" esp_err_t handler_get_main( httpd_req_t * req )
{
    s_WebServer.MainPage( req );
    return ESP_OK;
}

extern "C" esp_err_t handler_get_favicon( httpd_req_t * req )
{
    httpd_resp_set_type( req, "image/x-icon" );
    httpd_resp_send( req, favicon_ico, sizeof(favicon_ico) );
    return ESP_OK;
}

bool Parser::parse( httpd_req_t * req )
{
    uint32_t fieldsParsed = 0;

    char buf[64];
    char * readend = buf;
    char * const bufend = &buf[sizeof(buf) - 1];
    int remaining = req->content_len;

    while (remaining || (readend != buf)) {
        int rest = bufend - readend;
        if (remaining && rest) {
            int readlen = httpd_req_recv( req, readend,
                                            min( remaining, rest ) );
            if (readlen <= 0) {
                if (readlen == HTTPD_SOCK_ERR_TIMEOUT) {
                    continue;  // Retry receiving if timeout occurred
                }
                ESP_LOGE( TAG, "httpd_req_recv failed with %d", readlen );
                return false;
            }
            remaining -= readlen;
            readend += readlen;
            *readend = 0;
        }
        char * const questmark = strchr( buf + 2, '?' );
        if (questmark) {
            *questmark = 0;
        }
        char * const equalsign = strchr( buf, '=' );
        if (! equalsign) {
            ESP_LOGE( TAG, "'=' missing in query string" );
            return false;
        }
        *equalsign = 0;
        for (uint8_t i = 0; i < mNofFields; ++i) {
            Input * const in = & mInArray[i];
            if (strcmp( buf, in->key ) || (fieldsParsed & (1 << i)))
                continue;
            // key match:
            fieldsParsed |= 1 << i;
            char * bp = in->buf;
            for (char * val = equalsign + 1; *val; ++val) {
                if ((*val == '%') && val[1] && val[2]) {
                    *bp = (((val[1] + ((val[1] >> 6) & 1) * 9) & 0xf) << 4)
                         | ((val[2] + ((val[2] >> 6) & 1) * 9) & 0xf);
                    val += 2;
                } else
                    *bp = *val;
                ++bp;
                if (bp >= & in->buf[in->len - 1])
                    break;
            }
            *bp = 0;
            in->len = bp - in->buf;
        }
        if (questmark) {
            uint8_t move = readend - questmark;  // incl. \0
            memmove( buf, questmark + 1, move );
            readend = &buf[move - 1];
        } else
            readend = buf;
    } // while remaining || readend != buf

    for (uint8_t i = 0; i < mNofFields; ++i)
        if (! (fieldsParsed & (1 << i))) {
            mInArray[i].len = 0;
            mInArray[i].buf[0] = 0;
        }

    return true;
}


extern "C" esp_err_t handler_get_wifi( httpd_req_t * req )
{
    SendInitialChunk( req, s_subWifi );
    SendConstChunk( req, "<form method=\"post\">"
                             "<table border=0>"
                              "<tr>"
                               "<td>SSID:</td>"
                               "<td><input type=\"text\" name=\"id\" value=\"" );
    const char * const id = Wifi::Instance().GetSsid();
    if (id && *id) {
        SendStringChunk( req, id );
    }
    SendConstChunk( req, "\" maxlength=15></td>"
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
    SendInitialChunk( req, s_subWifi );
    if (!req->content_len) {
        SendConstChunk( req, "no data - nothing to be done" );
        SendFinalChunk( req );
        return ESP_OK;
    }

    char id[16];
    char pw[32];
    Parser::Input in[] = { {"id",id,sizeof(id)}, {"pw",pw,sizeof(pw)} };
    Parser parser{ in, sizeof(in) / sizeof(in[0]) };

    if (! parser.parse( req )) {
        SendConstChunk( req, "unexpected end of data while parsing data" );
        SendFinalChunk( req );
        return ESP_OK;
    }

    if ((! in[0].len) || (! in[1].len)) {
        if (! in[0].len) SendConstChunk( req, "SSID not set" );
        if ((! in[0].len) && (! in[1].len)) SendConstChunk( req, " and " );
        if (! in[1].len) SendConstChunk( req, "Password not set" );
        SendFinalChunk( req );
        return ESP_OK;
    }

    uint8_t diff;
    if (strcmp( id, Wifi::Instance().GetSsid() ))
        diff |= 1;
    if (strcmp( pw, Wifi::Instance().GetPassword() ))
        diff |= 2;
    if (!diff) {
        SendConstChunk( req, "data unchanged" );
        SendFinalChunk( req );
        return ESP_OK;
    }

	// ESP_LOGI( TAG, "received ssid \"%s\", password \"%s\"", id, pw );
	if (! Wifi::Instance().SetParam( id, pw )) {
		SendConstChunk( req, "setting wifi parameter failed - try again" );
        SendFinalChunk( req );
		return ESP_OK;
	}

    SendConstChunk( req, "configuration has been set" );
    SendFinalChunk( req );
    return ESP_OK;
}

extern "C" esp_err_t handler_get_update( httpd_req_t * req )
{
    Updator &updator = Updator::Instance();
    uint8_t progress = updator.Progress();
    if ((progress == 0) || (progress == 95) || (progress >= 99)) {
        SendInitialChunk( req, s_subUpdate );
        SendConstChunk( req, "  <form method=\"post\">\n" // enctype=\"multipart/form-data\"
                             "   <table>\n"
                             "    <tr><td>uri:</td>\n"
                             "     <td>" );
        if (progress != 95)
            SendConstChunk( req, "<input type=\"text\" name=\"uri\" value=\"" );
        const char * const uri = updator.GetUri();
        if (uri && *uri)
            SendStringChunk( req, uri );
        if (progress != 95)
            SendConstChunk( req, "\" maxlength=79 alt=\"setup a web server for download to the device\">" );
        SendConstChunk( req, "</td></tr>\n"
                             "    <tr><td>" );
        const char * text = 0;
        switch (progress)
        {
            case  95: text = "test booting"; break;
            case  99: text = "update failed"; break;
            case 100: text = "update succeeded"; break;
        }
        if (text && *text)
            SendStringChunk( req, text );
        SendConstChunk( req, "</td><td><button type=\"submit\">" );
        text = 0;
        switch (progress)
        {
            case   0: text = "start update"; break;
            case  95: text = "confirm well booting"; break;
            case  99: text = "restart update"; break;
            case 100: text = "again update"; break;
        }
        if (text && *text)
            SendStringChunk( req, text );
        SendConstChunk( req, "</button></td></tr>\n"
                             "   </table>\n"
                             "  </form>\n" );
        SendFinalChunk( req );
        return ESP_OK;
    }

    SendInitialChunk( req, s_subUpdate, "<meta http-equiv=\"refresh\" content=\"1; URL=/update\">" );
    SendConstChunk( req, "  <table>\n"
                         "   <tr><td>uri:</td><td>" );
    SendStringChunk( req, updator.GetUri() );
    SendConstChunk( req, "</td></tr>\n"
                         "   <tr><td /><td>update in progress</td></tr>\n"
                         "    <tr><td>progress:</td><td>" );
    char buf[8];
    sprintf( buf, "%d", progress );
    SendStringChunk( req, buf );
    SendConstChunk( req, "&nbsp;&percnt;</td></tr>\n"
                         "   <tr><td>message:</td><td>" );
    SendStringChunk( req, updator.GetMsg() );
    SendConstChunk( req, "</td></tr>\n"
                         "   </table>\n" );
    SendFinalChunk( req );
    return ESP_OK;
}

extern "C" esp_err_t handler_post_update( httpd_req_t * req )
{
    ESP_LOGD( TAG, "handler_post_update enter" ); EXPRD(vTaskDelay(1))

    Updator &updator = Updator::Instance();
    uint8_t progress = updator.Progress();
    if ((progress > 0) && (progress < 99)) {
        if (progress == 95)
            updator.Confirm();
        SendInitialChunk( req, s_subUpdate, "<meta http-equiv=\"refresh\" content=\"1; URL=/update\">" );
        SendFinalChunk( req );
        return ESP_OK;
    }

    char uri[80];
    Parser::Input in[] = { {"uri",uri,sizeof(uri)} };
    Parser parser{ in, sizeof(in) / sizeof(in[0]) };

    if (! parser.parse( req )) {
        SendInitialChunk( req, s_subUpdate );
        SendConstChunk( req, "unexpected end of data while parsing data" );
        SendFinalChunk( req );
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
        SendInitialChunk( req, s_subUpdate );
        SendConstChunk( req, "  <form method=\"post\">\n" // enctype=\"multipart/form-data\"
                             "   <table>\n"
                             "    <tr><td>uri:</td>\n"
                             "     <td><input type=\"text\" name=\"uri\" value=\"" );
        SendStringChunk( req, uri );
        SendConstChunk( req, "\" maxlength=79 alt=\"setup a web server for download to the device\"></td></tr>\n"
                             "    <tr><td /><td><button type=\"submit\">try again</button></td></tr>\n"
                             "    <tr><td>message:</td><td>" );
        SendStringChunk( req, err );
        SendConstChunk( req, "</td></tr>\n"
                             "   </table>\n" );
        SendFinalChunk( req );
        return ESP_OK;
    }

    SendInitialChunk( req, s_subUpdate, "<meta http-equiv=\"refresh\" content=\"1; URL=/update\">" );
    SendConstChunk( req, "   <table>\n"
                         "    <tr><td>uri:</td>\n"
                         "     <td>" );
    SendStringChunk( req, updator.GetUri() );
    SendConstChunk( req, "</td></tr>\n"
                         "    <tr><td /><td>update started</td></tr>\n"
                         "    <tr><td>progress:</td><td>1&nbsp;&percnt;</td></tr>\n"
                         "   </table>\n" );
    SendFinalChunk( req );
    updator.Go();
    return ESP_OK;
}

extern "C" esp_err_t handler_get_reboot( httpd_req_t * req )
{
    SendInitialChunk( req, s_subReboot );
    static char s_fmt[] = "<form method=\"post\">"
            "<button type=\"submit\">reboot</button>"
            "</form>";
    SendCharsChunk( req, s_fmt );
    SendFinalChunk( req );
    return ESP_OK;
}

extern "C" esp_err_t handler_post_reboot( httpd_req_t * req )
{
    SendInitialChunk( req, s_subReboot, "<meta http-equiv=\"refresh\" content=\"7; URL=/\">" );
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

const WebServer::Page page_wifi    { uri_get_wifi,   s_subWifi };
const WebServer::Page page_update  { uri_get_update, s_subUpdate };
const WebServer::Page page_reboot  { uri_get_reboot, s_subReboot };

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
    SendInitialChunk( req );

    const esp_app_desc_t *const desc = esp_ota_get_app_description();
    SendConstChunk( req, "  <table border=0>\n"
                         "   <tr><td>Project version:</td>" "<td>" );
    SendStringChunk( req, desc->version );
    SendConstChunk( req, "</td></tr>\n"
                         "   <tr><td>IDF version:</td>"     "<td>" );
    SendStringChunk( req, desc->idf_ver );
    SendConstChunk( req, "</td></tr>\n"
                         "  </table>\n" );

    SendConstChunk( req, "  <br />\n" );

    for (PageList *elem = mAnchor; elem; elem = elem->Next) {
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
    ESP_LOGI( TAG, "Start web server" );
    mServer = start_webserver();

    ESP_LOGI( TAG, "Registering URI handlers" );
    httpd_register_uri_handler( mServer, &uri_main );
    httpd_register_uri_handler( mServer, &uri_favicon );

    ESP_LOGD( TAG, "AddPage wifi" );
    AddPage( page_wifi, &uri_post_wifi );

    ESP_LOGD( TAG, "AddPage update" );
    AddPage( page_update, &uri_post_update );

    AddPage( page_reboot, &uri_post_reboot );
}
