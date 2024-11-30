/*
 * Indicator.cpp
 */

#include "Indicator.h"

#include <esp_log.h>

const char *const TAG = "Indicator";

namespace
{
TickType_t now()
{
    return xTaskGetTickCount();
}
}

extern "C" void IndicatorTask( void * indicator )
{
    ((Indicator*) indicator)->Run();
}

Indicator::Indicator()
{
}

static Indicator s_indicator{};

Indicator& Indicator::Instance()
{
    // app will crash when using instance pattern:
    // static Indicator inst{};
    // return inst;
    return s_indicator;
}

bool Indicator::Init( gpio_num_t pinPrimary, gpio_num_t pinSecondary ) 
{
    mPin[0] = pinPrimary;
    mPin[1] = pinSecondary;

    gpio_config_t io_conf;

    io_conf.pin_bit_mask = (1 << mPin[0]);
    if (mPin[1] < GPIO_NUM_MAX)
        io_conf.pin_bit_mask |= (1 << mPin[1]);
    io_conf.mode = GPIO_MODE_OUTPUT;       // set as output mode
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;    // disable pull-up mode
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;  // disable pull-down mode
    io_conf.intr_type = GPIO_INTR_DISABLE;      // disable interrupt

    gpio_config( &io_conf );    // configure GPIO with the given settings
    gpio_set_level( mPin[0], 0 );   // low active - switch on
    if (mPin[1] < GPIO_NUM_MAX)
        gpio_set_level( mPin[1], 0 );   // low active - switch on

    mSemaphore = xSemaphoreCreateBinary();
    xTaskCreate( IndicatorTask, "Indicator", /*stack size*/1024, this,
                 /*prio*/ 1, &mTaskHandle );
    if (!mTaskHandle) {
        ESP_LOGE( TAG, "xTaskCreate failed" );
        return false;
    }

    if (!mSemaphore) {
        ESP_LOGE( TAG, "xSemaphoreCreateBinary failed" );
        return false;
    }

    return true;
}

void Indicator::Indicate( Indicator::STATUS status )
{
    unsigned long sigMask = 0;
    switch (status) {
    case STATUS_ERROR:      // ##########_##_##_##_
        sigMask = 0x1212121a;
        break;
    case STATUS_AP:         // #######_#_#######_#_
        sigMask = 0x1117;
        break;
    case STATUS_CONNECT:    // #########_#########_
        sigMask = 0x19;
        break;
    case STATUS_IDLE:       // #___________________
        sigMask = 0xa091;
        break;
    case STATUS_ACTIVE:     // ##########__________
        sigMask = 0xaa;
        break;
    }
    SigMask( sigMask );
}

void Indicator::SigMask( unsigned long priSigMask, unsigned long secSigMask )
{
    if ((mSigMask[0] == priSigMask) && (mSigMask[1] == secSigMask)) {
        mSigDelay = 0;
        return;
    }
    mBlink[0] = mBlink[1] = 0;
    mSigMask[0] = priSigMask;
    mSigMask[1] = secSigMask;
    for (uint8_t pin = 0; pin < 2; ++pin) {
        if (mSigMask[pin] <= 1)
            continue;
        uint8_t slots = 0;
        for (unsigned long mask = mSigMask[pin]; mask; mask >>= 4)
            slots += (mask & 0xf);
        mSigSlots[pin] = slots;
    }
    mSigDelay = 0;
    mSigStart = now();
    xSemaphoreGive( mSemaphore );
}

void Indicator::SigDelay( unsigned long ticksDelay )
{
    if (! mSigDelay) {
        // gpio_set_level( mPin[0], 1 ); - not to switch primary
        if (mPin[1] < GPIO_NUM_MAX)
            gpio_set_level( mPin[1], 1 );
    }
    mSigDelay = now() + ticksDelay;
    if (! mSigDelay)
        --mSigDelay;
}

void Indicator::Blink( uint8_t numPri, uint8_t numSec )
{
    mBlink[0] = numPri;
    mBlink[1] = numSec;
    xSemaphoreGive( mSemaphore );
}

void Indicator::Steady( uint8_t on )
{
    mBlink[0] = on ? 0xff : 0;
    xSemaphoreGive( mSemaphore );
}

void Indicator::Access( uint8_t ok )
{
    if (ok) {
        mBlink[1] = 1;  // pass -> 1x green
    } else {
        mBlink[0] = 2;  // fail -> 2x red
    }
    xSemaphoreGive( mSemaphore );
}

