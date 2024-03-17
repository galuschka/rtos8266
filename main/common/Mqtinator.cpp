/*
 * Mqtinator.cpp
 */
//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "Mqtinator.h"
#include "Indicator.h"
#include "BootCnt.h"
#include "Wifi.h"
#include "WebServer.h"
#include "HttpHelper.h"
#include "HttpTable.h"
#include "HttpParser.h"

#include <string.h>

#include <esp_log.h>
#include <mqtt_client.h>
#include <lwip/apps/mqtt_priv.h>
#include <nvs.h>

#if (LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG)
#define EXPRD(expr) do { expr; } while(0);
#else
#define EXPRD(expr)
#endif

namespace
{
const char *const TAG              = "Mqtinator";

const char *const s_subMqtt        = "MQTT parameter";
const char *const s_nvsNamespace   = "mqtt";
const char *const s_keyHost        = "host";
const char *const s_keyPort        = "port";
const char *const s_keyPubTopic[2] = { "pub", "wdPub" };
const char *const s_keySubTopic[2] = { "sub", "wdSub" };
const char *const s_keyFormat      = "format";
const char *const s_keyPrimStatIdx = "primStatIdx";
const char *const s_keySecStatIdx  = "secStatIdx";
const char *const s_keyAlive       = "alive";

mqtt_client_t     s_client;
Mqtinator         s_mqtinator{};
}

extern "C" {

esp_err_t handler_get_mqtt( httpd_req_t * req )
{
    s_mqtinator.HttpReq( req, false );
    return ESP_OK;
}

esp_err_t handler_post_mqtt( httpd_req_t * req )
{
    s_mqtinator.HttpReq( req, true );
    return ESP_OK;
}

}

namespace {

const httpd_uri_t uri_get_mqtt  = { .uri = "/mqtt", .method = HTTP_GET,  .handler = handler_get_mqtt,  .user_ctx = 0 };
const httpd_uri_t uri_post_mqtt = { .uri = "/mqtt", .method = HTTP_POST, .handler = handler_post_mqtt, .user_ctx = 0 };
const WebServer::Page page_mqtt   { uri_get_mqtt, "MQTT" };

TickType_t now()
{
    return xTaskGetTickCount();
}
unsigned long expiration( unsigned long secs )
{
    TickType_t exp = xTaskGetTickCount() + (secs * configTICK_RATE_HZ);
    if (!exp)
        --exp;
    return exp;
}
bool expired( TickType_t exp )
{
    if (! exp)
        return false;
    long diff = now() - exp;
    return (diff >= 0);
}
TickType_t remaining( unsigned long exp )
{
    long diff = exp - now();
    return (TickType_t) (diff < 0 ? 0 : diff);
}

}

extern "C" {

static void MqtinatorTask( void * mqtinator );

static void mqtt_connection_cb( mqtt_client_t * client,
                                void * arg, mqtt_connection_status_t status );
static void mqtt_pub_request_cb( void * arg, err_t result );
static void mqtt_sub_request_cb( void * arg, err_t result );
static void mqtt_sub_topic_cb( void * arg, const char * topic, u32_t tot_len );
static void mqtt_sub_data_cb( void * arg, const u8_t * data, u16_t len, u8_t flags );

} // extern "C"


Mqtinator& Mqtinator::Instance()
{
    return s_mqtinator;
}

bool Mqtinator::Init()
{
    mSemaphore = xSemaphoreCreateBinary();
    if (!mSemaphore) {
        ESP_LOGE( TAG, "failed to create task unblocking semaphore" );
        return false;
    }

    mCallMutex = xSemaphoreCreateMutex();
    if (!mCallMutex) {
        ESP_LOGE( TAG, "failed to create mutex semaphore" );
        return false;
    }

    mCbWaitSema = xSemaphoreCreateBinary();
    if (!mCbWaitSema) {
        ESP_LOGE( TAG, "failed to create function waiting semaphore" );
        return false;
    }

    xTaskCreate( MqtinatorTask, "Mqtinator", /*stack size*/2048, this, /*prio*/ 1, &mTaskHandle );
    if (!mTaskHandle) {
        ESP_LOGE( TAG, "xTaskCreate failed" );
        return false;
    }

    return true;
}

