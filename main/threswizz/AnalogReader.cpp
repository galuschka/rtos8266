/*
 * AnalogReader.cpp
 */

#include "AnalogReader.h"

#include "FreeRTOS.h"
#include "task.h"           // vTaskCreate()

#include "esp_log.h"        // ESP_LOGE()
#include "driver/adc.h"     // adc_init(), adc_read()

#include "Relay.h"

const char *const TAG = "AnalogReader";

AnalogReader::AnalogReader( gpio_num_t gpioSensorPwrSply, Relay &relay1, Relay & relay2 )
                            : mRelay1       { relay1 },
                              mRelay2       { relay2 },
                              GpioPwr       { gpioSensorPwrSply }
{
}

AnalogReader::~AnalogReader()
{
    int delay = DelayReport;
    DelayAvg = 0;
    DelayReport = 0;
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

extern "C" void AnalogReaderTask( void * analogreader )
{
    ((AnalogReader*) analogreader)->Run();
}

void AnalogReader::Run()
{
    while (DelayAvg)
    {
        TickType_t repStart = xTaskGetTickCount();
        TickType_t start = repStart;

        uint8_t measRemain = NumMeasAvg;
        uint8_t measValid = 0;
        uint16_t avg = 0;

        while (DelayAvg) {
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
                avg += val;
                ++measValid;
            }
            if (! --measRemain)
                break;

            start += DelayAvg;
            int delay = (int) (start - xTaskGetTickCount());
            if (delay > 0)
                vTaskDelay( delay );
        }

        bool on = mRelay1.Status() & mRelay2.Status();

        if (! measValid)
            avg = INV_VALUE;
        else
            avg = (avg + (measValid/2)) / measValid;

        if (Callback)
            Callback( UserArg, avg );

        *StorePtr++ = avg | (on << 15);
        if (StorePtr >= StoreEnd)
            StorePtr -= DimStore;

        start = repStart + DelayReport;
        int delay = (int) (start - xTaskGetTickCount());
        if (delay > 0)
            vTaskDelay( delay );
        else
            vTaskDelay( DelayReport );  // should not happen - just safety
    }
}

bool AnalogReader::Init( uint8_t reportInterval, uint8_t numMeasAvg, uint8_t measAvgFrequency, uint16_t dimStore )
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
    for (int i = 0; i < dimStore; ++i)
        Store[i] = INV_VALUE;

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

    DelayAvg    = (uint16_t)(configTICK_RATE_HZ / measAvgFrequency);
    DelayReport = (uint16_t)(configTICK_RATE_HZ * reportInterval);
    NumMeasAvg  = numMeasAvg;

    xTaskCreate( AnalogReaderTask, "AnalogReader", /*stack size*/1024, this, /*prio*/1, & TaskHandle );
    if (!TaskHandle) {
        ESP_LOGE( TAG, "xTaskCreate failed" );
        free( Store );
        Store = 0;
        StorePtr = 0;
        StoreEnd = 0;
        DimStore = 0;
        DelayAvg = 0;
        DelayReport = 0;
        NumMeasAvg = 0;
        return false;
    }

    return true;
}

void AnalogReader::SetCallback( callback_t callback, void * userarg )
{
    UserArg = userarg;
    Callback = callback;
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

void AnalogReader::GetValues( AnalogReader::value_t * dest, uint16_t dim ) const
{
    if (!Store)
        return;
    const value_t *ptr = ValuePtr( StorePtr, DimStore - dim );
    while (dim--) {
        *dest++ = *ptr++;
        if (ptr >= StoreEnd)
            ptr -= DimStore;
    }
}

AnalogReader::value_t AnalogReader::GetValue() const
{
    if (!Store)
        return 0;
    return (*ValuePtr( StorePtr, -1 ) & 0x7fff);
}
