/*
 * rgb.cpp
 * listening for mqtt message and call Fade() with appropriate parameters
 */
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

/* what we get from domoticz:

    dz/cg/dn/344 {
        "Battery" : 255,
        "Color" : 
        {
            "b" : 145,      <- blue
            "cw" : 0,
            "g" : 255,      <- green
            "m" : 3,        <- ColorModeRGB / Valid fields: r, g, b
            "r" : 245,      <- red
            "t" : 0,        <- temperature
            "ww" : 0
        },
        "LastUpdate" : "2024-03-06 11:11:18",
        "Level" : 55,               <- dimm percentage
        "RSSI" : 12,
        "description" : "",
        "dtype" : "Color Switch",
        "hwid" : "7",
        "id" : "00082344",
        "idx" : 344,
        "name" : "RGB-Test",
        "nvalue" : 15,              <- 0=off 14=full light, 15=on, 20=night light
        "org_hwid" : "7",
        "stype" : "RGB",
        "svalue1" : "55",           <- dimm percentage
        "switchType" : "Dimmer",
        "unit" : 1
    }
*/

#include "rgb.h"
#include "Json.h"
#include "Fader.h"
#include "Mqtinator.h"
#include "HttpHelper.h"

#include <esp_log.h>

#if 0
// from https://www.domoticz.com/wiki/Domoticz_API/JSON_URL%27s
namespace dz
{
    enum ColorMode {
        ColorModeNone   = 0,  // Illegal
        ColorModeWhite  = 1,  // White. Valid fields: none
        ColorModeTemp   = 2,  // White with color temperature. Valid fields: t
        ColorModeRGB    = 3,  // Color. Valid fields: r, g, b.
        ColorModeCustom = 4,  // Custom (color + white). Valid fields: r, g, b, cw, ww, depending on device capabilities

        ColorModeLast = ColorModeCustom,
    };

    struct Color {
        ColorMode m;
        uint8_t   r;   // Range:0..255, Red level
        uint8_t   g;   // Range:0..255, Green level
        uint8_t   b;   // Range:0..255, Blue level

        uint8_t   t;   // Range:0..255, Color temperature (warm / cold ratio, 0 is coldest, 255 is warmest)
        uint8_t   cw;  // Range:0..255, Cold white level
        uint8_t   ww;  // Range:0..255, Warm white level (also used as level for monochrome white)
    };
} // namespace dz
#endif

const char * TAG = "RGB";

namespace {
    RGB * s_rgb;
}

extern "C" {

void on_mqtt_input( const char * topic, const char * data )
{
    ESP_LOGI( TAG, "got \"%s\" \"%.16s...\" (%d bytes)", topic, data, strlen(data) );

    if (s_rgb)
        s_rgb->Subscription( topic, data );
}

void RgbTask( void * rgb )
{
    ((RGB *) rgb)->Run();
}

}

RGB::RGB( Fader & fader, uint16_t dzDevIdx )
    : mFader { fader }
    , mDevIdx { dzDevIdx }
{
    s_rgb = this;
}

bool RGB::Start()
{
    xTaskCreate( RgbTask, TAG, /*stack size*/4096, this, /*prio*/ 1, &mTaskHandle );
    if (!mTaskHandle) {
        ESP_LOGE( TAG, "xTaskCreate failed" );
        return false;
    }
    return true;
}

void RGB::Subscription( const char * topic, const char * data )
{
    mTopic = topic;
    mData  = data;
    xTaskNotify( mTaskHandle, 1, eSetBits );
}

void RGB::Run()
{
    Mqtinator & mqtinator = Mqtinator::Instance();
    char   indexBuf[8];
    char * index = HttpHelperI2A( indexBuf, mDevIdx );
    if (index) {
        mqtinator.Sub( index, on_mqtt_input );
    } else {
        ESP_LOGE( TAG, "index buffer too small" );
    }
    while (true) {
        uint32_t notification = 0;
        if (! xTaskNotifyWait( 0, 0xffffffff, & notification, portMAX_DELAY ))
            continue;

        if (notification & 1) {
            HandleInput();
        }
    }
}

void RGB::HandleInput()
{
    const char * data = mData.c_str();
    JsonMap map{ data };
/*
    ESP_LOGD( TAG, "json object:         %p", & map );
    ESP_LOGD( TAG, "json Map():          %p",   map.Map() );
    ESP_LOGD( TAG, "map['Level']:        %p", & map["Level"] );
    ESP_LOGD( TAG, "map['Level'].Type(): %d",   map["Level"].Type() );
    ESP_LOGD( TAG, "map['Level'].Num():  %p",   map["Level"].Num() );
    ESP_LOGD( TAG, "json has %d dict elements", map.Map()->size() );
    vTaskDelay(2);
    ESP_LOGD( TAG, "dumping map" );
    vTaskDelay(2);
    map.Dump();
    vTaskDelay(2);
*/
    const JsonObj::num_t * level = map["Level"].Num();
    if (! level) {
        ESP_LOGE( TAG, "no number \"Level\" (%d)", map["Level"].Type() );
        return;
    }
    if (*level == 0) {
        mFader.Fade( 0, nullptr );
        return;
    }

    const JsonObj::num_t * nvalue = map["nvalue"].Num();
    if (! nvalue) {
        ESP_LOGE( TAG, "no number \"nvalue\" (%d)", map["nvalue"].Type() );
        return;
    }
    // 0=off 14=full light, 15=on, 20=night light
    if (*nvalue == 0) {
        mFader.Fade( 0, nullptr );
        return;
    }
    if (*nvalue != 15) {
        ESP_LOGE( TAG, "unhandled \"nvalue\" %d", (int) (*nvalue) );
        return;
    }

    const JsonObj & col = map["Color"];
    if (! col) {
        ESP_LOGE( TAG, "no \"Color\" (obj %d)", col.Type() );
        return;
    }
    const JsonObj::num_t * m = col["m"].Num();
    if (! m) {
        ESP_LOGE( TAG, "no number \"m\" in \"Color\" (%d)", col["m"].Type() );
        return;
    }
    if (*m != 3) {
        ESP_LOGE( TAG, "unhandled color mode %d (just know mode 3)", (int) (*m) );
        return;
    }

    const JsonObj::num_t * r = col["r"].Num();
    if (! r) {
        ESP_LOGE( TAG, "no number \"r\" in \"Color\" (%d)", col["r"].Type() );
        return;
    }
    const JsonObj::num_t * g = col["g"].Num();
    if (! g) {
        ESP_LOGE( TAG, "no number \"g\" in \"Color\" (%d)", col["g"].Type() );
        return;
    }
    const JsonObj::num_t * b = col["b"].Num();
    if (! b) {
        ESP_LOGE( TAG, "no number \"b\" in \"Color\" (%d)", col["b"].Type() );
        return;
    }

    ESP_LOGI( TAG, "will fade to %d %% [%d,%d,%d]",
                (int) *level, (int) *r, (int) *g, (int) *b );

    mFader.Fade( 0, (float) (*r * *level / 25500.0),
                    (float) (*g * *level / 25500.0),
                    (float) (*b * *level / 25500.0) );
}