void Mqtinator::AddPage( WebServer & webServerInstance )
{
    webServerInstance.AddPage( page_mqtt, & uri_post_mqtt );
}

extern "C" void MqtinatorTask( void * mqtinator )
{
    ((Mqtinator *) mqtinator)->Run();
}
void Mqtinator::Run()
{
    ReadParam();
    Connect();

    if (mAlivePeriod)
        mNextAlive = expiration( 5 );  // 1st alive after 5 seconds

    while (1)
    {
        // ESP_LOGD( TAG, "task waits for next event" );
        if (! mNextAlive)
            xSemaphoreTake( mSemaphore, portMAX_DELAY );
        else {
            long ticksToWait = mNextAlive - now();
            if (ticksToWait > 0)
                xSemaphoreTake( mSemaphore, ticksToWait );
        }
        // ESP_LOGD( TAG, "task continues" );

        if (mToConnect) {
            mToConnect = false;
            if (mConnStatus == MQTT_CONNECT_ACCEPTED) {
                ESP_LOGD( TAG, "task disconnects..." );
                mqtt_disconnect( & s_client );
                mConnStatus = MQTT_CONNECT_DISCONNECTED;
            }
            ESP_LOGD( TAG, "suspend 30 seconds to re-connect..." );
            xSemaphoreTake( mSemaphore, configTICK_RATE_HZ * 30 );
            ESP_LOGD( TAG, "task re-connects..." );
            Connect();
        }

        if (mDataComplete) {
            mDataComplete = false;
            ESP_LOGD( TAG, "task got \"%s\" \"%.16s ...\" (%d/%d bytes)", mInTopic, mInData, strlen(mInData), mInLen );

            int8_t const g = (mTopicGroup < 0) || (mTopicGroup >= 2) ? 0 : mTopicGroup;
            auto it = mSubCallbackMap[g].find( mInTopic );
            if (it != mSubCallbackMap[g].end()) {
                ESP_LOGD( TAG, "direct subscription found - calling callback function" );
                (it->second)( mInTopic, mInData );
            } else {
                it = mSubCallbackMap[g].find( "#" );
                if (it != mSubCallbackMap[g].end()) {
                    ESP_LOGD( TAG, "global subscription found - calling callback function" );
                    (it->second)( mInTopic, mInData );
                } else {
                    ESP_LOGD( TAG, "subscription not found - drop \"%s\"", mInTopic );
                }
            }
        }

        if ((mConnStatus == MQTT_CONNECT_ACCEPTED) && expired(mNextAlive)) {
            mNextAlive = expiration( mAlivePeriod );
            const unsigned long * sigmask = Indicator::Instance().SigMask();
            if (mFormat == 'd') {  // domoticz format
                for (u_char led = 0; led < 2; ++led) {
                    if (mStatusIdx[led]) {
                        ESP_LOGD( TAG, "publishing status \"0x%lx\"", sigmask[led] ); EXPRD(vTaskDelay(1))
                        Pub( mStatusIdx[led], sigmask[led] );
                    }
                }
            } else {
                std::string msg{};
                for (u_char led = 0; led < 2; ++led, msg += " ") {
                    if (sigmask[led])
                        msg += "0x" + HttpHelper::HexString( (uint32_t) sigmask[led], 0 );
                    else
                        msg += "0";
                }
                msg += " " + HttpHelper::String( (uint32_t) BootCnt::Instance().Cnt() );
                msg += " " + HttpHelper::String( (uint32_t) mCbPassedCnt );
                msg += " " + HttpHelper::String( (uint32_t) mCbFailedCnt );
                msg += " " + HttpHelper::String( (uint32_t) mCbTimoCnt );
                ESP_LOGD( TAG, "publishing status \"%s\"", msg.c_str() ); EXPRD(vTaskDelay(1))
                WdPub( "status", msg.c_str() );
            }
        }
    }
}

