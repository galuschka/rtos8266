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
    HttpHelper( httpd_req_t * req, const char * subheader = 0 );
    HttpHelper( httpd_req_t * req, std::string subheader );
    ~HttpHelper();
    void Head( const char * meta = 0 );

    void Add( const char        * str )       { Add( str, strlen( str ) ); }
    void Add( const std::string & str )       { Add( str.c_str(), str.length() ); }
    void Add( long                val )       { Add( String( val ) ); }
    void Add( double val, int precision = 0 ) { Add( String( val, precision ) ); }

    void Add( const char * str, std::size_t len );

    static std::string String( long   val, int minLength = 1 );
    static std::string String( double val, int precision = 0 );
    static std::string String( float  val, int precision = 0 ) { return String((double)val,precision); };
    static std::string HexString( uint32_t val, int minLength =  8 );
    static std::string HexString( uint64_t val, int minLength = 16 );

private:
    bool                mChunks { false };
    bool                mInHead { false };
    httpd_req_t * const mReq;
    std::string   const mSubheader;
    std::string         mBuf {};
};
