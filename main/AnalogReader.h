/*
 * Analog.h
 *
 * Read analog pin in independant thread and support methods to read latest values
 *
 *  Created on: 06.05.2020
 *      Author: galuschka
 */

#ifndef MAIN_ANALOGREADER_H_
#define MAIN_ANALOGREADER_H_

#include "FreeRTOS.h"
#include "task.h"           // TaskHandle_t
#include "driver/gpio.h"    // gpio_num_t

class AnalogReader
{
public:
    typedef unsigned short value_t;

    AnalogReader( gpio_num_t gpioSensorPwrSply, int frequency, int dimStore );
    ~AnalogReader();

    // create and run the thread - read values with given frequency
    bool Init();
    void GetValues( value_t * dest, int dim ) const; // copy last <dim> values to <dest> array

    value_t Average( int num ) const; // return average of the last <num> values

    void Run(); // internal thread function

private:
    const value_t* ValuePtr( const value_t * start, int index ) const; // relative start (wrapped)

    TaskHandle_t TaskHandle;
    value_t *Store;     // the value array
    value_t *StorePtr;  // write pointer
    value_t *StoreEnd;  // wrap margin (= Store + dimStore)
    int DimStore;  // save the dimension
    int Delay;     // delay for thread routine
    gpio_num_t GpioPwr;  // to switch sensor power supply
};

#endif /* MAIN_ANALOGREADER_H_ */