void Mqtinator::CallPrep( Mqtinator::CALL_STATUS callStatus )
{
    xSemaphoreTake( mCallMutex, portMAX_DELAY );
    mCallStatus = callStatus;
}

void Mqtinator::CallFailed( Mqtinator::CALL_STATUS callStatus )
{
    mCallStatus = CALL_IDLE;
    xSemaphoreGive( mCallMutex );
}

void Mqtinator::CallDone( Mqtinator::CALL_STATUS callStatus, bool passed )
{
    if (mCallStatus == callStatus) {
        mCallStatus |= passed ? CALL_DONE_PASSED : CALL_DONE_FAILED;
        xSemaphoreGive( mCbWaitSema );
    }
}

bool Mqtinator::CallWait( Mqtinator::CALL_STATUS callStatus )
{
    if (mCallStatus == callStatus) {
        unsigned long exp = expiration( 2 );  // we should get response after x second

        while (1) {
            xSemaphoreTake( mCbWaitSema, remaining( exp ) );
            if (mCallStatus != callStatus)
                break;
            if (expired( exp )) {
                ESP_LOGE( TAG, "callback timed out: waiting for call status %#x being changed", callStatus );
                ++mCbTimoCnt;
                mCallStatus = CALL_IDLE;
                xSemaphoreGive( mCallMutex );
                return false;
            }
        }
    }
    bool passed = (mCallStatus == (callStatus | CALL_DONE_PASSED));
    if (passed)
        ++mCbPassedCnt;
    else
        ++mCbFailedCnt;

    mCallStatus = CALL_IDLE;
    xSemaphoreGive( mCallMutex );
    return passed;
}

bool Mqtinator::Connect()
{
    if (! (mHost.addr && mPort))
        return false;

    ESP_LOGD( TAG, "Connect initiated" );
    struct mqtt_connect_client_info_t ci;
    memset( & ci, 0, sizeof(ci) );
    ESP_LOGD( TAG, "Connect GetHost()..." );
    ci.client_id = Wifi::Instance().GetHost();

    mConnStatus = MQTT_CONNECT_DISCONNECTED;
    CallPrep( CALL_CONNECT );
    ESP_LOGD( TAG, "mqtt_client_connect()... - indicating \"status connect\"" );
    Indicator::Instance().Indicate( Indicator::STATUS_CONNECT );
    err_t e = mqtt_client_connect( & s_client, & mHost, mPort, &mqtt_connection_cb, 0, & ci );
    if (e != ERR_OK) {
        CallFailed( CALL_CONNECT );
        // For now just reboot if something goes wrong
        ESP_LOGE( TAG, "mqtt_client_connect return %d -> reboot in 1 second", e );
        Indicator::Instance().Indicate( Indicator::STATUS_ERROR );
        vTaskDelay( configTICK_RATE_HZ );
        esp_restart();

        ESP_LOGE( TAG, "restart returned" );
        return false;
    }

    ESP_LOGD( TAG, "mqtt_client_connect initiated - waiting for callback" );
    CallWait( CALL_CONNECT );

    if (mConnStatus != MQTT_CONNECT_ACCEPTED)
    {
        ESP_LOGW( TAG, "Disconnected - status: %d", mConnStatus );
        Indicator::Instance().Indicate( Indicator::STATUS_ERROR );
        // try to reconnect
        mToConnect = true;
        return true;
    }

    ESP_LOGI( TAG, "Successfully connected - signaling \"0 0\"" );
    Indicator::Instance().SigMask( 0, 0 );

    mqtt_set_inpub_callback( & s_client, &mqtt_sub_topic_cb, &mqtt_sub_data_cb, /*arg*/0 );

    for (int8_t topicGroup = 0; topicGroup < 2; ++topicGroup) {
        if (mSubCallbackMap[topicGroup].empty())
            continue;
        std::string prefix = mSubTopic[topicGroup];
        if (!prefix.empty())
            prefix += "/";
        for (auto it : mSubCallbackMap[topicGroup]) {
            std::string fullTopic;
            if (it.first == "-")
                fullTopic = mSubTopic[topicGroup];
            else
                fullTopic = prefix + it.first;

            ESP_LOGD( TAG, "subscribing to %s", fullTopic.c_str() );
            CallPrep( CALL_SUBSCRIBE );
            err_t err = mqtt_subscribe( & s_client, fullTopic.c_str(), 1, &mqtt_sub_request_cb, /*arg*/0 );
            if (err != ESP_OK)
                CallFailed( CALL_SUBSCRIBE );
            else
                CallWait( CALL_SUBSCRIBE );
        }
    }
    return true;
}

