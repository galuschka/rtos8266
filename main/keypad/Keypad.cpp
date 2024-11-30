/*
 * Keypad.cpp
 */

//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#if (LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG)
#define EXPRD(expr) do { expr; } while(0);
#else
#define EXPRD(expr)
#endif

#include "Keypad.h"
#include "Indicator.h"
#include "Mqtinator.h"

#include "esp_log.h"        // ESP_LOGI()

#include "FreeRTOSConfig.h"
#include "FreeRTOS.h"
#include "task.h"

#include "portmacro.h"
#include "esp8266/gpio_struct.h"

const char *const TAG = "Keypad";

//@formatter:off

Keypad::Keypad( u16 pullUpPins,
                u16 pullDownPins,
                const u8 * col, u8 nofCols,
                const u8 * row, u8 nofRows )
              : mPullUpPins   { pullUpPins },
                mPullDownPins { pullDownPins },
                mMultiKey   { 0 },
                mNumMask    { 0 }
//@formatter:on
{
    u16 allCols = 0;
    for (u8 i = 0; i < nofCols; ++i)
        allCols |= 1 << col[i];
    u16 allRows = 0;
    for (u8 i = 0; i < nofRows; ++i)
        allRows |= 1 << row[i];

    if (allCols & (mPullUpPins | mPullDownPins)) { // hard wired must be input
        mColOut = 0;
        mOut = row; mNofOut = nofRows; mAllOut = allRows;
        mIn  = col; mNofIn  = nofCols; mAllIn  = allCols;
    } else {
        mColOut = 1;
        mOut = col; mNofOut = nofCols; mAllOut = allCols;
        mIn  = row; mNofIn  = nofRows; mAllIn  = allRows;
    }

    // The GPIO of esp8266 can not be pulled down except RTC GPIO (16) which can not be pulled up.

    mOutConf.mode         = GPIO_MODE_DISABLE;      // disable when idle
    mOutConf.pull_up_en   = GPIO_PULLUP_DISABLE;    // pull-up mode
    mOutConf.pull_down_en = GPIO_PULLDOWN_DISABLE;  // pull-down mode
    mOutConf.intr_type    = GPIO_INTR_DISABLE;      // disable interrupt

    mOutConf.pin_bit_mask = mAllOut;
    gpio_config( &mOutConf ); // output: disable -> enable when used

    mInConf.mode         = GPIO_MODE_INPUT;         // input
    mInConf.pull_up_en   = GPIO_PULLUP_DISABLE;     // pull-up mode
    mInConf.pull_down_en = GPIO_PULLDOWN_DISABLE;   // pull-down mode
    mInConf.intr_type    = GPIO_INTR_DISABLE;       // disable interrupt
    mInConf.pin_bit_mask = mAllIn;
    gpio_config( &mInConf ); // input: pull up / pull down on measurement

    mInConf.pin_bit_mask = mAllIn & ~(mPullUpPins | mPullDownPins);
}

void Keypad::InPullUp()
{
    mInConf.pull_up_en   = GPIO_PULLUP_ENABLE;
    mInConf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config( &mInConf ); // input without hard wired pull up/down -> pull up
}

void Keypad::InPullDown()
{
    mInConf.pull_up_en   = GPIO_PULLUP_DISABLE;
    mInConf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    gpio_config( &mInConf ); // input without hard wired pull up/down -> pull up
}

void Keypad::InOpenDrain()
{
    mInConf.pull_up_en   = GPIO_PULLUP_DISABLE;
    mInConf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config( &mInConf ); // input without hard wired pull up/down -> pull up
}


u8 Keypad::Num( u8 o, u8 i )  // -> outpin pin o x input pin i -> keypad number 0..15
{
                              // col: 0,1,2,     3
    static const u8 s_num[4][4] = { { 1,2,3,   0xA },   // row 0
                                    { 4,5,6,   0xB },   // row 1
                                    { 7,8,9,   0xC },   // row 2
                                  { 0xF,0,0xE, 0xD } }; // row 3
    if (mColOut)
        return s_num[i][o];  // [row][col] = [i][o]
    return s_num[o][i];      // [row][col] = [o][i]
}

void Keypad::OnSequence( const char * seq )   // pause after sequence / seq is hex string
{
    ESP_LOGI( TAG, "seq %s", seq );

    Mqtinator::Instance().Pub( "seq", seq );
    Indicator::Instance().SigDelay( configTICK_RATE_HZ/4 );  // 0.25 (more) seconds signal pause
}

void Keypad::OnKeyPress( u8 num )
{
    char c = num + '0';
    switch (num) {
        case 10: c = 'A'; break;
        case 11: c = 'B'; break;
        case 12: c = 'C'; break;
        case 13: c = 'D'; break;
        case 14: c = '#'; break;
        case 15: c = '*'; break;
    }
    ESP_LOGI( TAG, "key %c - mask = %#x", c, 1 << num );

    static char s_buf[2] = "x";
    s_buf[0] = c;
    Mqtinator::Instance().Pub( "key", s_buf );
}

void Keypad::OnRelease()
{
    ESP_LOGD( TAG, "key rel. (mask = 0)" );
    Mqtinator::Instance().Pub( "rel", "0" );
}

void Keypad::OnMultiKey( u16 mask, u16 oldmask )
{
    ESP_LOGI( TAG, "key ev - mask = %#x", mask );
    const char * const topic = ((oldmask & mask) == mask) ? "mrel" : "mask";
    static char s_buf[8];
    char * bp = & s_buf[sizeof(s_buf) - 1];
    *bp = 0;
    do {
        *--bp = (mask & 0xf) + '0' + ((((mask & 0xf) / 10) * ('A' - '0' - 10)));
        mask >>= 4;
    } while (mask && (bp > s_buf));
    if (bp >= &s_buf[2]) {
        *--bp = 'x';  // leading 0x
        *--bp = '0';
    }
    Mqtinator::Instance().Pub( topic, bp );
}

