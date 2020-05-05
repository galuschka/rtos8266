/*
 * WebServer.h
 *
 *  Created on: 29.04.2020
 *      Author: galuschka
 */

#ifndef MAIN_WEBSERVER_H_
#define MAIN_WEBSERVER_H_

#include <esp_http_server.h>

class WebServer
{
private:
    WebServer();
public:
    static WebServer& Instance();
    void Init();

private:
    httpd_handle_t server { 0 };
};

#endif /* MAIN_WEBSERVER_H_ */