bool Mqtinator::Pub( uint16_t idx, unsigned long val )
{
    return Pub( idx, std::to_string( val ) );
}

bool Mqtinator::Pub( uint16_t idx, const std::string str )
{
    std::string msg = "{ \"idx\": " + std::to_string( idx )
                    + ", \"nvalue\": 0"
                    + ", \"svalue\": \"" + str + "\""
                    + " }";
    ESP_LOGD( TAG, "publishing \"%s\"", msg.c_str() );
    return Pub( nullptr, msg.c_str() );
}

bool Mqtinator::Pub( const char * topic, const char * string, uint8_t qos, uint8_t retain )
{
    return PubExtended( 0, topic, string, qos, retain );
}

bool Mqtinator::WdPub( const char * topic, const char * string, uint8_t qos, uint8_t retain )
{
    if (! mPubTopic[1][0])
        return false;
    return PubExtended( 1, topic, string, qos, retain );
}

bool Mqtinator::PubExtended( int8_t topicGroup, const char * topic, const char * string, uint8_t qos, uint8_t retain )
{
    if (mConnStatus != MQTT_CONNECT_ACCEPTED)
        return false;

    /* qos 0: fire & forget
    **     1: send until ack
    **     2: assert one delivery (avoid double delivery on missing ack)
    */
    /* retain 0: don't retain
    **        1: retain: subscribers in future will get it too
    */
    std::string fullTopic = mPubTopic[topicGroup];
    if (topic) {
        if (!fullTopic.empty())
            fullTopic += "/";
        fullTopic += topic;
    }

    CallPrep( CALL_PUBLISH );
    err_t e = mqtt_publish( & s_client, fullTopic.c_str(), string, strlen(string),
                            qos, retain,
                            mqtt_pub_request_cb, /*arg*/0 );
    if (e != ESP_OK)
        CallFailed( CALL_PUBLISH );
    else
        CallWait( CALL_PUBLISH );

    // maybe better to not delay alive on each pub, so we can easily check for alive timeout
    // if (mAlivePeriod)
    //     mNextAlive = expiration( mAlivePeriod );

    if (e != ERR_OK) {
        ESP_LOGE( TAG, "Publish err: %d\n", e );
        return false;
    }
    return true;
}

bool Mqtinator::Sub( const char * topic, SubCallback callback )
{
    return SubExtended( 0, topic, callback );
}

bool Mqtinator::WdSub( const char * topic, SubCallback callback )
{
    if (! mSubTopic[1][0])
        return false;
    return SubExtended( 1, topic, callback );
}

bool Mqtinator::SubExtended( int8_t topicGroup, const char * topic, SubCallback callback )
{
    std::string fullTopic = mSubTopic[topicGroup];
    if (topic) {
        if (!fullTopic.empty())
            fullTopic += "/";
        fullTopic += topic;
    }

    if (! topic)
        topic = "-";

    err_t e = ERR_OK;
    if (callback) {
        // Subscribe to a topic with QoS level 1, call mqtt_sub_request_cb with result
        mSubCallbackMap[topicGroup][ topic ] = callback;
        if (mConnStatus == MQTT_CONNECT_ACCEPTED) {
            CallPrep( CALL_SUBSCRIBE );
            e = mqtt_subscribe( & s_client, fullTopic.c_str(), /*qos*/1, &mqtt_sub_request_cb, /*arg*/0 );
            if (e != ERR_OK) {
                ESP_LOGW( TAG, "mqtt_subscribe return: %d\n", e );
                CallFailed( CALL_SUBSCRIBE );
            } else
                CallWait( CALL_SUBSCRIBE );
        }
    } else {
        if (mConnStatus == MQTT_CONNECT_ACCEPTED) {
            CallPrep( CALL_SUBSCRIBE );
            e = mqtt_unsubscribe( & s_client, fullTopic.c_str(), &mqtt_sub_request_cb, /*arg*/0 );
            if (e != ERR_OK) {
                ESP_LOGW( TAG, "mqtt_unsubscribe return: %d\n", e );
                CallFailed( CALL_SUBSCRIBE );
            } else
                CallWait( CALL_SUBSCRIBE );
        }
        mSubCallbackMap[topicGroup].erase( topic );
    }
    return (e == ERR_OK);
}

