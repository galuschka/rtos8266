/*
 * Monitor.cpp
 *
 *  Created on: 06.05.2020
 *      Author: holger
 */

#include "Monitor.h"
#include "WebServer.h"

#define SendCharsChunk( req, chararray )  httpd_resp_send_chunk( req, chararray, sizeof(chararray) - 1 )

namespace
{
void SendStringChunk( httpd_req_t * req, const char * string )
{
    httpd_resp_send_chunk( req, string, strlen( string ) );
}
}

extern "C" esp_err_t handler_get( httpd_req_t * req );

//@formatter:off
const httpd_uri_t s_uri =       { .uri = "/monitor",
                                  .method = HTTP_GET,
                                  .handler = handler_get,
                                  .user_ctx = 0 };
const WebServer::Page s_page    { s_uri, "Monitor analog pin values" };
//@formatter:on

Monitor *s_monitor = 0;

extern "C" esp_err_t handler_get( httpd_req_t * req )
{
    if (s_monitor)
        s_monitor->Show( req );
    return ESP_OK;
}

Monitor::Monitor( AnalogReader & analog_reader ) :
        Reader { analog_reader }
{
    s_monitor = this;
    WebServer::Instance().AddPage( s_page, 0 );
}

Monitor::~Monitor()
{
    s_monitor = 0;
}

void Monitor::Show( struct httpd_req * req )
{
    static char s_data1[] =
            "<head><meta http-equiv=\"refresh\" content=\"1\"></head>"
                    "<body>"
                    "<table border=0>";
    static char s_data9[] = "</table>"
            "</body>";

    SendCharsChunk( req, s_data1 );

    AnalogReader::value_t val[100];
    Reader.GetValues( val, sizeof(val) / sizeof(val[0]) );
    for (int r = 0; r < 10; ++r) {
        SendStringChunk( req, "<tr align=\"right\">" );
        for (int c = 0; c < 10; ++c) {
            SendStringChunk( req, "<td>" );
            char str[10];
            snprintf( str, sizeof(str), "%u", val[r * 10 + c] );
            SendStringChunk( req, str );
            SendStringChunk( req, "</td>" );
        }
        SendStringChunk( req, "</tr>" );
    }

    SendCharsChunk( req, s_data9 );

    httpd_resp_send_chunk( req, 0, 0 );
}
