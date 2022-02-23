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
#include "nvs.h"                // nvs_open(), ...

namespace {
    const char *const TAG            = "Updator";
    const char *const s_nvsNamespace = "update";
    const char *const s_keyUri       = "uri";
    Updator s_updator{};
};

extern "C" void UpdatorTask( void * updator )
{
    ((Updator *) updator)->Run();
}

Updator& Updator::Instance()
{
    return s_updator;
}

bool Updator::Init()
{
    ReadUri();

    mSemaphore = xSemaphoreCreateBinary( );
    if (!mSemaphore) {
        ESP_LOGE( TAG, "xSemaphoreCreateBinary failed" );
        return false;
    }
    xTaskCreate( UpdatorTask, TAG, /*stack size*/4096, this,
                 /*prio*/ 1, &mTaskHandle );
    if (!mTaskHandle) {
        ESP_LOGE( TAG, "xTaskCreate failed" );
        return false;
    }

    return true;
}

void Updator::ReadUri()
{
    ESP_LOGI( TAG, "Reading Updator configuration" );

    nvs_handle my_handle;
    if (nvs_open( s_nvsNamespace, NVS_READONLY, &my_handle ) == ESP_OK) {
        ESP_LOGD( TAG, "Reading URI" );
        size_t len = sizeof(mUri);
        nvs_get_str( my_handle, s_keyUri, mUri, &len );

        nvs_close( my_handle );
    }
    if (!mUri[0]) {
        ESP_LOGI( TAG, "URI not set" );
    } else {
        ESP_LOGI( TAG, "URI \"%s\"", mUri );
    }
}

bool Updator::SetUri( const char * uri )
{
    if (!*uri) {
        ESP_LOGE( TAG, "no uri speified" );
        return false;
    }
    if ((mProgress > 0) && (mProgress < 99)) {
        ESP_LOGE( TAG, "Update in progress - uri cannot be changed" );
        return false;
    }
    nvs_handle my_handle;
    if (nvs_open( s_nvsNamespace, NVS_READWRITE, &my_handle ) != ESP_OK)
        return false;
    esp_err_t esp = nvs_set_str( my_handle, s_keyUri, uri );
    nvs_close( my_handle );

    if (esp == ESP_OK) {
        strncpy( mUri, uri, sizeof(mUri) - 1 );
    }
    return esp == ESP_OK;
}

bool Updator::Go( void )
{
    if (!mUri[0]) {
        ESP_LOGE( TAG, "no uri specified" );
        return false;
    }
    if ((mProgress > 0) && (mProgress < 99)) {
        ESP_LOGE( TAG, "Updator already in progress - cannot start" );
        return false;
    }
    mProgress = 1;
    xSemaphoreGive( mSemaphore );
    return true;
}

bool Updator::Confirm( void )
{
    if (mProgress != 95) {
        ESP_LOGE( TAG, "Updator not in confirmation status" );
        return false;
    }
    mProgress = 96;
    xSemaphoreGive( mSemaphore );
    return true;
}

void Updator::Run( void )
{
    while (1) {
        xSemaphoreTake( mSemaphore, portMAX_DELAY );
        if (mProgress == 1) {
            mProgress = 2;
            Update();
        } else if (mProgress == 96) {
            mProgress = 97;
            // TODO: mark stable
            mProgress = 100;
        }
    }
}

void Updator::Update( void )
{
    esp_http_client_config_t config;
    memset( & config, 0, sizeof(config) );
    config.url = GetUri();

    mProgress = 3;
    char *upgrade_data_buf = (char *) malloc( CONFIG_OTA_BUF_SIZE );
    if (!upgrade_data_buf) {
        ESP_LOGE(TAG, "Couldn't allocate memory to upgrade data buffer");
        return;
    }

    mProgress = 4;
    esp_http_client_handle_t client = esp_http_client_init( & config );
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP connection");
        free( upgrade_data_buf );
        return;
    }

    do { // while(0)
        mProgress = 5;
        esp_err_t e = esp_http_client_open( client, 0 );
        if (e != ESP_OK) {
            ESP_LOGE( TAG, "Failed to open HTTP connection: %d", e );
            break;
        }
        mProgress = 6;
        int const totalLen = esp_http_client_fetch_headers( client );
        if (totalLen <= 0) {
            ESP_LOGE( TAG, "Failed to fetch headers: len = %d", totalLen );
            break;
        }

        ESP_LOGI(TAG, "Starting OTA...");
        const esp_partition_t *update_partition = NULL;
        mProgress = 7;
        update_partition = esp_ota_get_next_update_partition(NULL);
        if (update_partition == NULL) {
            ESP_LOGE(TAG, "Passive OTA partition not found");
            break;
        }
        ESP_LOGI( TAG, "Writing to partition subtype %d at offset 0x%x",
                  update_partition->subtype, update_partition->address );

        esp_ota_handle_t update_handle = 0;
        mProgress = 8;
        e = esp_ota_begin( update_partition, OTA_SIZE_UNKNOWN, &update_handle );
        if (e != ESP_OK) {
            ESP_LOGE( TAG, "esp_ota_begin failed, error=%d", e );
            break;
        }
        ESP_LOGI( TAG, "esp_ota_begin succeeded" );
        ESP_LOGI( TAG, "Please Wait. This may take time" );
        int binary_file_len = 0;
        mProgress = 9;
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
                mProgress = 10 + (binary_file_len * 80 + totalLen/2) / totalLen;
            }
        }
        ESP_LOGD( TAG, "Total binary data length writen: %d", binary_file_len );

        mProgress = 91;
        esp_err_t e2 = esp_ota_end( update_handle );
        if (e2 != ESP_OK) {
            ESP_LOGE( TAG, "Error: esp_ota_end failed! err=0x%d. Image is invalid", e2 );
            break;
        }
        mProgress = 92;
        if (e != ESP_OK) {
            break;
        }

        mProgress = 93;
        e = esp_ota_set_boot_partition( update_partition );
        if (e != ESP_OK) {
            ESP_LOGE( TAG, "esp_ota_set_boot_partition failed! err=0x%d", e );
            break;
        }
        ESP_LOGI(TAG, "esp_ota_set_boot_partition succeeded"); 
        // TODO: 94: mark boot image "to test"
        // 95: halt update to confirm -> Confirm 90->91
        mProgress = 95;
    } while(0);

    free( upgrade_data_buf );
    esp_http_client_cleanup( client );

    if (mProgress == 95) {
        esp_restart();
        // on next boot cycle:
        // bootloader:  case boot image "to test":
        //                  mark boot image "testing"
        //                  boot new image
        // program: 95->96: manually by web interface: mark boot image "normal"
        //             100: finished
        // if 98 no done and system reboots: (power cycle reboot)
        // bootloader:  case boot image "testing":
        //                  esp_ota_set_boot_partition = old image (fallback)
        //                  mark boot image "normal"
        //                  boot old image
    } else {
        mProgress = 99;  // enables retry
    }
}