bool Mqtinator::ReadParam()
{
    ESP_LOGI( TAG, "Reading Mqtinator configuration" );

    nvs_handle my_handle;
    if (nvs_open( s_nvsNamespace, NVS_READONLY, &my_handle ) != ESP_OK) {
        ESP_LOGD( TAG, "cannot open mqtinator nvs namespace" );
        return false;
    }

    size_t len;
    uint8_t fmtu8;

    nvs_get_u32( my_handle,    s_keyHost,        & mHost.addr );
    nvs_get_u16( my_handle,    s_keyPort,        & mPort );
    if (nvs_get_u8( my_handle, s_keyFormat,      & fmtu8 ) == ESP_OK)
                                                   mFormat = fmtu8;
    nvs_get_u16( my_handle,    s_keyPrimStatIdx, & mStatusIdx[0] );
    nvs_get_u16( my_handle,    s_keySecStatIdx,  & mStatusIdx[1] );
    nvs_get_u16( my_handle,    s_keyAlive,       & mAlivePeriod );

    for (int8_t topicGroup = 0; topicGroup < 2; ++topicGroup) {
        len = sizeof(mPubTopic[0]); nvs_get_str( my_handle, s_keyPubTopic[topicGroup], mPubTopic[topicGroup], &len );
        len = sizeof(mSubTopic[0]); nvs_get_str( my_handle, s_keySubTopic[topicGroup], mSubTopic[topicGroup], &len );
    }
    nvs_close( my_handle );
    return true;
}

bool Mqtinator::SetParam()
{
    nvs_handle my_handle;
    if (nvs_open( s_nvsNamespace, NVS_READWRITE, &my_handle ) != ESP_OK) {
        ESP_LOGD( TAG, "cannot open mqtinator nvs namespace" );
        return false;
    }
    esp_err_t e = ESP_OK;
    if (e == ESP_OK) e = nvs_set_u32( my_handle, s_keyHost,        mHost.addr );
    if (e == ESP_OK) e = nvs_set_u16( my_handle, s_keyPort,        mPort );
    for (int8_t topicGroup = 0; topicGroup < 2; ++topicGroup) {
        if (e == ESP_OK) e = nvs_set_str( my_handle, s_keyPubTopic[topicGroup], mPubTopic[topicGroup] );
        if (e == ESP_OK) e = nvs_set_str( my_handle, s_keySubTopic[topicGroup], mSubTopic[topicGroup] );
    }
    if (e == ESP_OK) e = nvs_set_u8(  my_handle, s_keyFormat,      mFormat );
    if (e == ESP_OK) e = nvs_set_u16( my_handle, s_keyPrimStatIdx, mStatusIdx[0] );
    if (e == ESP_OK) e = nvs_set_u16( my_handle, s_keySecStatIdx,  mStatusIdx[1] );
    if (e == ESP_OK) e = nvs_set_u16( my_handle, s_keyAlive,       mAlivePeriod );
    if (e == ESP_OK)
        e = nvs_commit( my_handle );
    else
        nvs_commit( my_handle );
    if (e != ESP_OK)
        return false;
    return true;
}


extern "C" void mqtt_connection_cb( mqtt_client_t           * client,
                                    void                    * arg,
                                    mqtt_connection_status_t  status )
{
    Mqtinator::Instance().CbConnect( client, arg, status );
}
void Mqtinator::CbConnect( mqtt_client_t * client, void * arg, mqtt_connection_status_t status )
{
    mConnStatus = status;
    CallDone( CALL_CONNECT, status == MQTT_CONNECT_ACCEPTED );
}

