/*
 * Mqtinator.h
 *
 *  Created on: 26.02.2022
 *      Author: galuschka
 */

#include <stdint.h>
#include <map>

#include <mqtt_client.h>
#include <lwip/apps/mqtt.h>

class Mqtinator
{
public:
    typedef void (*SubCallback)( const char * topic, const char * data );

    Mqtinator() {};
    static Mqtinator & Instance();

    void Run();
    bool Init();
    bool Pub( const char * topic, const char * string, uint8_t qos = 1, uint8_t retain = 0 );
    bool Sub( const char * topic, SubCallback  callback );

    void CbConnect(  mqtt_client_t * client, 
                     void * arg, mqtt_connection_status_t status );  // on status change
    void CbPubDone(  void * arg, err_t result );                     // on Pub finished
    void CbSubDone(  void * arg, err_t result );                     // on Sub finished
    void CbSubTopic( void * arg, const char * topic, u32_t tot_len );         // topic of Sub
    void CbSubData(  void * arg, const u8_t * data, u16_t len, u8_t flags );  // data of Sub

    ip_addr_t    GetHost()      { return mHost; };
    uint16_t     GetPort()      { return mPort; };
    const char * GetPubTopic()  { return mPubTopic; };
    const char * GetSubTopic()  { return mSubTopic; };

    bool SetParam( ip_addr_t    host,
                   uint16_t     port,
                   const char * pubTopic,
                   const char * subTopic );
private:
    bool ReadParam();
    bool Connect();

    mqtt_connection_status_t mStatus { MQTT_CONNECT_DISCONNECTED };

    bool      mToConnect     { false };
    bool      mDataComplete  { false };

    ip_addr_t mHost          { 0 };
    uint16_t  mPort          { 1883 };
    char      mPubTopic[16]  { "" };
    char      mSubTopic[16]  { "" };

    uint16_t  mInLen         { 0 };
    uint16_t  mInReadLen     { 0 };
    char      mInTopic[16]   { "" };
    char      mInData[32]    { "" };

    std::map<std::string, SubCallback> mSubCallbackMap {};

    TaskHandle_t mTaskHandle     { 0 };
    SemaphoreHandle_t mSemaphore { 0 };
};
