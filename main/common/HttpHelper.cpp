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
#include "WebServer.h"

namespace {
const char * TAG = "HttpHelper";
}

HttpHelper::HttpHelper( httpd_req_t * req, const char * h2text, const char * navitext )
    : mReq      { req }
    , mH2Text   { h2text }
    , mNaviText { navitext }
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
         " <head><meta charset=\"utf-8\"/>\n" );
    if (meta)
        Add( meta );
    Add( "\n\
  <style>\n\
   h1 { float: left; }\n\
   ul { list-style-type: none; }\n\
   li {\n\
    float: left;\n\
    display: block;\n\
    min-height: 48px;\n\
   }\n\
   li a {\n\
    padding: 8px;\n\
    margin: 8px;\n\
    border: 2px;\n\
    border-radius: 5px;\n\
    border-color: #ee4;\n\
    background-color: #ee4;\n\
   }\n\
   li a:hover:not(.active) {\n\
    border-color: #008;\n\
    background-color: #008;\n\
    color: #fff;\n\
   }\n\
   li a.active {\n\
    border-color: #088;\n\
    background-color: #088;\n\
    color: #fff;        \n\
    text-decoration: none;\n\
   }\n\
  </style>\n" );

    const char * host = Wifi::Instance().GetHost();

    Add( "  <title>" );
    Add( host );
    if (mNaviText) {
        Add( "-" );
        Add( mNaviText );
    } else if (mH2Text) {
        Add( "-" );
        Add( mH2Text );
    }
    {
        const char * const color = Wifi::Instance().GetBgCol();
        Add( "</title>\n"
            " </head>\n"
            " <body style=\"background-color:" );
        if (color && *color)
            Add( color );
        else
            Add( "lightblue" );
        Add( ";\">\n" );
    }
    Add( "  <h1>" );
    Add( host );
    Add( "</h1>\n" );
    Add( "  <div style=\"float: right;\">" );
    Add( "   <ul>\n" );
    for (WebServer::PageList const * pagelist = WebServer::Instance().GetPageList();
            pagelist;
            pagelist = pagelist->Next)
    {
        if (! pagelist->Page.NaviText)  // "hidden"
            continue;
        Add( (std::string) "   <li><a href=\"" + pagelist->Page.Uri.uri + "\"" );
        if (mNaviText && (strcmp( mNaviText, pagelist->Page.NaviText ) == 0))
            Add( " class=\"active\"" );
        Add( (std::string) ">" + pagelist->Page.NaviText + "</a></li>\n" );
    }
    Add( "   </ul>\n" );
    Add( "  </div>" );
    Add( "  <div style=\"clear: both\"></div>\n" );
    if (mH2Text) {
        Add( "  <h2>" );
        Add( mH2Text );
        Add( "</h2>\n" );
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

std::string HttpHelper::String( uint32_t val, int minLength )
{
    ESP_LOGD( TAG, "stringify uint %u", val );
    char buf[12];
    char * bp = & buf[sizeof(buf)-1];
    *bp = 0;
    do {
        --minLength;
        *--bp = (val % 10) + '0';
        val /= 10;
    } while ((val || (minLength > 0)) && (bp > buf));
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