extern "C" void mqtt_pub_request_cb( void * arg, err_t result )
{
    Mqtinator::Instance().CbPubDone( arg, result );
}
void Mqtinator::CbPubDone( void * arg, err_t result )
{
    ESP_LOGD( TAG, "CbPubDone" );
    CallDone( CALL_PUBLISH, result == ESP_OK );
}

extern "C" void mqtt_sub_request_cb( void * arg, err_t result )
{
    Mqtinator::Instance().CbSubDone( arg, result );
}
void Mqtinator::CbSubDone( void * arg, err_t result )
{
    ESP_LOGD( TAG, "CbSubDone" );
    CallDone( CALL_SUBSCRIBE, result == ESP_OK );
}

extern "C" void mqtt_sub_topic_cb( void * arg, const char * topic, u32_t tot_len )
{
    Mqtinator::Instance().CbSubTopic( arg, topic, tot_len );
}
void Mqtinator::CbSubTopic( void * arg, const char * topic, u32_t tot_len )
{
    ESP_LOGD( TAG, "CbSubTopic \"%s\" with %d bytes", topic, tot_len );
    int8_t topicGroup;
    for (topicGroup = 0; topicGroup < 2; ++topicGroup) {
        uint8_t slen = strlen(mSubTopic[topicGroup]);
        if (slen && ! strncmp( topic, mSubTopic[topicGroup], slen )) {
            topic += slen;
            if (*topic == '/')
                ++topic;
            else if (! *topic)
                topic = "-";
            break;
        }
    }
    if (topicGroup >= 2)
        topicGroup = -1;  // no topic group - just a direct match
    mTopicGroup = topicGroup;
    strncpy( mInTopic, topic, sizeof(mInTopic) - 1 );
    mInTopic[sizeof(mInTopic) - 1] = 0;
    mInLen = (uint16_t) tot_len;
    mInReadLen = 0;
}

extern "C" void mqtt_sub_data_cb( void * arg, const u8_t * data, u16_t len, u8_t flags )
{
    Mqtinator::Instance().CbSubData( arg, data, len, flags );
}
void Mqtinator::CbSubData( void * arg, const u8_t * data, u16_t len, u8_t flags )
{
    ESP_LOGD( TAG, "CbSubData \"%.*s\"", len, data );
    char * const end = & mInData[sizeof(mInData) - 1];
    char * start = & mInData[mInReadLen];
    if (start > end)
        start = end;
    uint16_t thislen = end - start;
    if (thislen > len)
        thislen = len;
    memcpy( start, data, thislen );
    mInReadLen += len;  // beyond end, we just increase mInReadLen, until complete
    if (mInReadLen == mInLen) {
        start[thislen] = 0;  // string terminator to be set on last read
        mDataComplete = true;
        xSemaphoreGive( mSemaphore );
    }
}

namespace {

std::string InputField( const char * key, uint8_t maxlength, const char * value, const char * type = "text" )
{
    std::string str{"<input"};
    str +=   " name=\"";      str += key;
    str += "\" type=\"";      str += type;
    str += "\" maxlength=\""; str += std::to_string((int) maxlength);
    str += "\" value=\"";     str += value;
    str += "\" />";
    return str;
}

std::string InputField( const char * key, uint8_t maxlength, uint16_t value )
{
    std::string val = std::to_string( value );
    return InputField( key, maxlength, val.c_str(), "number" );
}

}

