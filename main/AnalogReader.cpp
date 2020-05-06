/*
 * Pressure.cpp
 *
 *  Created on: 06.05.2020
 *      Author: galuschka
 */

#include "AnalogReader.h"

#include "FreeRTOS.h"
#include "task.h"           // vTaskCreate()

#include "esp_log.h"        // ESP_LOGE()
#include "driver/adc.h"     // adc_init(), adc_read()

const char *const TAG = "AnalogReader";

AnalogReader::AnalogReader() :
        TaskHandle { 0 }, Store { 0 }, StorePtr { 0 }, StoreEnd { 0 }, DimStore {
                0 }, Delay { 0 }
{
}

AnalogReader::~AnalogReader()
{
    int delay = Delay;
    Delay = 0;
    vTaskDelay( delay );
    vTaskDelete( TaskHandle );
    TaskHandle = 0;

    value_t *tofree = Store;
    Store = 0;
    StorePtr = 0;
    StoreEnd = 0;
    DimStore = 0;
    free( tofree );
}

extern "C" void PressureTask( void * pressure )
{
    ((AnalogReader*) pressure)->Run();
}

void AnalogReader::Run()
{
    while (Delay) {
        uint16_t val;
        const esp_err_t err = adc_read( &val );
        if (err != ESP_OK) {
            ESP_LOGE( TAG, "adc_read() failed: %d", err );
        } else {
            *StorePtr++ = val;
            if (StorePtr >= StoreEnd)
                StorePtr -= DimStore;
        }
        vTaskDelay( Delay );
    }
}

bool AnalogReader::Init( int frequency, int dimStore )
{
    {
        adc_config_t adc_config;

        adc_config.mode = ADC_READ_TOUT_MODE;
        adc_config.clk_div = 8; // ADC sample collection clock = 80MHz/clk_div (in range 8..32)
        esp_err_t err = adc_init( &adc_config );
        if (err != ESP_OK) {
            ESP_LOGE( TAG, "adc_init() failed: %d", err );
            return false;
        }
    }

    Store = (value_t*) calloc( dimStore, sizeof(value_t) );
    if (!Store) {
        ESP_LOGE( TAG, "low memory: allocating %d x %d bytes failed", dimStore,
                sizeof(value_t) );
        return false;
    }
    StoreEnd = Store + dimStore;    // beyond end
    StorePtr = Store;               // write pointer
    DimStore = dimStore;
    Delay = configTICK_RATE_HZ / frequency;

    xTaskCreate( PressureTask, "Pressure", /*stack size*/1024, this, /*prio*/1,
            &TaskHandle );
    if (! TaskHandle) {
        ESP_LOGE( TAG, "xTaskCreate failed" );
        free( Store );
        Store = 0;
        StorePtr = 0;
        StoreEnd = 0;
        DimStore = 0;
        return false;
    }

    return true;
}

const AnalogReader::value_t* AnalogReader::ValuePtr(
        const AnalogReader::value_t * start, int index ) const
{
    if (!start)
        return 0;

    while (index < 0)
        index += DimStore;

    const value_t *ptr = start + index;
    while (ptr >= StoreEnd)
        ptr -= DimStore;
    return ptr;
}

void AnalogReader::GetValues( AnalogReader::value_t * dest, int dim ) const
{
    if (!Store)
        return;
    const value_t *ptr = ValuePtr( StorePtr, -dim );
    while (dim--) {
        *dest++ = *ptr++;
        if (ptr >= StoreEnd)
            ptr -= DimStore;
    }
}

AnalogReader::value_t AnalogReader::Average( int num ) const
{
    if (!Store)
        return 0;
    unsigned long sum = 0;
    const value_t *ptr = ValuePtr( StorePtr, -num );
    for (int i = num; i; --i) {
        sum += *ptr++;
        if (ptr >= StoreEnd)
            ptr -= DimStore;
    }
    return (value_t) (sum / num);
}
