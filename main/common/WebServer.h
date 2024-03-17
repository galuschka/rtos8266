/*
 * WebServer.h
 */

#ifndef MAIN_WEBSERVER_H_
#define MAIN_WEBSERVER_H_

#include "Wifi.h"
#include <esp_http_server.h>

class WebServer
{
public:
    struct Page
    {
        const httpd_uri_t &Uri;
        const char *NaviText;

        Page( const httpd_uri_t & uri, const char * navitext ) :
                Uri { uri }, NaviText { navitext }
        {
        }
    };
    struct PageList
    {
        const WebServer::Page &Page;
        PageList *Next;

        PageList( const WebServer::Page & p ) :
                Page { p }, Next { 0 }
        {
        }
    };

    WebServer() {};
    static WebServer& Instance();

    void Init();
    void InitPages();
    void AddPage( const Page & page, const httpd_uri_t * postUri = 0 );
    void AddUri( const httpd_uri_t & uri );

    void MainPage( httpd_req_t * req );
    PageList const * GetPageList() { return mAnchor; }

private:
    httpd_handle_t mServer  { 0 };
    PageList      *mAnchor  { 0 };
    PageList      *mLastElem{ 0 };
};

#endif /* MAIN_WEBSERVER_H_ */
