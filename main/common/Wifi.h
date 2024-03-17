/*
 * Wifi.h
 */

#ifndef MAIN_WIFI_H_
#define MAIN_WIFI_H_

#include <lwip/netif.h>
#include <event_groups.h>
#include <esp_event_base.h>
#include <tcpip_adapter.h>

class Indicator;
class WebServer;
struct httpd_req;

class Wifi
{
private:
    enum
    {
        GOT_IPV4_BIT   = 1 << 0,
        LOST_IPV4_BIT  = 1 << 1,
        NEW_CLIENT_BIT = 1 << 2,
    };
    enum
    {
        MODE_IDLE,
        MODE_CONNECTING,
        MODE_RECONNECTING,
        MODE_WAITDHCP,
        MODE_CONNECTFAILED,
        MODE_ACCESSPOINT,
        MODE_STATION,
    };
public:
    Wifi() {};
    static Wifi & Instance();

    void Init( int connTimoInSecs );

    u32_t        GetIpAddr()             const { return mIpAddr.addr; }
    const char * GetHost()               const { return mHost; }
    const char * GetBgCol()              const { return mBgCol; }
    const char * GetSsid(int i)          const { return mSsid[i]; }
    const char * GetPassword(int i)      const { return mPasswd[i]; }
    bool         StationMode()           const { return mMode == MODE_STATION; }
    bool         AccessPoint()           const { return mMode == MODE_ACCESSPOINT; }
    u16_t        NoStationCounter(int i) const { return mNoStation[i]; }

    void AddPage( WebServer & webserver );
    void Setup( struct httpd_req * req, bool post = false );  // set hostname, etc.

    bool SetParam( const char * host,  const char * bgcol,
                   const char * ssid0, const char * password0,
                   const char * ssid1, const char * password1 );

    void Event( esp_event_base_t event_base, int32_t event_id, void * event_data );
    void GotIp( ip_event_got_ip_t * event_data );
    void LostIp();
    void NewClient( ip_event_ap_staipassigned_t * event );
private:
    void ReadParam();
    void ModeAp();
    bool ModeSta( int connTimoInSecs );
    void SaveNoStation() const;

private:
    EventGroupHandle_t mConnectEventGroup { 0 };
    ip4_addr_t mIpAddr  { 0 };
    char mHost[16]      { "" };
    char mBgCol[12]     { "" };
    char mSsid[2][16]   { "", "" };
    char mPasswd[2][32] { "", "" };
    int  mStaIdx;
    char mMode          { MODE_IDLE };
    bool mReconnect     { false };
    uint16_t mNoStation[2] { 0, 0 };
};

#endif /* MAIN_WIFI_H_ */
