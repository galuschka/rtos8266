/*
 * WebServer.h
 *
 *  Created on: 29.04.2020
 *      Author: galuschka
 */

#include "WebServer.h"

#include "Wifi.h"

#include "esp_log.h"   			// ESP_LOGI()
#include "esp_event_base.h"   	// esp_event_base_t
#include "esp_ota_ops.h"   		// esp_ota_begin(), ...

static const char *TAG = "WebServer";

#define min(a,b) ((a) < (b) ? a : b)

#define SendCharsChunk( req, chararray )  httpd_resp_send_chunk( req, chararray, sizeof(chararray) - 1 )

namespace
{
void SendStringChunk( httpd_req_t * req, const char * string )
{
    httpd_resp_send_chunk( req, string, strlen( string ) );
}
}

extern "C" esp_err_t handler_get_main( httpd_req_t * req )
{
    WebServer::Instance().MainPage( req );
    return ESP_OK;
}

extern "C" esp_err_t handler_get_wifi( httpd_req_t * req )
{
//@formatter:off

    static char s_data1[] = "<form method=\"post\">"
                             "<table border=0>"
                              "<tr>"
                               "<td>SSID:</td>"
                               "<td><input type=\"text\" name=\"id\" value=\"";
    static char s_data3[] =                "\" maxlength=31></td>"
                              "</tr>"
                              "<tr>"
                               "<td>Password:</td>"
                               "<td><input type=\"password\" name=\"pw\" maxlength=31></td>"
                              "</tr>"
                              "<tr>"
                               "<td />"
                               "<td><button type=\"submit\">set</button></td>"
                              "</tr>"
                             "</table>"
                            "</form>";
//@formatter:on

    SendCharsChunk( req, s_data1 );
    SendStringChunk( req, Wifi::Instance().GetSsid() );
    SendCharsChunk( req, s_data3 );
    httpd_resp_send_chunk( req, 0, 0 );
    return ESP_OK;
}

extern "C" esp_err_t handler_post_wifi( httpd_req_t * req )
{
    if (!req->content_len) {
        const char s_msg[] = "no data - nothing done";
        SendCharsChunk( req, s_msg );
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
                        const int strlenbuf = strlen( buf );
                        httpd_resp_send( req, buf,
                                min( sizeof(buf), strlenbuf ) );
                        return ESP_OK;
                    }
                    status = VALUE;
                    vp = val;
                } else if (vp >= end) {
                    snprintf( buf, sizeof(buf), "key too long (\"%.*s%c...\")",
                            sizeof(key) - 1, key, *bp );
                    const int strlenbuf = strlen( buf );
                    httpd_resp_send( req, buf, min( sizeof(buf), strlenbuf ) );
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
                    const char s_msg[] = "value too long";
                    SendCharsChunk( req, s_msg );
                    snprintf( buf, sizeof(buf),
                            "\"%s\" value too long (max %d)", key, end - val );
                    const int strlenbuf = strlen( buf );
                    httpd_resp_send( req, buf, min( sizeof(buf), strlenbuf ) );
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
        const char s_msg[] = "unexpected end of data while parsing key";
        SendCharsChunk( req, s_msg );
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
        const int strlenbuf = strlen( buf );
        httpd_resp_send( req, buf, min( sizeof(buf), strlenbuf ) );
        return ESP_OK;
    }

    Wifi &wifi = Wifi::Instance();

    if (strcmp( id, wifi.GetSsid() ))
        x |= 1;
    if (strcmp( pw, wifi.GetPassword() ))
        x |= 2;
    if (!x) {
        const char s_msg[] = "data unchanged";
        SendCharsChunk( req, s_msg );
        return ESP_OK;
    }
#if 0
	// ESP_LOGI( TAG, "received ssid \"%s\", password \"%s\"", id, pw );
	if (! wifi.SetParam( id, pw )) {
		const char s_err[] = "setting wifi parameter failed - try again";
		SendCharsChunk( req, s_err );
		return ESP_OK;

	}
#endif
    const char s_ok[] = "configuration would have been set";
    SendCharsChunk( req, s_ok );
    return ESP_OK;
}

static char s_update[] =
        "<form method=\"post\" enctype=\"multipart/form-data\">"
                "<label>choose binary file: "
                "<input name=\"file\" type=\"file\" accept=\"application/octet-stream\">"
                "</label>"
                "<br />"
                "<button>update</button>"
                "</form>";

extern "C" esp_err_t handler_get_update( httpd_req_t * req )
{
    SendCharsChunk( req, s_update );
    return ESP_OK;
}

