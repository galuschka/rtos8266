/* Temperator.h
 */
#pragma once

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>

#include <string>
#include <vector>
#include <math.h>   // NANF

#include <driver/gpio.h>
// include <pair>
// incldue <ds18b20/ds18b20.h>    // ds18b20_addr_t

struct httpd_req;

class Temperator
{
public:
    typedef void (* callback_t)( void * userarg, uint16_t idx, float temp );
    struct DevInfo {
        uint64_t    addr;   // OneWire ROM address
        std::string name;   // given name (given by web interface)
        float       value;  // last seen temperature value
        TickType_t  time;   // xTaskGetTickCount() when value read
       	uint16_t    idx;    // Domoticz virtual device idx -> '{"idx":..., "nvalue":..., "svalue":""..."}'

        DevInfo() : addr { 0 },
                    name { "" },
                    value { NAN },
                    time { 0 },
                    idx { 0 }
        {};
        DevInfo( uint64_t aAddr, const char * aName, const uint16_t aIdx )
                  : addr { aAddr },
                    name { aName ? aName : "" },
                    value { NAN },
                    time { 0 },
                    idx { aIdx }
        {};
    };

    enum MODE {
        SCAN,   // scan devices (again) - triggered by web interface
        NORMAL, // normal run
    };
    enum INTERVAL {
        FAST,   // while temperatures change fast detected
        SLOW,   // normal measurement interval
        ERROR,  // while no device detected / error
        COUNT
    };
    static constexpr uint8_t MaxNofDev    =  8;  // max. # of devices connected
    static constexpr uint8_t MaxDevStored = 24;  // max. # of devices stored in nvs

    Temperator( gpio_num_t pin );
    void OnTempRead( callback_t callback, void * userarg );
    bool Start();  // create task and Run inside that task
    void Setup( struct httpd_req * req, bool post = false );
    void Run();   // to let it run in main loop (never returns)
    void Rescan();
    void ReadConfig();
    void WriteDevInfo( uint8_t idx );
    void WriteDevMask();
    void WriteInterval( uint8_t idx );

private:
    gpio_num_t const        mPin;
    MODE                    mMode { NORMAL };
    uint16_t                mDevMask {0};   // bit mask as indices to mDevInfo to found devices
    std::vector<DevInfo>    mDevInfo {};    // addr and name of each device
    TickType_t              mInterval[INTERVAL::COUNT] { configTICK_RATE_HZ, configTICK_RATE_HZ * 10, configTICK_RATE_HZ * 60 };
    TaskHandle_t            mTaskHandle { 0 };
    SemaphoreHandle_t       mSemaphore {};
    callback_t              mCallback { nullptr };
    void                  * mUserArg{ nullptr };
};
