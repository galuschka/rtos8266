/*
 * HttpParser.cpp
 */
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "HttpParser.h"

#include "esp_log.h"   			// ESP_LOGI()

#define min(a,b) ((a) < (b) ? a : b)

namespace
{
const char * const TAG = "HttpParser";
}

const char * HttpParser::ParseUriParam( httpd_req_t * req )
{
    const char * str = strchr( req->uri, '?' );
    if (str) {
        ++str;
        const char * const strend = strchr( str, 0 );
        while (str < strend) {
            const char * ampersand = strchr( str + 2, '&' );
            if (! ampersand)
                ampersand = strend;
            const char * const parseErr = Parse( str, ampersand );
            if (parseErr)
                return parseErr;

            str = ampersand + 1;
        }
    }

    ClearUnparsed();
    return nullptr;
}

const char * HttpParser::ParsePostData( httpd_req_t * req )
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
                return "subsequentail httpd_req_recv failed";
            }
            remaining -= readlen;
            readend += readlen;
            *readend = 0;
        }
        const char * ampersand = strchr( buf + 2, '&' );
        if (! ampersand)
            ampersand = readend;

        ESP_LOGD( TAG, "Parse( \"%.*s\" / ampersand at offset %d )", readend-buf,buf, ampersand-buf );
        const char * const parseErr = Parse( buf, ampersand );
        if (parseErr)
            return parseErr;

        if (ampersand != readend) {
            uint8_t move = readend - ampersand;  // incl. \0
            memmove( buf, ampersand + 1, move );
            readend = &buf[move - 1];
        } else
            readend = buf;
    } // while remaining || readend != buf

    ClearUnparsed();
    return nullptr;
}

const char * HttpParser::Parse( const char * str, const char * end )
{
    const char * equalsign = strchr( str + 1, '=' );
    if ((! equalsign) || (equalsign > end))
        equalsign = end;

    uint8_t const keylen = (uint8_t) (equalsign - str);

    for (uint8_t i = 0; i < mNofFields; ++i) {
        Input * const in = & mInArray[i];
        if (strncmp( str, in->key, keylen ) || in->key[keylen])
            continue;

        // key match:

        if (mFieldsParsed & (1 << i)) {
            ESP_LOGI( TAG, "parsed duplicate key \"%.*s\"", keylen, str );
            return nullptr;  // silently skip duplicate fields
        }

        mFieldsParsed |= 1 << i;
        if (! (in->buf && in->len))
            return nullptr;
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
        return nullptr;
    }
    ESP_LOGI( TAG, "parsed unknown key \"%.*s\"", keylen, str );
    return nullptr;  // silently skip unknown fields
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
