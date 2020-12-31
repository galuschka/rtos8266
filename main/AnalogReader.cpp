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

#include "Relay.h"

const char *const TAG = "AnalogReader";

//@formatter:off
AnalogReader::AnalogReader( gpio_num_t gpioSensorPwrSply, Relay &relay )
                            : mRelay        { relay },
                              TaskHandle    { 0 },
                              Store         { 0 },
                              StorePtr      { 0 },
                              StoreEnd      { 0 },
                              DimStore      { 0 },
                              Delay         { 0 },
                              GpioPwr       { gpioSensorPwrSply }
{
//@formatter:on
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
    GpioPwr = GPIO_NUM_MAX;
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
        if (GpioPwr != GPIO_NUM_MAX) {
            gpio_set_level( GpioPwr, 1 );
            vTaskDelay( 2 );
        }
        const esp_err_t err = adc_read( &val );
        if (GpioPwr != GPIO_NUM_MAX)
            gpio_set_level( GpioPwr, 0 );

        if (err != ESP_OK) {
            ESP_LOGE( TAG, "adc_read() failed: %d", err );
        } else {
            val |= mRelay.Status() << 15;
            *StorePtr++ = val;
            if (StorePtr >= StoreEnd)
                StorePtr -= DimStore;
        }
        if (GpioPwr != GPIO_NUM_MAX)
            vTaskDelay( Delay - 2 );
        else
            vTaskDelay( Delay );
    }
}

bool AnalogReader::Init( int frequency, int dimStore )
{
    {
        adc_config_t adc_config;

        adc_config.mode = ADC_READ_TOUT_MODE;
        adc_config.clk_div = 32; // ADC sample collection clock = 80MHz/clk_div (in range 8..32)
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

    if (GpioPwr != GPIO_NUM_MAX) {
        gpio_config_t io_conf;

        io_conf.pin_bit_mask = (1 << GpioPwr);  // the pin
        io_conf.mode = GPIO_MODE_OUTPUT;          // set as output mode
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;       // disable pull-up mode
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;  // disable pull-down mode
        io_conf.intr_type = GPIO_INTR_DISABLE;         // disable interrupt

        gpio_config( &io_conf );    // configure GPIO with the given settings
    }

    DimStore = dimStore;
    StoreEnd = Store + dimStore;    // beyond end
    StorePtr = Store;               // write pointer
    Delay    = (int)(configTICK_RATE_HZ / frequency);

    xTaskCreate( PressureTask, "Pressure", /*stack size*/2048, this, /*prio*/1,
            &TaskHandle );
    if (!TaskHandle) {
        ESP_LOGE( TAG, "xTaskCreate failed" );
        free( Store );
        Store = 0;
        StorePtr = 0;
        StoreEnd = 0;
        DimStore = 0;
        Delay = 0;
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
        sum += (*ptr++ & 0x7fff);
        if (ptr >= StoreEnd)
            ptr -= DimStore;
    }
    return (value_t) (sum / num);
}
