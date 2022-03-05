/*
 * WebServer.h
 *
 *  Created on: 29.04.2020
 *      Author: galuschka
 */

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "WebServer.h"
#include "Wifi.h"
#include "Mqtinator.h"
#include "Updator.h"

#include <string.h>     // memmove()
#include <string>       // std::string

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
const char * const s_subMqtt    = "MQTT parameter";
const char * const s_subUpdate  = "Firmware update";
const char * const s_subReboot  = "Restart device";
WebServer          s_WebServer{};


void SendStringChunk( httpd_req_t * req, const char * string )
{
    httpd_resp_send_chunk( req, string, strlen( string ) );
}

void SendInitialChunk( httpd_req_t * req, const char * subheader = 0, const char * meta = 0 )
{
    const char * const host = Wifi::Instance().GetHost();

    SendConstChunk( req, "<!DOCTYPE html>\n"
                         "<html>\n"
                         " <head><meta charset=\"utf-8\"/>" );
    if (meta) {
        SendStringChunk( req, meta );
    }
    SendConstChunk( req, "  <title>" );
    SendStringChunk( req, host );
    if (subheader) {
        SendConstChunk( req, " - " );
        SendStringChunk( req, subheader );
    }
    SendConstChunk( req, "</title>\n </head>\n"
                           " <body>\n" );
    if (subheader) {
        SendConstChunk( req, "  <h1><a href=\"/\">" );
        SendStringChunk( req, Wifi::Instance().GetHost() );
        SendConstChunk( req, "</a></h1>\n  <h2>" );
        SendStringChunk( req, subheader );
        SendConstChunk( req, "  </h2>\n" );
    } else {
        SendConstChunk( req, "  <h1>" );
        SendStringChunk( req, Wifi::Instance().GetHost()  );
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
    Parser( Input * inArray, uint8_t nofFields ) : mInArray{inArray}, mNofFields{nofFields}, mFieldsParsed{0} {};

    bool ParsePostData( httpd_req_t * req );
    bool ParseUriParam( httpd_req_t * req );
    uint8_t Fields() { return mFieldsParsed; };  // +fields without ...=value
  private:
    bool Parse( const char * str, const char * end );
    void ClearUnparsed();

    Input * mInArray;
    uint8_t mNofFields;
    uint8_t mFieldsParsed;
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

bool Parser::ParseUriParam( httpd_req_t * req )
{
    const char * str = strchr( req->uri, '?' );
    if (str) {
        ++str;
        const char * const strend = strchr( str, 0 );
        while (str < strend) {
            const char * ampersand = strchr( str + 2, '&' );
            if (! ampersand)
                ampersand = strend;
            if (! Parse( str, ampersand )) {
                return false;
            }
            str = ampersand + 1;
        }
    }

    ClearUnparsed();
    return true;
}

bool Parser::ParsePostData( httpd_req_t * req )
{
    char buf[100];
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
        const char * ampersand = strchr( buf + 2, '&' );
        if (! ampersand)
            ampersand = readend;
        if (! Parse( buf, ampersand )) {
            return false;
        }
        if (ampersand != readend) {
            uint8_t move = readend - ampersand;  // incl. \0
            memmove( buf, ampersand + 1, move );
            readend = &buf[move - 1];
        } else
            readend = buf;
    } // while remaining || readend != buf

    ClearUnparsed();
    return true;
}

bool Parser::Parse( const char * str, const char * end )
{
    const char * equalsign = strchr( str + 1, '=' );
    if ((! equalsign) || (equalsign > end))
        equalsign = end;

    uint8_t const keylen = (uint8_t) (equalsign - str);

    for (uint8_t i = 0; i < mNofFields; ++i) {
        Input * const in = & mInArray[i];
        if ((mFieldsParsed & (1 << i))
            || strncmp( str, in->key, keylen )
            || in->key[keylen])
            continue;

        // key match:
        mFieldsParsed |= 1 << i;
        char * bp = in->buf;
        for (const char * val = equalsign + 1; val < end; ++val) {
            if ((*val == '%') && ((val+2) < end)) {
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
        return true;
    }
    ESP_LOGI( TAG, "parsed unknown key %.*s", keylen, str );
    return true;  // silently skip unknown fields
}

template<size_t N> class Row
{
    std::string cell[N] {};
public:
    Row() {};
    std::string & operator[](size_t i) { return cell[i<N?i:N-1]; };

    std::string & get( std::string & s ) {
        for (size_t i = 0; i < N; ++i) {
            s += "<td>";
            s += cell[i];
            s += "</td>";
        }
        return s;
    };
};

template<size_t R, size_t C> class Table
{
    Row<C> row[R] {};
public:
    Table() {};
    Row<C>& operator[](size_t i) { return row[i<R?i:R-1]; };

    std::string & get( std::string & s ) {
        for (size_t i = 0; i < R; ++i) {
            s += "    <tr>";
            row[i].get( s );
            s += "</tr>\n";
        }
        return s;
    };
};

void Parser::ClearUnparsed()
{
    for (uint8_t i = 0; i < mNofFields; ++i)
        if (! (mFieldsParsed & (1 << i))) {
            mInArray[i].len = 0;
            mInArray[i].buf[0] = 0;
        }
}


extern "C" esp_err_t handler_get_wifi( httpd_req_t * req )
{
    SendInitialChunk( req, s_subWifi );
    SendConstChunk( req, "<form method=\"post\">"
                             "<table border=0>"
                              "<tr>"
                               "<td>Hostname:</td>"
                               "<td><input type=\"text\" name=\"host\" value=\"" );
    {
        const char * const host = Wifi::Instance().GetHost();
        if (host && *host)
            SendStringChunk( req, host );
    }
    SendConstChunk( req, "\" maxlength=15></td>"
                               "<td>(used also as SSID in AP mode)</td>"
                              "</tr>\n   "
                              "<tr>"
                               "<td>SSID:</td>"
                               "<td><input type=\"text\" name=\"id\" value=\"" );
    {
        const char * const id = Wifi::Instance().GetSsid();
        if (id && *id)
            SendStringChunk( req, id );
    }
    SendConstChunk( req, "\" maxlength=15></td>"
                               "<td>(remote SSID in station mode)</td>"
                              "</tr>\n   "
                              "<tr>"
                               "<td>Password:</td>"
                               "<td><input type=\"password\" name=\"pw\" maxlength=31></td>"
                               "<td>(keep empty to not change)</td>"
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

    char host[16];
    char id[16];
    char pw[32];
    Parser::Input in[] = { { "host", host, sizeof(host) },
                           { "id",   id,   sizeof(id)   },
                           { "pw",   pw,   sizeof(pw)   } };
    Parser parser{ in, sizeof(in) / sizeof(in[0]) };

    if (! parser.ParsePostData( req )) {
        SendConstChunk( req, "unexpected end of data while parsing data" );
        SendFinalChunk( req );
        return ESP_OK;
    }

    if (! strcmp( host, Wifi::Instance().GetHost() ))
        host[0] = 0;
    if (! strcmp( id, Wifi::Instance().GetSsid() ))
        id[0] = 0;
    if (! strcmp( pw, Wifi::Instance().GetPassword() ))
        pw[0] = 0;
    if (! (host[0] || id[0] || pw[0])) {
        SendConstChunk( req, "data unchanged" );
        SendFinalChunk( req );
        return ESP_OK;
    }

	// ESP_LOGI( TAG, "received host \"%s\", ssid \"%s\", password \"%s\"", host, id, pw );
	if (! Wifi::Instance().SetParam( host, id, pw )) {
		SendConstChunk( req, "setting wifi parameter failed - try again" );
        SendFinalChunk( req );
		return ESP_OK;
	}

    SendConstChunk( req, "configuration has been set" );
    SendFinalChunk( req );
    return ESP_OK;
}


extern "C" esp_err_t handler_get_mqtt( httpd_req_t * req )
{
    Table<5,4> table;

    table[0][0] = "Host:";
    table[0][1] = "&nbsp;";
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

    SendInitialChunk( req, s_subMqtt );
    SendConstChunk( req, "  <form method=\"post\">\n"
                         "   <table border=0>\n" );
    {
        std::string s;
        table.get( s );
        SendStringChunk( req, s.c_str() );
    }
    SendConstChunk( req, "\n </table>\n "
                        "</form>" );
    SendFinalChunk( req );
    return ESP_OK;
}

extern "C" esp_err_t handler_post_mqtt( httpd_req_t * req )
{
    SendInitialChunk( req, s_subMqtt );
    if (!req->content_len) {
        SendConstChunk( req, "no data - nothing to be done" );
        SendFinalChunk( req );
        return ESP_OK;
    }

    char hostBuf[16];
    char portBuf[8];
    char pub[16];
    char sub[16];
    Parser::Input in[] = { { "host", hostBuf, sizeof(hostBuf) },
                           { "port", portBuf, sizeof(portBuf) },
                           { "pub",  pub,     sizeof(pub)  },
                           { "sub",  sub,     sizeof(sub)  } };
    Parser parser{ in, sizeof(in) / sizeof(in[0]) };

    if (! parser.ParsePostData( req )) {
        SendConstChunk( req, "unexpected end of data while parsing data" );
        SendFinalChunk( req );
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
        SendConstChunk( req, "data unchanged" );
        SendFinalChunk( req );
        return ESP_OK;
    }

	// ESP_LOGI( TAG, "received host \"%s\", ssid \"%s\", password \"%s\"", host, id, pw );
	if (! Mqtinator::Instance().SetParam( host, port, pub, sub )) {
		SendConstChunk( req, "setting MQTT parameter failed - try again" );
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
    if ((progress == 0) || (progress == 90) || (progress == 95) || (progress >= 99)) {
        SendInitialChunk( req, s_subUpdate );
        SendConstChunk( req, "  <form method=\"post\">\n" // enctype=\"multipart/form-data\"
                             "   <table>\n"
                             "    <tr><td align=\"right\">uri:</td><td>&nbsp;</td>\n"
                             "     <td>" );
        const char * const uri = updator.GetUri();
        if ((progress == 0) || (progress >= 99)) {
            SendConstChunk( req, "<input type=\"text\" name=\"uri\" value=\"" );
            if (uri && *uri)
                SendStringChunk( req, uri );
            SendConstChunk( req, "\" maxlength=79 alt=\"setup a web server for download to the device\">" );
        } else if (uri && *uri)
            SendStringChunk( req, uri );
        SendConstChunk( req, "</td></tr>\n" );
        const char * text = 0;
        switch (progress)
        {
            case  90: text = "wait for reboot"; break;
            case  95: text = "test booting"; break;
            case  99: text = "update failed"; break;
            case 100: text = "update succeeded"; break;
        }
        if (text && *text) {
            SendConstChunk( req, "    <tr><td align=\"right\">status:</td><td /><td>" );
            SendStringChunk( req, text );
            SendConstChunk( req, "</td></tr>\n" );
        }
        SendConstChunk( req, "    <tr><td /><td /><td><button type=\"submit\">" );
        text = 0;
        switch (progress)
        {
            case   0: text = "start update"; break;
            case  90: text = "reboot"; break;
            case  95: text = "confirm well booting"; break;
            case  99: text = "retry update"; break;
            case 100: text = "update again"; break;
        }
        if (text && *text)
            SendStringChunk( req, text );
        SendConstChunk( req, "</button></td></tr>\n" );
        if (progress == 99) {
            const char * msg = updator.GetMsg();
            if (msg && *msg) {
                SendConstChunk( req, "    <tr><td align=\"right\">last status:</td><td /><td>" );
                SendStringChunk( req, msg );
                SendConstChunk( req, "</td></tr>\n" );
            }
        }
        SendConstChunk( req, "   </table>\n"
                             "  </form>\n" );
        SendFinalChunk( req );
        return ESP_OK;
    }

    SendInitialChunk( req, s_subUpdate, "<meta http-equiv=\"refresh\" content=\"1; URL=/update\">" );
    SendConstChunk( req, "  <table>\n"
                         "   <tr><td align=\"right\">uri:</td><td>&nbsp;</td><td>" );
    SendStringChunk( req, updator.GetUri() );
    SendConstChunk( req, "</td></tr>\n"
                         "   <tr><td align=\"right\">status:</td><td /><td>update in progress</td></tr>\n"
                         "   <tr><td align=\"right\">progress:</td><td /><td><progress value=\"" );
    char buf[8];
    sprintf( buf, "%d", progress );
    SendStringChunk( req, buf );
    SendConstChunk( req, "\" max=\"100\"></progress></td></tr>\n"
                         "   <tr><td align=\"right\">status:</td><td /><td>" );
    const char * msg = updator.GetMsg();
    if (msg && *msg)
        SendStringChunk( req, msg );
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
    if (progress == 90) {
        SendInitialChunk( req, s_subUpdate, "<meta http-equiv=\"refresh\" content=\"10; URL=/update\">" );
        SendConstChunk( req, "  <h3>system will reboot...</h3>\n"
                                "  <br />\n"
                                "  <br />This page should become refreshed automatically, when system is up again.\n"
                                "  <br />\n"
                                "  <br />When the device does not boot properly, power off and on.\n"
                                "  <br />This will again activate the old image as fallback image.\n"
                                "  <br />\n"
                                "  <br />On proper boot up, you have to confirm this by the button\n"
                                "  <br />shown in this sub page after the page has been refreshed.\n" );
        SendFinalChunk( req );
        vTaskDelay( configTICK_RATE_HZ / 4 );  // give http stack a chance to send out
        esp_restart();
    }
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

    if (! parser.ParsePostData( req )) {
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
                             "    <tr><td align=\"right\">uri:</td><td>&nbsp;</td>\n"
                             "     <td><input type=\"text\" name=\"uri\" value=\"" );
        SendStringChunk( req, uri );
        SendConstChunk( req, "\" maxlength=79 alt=\"setup a web server for download to the device\"></td></tr>\n"
                             "    <tr><td /><td /><td><button type=\"submit\">try again</button></td></tr>\n"
                             "    <tr><td align=\"right\">status:</td><td /><td>" );
        SendStringChunk( req, err );
        SendConstChunk( req, "</td></tr>\n"
                             "   </table>\n" );
        SendFinalChunk( req );
        return ESP_OK;
    }

    SendInitialChunk( req, s_subUpdate, "<meta http-equiv=\"refresh\" content=\"1; URL=/update\">" );
    SendConstChunk( req, "   <table>\n"
                         "    <tr><td align=\"right\">uri:</td><td>&nbsp;</td>\n"
                         "     <td>" );
    SendStringChunk( req, updator.GetUri() );
    SendConstChunk( req, "</td></tr>\n"
                         "    <tr><td align=\"right\">status:</td><td /><td>triggering update</td></tr>\n"
                         "    <tr><td align=\"right\">progress:</td><td /><td><progress value=\"0\" max=\"100\"></progress></td></tr>\n"
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

extern "C" esp_err_t handler_get_readflash( httpd_req_t * req )
{
    unsigned long addr = 0;
    unsigned long size = 0;
    {
        char addrBuf[16];
        char sizeBuf[16];
        Parser::Input in[] = { { "addr", addrBuf, sizeof(addrBuf) },
                               { "size", sizeBuf, sizeof(sizeBuf) } };
        Parser parser{ in, sizeof(in) / sizeof(in[0]) };

        if (! parser.ParseUriParam( req )) {
            SendConstChunk( req, "unexpected end of data while parsing data" );
            httpd_resp_send_chunk( req, 0, 0 );
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
            if (addrErr)
                SendStringChunk( req, addrErr );
            if (addrErr && sizeErr)
                SendConstChunk( req, " and " );
            if (sizeErr)
                SendStringChunk( req, sizeErr );
            httpd_resp_send_chunk( req, 0, 0 );
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
const httpd_uri_t uri_main =        { .uri = "/",
                                      .method = HTTP_GET,
                                      .handler = handler_get_main,
                                      .user_ctx = 0 };
const httpd_uri_t uri_favicon =     { .uri = "/favicon.ico",
                                      .method = HTTP_GET,
                                      .handler = handler_get_favicon,
                                      .user_ctx = 0 };
const httpd_uri_t uri_readflash =   { .uri = "/readflash",
                                      .method = HTTP_GET,
                                      .handler = handler_get_readflash,
                                      .user_ctx = 0 };
const httpd_uri_t uri_get_wifi =    { .uri = "/wifi",
                                      .method = HTTP_GET,
                                      .handler = handler_get_wifi,
                                      .user_ctx = 0 };
const httpd_uri_t uri_post_wifi =   { .uri = "/wifi",
                                      .method = HTTP_POST,
                                      .handler = handler_post_wifi,
                                      .user_ctx = 0 };
const httpd_uri_t uri_get_mqtt =    { .uri = "/mqtt",
                                      .method = HTTP_GET,
                                      .handler = handler_get_mqtt,
                                      .user_ctx = 0 };
const httpd_uri_t uri_post_mqtt =   { .uri = "/mqtt",
                                      .method = HTTP_POST,
                                      .handler = handler_post_mqtt,
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
const WebServer::Page page_mqtt    { uri_get_mqtt,   s_subMqtt };
const WebServer::Page page_update  { uri_get_update, s_subUpdate };
const WebServer::Page page_reboot  { uri_get_reboot, s_subReboot };

//@formatter:on

extern "C" httpd_handle_t start_webserver( void )
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;

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
                         "   <tr><td>Project version:</td>" "<td>&nbsp;</td>" "<td>" );
    SendStringChunk( req, desc->version );
    SendConstChunk( req, "</td></tr>\n"
                         "   <tr><td>IDF version:</td>"     "<td />" "<td>" );
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
    if (! mServer)
        return;

    ESP_LOGI( TAG, "Registering URI handlers" ); EXPRD( vTaskDelay(1) )
    httpd_register_uri_handler( mServer, &uri_main );
    httpd_register_uri_handler( mServer, &uri_favicon );
    httpd_register_uri_handler( mServer, &uri_readflash );

    ESP_LOGD( TAG, "Add sub pages" ); EXPRD( vTaskDelay(1) )
    AddPage( page_wifi,   &uri_post_wifi );
    AddPage( page_mqtt,   &uri_post_mqtt );
    AddPage( page_update, &uri_post_update );
    AddPage( page_reboot, &uri_post_reboot );
}
