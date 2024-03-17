/*
 * WebServer.h
 */
//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "WebServer.h"

#include "Wifi.h"
#include "Indicator.h"
#include "Mqtinator.h"
#include "Updator.h"
#include "BootCnt.h"
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

extern "C" {

extern uint64_t g_esp_os_cpu_clk;

httpd_handle_t  start_webserver( void );
void            stop_webserver( httpd_handle_t server );
void            disconnect_handler( void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data );
void            connect_handler(    void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data );

esp_err_t       handler_get_main(      httpd_req_t * req );
esp_err_t       handler_get_favicon(   httpd_req_t * req );
esp_err_t       handler_get_readflash( httpd_req_t * req );

}

namespace {

const char * const TAG            = "WebServer";
WebServer          s_WebServer{};

const httpd_uri_t uri_main        = { .uri = "/",            .method = HTTP_GET, .handler = handler_get_main,      .user_ctx = 0 };
const httpd_uri_t uri_readflash   = { .uri = "/readflash",   .method = HTTP_GET, .handler = handler_get_readflash, .user_ctx = 0 };
const httpd_uri_t uri_get_favicon = { .uri = "/favicon.ico", .method = HTTP_GET, .handler = handler_get_favicon,   .user_ctx = 0 };
const WebServer::Page page_home     { uri_main, "Home" };

} // namespace

WebServer& WebServer::Instance()
{
    return s_WebServer;
}

void WebServer::AddPage( const WebServer::Page & page, const httpd_uri_t * postUri )
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

void WebServer::AddUri( const httpd_uri_t & uri )
{
    httpd_register_uri_handler( mServer, & uri );
}

void WebServer::Init()
{
    ESP_LOGI( TAG, "Start web server" );
    mServer = start_webserver();
    if (! mServer)
        return;

    ESP_LOGD( TAG, "Registering URI handlers" ); EXPRD( vTaskDelay(1) )

    httpd_register_uri_handler( mServer, &uri_readflash );  // debug interface to read flash data

    AddPage( page_home, &uri_main );
}

void WebServer::InitPages()
{
    ESP_LOGD( TAG, "Add sub pages" ); EXPRD( vTaskDelay(1) )

    Wifi::Instance()     .AddPage( *this );
    Mqtinator::Instance().AddPage( *this );
    Updator::Instance()  .AddPage( *this );

    httpd_register_uri_handler( mServer, &uri_get_favicon );
}

void WebServer::MainPage( httpd_req_t * req )
{
    HttpHelper hh{ req, 0, "Home" };
    Table<8,3> table;
    Indicator & indicator = Indicator::Instance();

    const esp_app_desc_t *const desc = esp_ota_get_app_description();
    table[0][1] = "&nbsp;";
    table[0][0] = "Project name:";     table[0][2] = desc->project_name;
    table[1][0] = "Project version:";  table[1][2] = desc->version;
    table[2][0] = "IDF version:";      table[2][2] = desc->idf_ver;
    table[3][0] = "RTOS version:";     table[3][2] = tskKERNEL_VERSION_NUMBER;
    table[4][0] = "signaling mask:";
    if (indicator.NofLEDs() > 1)
        table[5][0] = "sec. sig. mask:";
    table[6][0] = "boot counter:";     table[6][2] = HttpHelper::String( (uint32_t) BootCnt::Instance().Cnt() );
    table[7][0] = "uptime:";

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
        table[7][2] = up;
    }

    hh.Add( "  <table>\n" );
    table.AddTo( hh );
    hh.Add( "  </table>\n" );
}

/////////////////// extern "C" ///////////////////

extern "C" {

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

#if 0  // not needed ... - or needed, when Wifi::Init() won't block

void connect_handler( void * arg, esp_event_base_t event_base,
        int32_t event_id, void * event_data )
{
    httpd_handle_t *server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI( TAG, "Starting web server" );
        *server = start_webserver();
    }
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

#endif


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

        const char * parseError = parser.ParseUriParam( req );
        if (parseError) {
            HttpHelper hh{ req, "read flash" };
            hh.Add( "parser error: " );
            hh.Add( parseError );
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

} // extern "C"
