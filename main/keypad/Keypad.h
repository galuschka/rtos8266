/*
 * Keypad.h
 *
 *  Created on: 07.01.2022
 *      Author: holger
 */

#ifndef MAIN_KEYPAD_H_
#define MAIN_KEYPAD_H_

#include "gpio.h"   // gpio_config_t

typedef unsigned char  u8;
typedef unsigned short u16;

class Keypad
{
public:
    Keypad( const u8 * col, u8 nofCols,
            const u8 * row, u8 nofRows );
    // virtual ~Keypad() = 0;

    virtual void OnSequence( const char * seq );   // pause after sequence / seq is hex string
    virtual void OnKeyPress( u8 num );   // 1st press of a key
    virtual void OnMultiKey( u16 mask ); // additional key pressed or not last key released
    virtual void OnRelease();            // all keys released

    void Run();

private:
    void RowsOutColsIn();       // fast check: all rows set 1 and colMask is read

    u8 const * mCol;     // GPIO pins
    u8 const * mRow;     // GPIO pins
    u8 const   mNofCols;
    u8 const   mNofRows;
    u16        mAllCols; // 1 << pin
    u16        mAllRows; // 1 << pin
    u16        mNumMask; // 1 << num

    gpio_config_t mInConf;
    gpio_config_t mOutConf;
};

#endif /* MAIN_KEYPAD_H_ */