void Indicator::Pause( bool pause )
{
    mPause = pause;
    xSemaphoreGive( mSemaphore );
}

void Indicator::Run()
{
    enum {
        SLOT_TICKS      = configTICK_RATE_HZ / 10,  // 0.1 secs
        SLOT_TICKS_HALF = SLOT_TICKS / 2,           // to round
    };

    if (mPin[1] < GPIO_NUM_MAX)
        gpio_set_level( mPin[1], 1 );  // low active - switch off

    uint8_t       blinking = 0;
    uint8_t       newBlinking = 0;
    uint8_t       sigSlots[2];
    unsigned long sigMask[2];

    while (true) {
        if (mPause) {
            if (mPin[1] < GPIO_NUM_MAX)
                gpio_set_level( mPin[1], 1 );
            do {
                xSemaphoreTake( mSemaphore, portMAX_DELAY );
            } while (mPause);
        }

        for (int pin = 0; pin < 2; ++pin) {
            sigMask[pin]  = mSigMask[pin];
            sigSlots[pin] = mSigSlots[pin];
        }

        uint8_t minSlots2wait = 0xff;  // = no timeout

        if (mSigDelay) {
            long remDelay = mSigDelay - now();
            if (remDelay > 0) {
                unsigned long delaySlots = (remDelay + SLOT_TICKS_HALF) / SLOT_TICKS;
                if (delaySlots < 0xff) {
                    minSlots2wait = (delaySlots < 1) ? 1 : (uint8_t) delaySlots;
                }
                for (int pin = 0; pin < 2; ++pin) {
                    sigMask[pin]  = 0;  // delay signalling -> 0
                    sigSlots[pin] = 1;  // delay signalling (avoid div!0)
                }
            } else {  // delay expired
                mSigDelay = 0;      // no more delay
                mSigStart = now();  // restart
            }
        }

        // {
            newBlinking = 0;
            for (int pin = 0; pin < 2; ++pin) {
                if (mBlink[pin]) {
                    newBlinking |= 1 << pin;  // which LEDs shall blink
                    if (mBlink[pin] == 0xff) {
                        sigMask[pin] = 1;  // steady on
                        sigSlots[pin] = 1;
                    } else {
                        sigMask[pin] = 0x12;  // 2 slots on / 1 slot off
                        sigSlots[pin] = 3;
                    }
                } else if (blinking & (1 << pin)) {
                    sigMask[pin] = 0;  // stay off for 10 slots after blinking just finished
                    if (minSlots2wait > 10)
                        minSlots2wait = 10;
                }
            }
            if (blinking != newBlinking) {
                blinking  = newBlinking;
                mSigStart = now();
            }
            if ((blinking == 1) ||  // blink LED 0 ==> LED 1 off (2-blinking)==1
                (blinking == 2)) {  // blink LED 1 ==> LED 0 off (2-blinking)==0
                sigMask[2-blinking] = 0;
            }
        // }

        TickType_t diff = now() - mSigStart;
        TickType_t slot = (diff + SLOT_TICKS_HALF) / SLOT_TICKS;

        for (int pin = 0; pin < 2; ++pin) {
            if (mPin[pin] >= GPIO_NUM_MAX)
                continue;

            if (sigMask[pin] == 0) {
                gpio_set_level( mPin[pin], 1 );   // low active - switch off
                continue;
            }
            if (sigMask[pin] == 1) {
                gpio_set_level( mPin[pin], 0 );   // low active - switch on
                continue;
            }

            uint8_t pSlot = (uint8_t) (slot % sigSlots[pin]);
            unsigned long mask = sigMask[pin];
            int next = mask & 0xf;
            int phase = 0;
            while (next <= pSlot) {
                mask >>= 4;
                next += (mask & 0xf);
                ++phase;
                if (! mask) {  // should not happen!
                    phase = 1;
                    next = sigSlots[pin];
                    break;
                }
            }

            gpio_set_level( mPin[pin], phase & 1 );

            if (phase && mBlink[pin] && (mBlink[pin] != 0xff))
                --(mBlink[pin]);

            // if (next > pSlot) {
            uint8_t slots2wait = (next - pSlot);
            if (minSlots2wait > slots2wait)
                minSlots2wait = slots2wait;
            // } else
            //     minSlots2wait = 1;  // should not happen - just for safety
        }
        if (minSlots2wait == 0xff)
            xSemaphoreTake( mSemaphore, portMAX_DELAY );
        else
            xSemaphoreTake( mSemaphore, ((TickType_t) minSlots2wait) * SLOT_TICKS );
    }
}