extern "C" esp_err_t handler_post_update( httpd_req_t * req )
{
    char buf[1024];

    if (req->content_len <= (2 * sizeof(buf))) {
        const char s_err[] = "<br /><br />too less data -> please retry";
        SendCharsChunk( req, s_update );
        SendCharsChunk( req, s_err );
        httpd_resp_send_chunk( req, 0, 0 );
        return ESP_OK;
    }

    const esp_partition_t *partition = 0;
    esp_ota_handle_t ota = 0;

    unsigned char nofdashes = 0;			// boundary starts with "-------..."
    unsigned char boundarylen = 0;			// boundary digit length
    char boundary[30]; 			// magic number (decimal up to 28 digits)
    int const lastlen = sizeof(buf) / 2; // last read to search trailing boundary
    int remaining = req->content_len - lastlen; // trailing boundary NOT read inside the loop
    int readlen;

    while (remaining) {
        if ((readlen = httpd_req_recv( req, buf, min( remaining, sizeof(buf) ) ))
                <= 0) {
            if (readlen == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;  // Retry receiving if timeout occurred
            }
            if (!ota) {
                const char s_err[] = "1st read failed";
                SendCharsChunk( req, s_err );
            } else {
                esp_ota_end( ota );
                const char s_err[] = "sub sequential read failed";
                SendCharsChunk( req, s_err );
            }
            return ESP_OK;
        }
        if (!ota) {
            char *cp = buf;
            for (nofdashes = 0; *cp == '-'; ++cp)
                ++nofdashes;
            if (!nofdashes) {
                const char s_err[] = "boundary dashes (---) missing";
                SendCharsChunk( req, s_err );
                return ESP_OK;
            }
            char *bp = boundary;
            while ((*cp >= '0') && (*cp <= '9')
                    && (bp < &boundary[sizeof(boundary)]))
                *bp++ = *cp++;
            boundarylen = bp - &boundary[0];
            if (!boundarylen) {
                const char s_err[] = "boundary digit missing";
                SendCharsChunk( req, s_err );
                return ESP_OK;
            }

            char *start = strchr( cp, 0xe9 );
            if ((!start) || (start >= &buf[readlen]) || (start[-1] != '\n')) {
                const char s_err[] = "0xe9 on new line missing";
                SendCharsChunk( req, s_err );
                return ESP_OK;
            }

            partition = esp_ota_get_next_update_partition( NULL );
            const esp_err_t err = esp_ota_begin( partition, OTA_SIZE_UNKNOWN,
                    &ota );
            if (err != ESP_OK) {
                const char s_err[] = "OTA initialization failed";
                SendCharsChunk( req, s_err );
                return ESP_OK;
            }
            const esp_err_t werr = esp_ota_write( ota, start,
                    &buf[readlen] - start );
            if (werr != ESP_OK) {
                esp_ota_end( ota );
#if 1
                for (int off = 0; off < (readlen - 0x10); off += 0x10) {
                    char dump[80];
                    char *cp = dump;
                    cp += sprintf( cp, "%02x: ", off );
                    for (int i = 0; i < 16; ++i) {
                        if ((buf[off + i] >= ' ') && (buf[off + i] < 0x7f)) {
                            *cp++ = buf[off + i];
                            if (buf[off + i] == '\\')
                                *cp++ = '\\';
                        } else {
                            *cp++ = '\\';
                            switch (buf[off + i]) {
                            case '\r':
                                *cp++ = 'r';
                                break;
                            case '\n':
                                *cp++ = 'n';
                                break;
                            default:
                                *cp++ = 'x';
#define hex(x) (((x) < 0x10 ? '0' + (x) : 'a' + (x) - 0x10))
                                *cp++ = hex( (buf[off + i] >> 4) & 0xf );
                                *cp++ = hex( buf[off + i] & 0xf );
                                break;
                            }
                        }
                    }
                    *cp = 0;
                    ESP_LOGE( TAG, dump );
                }
#endif
                const char s_err[] = "1st OTA write failed";
                SendCharsChunk( req, s_err );
                return ESP_OK;
            }
        } else {
            const esp_err_t werr = esp_ota_write( ota, buf, readlen );
            if (werr != ESP_OK) {
                esp_ota_end( ota );
                const char s_err[] = "sub sequential OTA write failed";
                SendCharsChunk( req, s_err );
                return ESP_OK;
            }
        }
        remaining -= readlen;
    }
    if (!ota) {
        const char s_err[] = "no data received";
        SendCharsChunk( req, s_err );
        return ESP_OK;
    }

    remaining = lastlen;
    char *end = buf;
    while (remaining) {
        if ((readlen = httpd_req_recv( req, end, remaining )) <= 0) {
            if (readlen == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;  // Retry receiving if timeout occurred
            }
            esp_ota_end( ota );
            const char s_err[] = "last read failed";
            SendCharsChunk( req, s_err );
            return ESP_OK;
        }
        remaining -= readlen;
        end += readlen;
    }
    for (char *bp = buf; bp < end; ++bp) {
        if (*bp != '-')
            continue;
        int thisnofdashes = 1;
        char *thisboundary = bp;
        while (*++thisboundary == '-')
            ++thisnofdashes;
        if (thisnofdashes < nofdashes) {
            // ESP_LOGI( TAG, "%d dash(es) skipped (searching %d dashes) \"%.*s\"", thisnofdashes, nofdashes, end - bp, bp );
            bp = thisboundary;
            continue;
        }
        bp = thisboundary - nofdashes;  // in case last octet(s) are '-' too
        if (strncmp( thisboundary, boundary, boundarylen )) {
            // ESP_LOGI( TAG, "boundary skipped (not matching \"%.*s\") \"%.*s\"", boundarylen, boundary, end - bp, bp );
            continue;
        }

        // we found the trailing boundary at bp
        // cut [\r[\n]] before bp, if existing:
        if ((bp > &buf[0]) && (bp[-1] == '\n')) {
            --bp;
            if ((bp > &buf[0]) && (bp[-1] == '\r'))
                --bp;
        }
        ESP_LOGI( TAG, "trailing boundary found: %.*s", end - bp, bp );
#if 0
		char * ap = bp - 1;
		while ((ap >= &buf[0]) &&
				(((*ap >= 0x20) && (*ap < 0x7f)) || (*ap == '\r') || (*ap == '\n')))
			--ap;
		++ap;
		if (ap != bp) {
			ESP_LOGI( TAG, "ascii data before boundary: %.*s", bp - ap, ap );
		}
#endif
        end = bp;
        break;
    }
    // either end is located at boundary or there is no boundary...
    const esp_err_t werr = esp_ota_write( ota, buf, end - &buf[0] );
    if (werr != ESP_OK) {
        esp_ota_end( ota );
        const char s_err[] = "last OTA write failed";
        SendCharsChunk( req, s_err );
        return ESP_OK;
    }

    esp_ota_end( ota );
    esp_ota_set_boot_partition( partition );

    const char s_data1[] = "update to partition ";
    const char s_data3[] = " succeeded -> please reboot"
            "<br />"
            "<form method=\"post\" action=\"/reboot\">"
            "<button type=\"submit\">reboot</button>"
            "</form>";
    SendCharsChunk( req, s_data1 );
    SendStringChunk( req, partition->label );
    SendCharsChunk( req, s_data3 );
    httpd_resp_send_chunk( req, 0, 0 );
    return ESP_OK;
}

