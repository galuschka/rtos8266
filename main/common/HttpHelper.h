/*
 * HttpHelper.h
 */

#pragma once

#include <esp_http_server.h>    // httpd_req_t
#include <string>               // std::string

class HttpHelper
{
    HttpHelper();
public:
    HttpHelper( httpd_req_t * req );
    ~HttpHelper();
    void Init( const char * subheader = 0, const char * meta = 0 );

    void Add( const char * str )        { Add( str, strlen( str ) ); }
    void Add( const std::string & str ) { Add( str.c_str(), str.length() ); }
    void Add( const char * str, std::size_t len );

private:
    bool                mChunks { false };
    std::string         mBuf;
    httpd_req_t * const mReq;
};
