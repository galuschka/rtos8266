/*
 * Mqtinator.cpp
 *
 *  Created on: 26.02.2022
 *      Author: galuschka
 */

//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "Mqtinator.h"
#include "Indicator.h"
#include "Wifi.h"

#include <string.h>

#include <esp_log.h>
#include <mqtt_client.h>
#include <lwip/apps/mqtt_priv.h>
#include <nvs.h>

namespace
{

const char *const TAG            = "MqttHelper";
const char *const s_nvsNamespace = "mqtt";
const char *const s_keyHost      = "host";
const char *const s_keyPort      = "port";
const char *const s_keyPubTopic  = "pub";
const char *const s_keySubTopic  = "sub";

mqtt_client_t     s_client;
Mqtinator         s_mqtinator{};

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
    mSemaphore = xSemaphoreCreateBinary( );
    xTaskCreate( MqtinatorTask, "Mqtinator", /*stack size*/2048, this,
                 /*prio*/ 1, &mTaskHandle );
    if (!mTaskHandle) {
        ESP_LOGE( TAG, "xTaskCreate failed" );
        return false;
    }

    if (!mSemaphore) {
        ESP_LOGE( TAG, "xSemaphoreCreateBinary failed" );
        return false;
    }
    return true;
}

extern "C" void MqtinatorTask( void * mqtinator )
{
    ((Mqtinator *) mqtinator)->Run();
}
void Mqtinator::Run()
{
    ReadParam();
    Connect();

    while (1)
    {
        // ESP_LOGD( TAG, "task waits for next event" );
        xSemaphoreTake( mSemaphore, portMAX_DELAY );
        // ESP_LOGD( TAG, "task continues" );

        if (mToConnect) {
            mToConnect = false;
            if (mStatus == MQTT_CONNECT_ACCEPTED) {
                ESP_LOGD( TAG, "task disconnects..." );
                mqtt_disconnect( & s_client );
                mStatus = MQTT_CONNECT_DISCONNECTED;
            }
            ESP_LOGD( TAG, "suspend 30 seconds to re-connect..." );
            xSemaphoreTake( mSemaphore, configTICK_RATE_HZ * 30 );
            ESP_LOGD( TAG, "task re-connects..." );
            Connect();
        }

        if (mDataComplete) {
            mDataComplete = false;
            ESP_LOGD( TAG, "task got \"%s\" \"%.16s ...\" (%d/%d bytes)", mInTopic, mInData, strlen(mInData), mInLen );
# if 0
            for (auto it : mSubCallbackMap)
                if (! strcmp( it.first, mInTopic )) {
                    ESP_LOGD( TAG, "subscription found - calling callback function" );
                    it.second( mInTopic, mInData );
                }
# else
            auto it = mSubCallbackMap.find( mInTopic );
            if (it != mSubCallbackMap.end()) {
                ESP_LOGD( TAG, "subscription found - calling callback function" );
                (it->second)( mInTopic, mInData );
            }
# endif
        }
    }
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

    mStatus = MQTT_CONNECT_DISCONNECTED;
    ESP_LOGD( TAG, "mqtt_client_connect()..." );
    Indicator::Instance().Indicate( Indicator::STATUS_CONNECT );
    err_t e = mqtt_client_connect( & s_client, & mHost, mPort, &mqtt_connection_cb, 0, & ci );

    if (e != ERR_OK) {
        // For now just reboot if something goes wrong
        Indicator::Instance().Indicate( Indicator::STATUS_ERROR );
        ESP_LOGE( TAG, "mqtt_client_connect return %d -> reboot in 1 second", e );
        vTaskDelay( configTICK_RATE_HZ );
        esp_restart();
    } else {
        ESP_LOGD( TAG, "mqtt_client_connect initiated" );
    }
    return (e == ESP_OK);
}

bool Mqtinator::Pub( const char * topic, const char * string, uint8_t qos, uint8_t retain )
{
    if (mStatus != MQTT_CONNECT_ACCEPTED)
        return false;

    /* qos 0: fire & forget
    **     1: send until ack
    **     2: assert one delivery (avoid double delivery on missing ack)
    */
    /* retain 0: don't retain
    **        1: retain: subscribers in future will get it too
    */
    std::string fullTopic = mPubTopic;
    if (topic) {
        if (!fullTopic.empty())
            fullTopic += "/";
        fullTopic += topic;
    }
    err_t e = mqtt_publish( & s_client, fullTopic.c_str(), string, strlen(string),
                            qos, retain,
                            mqtt_pub_request_cb, /*arg*/0 );
    if (e != ERR_OK) {
        ESP_LOGE( TAG, "Publish err: %d\n", e );
        return false;
    }
    return true;
}