void Keypad::Run()
{
    uint8_t const longDelay  = configTICK_RATE_HZ / 10;
    uint8_t const shortDelay = configTICK_RATE_HZ / 50;
    uint8_t delay = longDelay;
    TickType_t lastRelease = 0;
    Indicator & indicator = Indicator::Instance();

    char seq[16];
    char * const seqend = & seq[sizeof(seq) - 1];
    char * sp = &seq[-1];

#if (LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG)
    u16  inMatchPrev[mNofOut];
    for (u8 o = 0; o < mNofOut; ++o)
        inMatchPrev[o] = 0;
#endif

    while (true) {
        vTaskDelay( delay );

        u16 numMask = 0;
        u8 num = 0;  // any (not checked when numMask == 0)

        GPIO.enable_w1ts = mAllOut;
        {
            u16 inMatch;
            u16 inPrev;
            u16 inHigh;
            u16 inLow;

            // InPullDown();
            GPIO.out_w1ts = mAllOut;   // high
            ets_delay_us(300);
            inHigh = GPIO.in & 0xffff;
            ets_delay_us(100);
            inHigh |= (GPIO.in & 0xffff);  // any pin pulled up after delay?

            InPullUp();
            GPIO.out_w1tc = mAllOut;   // low
            ets_delay_us(300);
            inLow = GPIO.in & 0xffff;
            ets_delay_us(100);
            inLow &= (GPIO.in & 0xffff);  // any pin pulled down after delay?
            InOpenDrain();

            inMatch = ((inHigh & mPullDownPins)
                    | (~inLow & ~mPullDownPins)) & mAllIn;

            if (inMatch) {  // at least one key pressed
                GPIO.enable_w1tc = mAllOut & ~(1 << mOut[0]);   // disable others
                for (u8 o = 0; o < mNofOut; ++o) {
                    GPIO.enable_w1ts = (1 << mOut[o]);  // enable out

                    // InPullDown();
                    GPIO.out_w1ts = 1 << mOut[o];       // high
                    ets_delay_us(300);
                    inHigh = GPIO.in & 0xffff;
                    do {
                        ets_delay_us(100);
                        inPrev = inHigh;
                        inHigh = GPIO.in & 0xffff;
                    } while ((inHigh ^ inPrev) & mPullDownPins & mAllIn);

                    InPullUp();
                    GPIO.out_w1tc = 1 << mOut[o];       // low
                    ets_delay_us(300);
                    inLow = GPIO.in & 0xffff;
                    do {
                        ets_delay_us(100);
                        inPrev = inLow;
                        inLow = GPIO.in & 0xffff;
                    } while ((inLow ^ inPrev) & ~mPullDownPins & mAllIn);
                    InOpenDrain();

                    GPIO.enable_w1tc = (1 << mOut[o]);  // disable out

                    inMatch = ((inHigh & mPullDownPins)
                            | (~inLow & ~mPullDownPins)) & mAllIn;

                    if (! inMatch) {
                        EXPRD( inMatchPrev[o] = 0 )
                        continue;
                    }
#if (LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG)
                    if (inMatchPrev[o] != inMatch) {
                        inMatchPrev[o] = inMatch;
                        ESP_LOGD( TAG, "out 0%o: (0%o & 0%o) | (~0%o & 0%o) == 0%o",
                                    1 << mOut[o], inHigh, mPullDownPins & mAllIn,
                                                  inLow, ~mPullDownPins & mAllIn,
                                                                            inMatch );
                    }
#endif
                    for (u8 i = 0; (i < mNofIn) && inMatch; ++i)
                        if (inMatch & (1 << mIn[i])) {
                            num = Num( o, i );      // [row][col]
                            numMask |= 1 << num;
                        } // for i
                } // for o

                if (numMask == 0xc000)  // '*'=0xf and '#'=0xe
                    esp_restart();      // (handy way to reset)

                if (! numMask && ! mNumMask) {
                    ESP_LOGW( TAG, "fast detection said 'at least one key pressed'"
                                   " - fix fast detection in case of frequent spurious detection" );
                }
            } // if colMatch
        }
        GPIO.enable_w1tc = mAllOut;

        if (numMask == mNumMask) {
            if (lastRelease) {
                TickType_t now = xTaskGetTickCount();
                TickType_t diff = now - lastRelease;
                if (diff >= (configTICK_RATE_HZ / 1)) {
                    // one second pause: send sequence
                    if ((sp > seq) && (sp < seqend)) {
                        *++sp = 0;
                        OnSequence( seq );
                    }
                    lastRelease = 0;
                    sp = &seq[-1];
                    delay = longDelay;
                }
            }
            continue;
        }

        indicator.SigDelay( configTICK_RATE_HZ + configTICK_RATE_HZ/5 );  // 1.2 (more) seconds signal pause

        if (! numMask) {
            lastRelease = xTaskGetTickCount();
            if (! lastRelease)
                lastRelease = 1;
            indicator.Steady( 0 );
            if (mMultiKey) {
                mMultiKey = 0;
                OnMultiKey( 0, mNumMask );
            } else
                OnRelease();
        } else {
            lastRelease = 0;
            if (! mNumMask) {
                indicator.Steady( 1 );
            }
            if (numMask != (1 << num))
                mMultiKey = 1;
            if (mMultiKey) {
                OnMultiKey( numMask, mNumMask );
            } else {
                if (sp < seqend) {
                    *++sp = num + '0' + ((num / 10) * ('A' - '0' - 10));
                }
                OnKeyPress( num );
            }
            delay = shortDelay;
        }

        mNumMask = numMask;
    }
}
