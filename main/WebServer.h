/*
 * WebServer.h
 *
 *  Created on: 29.04.2020
 *      Author: galuschka
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
        const char *LinkText;

        Page( const httpd_uri_t & uri, const char * text ) :
                Uri { uri }, LinkText { text }
        {
        }
    };
private:
    struct PageList
    {
        const WebServer::Page &Page;
        PageList *Next;

        PageList( const WebServer::Page & p ) :
                Page { p }, Next { 0 }
        {
        }
    };
public:
    WebServer() {};
    static WebServer& Instance();

    void Init();
    void AddPage( const Page & page, const httpd_uri_t * postUri = 0 );
    void MainPage( httpd_req_t * req );

private:
    httpd_handle_t mServer  { 0 };
    PageList      *mAnchor  { 0 };
    PageList      *mLastElem{ 0 };
};

#endif /* MAIN_WEBSERVER_H_ */
