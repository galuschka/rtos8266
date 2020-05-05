/*
 * Wifi.h
 *
 *  Created on: 29.04.2020
 *      Author: galuschka
 */

#ifndef MAIN_WIFI_H_
#define MAIN_WIFI_H_

#include "lwip/netif.h"
#include "event_groups.h"
#include "esp_event_base.h"
#include "tcpip_adapter.h"

class Wifi
{
private:
    enum
    {
        GOT_IPV4_BIT = 1 << 0,
    };
    enum
    {
        MODE_IDLE,
        MODE_CONNECTING,
        MODE_CONNECTFAILED,
        MODE_ACCESSPOINT,
        MODE_STATION,
    };

    Wifi();
public:
    static Wifi& Instance();
    void Init( int connTimoInSecs );
    u32_t GetIpAddr()
    {
        return mIpAddr.addr;
    }
    const char* GetSsid()
    {
        return mSsid;
    }
    const char* GetPassword()
    {
        return mPassword;
    }
    bool StationMode()
    {
        return mMode == MODE_STATION;
    }
    bool AccessPoint()
    {
        return mMode == MODE_ACCESSPOINT;
    }

    bool SetParam( const char * ssid, const char * password );

    void Event( esp_event_base_t event_base, int32_t event_id,
            void * event_data );
    void GotIp( ip_event_got_ip_t * event_data );
private:
    void ReadParam();
    void ModeAp();
    bool ModeSta( int connTimoInSecs );

private:
    EventGroupHandle_t mConnectEventGroup;
    ip4_addr_t mIpAddr { 0 };
    char mSsid[32];
    char mPassword[32];
    char mMode;
};

#endif /* MAIN_WIFI_H_ */
