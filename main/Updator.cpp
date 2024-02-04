/*
 * Updator.cpp
 *
 *  Created on: 29.04.2020
 *      Author: galuschka
 */

//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "Updator.h"

#include <string.h>             // memset()

#include "esp_http_client.h"   	// esp_http_client_config_t
#include "esp_ota_ops.h"   		// esp_ota_begin(), ...
#include "esp_system.h"   	    // esp_restart()

#include "esp_log.h"

const char *const TAG = "Updator";

extern "C" void UpdatorTask( void * updator )
{
    ((Updator *) updator)->Run();
}

Updator::Updator() :
        mGo { false },
        mUrl { 0 },
        mTaskHandle { 0 },
        mSemaphore { 0 }
{
}

static Updator s_updator{};

Updator& Updator::Instance()
{
    return s_updator;
}

bool Updator::Init( const char * url )
{
    mUrl = url;
    mSemaphore = xSemaphoreCreateBinary( );
    if (!mSemaphore) {
        ESP_LOGE( TAG, "xSemaphoreCreateBinary failed" );
        return false;
    }

    xTaskCreate( UpdatorTask, "Updator", /*stack size*/4096, this,
                 /*prio*/ 1, &mTaskHandle );
    if (!mTaskHandle) {
        ESP_LOGE( TAG, "xTaskCreate failed" );
        return false;
    }

    return true;
}

void Updator::Go( void )
{
    mGo = true;
    xSemaphoreGive( mSemaphore );
}

void Updator::Run( void )
{
    while (1) {
        xSemaphoreTake( mSemaphore, portMAX_DELAY );
        if (mGo) {
            mGo = false;
            Update();
        }
    }
}

void Updator::Update( void )
{
    esp_http_client_config_t config;
    memset( & config, 0, sizeof(config) );
    config.url = Url();

    char *upgrade_data_buf = (char *) malloc( CONFIG_OTA_BUF_SIZE );
    if (!upgrade_data_buf) {
        ESP_LOGE(TAG, "Couldn't allocate memory to upgrade data buffer");
        return;
    }

    esp_http_client_handle_t client = esp_http_client_init( & config );
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP connection");
        free( upgrade_data_buf );
        return;
    }

    esp_err_t succ = ESP_FAIL;
    do { // while(0)
        esp_err_t e = esp_http_client_open( client, 0 );
        if (e != ESP_OK) {
            ESP_LOGE( TAG, "Failed to open HTTP connection: %d", e );
            break;
        }
        int len = esp_http_client_fetch_headers( client );
        if (len <= 0) {
            ESP_LOGE( TAG, "Failed to fetch headers: len = %d", len );
            break;
        }

        ESP_LOGI(TAG, "Starting OTA...");
        const esp_partition_t *update_partition = NULL;
        update_partition = esp_ota_get_next_update_partition(NULL);
        if (update_partition == NULL) {
            ESP_LOGE(TAG, "Passive OTA partition not found");
            break;
        }
        ESP_LOGI( TAG, "Writing to partition subtype %d at offset 0x%x",
                  update_partition->subtype, update_partition->address );

        esp_ota_handle_t update_handle = 0;
        e = esp_ota_begin( update_partition, OTA_SIZE_UNKNOWN, &update_handle );
        if (e != ESP_OK) {
            ESP_LOGE( TAG, "esp_ota_begin failed, error=%d", e );
            break;
        }
        ESP_LOGI( TAG, "esp_ota_begin succeeded" );
        ESP_LOGI( TAG, "Please Wait. This may take time" );
        int binary_file_len = 0;
        while (1) {
            int data_read = esp_http_client_read( client, upgrade_data_buf, CONFIG_OTA_BUF_SIZE );
            if (data_read == 0) {
                ESP_LOGI( TAG, "Connection closed - all data received" );
                e = ESP_OK;
                break;
            }
            if (data_read < 0) {
                ESP_LOGE( TAG, "Error: SSL data read error" );
                e = ESP_FAIL;
                break;
            }
            if (data_read > 0) {
                e = esp_ota_write( update_handle, (const void *)upgrade_data_buf, data_read );
                if (e != ESP_OK) {
                    ESP_LOGE( TAG, "Error: esp_ota_write failed! err=0x%d", e );
                    break;
                }
                binary_file_len += data_read;
                ESP_LOGD( TAG, "Written image length %d", binary_file_len );
            }
        }
        ESP_LOGD( TAG, "Total binary data length writen: %d", binary_file_len );

        esp_err_t e2 = esp_ota_end( update_handle );
        if (e2 != ESP_OK) {
            ESP_LOGE( TAG, "Error: esp_ota_end failed! err=0x%d. Image is invalid", e2 );
            break;
        }
        if (e != ESP_OK)
            break;

        e = esp_ota_set_boot_partition( update_partition );
        if (e != ESP_OK) {
            ESP_LOGE( TAG, "esp_ota_set_boot_partition failed! err=0x%d", e );
            break;
        }
        ESP_LOGI(TAG, "esp_ota_set_boot_partition succeeded"); 
        succ = ESP_OK;
    } while(0);

    free( upgrade_data_buf );
    esp_http_client_cleanup( client );

    if (succ == ESP_OK) {
        esp_restart();
    }
}
