/*
 * Keypad.h
 */

#ifndef MAIN_KEYPAD_H_
#define MAIN_KEYPAD_H_

#include <driver/gpio.h>   // gpio_config_t

typedef unsigned char  u8;
typedef unsigned short u16;

class Keypad
{
public:
    Keypad( u16 pullUpPins,
            u16 pullDownPins,
            const u8 * col, u8 nofCols,
            const u8 * row, u8 nofRows
          );

    virtual void OnKeyPress( u8 num );                // 1st press of a single key
    virtual void OnRelease();                         // key released after single key pressed
    virtual void OnSequence( const char * seq );      // pause after sequence / seq is hex string
    virtual void OnMultiKey( u16 mask, u16 oldmask ); // additional key pressed or released

    void Run();

private:
    void    InPullUp();         // enable pull up   resistor for input pins without hard wired resistor
    void    InPullDown();       // enable pull down resistor for input pins without hard wired resistor
    void    InOpenDrain();      // disable any pull resistor for input pins without hard wired resistor
    u8      Num( u8 o, u8 i );  // -> outpin pin o x input pin i -> keypad number 0..15

    u16 const  mPullUpPins;   // must be input pins - hard wired pull up
    u16 const  mPullDownPins; // must be input pins - hard wired pull down
    u8 const * mOut;    // GPIO output pins
    u8 const * mIn;     // GPIO input pins
    u8         mColOut;  // true when columns are output
    u8         mNofOut; // number of output pins
    u8         mNofIn;  // number of input pins
    u8         mMultiKey;   // set, when multiple keys pressed until all released
    u16        mAllOut;     // 1 << pin1 | 1 << pin2 | ...
    u16        mAllIn;      // 1 << pin1 | 1 << pin2 | ...
    u16        mNumMask;    // 1 << num

    gpio_config_t mInConf;
    gpio_config_t mOutConf;
};

#endif /* MAIN_KEYPAD_H_ */