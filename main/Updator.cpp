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
#include "esp_flash_data_types.h" // esp_ota_select_entry_t, OTA_TEST_STAGE
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

    {
        const esp_partition_t* running = esp_ota_get_running_partition();
        const esp_partition_t* otadata = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);

        uint8_t n = get_ota_partition_count();
        for (uint8_t i = 0; i < 2; ++i) {
            esp_ota_select_entry_t s;
            if (spi_flash_read(otadata->address + ((int) i * SPI_FLASH_SEC_SIZE), &s, sizeof(esp_ota_select_entry_t)) == ESP_OK) {
                ESP_LOGD( TAG, "read ota_select[%d]: ota_seq = 0x%02x (%% %d = %d; running 0x%02x), test_stage = 0x%08x",
                                                i, s.ota_seq, n, s.ota_seq % n, running->subtype, s.test_stage );
                uint8_t const lz = __builtin_clz(s.test_stage);
                bool const isMask = ((s.test_stage + 1) == (0x80000000 >> (lz - 1)));
                if (isMask
                    && ((lz & 3) == OTA_TEST_STAGE_LZ_MOD4_TESTING)
                    && ((s.ota_seq % n) == (running->subtype & PART_SUBTYPE_OTA_MASK))) {
                    ESP_LOGI( TAG, "test of ota image %d in progress - set progress to \"test to be confirmed\"", s.ota_seq % n );
                    mProgress = 95; // to confirm
                    return true;
                }
            }
        }
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
    nvs_commit( my_handle );
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
    mMsg = "trigger";
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

    {
        const esp_partition_t* running = esp_ota_get_running_partition();
        const esp_partition_t* otadata = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);

        uint8_t n = get_ota_partition_count();
        for (uint8_t i = 0; i < 2; ++i) {
            esp_ota_select_entry_t s;
            if (spi_flash_read(otadata->address + ((int) i * SPI_FLASH_SEC_SIZE), &s, sizeof(esp_ota_select_entry_t)) == ESP_OK) {
                ESP_LOGD( TAG, "read ota_select[%d]: ota_seq = 0x%02x (%% %d = %d; running 0x%02x), test_stage = 0x%08x",
                                                i, s.ota_seq, n, s.ota_seq % n, running->subtype, s.test_stage );

                uint8_t const lz = __builtin_clz(s.test_stage);
                bool const isMask = ((s.test_stage + 1) == (0x80000000 >> (lz - 1)));
                if (isMask
                    && ((lz & 3) == OTA_TEST_STAGE_LZ_MOD4_TESTING)
                    && ((s.ota_seq % n) == (running->subtype & PART_SUBTYPE_OTA_MASK))) {
                    // test succeeded!
                    ESP_LOGI( TAG, "successful test confirmation - set test_stage \"passed\" and progress \"ready\"" );
                    s.test_stage >>= 2;  // in difference to testing -> failed (>>=1) we shift by 2: testing -> passed
                    spi_flash_write( otadata->address + ((int) i * SPI_FLASH_SEC_SIZE) + offsetof(esp_ota_select_entry_t,test_stage),
                                     &s.test_stage, sizeof(s.test_stage) );
                    mProgress = 100;
                }
            }
        }
    }
    if (mProgress != 100) {
        mMsg = "no testing image anymore in otadata partition";
        mProgress = 99;  // failed
    }
    return true;
}

void Updator::Run( void )
{
    while (1) {
        xSemaphoreTake( mSemaphore, portMAX_DELAY );
        if (mProgress == 1) {
            mProgress = 2;
            mMsg = "start update";
            Update();
        } else if (mProgress == 96) {
            mProgress = 97;
            mMsg = "manifest new image";
            // TODO: mark stable
            mProgress = 100;
            mMsg = "update succeeded";
        }
    }
}