void Mqtinator::HttpReq( httpd_req_t * req, bool post )
{
    std::string err{""};

    while (post)
    {
        if (!req->content_len) {
            err = "no data";
            break;
        }

        char hostBuf[16];
        char portBuf[8];
        char formatBuf[4];
        char primStatIdxBuf[8];
        char secStatIdxBuf[8];
        char aliveBuf[8];
        char pub[2][sizeof(mPubTopic[0])];
        char sub[2][sizeof(mSubTopic[0])];
        HttpParser::Input in[] = {
            { s_keyHost,        hostBuf,        sizeof(hostBuf) },
            { s_keyPort,        portBuf,        sizeof(portBuf) },
            { s_keyPubTopic[0], pub[0],         sizeof(pub[0]) },
            { s_keySubTopic[0], sub[0],         sizeof(sub[0]) },
            { s_keyPubTopic[1], pub[1],         sizeof(pub[1]) },
            { s_keySubTopic[1], sub[1],         sizeof(sub[1]) },
            { s_keyFormat,      formatBuf,      sizeof(formatBuf) },
            { s_keyPrimStatIdx, primStatIdxBuf, sizeof(primStatIdxBuf) },
            { s_keySecStatIdx,  secStatIdxBuf,  sizeof(secStatIdxBuf) },
            { s_keyAlive,       aliveBuf,       sizeof(aliveBuf) } };
        HttpParser parser{ in, sizeof(in) / sizeof(in[0]) };

        const char * parseError = parser.ParsePostData( req );
        if (parseError) {
            err = "parser error: ";
            err += parseError;
            break;
        }

        uint8_t    changes = 0;
        ip_addr_t  host;
        char     * end;
        host.addr = ipaddr_addr( hostBuf );
        uint16_t   port        =   (uint16_t) strtoul( portBuf,  & end, 0 );
        char       format      = formatBuf[0] == '-' ? 0 : formatBuf[0];
        uint16_t   statIdx[2]  = { (uint16_t) strtoul( primStatIdxBuf, & end, 0 ),
                                   (uint16_t) strtoul( secStatIdxBuf, & end, 0 ) };
        uint16_t   alivePeriod =   (uint16_t) strtoul( aliveBuf, & end, 0 );
        uint16_t const oldAlivePeriod = mAlivePeriod;

        if (host.addr   != mHost.addr)    changes |= 1 << 0;
        if (port        != mPort)         changes |= 1 << 1;
        for (int8_t topicGroup = 0; topicGroup < 2; ++topicGroup) {
            if (strcmp( pub[topicGroup], mPubTopic[topicGroup] ))
                changes |= 1 << 2;
            if (strcmp( sub[topicGroup], mSubTopic[topicGroup] ))
                changes |= 1 << 3;
        }
        if (format      != mFormat)       changes |= 1 << 4;
        if (statIdx[0]  != mStatusIdx[0]) changes |= 1 << 5;
        if (statIdx[1]  != mStatusIdx[1]) changes |= 1 << 6;
        if (alivePeriod != mAlivePeriod)  changes |= 1 << 7;

        if (! changes) {
            err = "data unchanged";
            break;
        }

        mHost.addr = host.addr;
        mPort = port;
        mFormat = format;
        mStatusIdx[0] = statIdx[0];
        mStatusIdx[1] = statIdx[1];
        mAlivePeriod = alivePeriod;
        if (! alivePeriod)
            mNextAlive = 0;

        for (int8_t topicGroup = 0; topicGroup < 2; ++topicGroup) {
            strncpy( mPubTopic[topicGroup], pub[topicGroup], sizeof( mPubTopic[0]) ); mPubTopic[topicGroup][ sizeof(mPubTopic[0]) - 1 ] = 0;
            strncpy( mSubTopic[topicGroup], sub[topicGroup], sizeof( mSubTopic[0]) ); mSubTopic[topicGroup][ sizeof(mSubTopic[0]) - 1 ] = 0;
        }
        if (! SetParam()) {
            ReadParam();
            err = "setting MQTT parameter failed - try again";
            break;
        }
        if (changes & 0xf) {
            mToConnect = true;
            xSemaphoreGive( mSemaphore );
        }
        else if (changes & 0x80) {  // alive period changed
            if (mAlivePeriod) {
                if (! oldAlivePeriod) {
                    mNextAlive = expiration( configTICK_RATE_HZ );
                    xSemaphoreGive( mSemaphore );
                } else if (oldAlivePeriod > alivePeriod) {
                    TickType_t next = (mNextAlive - ((oldAlivePeriod - mAlivePeriod) * configTICK_RATE_HZ));
                    if (! next)
                        --next;
                    long diff = next - now();
                    if (diff < configTICK_RATE_HZ)
                        mNextAlive = expiration( configTICK_RATE_HZ );
                    else
                        mNextAlive = next;
                    xSemaphoreGive( mSemaphore );
                } // else: enlarge period -> not change next
            }
        }
        break;
    } // pseudo while loop - just checked for post with some early break

    HttpHelper hh{ req, s_subMqtt, "MQTT" };

    hh.Add( "  <form method=\"post\">\n"
            "   <table border=0>\n" );
    {
        Table<11,4> table;
        table.Right( 0 );
        table[0][1] = "&nbsp;";

        table[0][0] = "Host:";
        {
            char buf[16];
            buf[0] = 0;
            if (mHost.addr)
                ip4addr_ntoa_r( & mHost, buf, sizeof(buf) );
            table[0][2] = InputField( s_keyHost, 15, buf );
        }
        table[0][3] = "(IPv4 address of MQTT broker)";

        table[1][0] = "Port:";
        table[1][2] = InputField( s_keyPort, 5, mPort );
        table[1][3] = "(listening port of MQTT broker)";

        table[2][0] = "Publish topic:";
        table[2][2] = InputField( s_keyPubTopic[0], sizeof(mPubTopic[0])-1, mPubTopic[0] );
        table[2][3] = "(top level topic for publish)";

        table[3][0] = "Subscribe topic:";
        table[3][2] = InputField( s_keySubTopic[0], sizeof(mSubTopic[0])-1, mSubTopic[0] );
        table[3][3] = "(top level subscription topic)";

        table[4][0] = "WD publish topic:";
        table[4][2] = InputField( s_keyPubTopic[1], sizeof(mPubTopic[0])-1, mPubTopic[1] );
        table[4][3] = "(top level topic for publish watchdog confirmation)";

        table[5][0] = "WD subscribe topic:";
        table[5][2] = InputField( s_keySubTopic[1], sizeof(mSubTopic[0])-1, mSubTopic[1] );
        table[5][3] = "(top level watchdog subscription topic)";

        table[6][0] = "General format:";
        {
            char buf[2];
            buf[0] = mFormat ? mFormat : '-';
            buf[1] = 0;
            table[6][2] = InputField( s_keyFormat, 1, buf );
        }
        table[6][3] = "(-: flat / d: domoticz)";

        table[7][0] = "Primary status idx:";
        table[7][2] = InputField( s_keyPrimStatIdx, 5, mStatusIdx[0] );
        table[7][3] = "(domoticz device index for publish primary status value)";

        table[8][0] = "Secondary status idx:";
        table[8][2] = InputField( s_keySecStatIdx, 5, mStatusIdx[1] );
        table[8][3] = "(domoticz device index for publish scondary status value)";

        table[9][0] = "Alive status message period:";
        table[9][2] = InputField( s_keyAlive, 5, mAlivePeriod );
        table[9][3] = "[s] (0 = no keep alives)";

        table[10][2] = "<button type=\"submit\">set</button>";
        if (post) {
            if (err.empty())
                table[10][3] = "setup succeeded";
            else
                table[10][3] = "setup failed: " + err;
        }

        table.AddTo( hh );
    }
    hh.Add( "\n   </table>\n"
            "  </form>" );

    hh.Add( "  <table border=0>\n" );
    {
        Table<3,3> table;
        table.Right( 0 );
        table.Right( 2 );
        table[0][1] = "&nbsp;";
        table[0][0] = "callback with success:";
        table[1][0] = "callback with error:";
        table[2][0] = "callback timed out:";
        table[0][2] = HttpHelper::String( (uint32_t) Mqtinator::Instance().GetCbPassedCnt() );
        table[1][2] = HttpHelper::String( (uint32_t) Mqtinator::Instance().GetCbFailedCnt() );
        table[2][2] = HttpHelper::String( (uint32_t) Mqtinator::Instance().GetCbTimoCnt() );
        table.AddTo( hh );
    }
    hh.Add( "\n  </table>\n" );
}
