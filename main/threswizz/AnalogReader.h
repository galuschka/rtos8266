/*
 * Analog.h
 *
 * Read analog pin in independant thread and support methods to read latest values
 */

#ifndef MAIN_ANALOGREADER_H_
#define MAIN_ANALOGREADER_H_

#include "FreeRTOS.h"
#include "task.h"           // TaskHandle_t
#include "driver/gpio.h"    // gpio_num_t

class Relay;

class AnalogReader
{
public:
    typedef unsigned short value_t;
    typedef void (*callback_t)( void * userarg, value_t value );
    enum { MAX_VALUE   = 0x3ff,   // 10 bit ADC
           INV_VALUE   = 0x400,   // indicate "no valid measurement value"
           MASK_VALUE  = 0x7ff,
           NOF_VALUES  = INV_VALUE,
           HALF_VALUES = NOF_VALUES / 2
         };

    AnalogReader( gpio_num_t gpioSensorPwrSply, Relay & relay1, Relay & relay2 );
    ~AnalogReader();

    // create and run the thread - read values with given frequency
    bool Init( uint8_t reportInterval, uint8_t numMeasAvg, uint8_t measAvgFrequency, uint16_t dimStore );
    void SetCallback( callback_t callback, void * userarg ); // set call back routine on measurement

    void GetValues( value_t * dest, uint16_t dim ) const; // copy last <dim> values to <dest> array
    value_t GetValue() const; // return last average value

    void Run(); // internal thread function

private:
    const value_t* ValuePtr( const value_t * start, int index ) const; // relative start (wrapped)

    Relay       & mRelay1;
    Relay       & mRelay2;
    TaskHandle_t  TaskHandle  { nullptr };
    value_t     * Store       { nullptr };  // the value array
    value_t     * StorePtr    { nullptr };  // write pointer
    value_t     * StoreEnd    { nullptr };  // wrap margin (= Store + dimStore)
    uint16_t      DimStore    { 0 };        // save the dimension
    uint16_t      DelayReport { 0 };        // delay for measurements reports       (e.g. 1.0 s in ticks)
    uint16_t      DelayAvg    { 0 };        // delay for average calc. measurements (e.g. 0.1 s in ticks)
    uint8_t       NumMeasAvg  { 0 };        // number of measurements for average calc.
    gpio_num_t    GpioPwr     { GPIO_NUM_MAX }; // PIN where to to switch sensor power supply
    callback_t    Callback    { nullptr };
    void        * UserArg     { nullptr };
};

#endif /* MAIN_ANALOGREADER_H_ */
