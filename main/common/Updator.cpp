/*
 * Updator.cpp
 */
//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "Updator.h"
#include "Indicator.h"
#include "BootCnt.h"
#include "WebServer.h"
#include "HttpParser.h"
#include "HttpHelper.h"
#include "HttpTable.h"

#include <string.h>             // memset()

#include <esp_http_client.h>   	// esp_http_client_config_t
#include <esp_ota_ops.h>   		// esp_ota_begin(), ...
#include <esp_system.h>   	    // esp_restart()
#include <esp_log.h>
#include <esp_flash_data_types.h> // esp_ota_select_entry_t, OTA_TEST_STAGE
#include <nvs.h>                // nvs_open(), ...


#if (LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG)
#define EXPRD(expr) do { expr; } while(0);
#else
#define EXPRD(expr)
#endif

namespace {
const char *const TAG            = "Updator";
const char *const s_nvsNamespace = "update";
const char *const s_keyUri       = "uri";
Updator           s_updator{};
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
    {
        const esp_partition_t* running = esp_ota_get_running_partition();
        const esp_partition_t* otadata = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);

        const uint8_t n = get_ota_partition_count();
        for (uint8_t i = 0; i < 2; ++i) {
            esp_ota_select_entry_t s;
            if (spi_flash_read(otadata->address + ((int) i * SPI_FLASH_SEC_SIZE), &s, sizeof(esp_ota_select_entry_t)) != ESP_OK)
                continue;

            ESP_LOGD( TAG, "read ota_select[%d]: ota_seq = 0x%02x (%% %d = %d; running 0x%02x), test_stage = 0x%08x",
                                            i, s.ota_seq, n, s.ota_seq % n, running->subtype, s.test_stage );
            uint8_t const lz = __builtin_clz(s.test_stage);
            bool const isMask = ((s.test_stage + 1) == (0x80000000 >> (lz - 1)));
            if (! (isMask
                && ((lz & 3) == OTA_TEST_STAGE_LZ_MOD4_TESTING)
                && ((s.ota_seq % n) == (running->subtype & PART_SUBTYPE_OTA_MASK))))
                continue;

            ESP_LOGI( TAG, "test of ota image %d in progress - set progress to \"test to be confirmed\"", s.ota_seq % n );
            mProgress = 95; // to confirm
            if (s.seq_label[0] == 0xff) {  // label not yet written
                const esp_app_desc_t *const desc = esp_ota_get_app_description();

                char * gitvers;
                uint32_t version = strtoul( desc->version, & gitvers, 10 );
                if (*gitvers == '-')
                    ++gitvers;

                size_t prjlen = strnlen( desc->project_name, 5 );
                memcpy( & s.seq_label[0], desc->project_name, prjlen );
                s.seq_label[prjlen++] = '-';

                size_t verslen = strnlen( gitvers, sizeof(desc->version) );
                if (verslen > (sizeof(s.seq_label) - (prjlen + 5)))
                    verslen = (sizeof(s.seq_label) - (prjlen + 5));
                memcpy( & s.seq_label[prjlen], gitvers, verslen );

                s.seq_label[prjlen + verslen] = 0;
                *((uint32_t *) & s.seq_label[sizeof(s.seq_label) - sizeof(uint32_t)]) = version;

                // ESP_LOGD( TAG, "proj./vers. %s/%s -> label: %s", desc->project_name, desc->version, s.seq_label );
                spi_flash_write( otadata->address + ((int) i * SPI_FLASH_SEC_SIZE) + offsetof(esp_ota_select_entry_t,seq_label),
                                 &s.seq_label[0], sizeof(s.seq_label) );
            }
            break;
        }
    }

    ReadUri();

    mSemaphore = xSemaphoreCreateBinary( );
    if (!mSemaphore) {
        ESP_LOGE( TAG, "xSemaphoreCreateBinary failed" );
        return false;
    }
    xTaskCreate( UpdatorTask, TAG, /*stack size*/4096, this, /*prio*/ 1, &mTaskHandle );
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

        const uint8_t n = get_ota_partition_count();
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
                    return true;
                }
            }
        }
    }
    if (mProgress != 100) {
        mMsg = "active partition not in testing stage (internal error)";
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
            Indicator::Instance().Pause(true);
            Update();
        } else if (mProgress == 96) {
            Indicator::Instance().Pause(false);
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

        BootCnt::Instance().Check();  // in case we erased boot counter page: write current counter as new base counter

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

///////////////// web interface /////////////////

extern "C" {

esp_err_t handler_get_reboot( httpd_req_t * req )
{
    s_updator.GetReboot( req );
    return ESP_OK;
}

esp_err_t handler_get_update( httpd_req_t * req )
{
    s_updator.GetUpdate( req );
    return ESP_OK;
}

esp_err_t handler_post_update( httpd_req_t * req )
{
    s_updator.PostUpdate( req );
    return ESP_OK;
}

esp_err_t handler_post_favicon( httpd_req_t * req )
{
    s_updator.PostFavicon( req );
    return ESP_OK;
}

}

namespace {
const char * const s_subUpdate  = "Firmware update";
const char * const s_subFavicon = "Favicon update";
const char * const s_subReboot  = "Restart device";

const httpd_uri_t uri_get_reboot    = { .uri = "/reboot",  .method = HTTP_GET,  .handler = handler_get_reboot,   .user_ctx = 0 };
const httpd_uri_t uri_get_update    = { .uri = "/update",  .method = HTTP_GET,  .handler = handler_get_update,   .user_ctx = 0 };
const httpd_uri_t uri_post_update   = { .uri = "/update",  .method = HTTP_POST, .handler = handler_post_update,  .user_ctx = 0 };
const httpd_uri_t uri_post_favicon  = { .uri = "/favicon", .method = HTTP_POST, .handler = handler_post_favicon, .user_ctx = 0 };
const WebServer::Page page_update     { uri_get_update, "Update" };
}

void Updator::AddPage( WebServer & webserver )
{
    webserver.AddUri(                 uri_get_reboot );
    webserver.AddPage( page_update, & uri_post_update );
    webserver.AddUri(                 uri_post_favicon );  // less important -> dropped in case out of max. uri
}

void Updator::PostFavicon( httpd_req_t * req )
{
    char type[16];
    char uri[80];
    HttpParser::Input in[] = { { "type", type, sizeof(type) },
                               { "img",  uri,  sizeof(uri)  } };
    HttpParser parser{ in, sizeof(in) / sizeof(in[0]) };

    HttpHelper hh{ req, s_subFavicon, "Update" };

    const char * parseError = parser.ParsePostData( req );
    if (parseError) {
        hh.Add( "parser error: " );
        hh.Add( parseError );
        return;
    }
    if (! (type[0] && uri[0])) {
        hh.Add( "incomplete form" );
        return;
    }

    esp_http_client_config_t config;
    memset( & config, 0, sizeof(config) );
    config.url = uri;
    esp_http_client_handle_t client = esp_http_client_init( & config );
    if (client == NULL) {
        hh.Add( "Failed to initialize HTTP connection" );
        return;
    }
    char * buf = 0;
    do { // while (0)
        esp_err_t e = esp_http_client_open( client, 0 );
        if (e != ESP_OK) {
            hh.Add( "Failed to open HTTP connection" );
            break;
        }
        int const totalLen = esp_http_client_fetch_headers( client );
        if (totalLen <= 0) {
            hh.Add( "Failed to fetch headers" );
            break;
        }
        int const status_code = esp_http_client_get_status_code( client );
        if (status_code != 200) {
            hh.Add( "Server response other than 200" );
            break;
        }
        if ((totalLen < 64) || (totalLen > 5 * 4096)) {
            hh.Add( "size not in 64B..20KB" );
            break;
        }
        buf = (char *) malloc( totalLen );
        if (! buf) {
            hh.Add( "Failed to allocate buffer" );
            break;
        }
        int remain = totalLen;
        do {
            int readLen = esp_http_client_read( client, &buf[totalLen - remain], remain);
            if (! readLen) {
                hh.Add( "no more data but uncomplete" );
                break;
            }
            remain -= readLen;
        } while (remain);
        if (remain) {
            break;
        }

        nvs_handle my_handle;
        if (nvs_open( "images", NVS_READWRITE, &my_handle ) != ESP_OK) {
            hh.Add( "cannot open NVS namespace" );
            break;
        }
        esp_err_t esp = nvs_set_str(  my_handle, "favicon-type", type );
        if (esp != ESP_OK) {
            hh.Add( "failed to store type" );
        } else {
            esp = nvs_set_blob( my_handle, "favicon", buf, totalLen );
            if (esp == ESP_OK)
                hh.Add( "failed to store image (size %d)", totalLen );
            else {
                nvs_commit( my_handle );
                hh.Add( "success" );
            }
        }
        nvs_close( my_handle );
    } while (0);

    if (buf)
        free( buf );

    esp_http_client_cleanup( client );
}


void Updator::GetUpdate( httpd_req_t * req )
{
    Updator &updator = Updator::Instance();
    uint8_t const progress = updator.Progress();

    HttpHelper hh{ req, s_subUpdate, "Update" };

    bool const editable = ((progress == 0) || (progress >= 99));
    bool const postable = (editable || (progress == 90) || (progress == 95));
    bool const showmsg  = ((! postable) || (progress == 99));

    if (! postable) {
        hh.Head( "<meta http-equiv=\"refresh\" content=\"1; URL=/update\">" );
    }

    if (editable) {
        Table<4,9> table;
        table[0][0] = "Idx";
        table[0][1] = "Partition";
        table[0][2] = "&nbsp;";
        table[0][3] = "Version";
        table[0][4] = "Label";
        table[0][5] = "Seq.";
        table[0][6] = "Test stage";
        table[0][7] = "Description";
        table[0][8] = "Address";

        table.Right(0);
        table.Right(1);
        table.Right(5);
        table.Right(6);
        table.Right(8);

        const esp_partition_t* running = esp_ota_get_running_partition();
        const esp_partition_t* otadata = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
        const uint8_t n = get_ota_partition_count();

        uint8_t                  actOtaIdx = 2;
        esp_ota_select_entry_t   s[2];
        esp_ota_select_entry_t * sel[2] = { 0, 0 };

        for (uint8_t i = 0; i < 2; ++i) {
            esp_err_t ret = spi_flash_read(otadata->address + (i * SPI_FLASH_SEC_SIZE), &s[i], sizeof(esp_ota_select_entry_t));
            if (ret != ESP_OK)
                memset( &s[i], 0xff, sizeof(esp_ota_select_entry_t) );
            else if ((s[i].ota_seq % n) == (running->subtype & PART_SUBTYPE_OTA_MASK)) {
                actOtaIdx = i;
                sel[0] = &s[i];
                sel[1] = &s[i^1];
            }
        }
        if (sel[0]) {
            table[1][0] = "[" + std::to_string((int)  actOtaIdx     ) + "]";
            table[2][0] = "[" + std::to_string((int) (actOtaIdx ^ 1)) + "]";
            table[1][1] = "active";
            table[2][1] = "inactive";
            table[1][8] = "0x" + HttpHelper::HexString( running->address, 1 );
        } else {
            table[1][0] = "[0]";
            table[2][0] = "[1]";
        }
        uint8_t switchable = 0;
        uint8_t test_stage[2] = {OTA_TEST_STAGE_LZ_MOD4_FAILED,OTA_TEST_STAGE_LZ_MOD4_FAILED};
        for (uint8_t i = 0; i < 2; ++i) {
            esp_ota_select_entry_t * sp = sel[i];
            if (! sp) {
                sp = & s[i];
                char x[2];
                x[1] = 0;
                x[0] = 'A' + (s[i].ota_seq % n);
                table[1+i][1] = x;
            }
            if (sp->seq_label[0] != 0xff)
            {
                uint32_t spVersion = *((uint32_t *) & sp->seq_label[sizeof(sp->seq_label) - sizeof(uint32_t)]);
                // sp->version is decimal MMmmppccbb major minor patch commit buildnum
                // show as M[.m[.p]][-c[-b]]
                uint32_t v = spVersion / 10000;
                std::string s{ std::to_string( v / 10000 ) };
                v %= 10000;
                if (v) {
                    s += "." + std::to_string( v / 100 );
                    v %= 100;
                    if (v) {
                        s += "." + std::to_string( v );
                    }
                }
                v = spVersion % 10000;
                if (v) {
                    s += "-" + std::to_string( v / 100 );
                    v %= 100;
                    if (v) {
                        s += "-" + std::to_string( v );
                    }
                }
                table[1+i][3] = s;
                table[1+i][4] = (char *) sp->seq_label;
            }
            table[1+i][5] = std::to_string( sp->ota_seq );

            table[1+i][6] = "0x" + HttpHelper::HexString( sp->test_stage, 1 );
            uint8_t const lz = __builtin_clz(sp->test_stage);
            bool const isMask = ((sp->test_stage + 1) == (0x80000000 >> (lz - 1)));
            if (isMask) {
                if (sp->test_stage > 0xf)
                    switchable |= 1 << i;
                test_stage[i] = (uint8_t) (lz & 3);
                switch (lz & 3)
                {
                    case OTA_TEST_STAGE_LZ_MOD4_TO_TEST: table[1+i][7] = "to test"; break;
                    case OTA_TEST_STAGE_LZ_MOD4_TESTING: table[1+i][7] = "testing"; break;
                    case OTA_TEST_STAGE_LZ_MOD4_FAILED:  table[1+i][7] = "failed";  break;
                    case OTA_TEST_STAGE_LZ_MOD4_PASSED:  table[1+i][7] = "passed";  break;
                }
            }
        }

        uint8_t idx = 2;
        uint8_t val = OTA_TEST_STAGE_LZ_MOD4_PASSED;
        // toggle active partition?
        if (sel[0] && switchable) {
            if (sel[0]->ota_seq > sel[1]->ota_seq) {  // active partition is newer
                // switchable, when test for other partition passed
                if ((switchable & 1) && (test_stage[1] == OTA_TEST_STAGE_LZ_MOD4_PASSED)) {
                    table[3][5] = "<button type=\"submit\">";
                    table[3][5] += "mark active as failed<br>to reactivate old image";
                    table[3][5] += "</button>";
                    table[3].Unite( 5 );
                    idx = actOtaIdx;
                    val = OTA_TEST_STAGE_LZ_MOD4_FAILED;
                }
            } else {  // active partition is older
                if (switchable & 2) {
                    table[3][5] = "<button type=\"submit\">";
                    table[3][5] += "retest newer image";
                    table[3][5] += "</button>";
                    table[3].Unite( 5 );
                    idx = actOtaIdx ^ 1;
                    val = OTA_TEST_STAGE_LZ_MOD4_TO_TEST;
                }
            }
        }

        hh.Add( "  <div style=\"float: right;\">\n" );
        if (val != OTA_TEST_STAGE_LZ_MOD4_PASSED) {
            hh.Add( "   <form method=\"get\" action=\"/reboot\">\n" );
            hh.Add( "    <input type=\"hidden\" name=\"idx\" value=\"" + std::to_string((int) idx) + "\" />" );
            hh.Add( "    <input type=\"hidden\" name=\"val\" value=\"" + std::to_string((int) val) + "\" />" );
        }
        hh.Add( "    <table>\n" );

        table.AddTo( hh, 1 );

        hh.Add( "    </table>\n" );
        if (val != OTA_TEST_STAGE_LZ_MOD4_PASSED)
            hh.Add( "   </form>\n" );
        hh.Add( "  </div>\n" );
    }

    hh.Add( "  <div>\n" );
    if (postable)
        hh.Add( "  <form method=\"post\">\n" );
    hh.Add( "  <table>\n" );
    {
        Table<4,4> table;
        table.Right( 0 );       // 1st column: right aligned
        table[0][1] = "&nbsp;"; // some padding

        table[0][0] = "uri:";
        if (editable)
            table[0][2] = "<input type=\"text\" name=\"uri\" maxlength=79"
                          " alt=\"setup a web server for download to the device\""
                          " value=\"";
        table[0][2] += updator.GetUri();
        if (editable)
            table[0][2] += "\">";
 
        table[1][0] = "status:";
        switch (progress)
        {
            case   0: table[1][2] = "ready to update";    break;
            case  90: table[1][2] = "wait for reboot";    break;
            case  95: table[1][2] = "test booting";       break;
            case  99: table[1][2] = "update failed";      break;
            case 100: table[1][2] = "update succeeded";   break;
            default:  table[1][2] = "update in progress"; break;
        }

        if (postable) {
            table[2][2] = "<button type=\"submit\">";
            switch (progress)
            {
                case   0: table[2][2] += "start update";         break;
                case  90: table[2][2] += "reboot";               break;
                case  95: table[2][2] += "confirm well booting"; break;
                case  99: table[2][2] += "retry update";         break;
                case 100: table[2][2] += "update again";         break;
            }
            table[2][2] += "</button>";
        } else {
            table[2][0] = "progress:";
            table[2][2] = "<progress max=\"100\" value=\"";
            char buf[8];
            sprintf( buf, "%d", progress );
            table[2][2] += buf;
            table[2][2] += "\" />";
        }

        if (showmsg) {
            const char * msg = updator.GetMsg();
            if (msg && *msg) {
                if (progress == 99)
                    table[3][0] = "last status message:";
                else
                    table[3][0] = "status message:";
                table[3][2] = msg;
            }
        }
        table.AddTo( hh );
    }
    hh.Add( "   </table>\n" );
    if (postable)
        hh.Add( "  </form>\n" );
    hh.Add( "  </div>\n" );
    hh.Add( "  <div style=\"clear: both\"></div>\n" );

    if (editable) {
        hh.Add( "  <br /><br />\n"
                " <h2>Favicon update</h2>\n"
                "  <form method=\"post\" action=\"/favicon\">\n"
                "   <table>\n" );
        {
            Table<3,4> table;
            table.Right( 0 );
            table[0][1] = "&nbsp;";

            table[0][0] = "type:";
            table[0][2] = "<input type=\"text\" name=\"type\" maxlength=16 value=\"image/x-icon\">";
            table[0][3] = "(... or image/gif etc.)";

            table[1][0] = "image URI:";
            table[1][2] = "<input type=\"text\" name=\"img\" maxlength=80";
            table[1][3] = "(where to get the image)";

            table[2][2] = "<button type=\"submit\">favicon update</button>";

            char const * strFw = updator.GetUri();
            if (*strFw) {
                table[1][2] += " value=\"";
                int lenFw = strlen( strFw );
                if (strcmp( strFw + lenFw - 4, ".bin" ) == 0)
                    lenFw -= 4;
                char const * build = strstr( strFw, "/build/" );
                if (build) {
                    const int len0 = build + 1 - strFw;  // incl. leading /
                    const int off  = len0 + 5;           // incl. trailing /
                    const int len1 = lenFw - off;
                    table[1][2].append( strFw, len0 );
                    table[1][2].append( "images" );
                    table[1][2].append( strFw + off, len1 );
                } else {
                    table[1][2].append( strFw, lenFw );
                }
                table[1][2] += ".ico\"";
            }
            table[1][2] += ">";

            table.AddTo( hh );
        }
        hh.Add( "   </table>\n"
                "  </form>\n" );
    }

    if (progress != 90) {
        hh.Add( "  <br /><br />\n"
                " <h2>Restart device</h2>\n"
                "  <form method=\"get\" action=\"/reboot\">\n"
                "   <button type=\"submit\">reboot</button>\n"
                "  </form>\n" );
        if (progress == 95)
            hh.Add( " (when reboot in another way than by \"confirm well booting\","
                    " the system will fallback to the previous partition)\n" );
    }
}

void Updator::PostUpdate( httpd_req_t * req )
{
    ESP_LOGD( TAG, "handler_post_update enter" ); EXPRD(vTaskDelay(1))

    Updator &updator = Updator::Instance();
    uint8_t progress = updator.Progress();

    if (progress == 90) {
        {
            HttpHelper hh{ req, s_subUpdate, "Update" };
            hh.Head( "<meta http-equiv=\"refresh\" content=\"5; URL=/update\">" );
            hh.Add( "  <h3>system will reboot...</h3>\n"
                    "  <br />\n"
                    "  <br />This page should become refreshed automatically, when system is up again.\n"
                    "  <br />\n"
                    "  <br />When the device does not boot properly, power off and on.\n"
                    "  <br />This will again activate the old image as fallback image.\n"
                    "  <br />\n"
                    "  <br />On proper boot up, you have to confirm this by the button\n"
                    "  <br />shown in this sub page after the page has been refreshed.\n" );
        }
        vTaskDelay( configTICK_RATE_HZ / 4 );  // give http stack a chance to send out
        esp_restart();
    }

    HttpHelper hh{ req, s_subUpdate, "Update" };

    if ((progress > 0) && (progress < 99)) {
        if (progress == 95)
            updator.Confirm();

        hh.Head( "<meta http-equiv=\"refresh\" content=\"1; URL=/update\">" );
        return;
    }

    char uri[80];
    HttpParser::Input in[] = { {"uri",uri,sizeof(uri)} };
    HttpParser parser{ in, sizeof(in) / sizeof(in[0]) };

    const char * parseError = parser.ParsePostData( req );
    if (parseError) {
        hh.Add( "parser error: " );
        hh.Add( parseError );
        return;
    }
    const char * err = 0;
    if (! uri[0])
        err = "no uri specified";
    else if (strcmp( updator.GetUri(), uri )) {
        if (! updator.SetUri( uri ))
            err = "failed to store new uri";
    }
    if (err) {
        hh.Add( "  <form method=\"post\">\n" // enctype=\"multipart/form-data\"
                "   <table>\n"
                "    <tr><td align=\"right\">uri:</td><td>&nbsp;</td>\n"
                "     <td><input type=\"text\" name=\"uri\" value=\"" );
        hh.Add( uri );
        hh.Add( "\" maxlength=79 alt=\"setup a web server for download to the device\"></td></tr>\n"
                "    <tr><td /><td /><td><button type=\"submit\">try again</button></td></tr>\n"
                "    <tr><td align=\"right\">status:</td><td /><td>" );
        hh.Add( err );
        hh.Add( "</td></tr>\n"
                "   </table>\n" );
        return;
    }

    hh.Head( "<meta http-equiv=\"refresh\" content=\"1; URL=/update\">" );
    hh.Add( "   <table>\n"
            "    <tr><td align=\"right\">uri:</td><td>&nbsp;</td>\n"
            "     <td>" );
    hh.Add( updator.GetUri() );
    hh.Add( "</td></tr>\n"
            "    <tr><td align=\"right\">status:</td><td /><td>triggering update</td></tr>\n"
            "    <tr><td align=\"right\">progress:</td><td /><td><progress value=\"0\" max=\"100\"></progress></td></tr>\n"
            "   </table>\n" );

    updator.Go();
}

void Updator::GetReboot( httpd_req_t * req )
{
    {   // will send when hh will loose scope

        HttpHelper hh{ req, s_subReboot, "Update" };
        hh.Head( "<meta http-equiv=\"refresh\" content=\"5; URL=/\">" );
        hh.Add( "This device will reboot - you will get redirected in 5 secs." );

        {  // re-test or fallback?
            char idx[4];
            char val[4];
            HttpParser::Input in[] = { {"idx",idx,sizeof(idx)},
                                       {"val",val,sizeof(val)} };
            HttpParser parser{ in, sizeof(in) / sizeof(in[0]) };

            const char * parseError = parser.ParseUriParam( req );
            if (parseError) {
                hh.Add( "<br /><br />\n" );
                hh.Add( "parser error: " );
                hh.Add( parseError );
                return;
            }

            if (idx[0] && val[0]) {
                hh.Add( "<br /><br />\n" );
                const esp_partition_t * otadata = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
                const uint8_t i = idx[0] & 1;
                const uint8_t newstage = val[0] & 3;
                const uint32_t addr = otadata->address + ((int) i * SPI_FLASH_SEC_SIZE) + offsetof(esp_ota_select_entry_t,test_stage);
                uint32_t test_stage;
                if (spi_flash_read( addr, &test_stage, sizeof(test_stage) ) == ESP_OK) {
                    const uint8_t lz = __builtin_clz(test_stage);
                    const uint8_t shift = ((32 + newstage) - lz) & 3;
                    if (shift) {
                        if ((lz+shift)<=32) {
                            test_stage >>= shift;
                            spi_flash_write( addr, &test_stage, sizeof(test_stage) );
                            hh.Add( "test stage adopted\n" );
                        } else
                            hh.Add( "cannot adopt test stage - too less free bits\n" );
                    } else
                        hh.Add( "test stage already set as requested\n" );
                } else
                    hh.Add( "read error while reading test stage at address %#x\n", addr );
            }
        }
    }

    vTaskDelay( configTICK_RATE_HZ / 10 );

    esp_restart();

    // no return - on error:
    ESP_LOGE( TAG, "esp_restart returned" );
}