bool Mqtinator::Sub( const char * topic, SubCallback callback )
{
    std::string fullTopic = mSubTopic;
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
        mSubCallbackMap[ topic ] = callback;
        if (mStatus == MQTT_CONNECT_ACCEPTED) {
            e = mqtt_subscribe( & s_client, fullTopic.c_str(), /*qos*/1, &mqtt_sub_request_cb, /*arg*/0 );
            if (e != ERR_OK) {
                ESP_LOGW( TAG, "mqtt_subscribe 1 return: %d\n", e );
            }
        }
    } else {
        if (mStatus == MQTT_CONNECT_ACCEPTED) {
            e = mqtt_unsubscribe( & s_client, fullTopic.c_str(), /*cb*/0, /*arg*/0 );
            if (e != ERR_OK) {
                ESP_LOGW( TAG, "mqtt_subscribe 0 return: %d\n", e );
            }
        }
        mSubCallbackMap.erase( topic );
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
    esp_err_t e = ESP_OK;

    if (e == ESP_OK)
        e = nvs_get_u32( my_handle, s_keyHost, & mHost.addr );
    if (e == ESP_OK)
        e = nvs_get_u16( my_handle, s_keyPort, & mPort );

    if (e == ESP_OK) {
        len = sizeof(mPubTopic);
        e = nvs_get_str( my_handle, s_keyPubTopic, mPubTopic, &len );
    }
    if (e == ESP_OK) {
        len = sizeof(mSubTopic);
        e = nvs_get_str( my_handle, s_keySubTopic, mSubTopic, &len );
    }

    nvs_close( my_handle );
    return (e == ESP_OK);
}

bool Mqtinator::SetParam( ip_addr_t    host,
                          uint16_t     port,
                          const char * pubTopic,
                          const char * subTopic )
{
    nvs_handle my_handle;
    if (nvs_open( s_nvsNamespace, NVS_READWRITE, &my_handle ) != ESP_OK) {
        ESP_LOGD( TAG, "cannot open mqtinator nvs namespace" );
        return false;
    }

    mHost.addr = host.addr;
    mPort = port;
    strncpy( mPubTopic, pubTopic, sizeof(mPubTopic) );  mPubTopic[ sizeof(mPubTopic) - 1 ] = 0;
    strncpy( mSubTopic, subTopic, sizeof(mSubTopic) );  mSubTopic[ sizeof(mSubTopic) - 1 ] = 0;

    esp_err_t e = ESP_OK;
    if (e == ESP_OK)
        e = nvs_set_u32( my_handle, s_keyHost,     mHost.addr );
    if (e == ESP_OK)
        e = nvs_set_u16( my_handle, s_keyPort,     mPort );
    if (e == ESP_OK)
        e = nvs_set_str( my_handle, s_keyPubTopic, mPubTopic );
    if (e == ESP_OK)
        e = nvs_set_str( my_handle, s_keySubTopic, mSubTopic );
    if (e == ESP_OK)
        e = nvs_commit( my_handle );
    else
        nvs_commit( my_handle );
    if (e != ESP_OK)
        return false;
    mToConnect = true;
    xSemaphoreGive( mSemaphore );
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
    mStatus = status;
    if (status != MQTT_CONNECT_ACCEPTED)
    {
        ESP_LOGW( TAG, "Disconnected - reason: %d", status );
        Indicator::Instance().Indicate( Indicator::STATUS_ERROR );
        // try to reconnect
        mToConnect = true;
        xSemaphoreGive( mSemaphore );
        return;
    }

    ESP_LOGI( TAG, "Successfully connected (client %p / %p)", client, & s_client );
    Indicator::Instance().Steady( 0 );
    mqtt_set_inpub_callback( client, &mqtt_sub_topic_cb, &mqtt_sub_data_cb, /*arg*/0 );
    if (! mSubCallbackMap.empty()) {
        std::string prefix = mSubTopic;
        if (!prefix.empty())
            prefix += "/";
        for (auto it : mSubCallbackMap) {
            std::string fullTopic;
            if (it.first == "-")
                fullTopic = mSubTopic;
            else
                fullTopic = prefix + it.first;

            ESP_LOGD( TAG, "subscribing to %s", fullTopic.c_str() );
            mqtt_subscribe( & s_client, fullTopic.c_str(), 1, &mqtt_sub_request_cb, /*arg*/0 );
        }
    }
}

extern "C" void mqtt_pub_request_cb( void * arg, err_t result )
{
    Mqtinator::Instance().CbPubDone( arg, result );
}
void Mqtinator::CbPubDone( void * arg, err_t result )
{
    ESP_LOGD( TAG, "CbPubDone" );
}

extern "C" void mqtt_sub_request_cb( void * arg, err_t result )
{
    Mqtinator::Instance().CbSubDone( arg, result );
}
void Mqtinator::CbSubDone( void * arg, err_t result )
{
    ESP_LOGD( TAG, "CbSubDone" );
}

extern "C" void mqtt_sub_topic_cb( void * arg, const char * topic, u32_t tot_len )
{
    Mqtinator::Instance().CbSubTopic( arg, topic, tot_len );
}
void Mqtinator::CbSubTopic( void * arg, const char * topic, u32_t tot_len )
{
    ESP_LOGD( TAG, "CbSubTopic \"%s\" with %d bytes", topic, tot_len );
    uint8_t slen = strlen(mSubTopic);
    if (! strncmp( topic, mSubTopic, slen )) {
        topic += slen;
        if (*topic == '/')
            ++topic;
        else
            topic = "-";
    }
    strncpy( mInTopic, topic, sizeof(mInTopic) - 1);
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
        mDataComplete = true;
        *end = 0;
        xSemaphoreGive( mSemaphore );
    }
}
