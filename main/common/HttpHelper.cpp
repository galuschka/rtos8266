/*
 * HttpHelper.cpp
 */

//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include <sstream>
#include <iomanip>

#include <math.h>   // pow()
#include <esp_log.h>

#include "HttpHelper.h"
#include "Wifi.h"

namespace {
const char * TAG = "HttpHelper";
}

HttpHelper::HttpHelper( httpd_req_t * req, const char * subheader ) : mReq {req}, mSubheader {subheader ? subheader : ""}
{
    mBuf.reserve(1024);
}

HttpHelper::HttpHelper( httpd_req_t * req, std::string subheader ) : mReq {req}, mSubheader {subheader}
{
    mBuf.reserve(1024);
}

HttpHelper::~HttpHelper()
{
    Add( "\n </body>\n</html>\n" );
    if (! mChunks) {
        if (mBuf.length())
            httpd_resp_send( mReq, mBuf.c_str(), mBuf.length() );
    } else {
        if (mBuf.length())
            httpd_resp_send_chunk( mReq, mBuf.c_str(), mBuf.length() );

        httpd_resp_send_chunk( mReq, 0, 0 );
    }
}

void HttpHelper::Head( const char * meta )
{
    mInHead = true;
    Add( "<!DOCTYPE html>\n"
         "<html>\n"
         " <head><meta charset=\"utf-8\"/>" );
    if (meta)
        Add( meta );

    const char * host = Wifi::Instance().GetHost();

    Add( "  <title>" );
    Add( host );
    if (mSubheader.length()) {
        Add( " - " );
        Add( mSubheader );
    }
    Add( "</title>\n"
         " </head>\n"
         " <body>\n" );
    if (mSubheader.length()) {
        Add( "  <h1><a href=\"/\">" );
        Add( host );
        Add( "</a></h1>\n  <h2>" );
        Add( mSubheader );
        Add( "  </h2>\n" );
    } else {
        Add( "  <h1>" );
        Add( host );
        Add( "</h1>\n" );
    }
    mInHead = false;
}

void HttpHelper::Add( const char * str, std::size_t len )
{
    if (! (mBuf.length() || mChunks || mInHead))
        Head();

    if ((mBuf.length() + len) >= mBuf.capacity()) {
        httpd_resp_send_chunk( mReq, mBuf.c_str(), mBuf.length() );
        mChunks = true;
        mBuf.clear();
    }
    mBuf.append( str, len );
}

std::string HttpHelper::String( long val, int minLength )
{
    ESP_LOGD( TAG, "stringify int %ld", val );
    char buf[12];
    char * minus = buf;
    char * bp = & buf[sizeof(buf)-1];
    *bp = 0;
    if (val < 0) {
        val = -val;
        minus++;
    }
    do {
        --minLength;
        *--bp = (val % 10) + '0';
        val /= 10;
    } while ((val || (minLength > 0)) && (bp > minus));
    if (minus > buf)
        *--bp = '-';
    return std::string( bp );
}

std::string HttpHelper::String( double val, int precision )
{
    long l = (long) ((val * pow( 10, precision )) + 0.5);
    std::string ret = String( l, precision + 1 );
    if (precision) {
        ret.insert( ret.cend() - precision, '.' );
    }
    ESP_LOGD( TAG, "stringified double to \"%s\"", ret.c_str() ); vTaskDelay(1);
    return ret;

    // ESP_LOGD( TAG, "stringify float %.*f", precision, val ); vTaskDelay(1);
    // std::ostringstream ss;
    // ss << std::fixed << std::setprecision(precision) << val;
    // return ss.str();
}

std::string HttpHelper::HexString( uint32_t val, int minLength )
{
    ESP_LOGD( TAG, "hexify ulong %08x", val );
    char buf[12];
    char * bp = & buf[sizeof(buf)-1];
    *bp = 0;
    do {
        *--bp = (val & 0xf) + '0' + ((((val & 0xf) / 10) * ('A' - '0' - 10)));
        val >>= 4;
        --minLength;
    } while ((val || (minLength > 0)) && (bp > buf));
    return std::string( bp );
}

std::string HttpHelper::HexString( uint64_t val, int minLength )
{
    return HexString( (uint32_t) (val >> 32), minLength - 8 ) + HexString( (uint32_t) val, 8 );
}
