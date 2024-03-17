/*
 * BootCnt.cpp
 */
//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "BootCnt.h"

#include <esp_log.h>
#include <esp_ota_ops.h>   		    // esp_ota_begin(), ...
#include <esp_flash_data_types.h>   // esp_ota_select_entry_t

/*
 * using 1st ota data entry:
 *       +------------+------------+------------+------------+
 *     0 | ota_seq    |        seq_label                     |
 *       +------------+------------+------------+------------+
 *  0x10 |         \0 | version    | test_stage | crc        |
 *       +------------+------------+------------+------------+
 *  0x20 | baseCnt    | bitmask0   |                         |
 *       +------------+------------+                         |
 *       |                                                   |
 *       |                                                   |
 *       |                                                   |
 *       |                 bitmask1[0..32]                   |
 *       |                                                   |
 *       |                                                   |
 *       |                                       ____________|
 *       |                                      |            |
 *       +------------+------------+------------+            |
 *  0xb0 |                                                   |
 *       |                                                   |
 *       |                                                   |
 *       |                bitmask2[0..1088]                  |
 *       |                                                   |
 *                                ...
 *       |                                                   |
 *       |                                                   |
 *       |                                                   |
 * 0xfff |                                                   |
 *       +------------+------------+------------+------------+
 */

namespace {
constexpr const char * const TAG = "BootCnt";
BootCnt                      s_bootCnt;
}

BootCnt & BootCnt::Instance()
{
    return s_bootCnt;
}

void BootCnt::Init()
{
    if (mCnt) {
        ESP_LOGE( TAG, "counter already set" );
        return;
    }
    const esp_partition_t * otadata = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (! otadata) {
        ESP_LOGE( TAG, "can't find data partition \"ota\"" );
        return;
    }

    constexpr size_t szEnt   = sizeof(esp_ota_select_entry_t);
    constexpr size_t szMask  = sizeof(uint32_t);
    constexpr size_t nofLvl2 = ((0x1000 - szEnt) / szMask) - 35;  // 981: maximum number of bitmasks in level 2
    /*
     * struct ota_page {
     *     esp_ota_select_entry_t  select;
     *     uint32_t                baseCnt;
     *     uint32_t                bitmask0;
     *     uint32_t                bitmask1[33];
     *     uint32_t                bitmask2[nofLvl2];
     * };
     */

    uint32_t baseCnt;
    if (spi_flash_read( otadata->address + szEnt, &baseCnt, szMask) != ESP_OK) {
        ESP_LOGE( TAG, "can't read base counter" );
        return;
    }
    if (baseCnt == 0xffffffff)
        baseCnt = 0;

    size_t const base[3] = { otadata->address + szEnt + szMask,
                             otadata->address + szEnt + szMask * 2,
                             otadata->address + szEnt + szMask * 35 };
    u_long   idx = 0;
    int      lz[3];
    size_t   off[4];
    u_char   level;
    uint32_t bitmask;

    off[0] = 0;
    for (level = 0; level < 3; ++level) {
        if (spi_flash_read(base[level] + off[level], &bitmask, szMask) != ESP_OK) {
            ESP_LOGE( TAG, "can't read bitmask %d at %#x", level, base[level] + off[level] );
            return;
        }
        lz[level] = __builtin_clz( bitmask );
        idx = (idx * 33) + lz[level];
        off[level + 1] = idx * szMask;  // off[3] is not for interest

        if ((level == 1) && (idx >= nofLvl2)) {  // base[2]+off[2] is beyond page
            ESP_LOGW( TAG, "maximum boot counter reached" );
            mCnt = baseCnt + (idx * 33) + 1;  // lz[3] unknown -> 0
            return;
        }
    }

    mCnt = baseCnt + idx + 1;

    do
        --level;
    while (level && (lz[level] == 32));

    bitmask = 0x7fffffff >> lz[level];
    if (spi_flash_write(base[level] + off[level], &bitmask, szMask) != ESP_OK) {
        ESP_LOGE( TAG, "can't write bitmask %d: %#x at %#x", level, bitmask, base[level] + off[level] );
        return;
    }
    ESP_LOGD( TAG, "wrote bitmask %d: %#x at %#x", level, bitmask, base[level] + off[level] );
}

void BootCnt::Check() const
{
    if (! mCnt) {
        ESP_LOGE( TAG, "counter not set" );
        return;
    }
    const esp_partition_t * otadata = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (! otadata) {
        ESP_LOGE( TAG, "can't find data partition \"ota\"" );
        return;
    }

    constexpr size_t szMask = sizeof(uint32_t);
    uint32_t baseCnt;

    size_t base;
    base = otadata->address + sizeof(esp_ota_select_entry_t);
    if (spi_flash_read(base, &baseCnt, szMask) != ESP_OK) {
        ESP_LOGE( TAG, "can't read base counter" );
        return;
    }
    if (baseCnt != 0xffffffff) {
        ESP_LOGD( TAG, "boot counter not changed" );
        return;
    }
    baseCnt = mCnt;
    if (spi_flash_write(base, &baseCnt, szMask) != ESP_OK) {
        ESP_LOGE( TAG, "can't restore boot counter" );
        return;
    }
    ESP_LOGD( TAG, "boot counter set" );
}
