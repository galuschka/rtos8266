/*
 * HttpParser.h
 */

#include <esp_http_server.h>    // httpd_req_t

class HttpParser
{
  public:
    struct Input {
        const char * key;  // field name
        char       * buf;  // buffer
        uint8_t      len;  // input to parse: buf size / output: length
        Input( const char * akey, char * abuf, uint8_t size ) : key{akey}, buf{abuf}, len{size} {};
    };
    HttpParser( Input * inArray, uint8_t nofFields )
        : mInArray{inArray},
          mNofFields{nofFields},
          mFieldsParsed{0}
    {};

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
