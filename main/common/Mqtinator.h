/*
 * Mqtinator.h
 */

#include <stdint.h>
#include <map>

#include <mqtt_client.h>
#include <lwip/apps/mqtt.h>

struct httpd_req;
class WebServer;

class Mqtinator
{
public:
    enum CALL_STATUS {
        CALL_IDLE = 0,
        CALL_CONNECT,
        CALL_SUBSCRIBE,
        CALL_PUBLISH,

        CALL_DONE_PASSED = 0x40,
        CALL_DONE_FAILED = 0x80,
    };
    typedef void (*SubCallback)( const char * topic, const char * data );

    Mqtinator() {};
    static Mqtinator & Instance();

    void HttpReq( struct httpd_req * req, bool post );
    void AddPage( WebServer & webServerInstance );  // controlled way to stack
    bool Init();
    void Run();
    bool Pub( uint16_t idx, unsigned long val );
    bool Pub( uint16_t idx, const std::string str );
    bool Pub( const char * topic, const char * string, uint8_t qos = 1, uint8_t retain = 0 );
    bool Sub( const char * topic, SubCallback callback );
    bool WdPub( const char * topic, const char * string, uint8_t qos = 1, uint8_t retain = 0 );
    bool WdSub( const char * topic, SubCallback callback );

    void CbConnect(  mqtt_client_t * client, 
                     void * arg, mqtt_connection_status_t status );  // on status change
    void CbPubDone(  void * arg, err_t result );                     // on Pub finished
    void CbSubDone(  void * arg, err_t result );                     // on Sub finished
    void CbSubTopic( void * arg, const char * topic, u32_t tot_len );         // topic of Sub
    void CbSubData(  void * arg, const u8_t * data, u16_t len, u8_t flags );  // data of Sub

    uint16_t GetCbPassedCnt() const { return mCbPassedCnt; };
    uint16_t GetCbFailedCnt() const { return mCbFailedCnt; };
    uint16_t GetCbTimoCnt()   const { return mCbTimoCnt; };

private:
    bool PubExtended( int8_t topicGroup, const char * topic, const char * string, uint8_t qos = 1, uint8_t retain = 0 );
    bool SubExtended( int8_t topicGroup, const char * topic, SubCallback callback );

    void CallPrep(   CALL_STATUS callStatus );
    void CallFailed( CALL_STATUS callStatus );
    void CallDone(   CALL_STATUS callStatus, bool passed );
    bool CallWait(   CALL_STATUS callStatus );

    bool Connect();
    bool ReadParam();
    bool SetParam();

    mqtt_connection_status_t mConnStatus { MQTT_CONNECT_DISCONNECTED };
    uint8_t                  mCallStatus { CALL_IDLE };

    bool       mToConnect    { false };
    bool       mDataComplete { false };

    ip_addr_t  mHost         { 0 };
    uint16_t   mPort         { 1883 };
    uint16_t   mAlivePeriod  { 0 };  // [s] 0 = no alive msgs
    uint16_t   mStatusIdx[2] { 0 };  // domoticz virtual device idx -> '{"idx":..., "nvalue":..., "svalue":""..."}'
    char       mFormat       { 0 };  // syntax for pub/sub data 0: default / 'd': domoticz / ...
    char       mPubTopic[2][32] { "", "" };  // strlen("keypad/alarm/up") = 15!
    char       mSubTopic[2][32] { "", "" };

    TickType_t mNextAlive    { 0 };
    uint16_t   mCbPassedCnt  { 0 };  // # of CallWait usual behavior with success
    uint16_t   mCbFailedCnt  { 0 };  // # of CallWait usual behavior with error
    uint16_t   mCbTimoCnt    { 0 };  // # of CallWait timeout expirations happened
    uint16_t   mInLen        { 0 };
    uint16_t   mInReadLen    { 0 };
    int8_t     mTopicGroup  { -1 };  // to which topic group we get subscription data
    char       mInTopic[31] { "" };  // just string behind (mSubTopic + "/")
    char       mInData[800] { "" };

    std::map<std::string, SubCallback> mSubCallbackMap[2] {};

    TaskHandle_t      mTaskHandle{ 0 };
    SemaphoreHandle_t mSemaphore { 0 };
    SemaphoreHandle_t mCbWaitSema{ 0 };
    SemaphoreHandle_t mCallMutex { 0 };
};
