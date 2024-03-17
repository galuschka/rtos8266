/*
 * Control.cpp
 *
 * controls relays by analog reader values with
 *  - overheat control -> safety off
 *  - too fast to switch on -> safety off
 *  - too fast switched off -> safety off
 */

//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "Control.h"

#include <string>   // std::string

#include <esp_log.h>
#include <nvs.h>            // nvs_open(), ...

#include "AnalogReader.h"
#include "Relay.h"
#include "Input.h"
#include "Monitor.h"
#include "Indicator.h"
#include "Mqtinator.h"

#include "HttpHelper.h"
#include "HttpTable.h"
#include "HttpParser.h"
#include "WebServer.h"
#include "Wifi.h"

const char * const TAG = "Control";

const char * const Control::mModeName[] { MODE_NAMES };

extern "C" esp_err_t get_config( httpd_req_t * req );
extern "C" esp_err_t post_config( httpd_req_t * req );

namespace
{
TickType_t now()
{
    return xTaskGetTickCount();
}

unsigned long expiration( TickType_t ticks )
{
    TickType_t exp = xTaskGetTickCount() + ticks;
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
}

const httpd_uri_t s_get_uri   = { .uri = "/switchctrl", .method = HTTP_GET,  .handler = get_config,  .user_ctx = 0 };
const httpd_uri_t s_post_uri  = { .uri = "/switchctrl", .method = HTTP_POST, .handler = post_config, .user_ctx = 0 };
const WebServer::Page s_page    { s_get_uri, "Control" };

const char *s_keyPwrOnMode = "pwrOnMode"; // to save the mode beyond reboot
const char *s_keyThresOff  = "thresOff";
const char *s_keyThresOn   = "thresOn";
const char *s_keyMinOff    = "minOff";    // even when to switch on: stay off for ... secs
const char *s_keyMinOn     = "minOn";     // safety switch off, when thresOff reached faster
const char *s_keyMaxOn     = "maxOn";     // pause
const char *s_keyValRgMin  = "valRgMin";  // when measurement value < valRgMin -> sensor error
const char *s_keyValRgMax  = "valRgMax";  // when measurement value > valRgMax -> sensor error
const char *s_keyValueTol  = "valueTol";  // tolerance to publish new value
const char *s_keyValueIdx  = "valueIdx";  // device index to publish value
const char *s_keyModeIdx   = "modeIdx";   // where to publish the mode
const char *s_keyTempIdx   = "tempIdx";   // which temperature sensor to watch
const char *s_keyTempMax   = "tempMax";   // overheat value in °C

Control *s_control = 0;

extern "C" {

esp_err_t get_config( httpd_req_t * req )
{
    if (s_control)
        s_control->Setup( req );
    return ESP_OK;
}

esp_err_t post_config( httpd_req_t * req )
{
    if (s_control)
        s_control->Setup( req, true );
    return ESP_OK;
}

void on_analog_value_read( void * control, unsigned short value )
{
    ((Control *) control)->AnalogValue( value );
}

void on_temperature( void * control, uint16_t idx, float temperature )
{
    ((Control *) control)->Temperature( idx, temperature );
}

void on_input( void * control )
{
    ((Control *) control)->NextTestStep();
}

void on_subscribe( const char * topic, const char * data )
{
    ESP_LOGI( TAG, "got \"%s\" \"%.16s...\" (%d bytes)", topic, data, strlen(data) );

    if (s_control)
        s_control->Subscription( topic, data );
}

}

Control::Control( AnalogReader & reader, Relay & relay1, Relay & relay2, Input & input, Monitor & monitor )
                : mReader        { reader },
                  mRelay1        { relay1 },
                  mRelay2        { relay2 },
                  mInput         { input },
                  mMonitor       { monitor }
{
    if (1 || Wifi::Instance().StationMode()) {
        s_control = this;
        WebServer::Instance().AddPage( s_page, & s_post_uri );
    }
}

void Control::NextTestStep()
{
    Notify( EV_INPUT );
}

void Control::Temperature( uint16_t idx, float temperature )
{
    if ((idx == mTempIdx) && (temperature >= mTempMax)) {
        SafetyOff( MODE_OVERHEAT );
        Notify( EV_MODECHANGED );
    }
}

void Control::AnalogValue( unsigned short value )
{
    mValue = value;
    Notify( EV_NEWVALUE );
}

void Control::SafetyOff( uint8_t newMode )
{
    if (mMode != newMode) {
        mRelay1.SetMode( Relay::MODE_OFF );
        vTaskDelay( configTICK_RATE_HZ / 10 );
        mRelay2.SetMode( Relay::MODE_OFF );
        mMode = newMode;
        ESP_LOGW( TAG, "safety switch off into mode %d", newMode );
    }
}

void Control::Notify( uint8_t ev )
{
    mEvents |= ev;
    if (mSemaphore)
        xSemaphoreGive( mSemaphore );
}


void Control::Subscription( const char * topic, const char * data )
{
    const char * cp = strstr( data, "\"idx\"" );
    if (! cp) {
        ESP_LOGE( TAG, "\"idx\" not found" );
        return;
    }
    cp += 5;
    while ((*cp == ' ') || (*cp == ':'))
        ++cp;
    char * end;
    unsigned long val = strtoul( cp, &end, 10 );
    if (val != mModeIdx) {
        ESP_LOGE( TAG, "\"idx\" : %lu does not match mode idx %d", val, mModeIdx );
        return;
    }
    cp = strstr( end, "\"svalue1\"" );
    if (! cp) {
        ESP_LOGE( TAG, "\"svalue1\" not found" );
        return;
    }
    cp += 9;
    while ((*cp == ' ') || (*cp == ':'))
        ++cp;
    if (*cp == '"')
        ++cp;
    val = strtoul( cp, &end, 10 );

    mModeRemote = (uint8_t)(val / 10);
    ESP_LOGI( TAG, "will set mode %d", mModeRemote );
    Notify( EV_REMOTE );
}

void Control::PublishMode()
{
    static uint8_t s_lastModePublished = COUNT_MODES;

    if (mModeIdx && (s_lastModePublished != mMode)) {
        s_lastModePublished = mMode;

        std::string msg = "{ \"idx\": " + std::to_string( mModeIdx )
                        + ", \"nvalue\": 0"
                        + ", \"svalue\": \"" + std::to_string( mMode * 10 ) + "\""
                        + " }";
        Mqtinator::Instance().Pub( 0, msg.c_str() ); // topic=0: no topic / just mPubTopic
    }
}

void Control::PublishValue( bool force )
{
    static value_t    s_lastValuePublished = AnalogReader::INV_VALUE;
    static TickType_t s_exp = 0;

    if (! mModeIdx)
        return;

    if (mValue == AnalogReader::INV_VALUE)
        return;

    if (! force) {
        if (! expired( s_exp )) {
            if (mValue > s_lastValuePublished) {
                if (mValue > (mValueTol + s_lastValuePublished))
                    force = true;
            } else {
                if ((mValue + mValueTol) < s_lastValuePublished)
                    force = true;
            }
            if (! force) {
                if (s_lastValuePublished != AnalogReader::INV_VALUE)
                    return;
            }
        }
    }

    s_lastValuePublished = mValue;
    s_exp = expiration( (configTICK_RATE_HZ * 59) + (configTICK_RATE_HZ / 2) );
    std::string msg = "{ \"idx\": " + std::to_string( mValueIdx )
                    + ", \"nvalue\": 0"
                    + ", \"svalue\": \"" + std::to_string( ((long) mValue * 100 + AnalogReader::HALF_VALUES) / AnalogReader::NOF_VALUES ) + "\""
                    + " }";
    Mqtinator::Instance().Pub( 0, msg.c_str() ); // topic=0: no topic / just mPubTopic
}

void Control::Run( Indicator & indicator )
{
    indicator.Indicate( Indicator::STATUS_IDLE );
    ReadParam();   // mMode will be set according pwrOnMode
    mMonitor.SetThres( mThresOff, mThresOn );

    TickType_t exp    = 0;      // expiration to change mode by time
    TickType_t expMin = 0;      // minOn / minOff check (set on auto switching)
    bool       autoOn = false;
    uint8_t    mode   = MODE_AUTO_OFF;  // initial mode of relais: auto off

    mEvents |= EV_MODECHANGED;          // force check on first loop

    mSemaphore = xSemaphoreCreateBinary();

    mReader.SetCallback( on_analog_value_read, this );
    mInput.SetCallback( on_input, this );

    if (mModeIdx) {
        uint16_t idx = mModeIdx;
        char     str[8];
        char   * cp = & str[8];
        *--cp = 0;
        do {
            *--cp = (idx % 10) + '0';
            idx /= 10;
        } while (idx);
        Mqtinator::Instance().Sub( cp, & on_subscribe );
    }

    while (true)
    {
        ++mLoopCnt;
        if (! mEvents) {
            if (! exp) {
                xSemaphoreTake( mSemaphore, portMAX_DELAY );
                ++mDelayCnt;
            } else {
                long diff = exp - now();
                if (diff <= 0)
                    mEvents |= EV_EXPIRATION;
                else {
                    xSemaphoreTake( mSemaphore, diff );
                    ++mDelayCnt;
                    long diff = exp - now();
                    if (diff <= 0)
                        mEvents |= EV_EXPIRATION;
                }
            }
        }

        bool pubVal      = false;
        bool forcePubVal = false;

        do {
            if (! (mEvents & EV_NEWVALUE))
                break;

            mEvents &= ~EV_NEWVALUE;

            if (mValue == AnalogReader::INV_VALUE) {
                if (mInvValCnt)
                    --mInvValCnt;
                if (! mInvValCnt) {
                    pubVal = forcePubVal = mMode != MODE_VALUE_OOR;
                    SafetyOff( MODE_NOVALUE );
                }
                break;
            }
            mInvValCnt = 3;  // restart count down

            if ((mValue < mValRange[0]) || (mValue > mValRange[1])) {
                pubVal = forcePubVal = mMode != MODE_VALUE_OOR;
                SafetyOff( MODE_VALUE_OOR );
                break;
            }

            pubVal = true;
            if (autoOn) {
                bool toSwitch = false;
                if (mThresOff > mThresOn)
                    toSwitch = mValue >= mThresOff;
                else
                    toSwitch = mValue <= mThresOff;

                if (toSwitch) {
                    autoOn = false;
                    mRelay2.AutoOn( autoOn );
                    if ((mode == MODE_AUTO_ON) && ! MODE_SAFETY_OFF(mMode)) {
                        mMode = MODE_AUTO_OFF;
                        vTaskDelay( configTICK_RATE_HZ / 4 );
                        if (expMin) {
                            long tim = now() - expMin;
                            if (tim < 0)               // we are switching off before expMin
                                mMode = MODE_FASTOFF;  // probably low air pressure
                        }
                        exp = 0;
                        if (mMode == MODE_AUTO_OFF)
                            expMin = expiration( mMinOffTicks );  // set just when on due to threshold reach
                    }
                    mRelay1.AutoOn( autoOn );
                    forcePubVal = true;
                }
            } else {
                bool toSwitch = false;
                if (mThresOff > mThresOn)
                    toSwitch = mValue <= mThresOn;
                else
                    toSwitch = mValue >= mThresOn;

                if (toSwitch) {
                    if ((mode == MODE_AUTO_OFF) && expMin) {
                        long tim = now() - expMin;
                        if (tim < 0)                    // we would switch on before expMin
                            SafetyOff( MODE_FASTON );   // too fast to switch on again
                    }
                    autoOn = true;  // auto off/on continues
                    mRelay1.AutoOn( autoOn );
                    if ((mode == MODE_AUTO_OFF) && ! MODE_SAFETY_OFF(mMode)) {
                        mMode = MODE_AUTO_ON;
                        vTaskDelay( configTICK_RATE_HZ / 4 );
                        // exp = expiration( mMaxOnTicks );  ** set for any mode transition to MODE_AUTO_ON
                        expMin = expiration( mMinOnTicks );  // set just when on due to threshold reach
                    }
                    mRelay2.AutoOn( autoOn );
                    forcePubVal = true;
                }
            }
        } while (0);

        if (mEvents & EV_EXPIRATION) {
            mEvents &= ~EV_EXPIRATION;

            exp = 0;
            switch (mode) {
                case MODE_AUTO_ON:    mMode = MODE_AUTO_PAUSE; break;  // max on timer timed out -> switch off for minOff secs
                case MODE_AUTO_PAUSE: mMode = MODE_AUTO_OFF;   break;  // min off timer timed out -> return to normal operation
                case MODE_TEST1:      mMode = MODE_TEST1_END;  break;
                case MODE_TEST2:      mMode = MODE_TEST2_END;  break;
                case MODE_TEST3:      mMode = MODE_AUTO_OFF;   break;  // end of test in general
            }
        }

        if (mEvents & EV_INPUT) {
            mEvents &= ~EV_INPUT;

            switch (mode) {
                case MODE_TESTOFF:   mMode = MODE_TEST1;  break;
                case MODE_TEST1:
                case MODE_TEST1_END: mMode = MODE_TEST2;  break;
                case MODE_TEST2:
                case MODE_TEST2_END: mMode = MODE_TEST3;  break;
                default:             mMode = MODE_TESTOFF; break;
            }
        }

        if (mEvents & EV_REMOTE) {
            mEvents &= ~EV_REMOTE;

            mMode = mModeRemote;
        }

        if ((mEvents & EV_MODECHANGED) || (mode != mMode)) {
            mEvents &= ~EV_MODECHANGED;

            if (mode != mMode) {
                uint16_t newMBit = 1 << mMode;
                uint16_t oldMBit = 1 << mode;
                if (newMBit & MMASK_AUTO) {                     // new mode is auto mode
                    if (! (oldMBit & MMASK_AUTO)) {             // old mode was not auto mode
                        mMode = autoOn ? MODE_AUTO_ON : MODE_AUTO_OFF;  // TEST3/PAUSE -> ON or OFF
                        newMBit = 1 << mMode;
                        mRelay1.SetMode( Relay::MODE_AUTO );
                        vTaskDelay( configTICK_RATE_HZ / 4 );
                        mRelay2.SetMode( Relay::MODE_AUTO );

                        if (mode != MODE_AUTO_PAUSE)
                            SavePwrOnMode( MODE_AUTO_OFF );

                        // first entry to auto mode -> set on next switch
                        exp = 0;
                        expMin = 0;
                    }
                } else {                                        // new mode is **not** auto mode
                    if (! (newMBit & MMASK_REL2))
                        mRelay2.SetMode( Relay::MODE_OFF );
                    if ((! (newMBit & MMASK_TEST)) && (oldMBit & (MBIT_AUTO_ON | MBIT_TEST3)))
                        vTaskDelay( configTICK_RATE_HZ / 4 );
                    if (! (newMBit & MMASK_REL1))
                        mRelay1.SetMode( Relay::MODE_OFF );

                    if (newMBit & MMASK_REL1)
                        mRelay1.SetMode( Relay::MODE_ON );
                    if (newMBit & MBIT_TEST3)
                        vTaskDelay( configTICK_RATE_HZ / 4 );
                    if (newMBit & MMASK_REL2)
                        mRelay2.SetMode( Relay::MODE_ON );

                    if (oldMBit & MMASK_AUTO) // old mode **was** auto mode
                        if (mMode != MODE_AUTO_PAUSE)
                            SavePwrOnMode( mMode );

                    // manual mode -> reset expiration (set individual)
                    exp = 0;
                    expMin = 0;
                }

                if (((oldMBit & MMASK_IS_ON) != 0) != ((newMBit & MMASK_IS_ON) != 0)) {
                    pubVal = true;
                    forcePubVal = true;
                }

                mode = mMode;
                switch (mode) {
                    case MODE_AUTO_OFF:
                        indicator.Indicate( Indicator::STATUS_IDLE );       // #___________________
                        break;
                    case MODE_AUTO_ON:
                        exp = expiration( mMaxOnTicks );
                        indicator.Indicate( Indicator::STATUS_ACTIVE );     // ##########__________
                        break;
                    case MODE_FASTON:
                        indicator.SigMask( 0x6167 );                        // #######______#______
                        break;
                    case MODE_FASTOFF:
                        indicator.SigMask( 0x511157 );                      // #######_____#_#_____
                        break;
                    case MODE_OVERHEAT:
                        indicator.SigMask( 0x41111147 );                    // #######____#_#_#____
                        break;
                    default:
                        if (newMBit & (MMASK_TEST | MBIT_AUTO_PAUSE))
                            exp = expiration( configTICK_RATE_HZ * 3 );
                        if (newMBit & MMASK_TEST)
                            indicator.Blink( MODE_TEST_NUM(mode) );         // #_[#_[#_]].
                        else if (newMBit & MMASK_SAFETY_OFF)
                            indicator.Indicate( Indicator::STATUS_ERROR );  // ##########_##_##_##_
                        else
                            indicator.Steady( 0 );                          // .
                        break;
                }
            }
            PublishMode();
        }
        if (pubVal)
            PublishValue( forcePubVal );
    }

    mReader.SetCallback( nullptr, this );
    mInput.SetCallback( nullptr, this );
}

void Control::ReadParam()
{
    ESP_LOGI( TAG, "Reading control configuration" );

    nvs_handle my_handle;
    if (nvs_open( "control", NVS_READONLY, &my_handle ) == ESP_OK) {
        {
            uint16_t val;
            if (nvs_get_u16( my_handle, s_keyPwrOnMode, & val ) == ESP_OK)
                mMode = (uint8_t) val;
            if (nvs_get_u16( my_handle, s_keyThresOff, & val ) == ESP_OK)
                mThresOff = val;
            if (nvs_get_u16( my_handle, s_keyThresOn, & val ) == ESP_OK)
                mThresOn = val;
            if (nvs_get_u16( my_handle, s_keyValRgMin, & val ) == ESP_OK)
                mValRange[0] = val;
            if (nvs_get_u16( my_handle, s_keyValRgMax, & val ) == ESP_OK)
                mValRange[1] = val;
            if (nvs_get_u16( my_handle, s_keyValueTol, & val ) == ESP_OK)
                mValueTol = val;
            if (nvs_get_u16( my_handle, s_keyValueIdx, & val ) == ESP_OK)
                mValueIdx = val;
            if (nvs_get_u16( my_handle, s_keyModeIdx, & val ) == ESP_OK)
                mModeIdx = val;
            if (nvs_get_u16( my_handle, s_keyTempIdx, & val ) == ESP_OK)
                mTempIdx = val;
            if (nvs_get_u16( my_handle, s_keyTempMax, & val ) == ESP_OK)
                mTempMax = (uint8_t) val;
        }
        {
            uint32_t val;
            if (nvs_get_u32( my_handle, s_keyMinOff, & val ) == ESP_OK)
                mMinOffTicks = val;
            if (nvs_get_u32( my_handle, s_keyMinOn, & val ) == ESP_OK)
                mMinOnTicks = val;
            if (nvs_get_u32( my_handle, s_keyMaxOn, & val ) == ESP_OK)
                mMaxOnTicks = val;
        }
        nvs_close( my_handle );

        ESP_LOGD( TAG, "thresOff = %6d/1023",   mThresOff );
        ESP_LOGD( TAG, "thresOn  = %6d/1023",   mThresOn  );
        ESP_LOGD( TAG, "minOff   = %8lu ticks", mMinOffTicks );
        ESP_LOGD( TAG, "minOn    = %8lu ticks", mMinOnTicks );
        ESP_LOGD( TAG, "maxOn    = %8lu ticks", mMaxOnTicks );
        ESP_LOGD( TAG, "valueTol = %6d",        mValueTol );
        ESP_LOGD( TAG, "valueIdx = %6d",        mValueIdx );
        ESP_LOGD( TAG, "modeIdx  = %6d",        mModeIdx  );
        ESP_LOGD( TAG, "tempIdx  = %6d",        mTempIdx  );
        ESP_LOGD( TAG, "tempMax  = %6d °C",     mTempMax  );
    }
}

void Control::SavePwrOnMode( uint8_t mode )
{
    nvs_handle my_handle;
    if (nvs_open( "control", NVS_READWRITE, &my_handle ) == ESP_OK) {
        SetU16( my_handle, s_keyPwrOnMode, mode );
        nvs_commit( my_handle );
        nvs_close( my_handle );
    }
}

void Control::WriteParam()
{
    ESP_LOGI( TAG, "Writing control configuration" );

    ESP_LOGD( TAG, "thresOff = %6d/1023",   mThresOff );
    ESP_LOGD( TAG, "thresOn  = %6d/1023",   mThresOn  );
    ESP_LOGD( TAG, "minOff   = %8lu ticks", mMinOffTicks );
    ESP_LOGD( TAG, "minOn    = %8lu ticks", mMinOnTicks );
    ESP_LOGD( TAG, "maxOn    = %8lu ticks", mMaxOnTicks );
    ESP_LOGD( TAG, "valueTol = %6d",        mValueTol );
    ESP_LOGD( TAG, "valueIdx = %6d",        mValueIdx );
    ESP_LOGD( TAG, "modeIdx  = %6d",        mModeIdx  );
    ESP_LOGD( TAG, "tempIdx  = %6d",        mTempIdx  );
    ESP_LOGD( TAG, "tempMax  = %6d °C",     mTempMax  );

    nvs_handle my_handle;
    if (nvs_open( "control", NVS_READWRITE, &my_handle ) == ESP_OK) {
        SetU16( my_handle, s_keyThresOff, mThresOff );
        SetU16( my_handle, s_keyThresOn,  mThresOn );
        SetU32( my_handle, s_keyMinOff,   mMinOffTicks );
        SetU32( my_handle, s_keyMinOn,    mMinOnTicks );
        SetU32( my_handle, s_keyMaxOn,    mMaxOnTicks );
        SetU16( my_handle, s_keyValRgMin, mValRange[0] );
        SetU16( my_handle, s_keyValRgMax, mValRange[1] );
        SetU16( my_handle, s_keyValueTol, mValueTol );
        SetU16( my_handle, s_keyValueIdx, mValueIdx );
        SetU16( my_handle, s_keyModeIdx,  mModeIdx );
        SetU16( my_handle, s_keyTempIdx,  mTempIdx );
        SetU16( my_handle, s_keyTempMax,  mTempMax );
        nvs_commit( my_handle );
        nvs_close( my_handle );
    }
}

void Control::SetU16( nvs_handle nvs, const char * key, uint16_t val )
{
    uint16_t oldval;
    if (nvs_get_u16( nvs, key, & oldval ) == ESP_OK)
        if (oldval == val)
            return;
    nvs_set_u16( nvs, key, val );
    ESP_LOGI( TAG, "set u16 %s=%d", key, val );
}

void Control::SetU32( nvs_handle nvs, const char * key, uint32_t val )
{
    uint32_t oldval;
    if (nvs_get_u32( nvs, key, & oldval ) == ESP_OK)
        if (oldval == val)
            return;
    nvs_set_u32( nvs, key, val );
    ESP_LOGI( TAG, "set u32 %s=%d", key, val );
}

namespace {

std::string InputField( const char * key, long min, long max, long val )
{
    std::string str {"<input type=\"number\" name=\"" }; str += key;
    str += "\" min=\"";   str += HttpHelper::String( min );
    str += "\" max=\"";   str += HttpHelper::String( max );
    str += "\" value=\""; str += HttpHelper::String( val );
    str += "\" />";
    return str;
}

long value2percent( long val )
{
    return (val * 100 + AnalogReader::HALF_VALUES) / AnalogReader::NOF_VALUES;
}

Control::value_t percent2value( long perc )
{
    return (Control::value_t) ((perc * AnalogReader::NOF_VALUES + 50) / 100);
}

}

void Control::Setup( struct httpd_req * req, bool post )
{
    HttpHelper hh{ req, "Configure switching thresholds", "Control" };

    if (post) {
        {
            char bufThresOff[4];
            char bufThresOn[4];
            char bufMinOff[4];
            char bufMinOn[4];
            char bufMaxOn[8];
            char bufValRgMin[4];
            char bufValRgMax[4];
            char bufValueTol[4];
            char bufValueIdx[6];
            char bufModeIdx[6];
            char bufTempIdx[6];
            char bufTempMax[4];
            HttpParser::Input in[] = { { s_keyThresOff, bufThresOff, sizeof(bufThresOff) },
                                       { s_keyThresOn,  bufThresOn,  sizeof(bufThresOn)  },
                                       { s_keyMinOff,   bufMinOff,   sizeof(bufMinOff)   },
                                       { s_keyMinOn,    bufMinOn,    sizeof(bufMinOn)    },
                                       { s_keyMaxOn,    bufMaxOn,    sizeof(bufMaxOn)    },
                                       { s_keyValRgMin, bufValRgMin, sizeof(bufValRgMin) },
                                       { s_keyValRgMax, bufValRgMax, sizeof(bufValRgMax) },
                                       { s_keyValueTol, bufValueTol, sizeof(bufValueTol) },
                                       { s_keyValueIdx, bufValueIdx, sizeof(bufValueIdx) },
                                       { s_keyModeIdx,  bufModeIdx,  sizeof(bufModeIdx)  },
                                       { s_keyTempIdx,  bufTempIdx,  sizeof(bufTempIdx)  },
                                       { s_keyTempMax,  bufTempMax,  sizeof(bufTempMax)  } };
            HttpParser parser{ in, sizeof(in) / sizeof(in[0]) };

            const char * parseError = parser.ParsePostData( req );
            if (parseError) {
                hh.Add( "parser error: " );
                hh.Add( parseError );
                return;
            }

            mThresOff    = percent2value( strtoul( bufThresOff, 0, 10 ) );
            mThresOn     = percent2value( strtoul( bufThresOn,  0, 10 ) );
            mMinOffTicks =               (strtoul( bufMinOff,   0, 10 ) * configTICK_RATE_HZ);
            mMinOnTicks  =               (strtoul( bufMinOn,    0, 10 ) * configTICK_RATE_HZ);
            mMaxOnTicks  =               (strtoul( bufMaxOn,    0, 10 ) * configTICK_RATE_HZ);
            mValRange[0] = percent2value( strtoul( bufValRgMin, 0, 10 ) );
            mValRange[1] = percent2value( strtoul( bufValRgMax, 0, 10 ) );
            mValueTol    = percent2value( strtoul( bufValueTol, 0, 10 ) );
            mValueIdx    = (uint16_t)     strtoul( bufValueIdx, 0, 10 );
            mModeIdx     = (uint16_t)     strtoul( bufModeIdx,  0, 10 );
            mTempIdx     = (uint16_t)     strtoul( bufTempIdx,  0, 10 );
            mTempMax     = (uint8_t)      strtoul( bufTempMax,  0, 10 );
        }
        mMonitor.SetThres( mThresOff, mThresOn );
        WriteParam();
    }
    hh.Add( " <form method=\"post\">\n"
            "  <table>\n" );
    {
        Table<14,5> table;
        table.Right( 0 );
        table.Right( 2 );
        table[ 0][1] = "&nbsp;";  // some space due to right adjust of Parameter
        table[ 0][2] = "Value";
        table[ 0][0] = "Parameter";                table[ 0][3] = "Unit";      table[ 0][4] = "Remarks";

        table[ 1][0] = "switching off threshold";  table[ 1][3] = "&percnt;";  table[ 1][4] = "switch off, when exceeding/underrun this value";
        table[ 2][0] = "switching on threshold";   table[ 2][3] = "&percnt;";  table[ 2][4] = "switch on, when underrun/exceeding this value";
        table[ 3][0] = "min. off time";            table[ 3][3] = "secs";      table[ 3][4] = "safety switch off, when to switch on faster";
        table[ 4][0] = "min. on time";             table[ 4][3] = "secs";      table[ 4][4] = "safety switch off, when switched off faster";
        table[ 5][0] = "max. on time";             table[ 5][3] = "secs";      table[ 5][4] = "force periodical pause of 3 seconds";
        table[ 6][0] = "min. valid value";         table[ 6][3] = "&percnt;";  table[ 6][4] = "safety switch off, when read value is below of this";
        table[ 7][0] = "max. valid value";         table[ 7][3] = "&percnt;";  table[ 7][4] = "safety switch off, when read value is above of this";
        table[ 8][0] = "value tolerance";          table[ 8][3] = "&percnt;";  table[ 8][4] = "force value report on bigger change";
        table[ 9][0] = "rel. value device idx";  /*table[ 9][3] = "&mdash;";*/ table[ 9][4] = "domoticz device index to report analog value";
        table[10][0] = "mode device idx";        /*table[10][3] = "&mdash;";*/ table[10][4] = "domoticz device index to report and set operation mode";
        table[11][0] = "temperature sensor idx"; /*table[11][3] = "&mdash;";*/ table[11][4] = "temperature device to be used for overheat control";
        table[12][0] = "overheat temperature";     table[12][3] = "&deg;C";    table[12][4] = "safety switch off, when overheat detected";

        table[ 1][2] = InputField( s_keyThresOff, 0,   100, value2percent( mThresOff ) );
        table[ 2][2] = InputField( s_keyThresOn,  0,   100, value2percent( mThresOn  ) );
        table[ 3][2] = InputField( s_keyMinOff,   1,    60, ((long) mMinOffTicks + configTICK_RATE_HZ/2) / configTICK_RATE_HZ );
        table[ 4][2] = InputField( s_keyMinOn,    1,    60, ((long) mMinOnTicks  + configTICK_RATE_HZ/2) / configTICK_RATE_HZ );
        table[ 5][2] = InputField( s_keyMaxOn,    1, 86400, ((long) mMaxOnTicks  + configTICK_RATE_HZ/2) / configTICK_RATE_HZ );
        table[ 6][2] = InputField( s_keyValRgMin, 0,   100, value2percent( mValRange[0] ) );
        table[ 7][2] = InputField( s_keyValRgMax, 0,   100, value2percent( mValRange[1] ) );
        table[ 8][2] = InputField( s_keyValueTol, 0,   100, value2percent( mValueTol ) );
        table[ 9][2] = InputField( s_keyValueIdx, 0,  9999, mValueIdx );
        table[10][2] = InputField( s_keyModeIdx,  0,  9999, mModeIdx );
        table[11][2] = InputField( s_keyTempIdx,  0,  9999, mTempIdx );
        table[12][2] = InputField( s_keyTempMax,  0,   100, mTempMax );

        table[13][2] = "<br /><center><button type=\"submit\">submit</button></center>";
        table[13][4] = "<br />submit the values to be stored on the device";
        table.AddTo( hh, 1 );
    }
    hh.Add( "  </table>\n"
            " </form>\n" );

    hh.Add( " <h3>Current mode and statistics counter</h3>\n"
            " <table>\n" );
    {
        Table<4,3> table;
        table.Right( 0 );
        table.Right( 1 );
        table[0][0] = "Counter"; table[0][1] = "Value"; table[0][2] = "Remarks";

        table[1][0] = "Current mode:";
        table[2][0] = "Loop counter:";
        table[3][0] = "Not waiting loop counter:";
        table[3][2] = "loops without need to wait (should be rare)";

        table[1][1] = std::to_string( mMode );
        table[1][2] = mModeName[ mMode < COUNT_MODES ? mMode : (uint8_t) COUNT_MODES ];
        table[2][1] = std::to_string( mLoopCnt );
        table[3][1] = std::to_string( mLoopCnt - mDelayCnt );

        table.AddTo( hh, 1 );
    }
    hh.Add( " </table>\n" );
}