extern "C" esp_err_t handler_get_reboot( httpd_req_t * req )
{
    static char s_fmt[] = "<form method=\"post\">"
            "<button type=\"submit\">reboot</button>"
            "</form>";
    SendCharsChunk( req, s_fmt );
    return ESP_OK;
}

extern "C" esp_err_t handler_post_reboot( httpd_req_t * req )
{
    const char s_info[] = "<head>"
            "<meta http-equiv=\"refresh\" content=\"7; URL=/\">"
            "</head>"
            "</body>"
            "This device will reboot - you will get redirected soon"
            "</body>";

    SendCharsChunk( req, s_info );
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

WebServer::WebServer()
{
    server = start_webserver();
}

WebServer& WebServer::Instance()
{
    static WebServer webserver { };
    return webserver;
}

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
//@formatter:off
    static char s_welcome[] = "<h1>%s</h1>"
                               "<table border=0>"
                                "<tr><td>Project version:</td>" "<td>%s</td></tr>"
                                "<tr><td>IDF version:</td>"     "<td>%s</td></tr>"
                               "</table>";
    static char s_br[]     = "<br />";
//@formatter:on

    {
        const esp_app_desc_t *const desc = esp_ota_get_app_description();
        char *welcome = NULL;
        int len = asprintf( &welcome, s_welcome, desc->project_name,
                desc->version, desc->idf_ver );
        httpd_resp_send_chunk( req, welcome, len );
        free( welcome );
    }
    SendCharsChunk( req, s_br );

    for (PageList *elem = Anchor; elem; elem = elem->Next) {
        SendCharsChunk( req, s_br );
        SendStringChunk( req, "<a href=\"" );
        SendStringChunk( req, elem->Page.Uri.uri );
        SendStringChunk( req, "\">" );
        SendStringChunk( req, elem->Page.LinkText );
        SendStringChunk( req, "</a>" );
    }

    httpd_resp_send_chunk( req, NULL, 0 );
}

void WebServer::Init()
{
    ESP_LOGI( TAG, "Registering URI handlers" );

    httpd_register_uri_handler( server, &uri_main );

    AddPage( page_wifi, &uri_post_wifi );

    if (1 || Wifi::Instance().StationMode()) {
        AddPage( page_update, &uri_post_update );
    }
    // AddPage( page_reboot, &uri_post_reboot );
}