void Updator::Update( void )
{
    esp_http_client_config_t config;
    memset( & config, 0, sizeof(config) );
    config.url = GetUri();

    mProgress = 3;
    mMsg = "malloc";
    char *upgrade_data_buf = (char *) malloc( CONFIG_OTA_BUF_SIZE );
    if (!upgrade_data_buf) {
        ESP_LOGE(TAG, "Couldn't allocate memory to upgrade data buffer");
        return;
    }

    mProgress = 4;
    mMsg = "http client init";
    esp_http_client_handle_t client = esp_http_client_init( & config );
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP connection");
        free( upgrade_data_buf );
        return;
    }

    do { // while(0)
        mProgress = 5;
        mMsg = "http client open";
        esp_err_t e = esp_http_client_open( client, 0 );
        if (e != ESP_OK) {
            ESP_LOGE( TAG, "Failed to open HTTP connection: %d", e );
            break;
        }
        mProgress = 6;
        mMsg = "http client fetch headers";
        int const totalLen = esp_http_client_fetch_headers( client );
        if (totalLen <= 0) {
            ESP_LOGE( TAG, "Failed to fetch headers: len = %d", totalLen );
            break;
        }

        ESP_LOGI(TAG, "Starting OTA...");
        const esp_partition_t *update_partition = NULL;
        mProgress = 7;
        mMsg = "get next partition";
        update_partition = esp_ota_get_next_update_partition(NULL);
        if (update_partition == NULL) {
            ESP_LOGE(TAG, "Passive OTA partition not found");
            break;
        }
        ESP_LOGI( TAG, "Writing to partition subtype %d at offset 0x%x",
                  update_partition->subtype, update_partition->address );

        esp_ota_handle_t update_handle = 0;
        mProgress = 8;
        mMsg = "flash erase";
        e = esp_ota_begin( update_partition, OTA_SIZE_UNKNOWN, &update_handle );
        if (e != ESP_OK) {
            ESP_LOGE( TAG, "esp_ota_begin failed, error=%d", e );
            break;
        }
        ESP_LOGI( TAG, "esp_ota_begin succeeded" );
        ESP_LOGI( TAG, "Please Wait. This may take time" );
        int data_written = 0;
        mProgress = 9;
        mMsg = "loop on http read and flash write";
        const char * lastAction;
        while (1) {
            lastAction = "http client read";
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
                lastAction = "flash write";
                e = esp_ota_write( update_handle, (const void *)upgrade_data_buf, data_read );
                if (e != ESP_OK) {
                    ESP_LOGE( TAG, "Error: esp_ota_write failed! err=0x%d", e );
                    break;
                }
                data_written += data_read;
                ESP_LOGD( TAG, "Written image length %d", data_written );
                mProgress = 10 + (data_written * 76 + totalLen/2) / totalLen;
            }
        }
        ESP_LOGD( TAG, "Total binary data length writen: %d", data_written );

        mProgress = 87;
        mMsg = "flash finalize";
        esp_err_t e2 = esp_ota_end( update_handle );
        if (e2 != ESP_OK) {
            ESP_LOGE( TAG, "Error: esp_ota_end failed! err=0x%d. Image is invalid", e2 );
            break;
        }
        mProgress = 88;
        if (e != ESP_OK) {
            mMsg = lastAction;
            break;
        }

        mProgress = 89;
        mMsg = "set boot partition";
        e = esp_ota_set_boot_partition( update_partition );
        if (e != ESP_OK) {
            ESP_LOGE( TAG, "esp_ota_set_boot_partition failed! err=0x%d", e );
            break;
        }
        ESP_LOGI(TAG, "esp_ota_set_boot_partition succeeded"); 
        mMsg = "mark boot partition 'to test'";
        // 90: halt update to confirm reboot
        mProgress = 90;
        mMsg = "halt to confirm";
    } while(0);

    free( upgrade_data_buf );
    esp_http_client_cleanup( client );

    if (mProgress != 90) {
        mProgress = 99;  // enables retry
    }

    // esp_restart();
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
}
