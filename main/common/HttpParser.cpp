/*
 * HttpParser.cpp
 */

#include "HttpParser.h"

#include "esp_log.h"   			// ESP_LOGI()

#define min(a,b) ((a) < (b) ? a : b)

namespace
{
const char * const TAG = "HttpParser";
}

bool HttpParser::ParseUriParam( httpd_req_t * req )
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

bool HttpParser::ParsePostData( httpd_req_t * req )
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

bool HttpParser::Parse( const char * str, const char * end )
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
        if (! (in->buf && in->len))
            return true;
        char * bp = in->buf;
        for (const char * val = equalsign + 1; val < end; ++val) {
            if ((*val == '%') && ((val+2) < end)) {
                *bp = (((val[1] + ((val[1] >> 6) & 1) * 9) & 0xf) << 4)
                     | ((val[2] + ((val[2] >> 6) & 1) * 9) & 0xf);
                val += 2;
            } else if (*val == '+')
                *bp = ' ';
            else
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

void HttpParser::ClearUnparsed()
{
    for (uint8_t i = 0; i < mNofFields; ++i)
        if (! (mFieldsParsed & (1 << i))) {
            mInArray[i].len = 0;
            if (mInArray[i].buf)
                mInArray[i].buf[0] = 0;
        }
}
