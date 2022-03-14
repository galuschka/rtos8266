/*
 * HttpHelper.cpp
 */

#include "HttpHelper.h"
#include "Wifi.h"


HttpHelper::HttpHelper( httpd_req_t * req ) : mReq {req}
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

void HttpHelper::Init( const char * subheader, const char * meta )
{
    Add( "<!DOCTYPE html>\n"
         "<html>\n"
         " <head><meta charset=\"utf-8\"/>" );
    if (meta)
        Add( meta );

    const char * host = Wifi::Instance().GetHost();

    Add( "  <title>" );
    Add( host );
    if (subheader) {
        Add( " - " );
        Add( subheader );
    }
    Add( "</title>\n"
         " </head>\n"
         " <body>\n" );
    if (subheader) {
        Add( "  <h1><a href=\"/\">" );
        Add( host );
        Add( "</a></h1>\n  <h2>" );
        Add( subheader );
        Add( "  </h2>\n" );
    } else {
        Add( "  <h1>" );
        Add( host );
        Add( "</h1>\n" );
    }
}

void HttpHelper::Add( const char * str, std::size_t len )
{
    if ((mBuf.length() + len) >= mBuf.capacity()) {
        httpd_resp_send_chunk( mReq, mBuf.c_str(), mBuf.length() );
        mChunks = true;
        mBuf.clear();
    }
    mBuf.append( str, len );
}
