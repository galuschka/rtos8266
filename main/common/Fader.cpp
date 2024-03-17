/*
 * Fader.cpp
 */
//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#if 1
# define DEBUG
# define REL(expr)
# define DBG(expr)   expr;
# define DBG_DELAY   vTaskDelay( 1 );
#else
# define NDEBUG
# define REL(expr)   expr;
# define DBG(expr)
# define DBG_DELAY
#endif

#include "Fader.h"

#include <portmacro.h>
#include <driver/hw_timer.h>    // hw_timer_init(), ...

#include <math.h>

#include <esp_log.h>
#include <driver/gpio.h>
#include "esp8266/gpio_struct.h"


extern "C" {

extern uint64_t g_esp_os_cpu_clk;  // at 80 MHz -> 32 bit roll over every 53 secs

void FaderTask( void * fader )
{
    ((Fader *) fader)->Run();
}

void FaderTimerIntr( void * fader )
{
    ((Fader *) fader)->TimerIntr();
}

} // extern C

namespace
{
const char * TAG = "Fader";

enum {
    TMR_CLK  = TIMER_BASE_CLK,      // 80 MHz
    TMR_DIV  = TIMER_CLKDIV_16,     // 4 (>> 4)
    TMR_FREQ = TMR_CLK >> TMR_DIV,  // 5 MHz
    TMR_MAX  = TMR_FREQ / 100,      // 50,000: limit to not flickering (100 Hz)
    TMR_10MS = TMR_FREQ / 100,      // 50,000: load value for 10 msec
    TMR_1MS  = TMR_FREQ / 1000,     //  5,000: load value for  1 msec
    TMR_MIN  = TMR_1MS  / 20,       //    250: minimum resolution (interrupt latency)
};

unsigned long now()
{
    return (unsigned long) g_esp_os_cpu_clk;
}

} // namespace


Fader::Fader(                   gpio_num_t gpionumRed, gpio_num_t gpionumGreen, gpio_num_t gpionumBlue )
        : mPwmGpioMask { (uint16_t) ((1 << gpionumRed)    | (1 << gpionumGreen)    | (1 << gpionumBlue)) }
        , mGpioNum {             (uint8_t) gpionumRed,  (uint8_t) gpionumGreen,  (uint8_t) gpionumBlue }
{
}

Fader::~Fader()
{
    xTaskNotify( mTaskHandle, 1 << NOTIFY_HALT, eSetBits );
}

bool Fader::Start()
{
    xTaskCreate( FaderTask, TAG, /*stack size*/4096, this, /*prio*/ 1, &mTaskHandle );
    if (!mTaskHandle) {
        ESP_LOGE( TAG, "xTaskCreate failed" );
        return false;
    }

    return true;
}

void Fader::Fade( float time, const float * level )
{
    Set & nextSet = mSet[mActiveSet ^ 1];
    for (int c = 0; c < 3; ++c) {
        nextSet.mStartLvl[c]  = mCurrLvl[c];
        nextSet.mTargetLvl[c] = level ? level[c] : 0;
    }

    nextSet.mStart  = now() - TMR_1MS;
    nextSet.mClocks = (ulong) (time * CPU_CLK_FREQ + 0.5);
    if (! nextSet.mClocks)
        nextSet.mClocks = 1;  // indicates "not fade"

    ESP_LOGD( TAG, "fade: %lu clocks starting at %lu (%lu interrputs)",
                        nextSet.mClocks, nextSet.mStart, mIntrCnt );

    if (mStatus == STATUS_IDLE) {
        Trigger( NOTIFY_TRIGGER );
    }
}

void Fader::Trigger( uint8_t notify )
{
    ESP_LOGD( TAG, "trigger task with notify %d", notify );
    DBG_DELAY
    xTaskNotify( mTaskHandle, 1 << notify, eSetBits );
}

void Fader::Run()
{
    ESP_LOGD( TAG, "task entry" );
    DBG_DELAY

    gpio_config_t io_conf;
    io_conf.intr_type    = GPIO_INTR_DISABLE;
    io_conf.mode         = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = mPwmGpioMask;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en   = GPIO_PULLUP_DISABLE;
    gpio_config( &io_conf );
    GPIO.out_w1tc |= mPwmGpioMask;  // all off

    ESP_LOGD( TAG, "will do timer init" );
    DBG_DELAY
    esp_err_t err;
    err = hw_timer_init( FaderTimerIntr, this );
    if (err != ESP_OK) {
        ESP_LOGE( TAG, "hw_timer_init failed (%d)", err );
    }

    ESP_LOGD( TAG, "will loop" );
    DBG_DELAY
    ulong lastIntrCnt = mIntrCnt - 1;
    while (true) {
        uint32_t notification = 0;
        BaseType_t succ = xTaskNotifyWait( 0, 0xffffffff, & notification, configTICK_RATE_HZ );

        ESP_LOGD( TAG, "wakeup: notify %d", notification );
        DBG_DELAY

        if (! succ) {
#           ifdef NDEBUG
                if (mIntrRunning && (lastIntrCnt == mIntrCnt)) {
                    ESP_LOGW( TAG, "lost interrupt?" );
                    hw_timer_alarm_ticks( TMR_DIV, TMR_MIN, false );
                }
#           endif
            lastIntrCnt = mIntrCnt;
            continue;
        }

        if (notification & (1 << NOTIFY_HALT)) {
            break;
        }

        if (notification & (1 << NOTIFY_TRIGGER)) {
            if (mStatus == STATUS_IDLE) {
                mStatus = STATUS_FADING;

                ESP_LOGD( TAG, "will fade in/out/mixed" );
                DBG_DELAY

                if (PhaseCalc( 1 )) {
                    mActiveSet ^= 1;
#                   ifdef DEBUG
                        Set & set = mSet[mActiveSet];
                        for (uint8_t g = 0; (g==0) || ((g <= 3) && set.tmrLoad[g]); ++g) {
                            ESP_LOGD( TAG, "tmrLoad[%d]: %6ld - pins 0x%04x", g, set.tmrLoad[g], set.mPinOnOff[g] );
                        }
                        DBG_DELAY  // wait for output
#                   else
                        if (! mIntrRunning) {
                            mPhase = 0;
                            err = hw_timer_alarm_ticks( TMR_DIV, tmrLoad[1], false );
                            if (err != ESP_OK) {
                                ESP_LOGE( TAG, "hw_timer_alarm_ticks failed (%d)", err );
                                continue;
                            }
                            mIntrRunning = true;
                        }
#                   endif
                } else {
                    ESP_LOGD( TAG, "no pwm after PhaseCalc" );
                    mStatus = STATUS_IDLE;  // no pwm to serve
                }
            }
        }

        if (notification & (1 << NOTIFY_FADEEND)) {
            if (mStatus == STATUS_FADING) {
                mStatus = STATUS_IDLE;

                ESP_LOGD( TAG, "end of fade in/out/mixed" );
              /*
                if ((mTgtLevel == 1) || ! (mCalcUpRGB & 2)) {
                    // io_conf.mode = GPIO_MODE_OUTPUT_OD;
                    // gpio_config( &io_conf );
                    gpio_set_level( (gpio_num_t) mGpioNum, 0 );  // -> open drain
                    ESP_LOGD( TAG, "gpio set open drain" );
                }
              */
            }
        }
    }

    ESP_LOGD( TAG, "exiting..." );

    hw_timer_enable( false );
    hw_timer_deinit();

    io_conf.mode = GPIO_MODE_OUTPUT_OD;  // GPIO_MODE_OUTPUT
    gpio_config( &io_conf );
    GPIO.out_w1tc |= mPwmGpioMask;  // all off
}

void Fader::TimerIntr()
{
    ++mIntrCnt;

    Set & set = mSet[mActiveSet];
    if (set.mClocks && (mPhase == 0))
        if (! PhaseCalc()) {
            mIntrRunning = false;
            return;  // no pwm to serve
        }

    if (mPhase)
        GPIO.out_w1tc |= set.mPinOnOff[mPhase];
    else
        GPIO.out_w1ts |= set.mPinOnOff[mPhase];

    if ((mPhase < 3) && set.mPinOnOff[mPhase+1])
        ++mPhase;
    else
        mPhase = 0;

    REL( hw_timer_set_load_n_en_isr( set.tmrLoad[mPhase] ) )
}

bool Fader::PhaseCalc( uint8_t setToggle )
{
    DBG( ESP_LOGD( TAG, "calc entry" ) )
    DBG_DELAY

    Set & set = mSet[mActiveSet^setToggle];

    ulong const runClks = now() - set.mStart;  // difference to respect rollover

    if (set.mClocks) {
        long const remainClks = set.mClocks - runClks;  // becomes negative when done
        if (remainClks < TMR_1MS) {
            set.mClocks = 0;
            set.mStart = 0;

            uint8_t pwm = 0;
            // mCalcUpRGB &= 7;
            for (int c = 0; c < 3; ++c) {
                mCurrLvl[c] = set.mTargetLvl[c];
                if (set.mTargetLvl[c] <= 0) {
                    GPIO.out_w1tc |= 1 << mGpioNum[c];
                    // mCalcUpRGB &= ~(1 << c);
                } else if (set.mTargetLvl[c] >= 1) {
                    GPIO.out_w1ts |= 1 << mGpioNum[c];
                    // mCalcUpRGB |= 1 << c;
                } else {
                    pwm |= 1 << c;
                }
            }
            if (! setToggle) {  // called from ISR
                xTaskNotify( mTaskHandle, 1 << NOTIFY_FADEEND, eSetBits );
            }

            if (! pwm) {
                DBG( ESP_LOGD( TAG, "no pwm anymore - just return" ) )
                set.mPinOnOff[0] = 0;
                set.tmrLoad[0] = 0;
                return false;  // no need to restart timer anymore
            }
            DBG( ESP_LOGD( TAG, "fade complete, but still to pwm" ) )
        } else {
            DBG( ESP_LOGD( TAG, "still to fade" ) )
        }
    }
    DBG_DELAY

    ulong tmrLoad[4];
    float colLoad[3];
    float load;
    uint8_t c;  // color rgb 0,1,2
    uint8_t r;  // rank shortest..longest phase
    uint8_t g;  // pwm group: 0 = off / 1,2,3 = grp 1,2,3 / 4 = on

    for (c = 0; c < 3; ++c) {
        load = set.mTargetLvl[c];

        if (set.mClocks && (set.mStartLvl[c] != set.mTargetLvl[c])) {  // still fading in or out
            float runRel = (runClks * 1.0 / set.mClocks);  // 0..1
            float adopt;
            if (set.mStartLvl[c] < set.mTargetLvl[c]) {   // fade-in
                adopt = set.mStartLvl[c];
            } else {                              // fade-out
                adopt = set.mTargetLvl[c];
                runRel = 1 - runRel;              // 1..0
            }
            runRel = adopt + ((1.0 - adopt) * runRel);  // adopt..1
            load *= (runRel * runRel);
        }
        if (load < 0)       load = 0;
        else if (load > 1)  load = 1;
        colLoad[c] = load;
    }
#ifdef DEBUG
    DBG( ESP_LOGD( TAG, "load per color:" ) )
    DBG_DELAY
    for (c = 0; c < 3; ++c) {
        DBG( ESP_LOGD( TAG, "color %d: %3d ‰", c, (int) (colLoad[c] * 1000) ) )
    }
    DBG_DELAY
#endif

    // order colLoad[c]:
#define TOGGLE(i,j) uint8_t x = col[i]; col[i] = col[j]; col[j] = x;

    uint8_t col[3] = { 0, 1, 2 };
    if (colLoad[col[0]] > colLoad[col[1]]) { TOGGLE(0,1) }
    if (colLoad[col[1]] > colLoad[col[2]]) { TOGGLE(1,2) }
    if (colLoad[col[0]] > colLoad[col[1]]) { TOGGLE(0,1) }
#undef TOGGLE

#ifdef DEBUG
    DBG( ESP_LOGD( TAG, "loads after sort:" ) )
    DBG_DELAY
    for (r = 0; r < 3; ++r) {
        DBG( ESP_LOGD( TAG, "rank %d: %3d ‰ - color %d", r, (int) (colLoad[col[r]] * 1000), col[r] ) )
    }
    DBG_DELAY
#endif

    // group by loads
    uint8_t rgb[5] = { 0, 0, 0, 0, 0 };  // [0,4]:steady off/on / [1,2,3]:off after timeout
    for (r = 0; (r < 3) && (colLoad[col[r]] == 0); ++r)
        rgb[0] |= 1 << col[r];  // steady off
    for (g = 0; (r < 3) && (colLoad[col[r]] < 1); ++r)
        rgb[++g] = 1 << col[r];  // will join later
    for (; r < 3; ++r)
        rgb[4] |= 1 << col[r];  // steady on

    DBG( ESP_LOGD( TAG, "rgb steady off: %d", rgb[0] ) )
    DBG( ESP_LOGD( TAG, "rgb pwm grp 1:  %d", rgb[1] ) )
    DBG( ESP_LOGD( TAG, "rgb pwm grp 2:  %d", rgb[2] ) )
    DBG( ESP_LOGD( TAG, "rgb pwm grp 3:  %d", rgb[3] ) )
    DBG( ESP_LOGD( TAG, "rgb steady on:  %d", rgb[4] ) )

    GPIO.out_w1tc |= pinsOfCols( rgb[0] );
    GPIO.out_w1ts |= pinsOfCols( rgb[4] );

    if (! g) {  // no pwm... - should not happen
        // ESP_LOGE( TAG, "no PWM color (probably rounding problem)" );
        DBG( ESP_LOGD( TAG, "no pwm grp - return here" ) )
        set.mPinOnOff[0] = 0;
        set.tmrLoad[0] = 0;
        return false;
    }
    uint8_t gmax = g;

    // now we have rgb[0,1,2,3,4] of col masks to colors with ascending load

    load = colLoad[highestbit(rgb[1])];  // lowest load
    ulong total;

    DBG( ESP_LOGD( TAG, "min. load:%3d ‰", (int) (load * 1000) ) )
    DBG_DELAY

    if (load < (TMR_MIN * 1.0 / TMR_1MS)) {
        // have to use inverse calculation of on:off
        tmrLoad[1] = TMR_MIN;
        total = (ulong) (TMR_MIN / load + 0.5);  // total
        if (total > TMR_MAX)
            total = TMR_MAX;
        for (g = 2; g <= gmax; ++g) {
            tmrLoad[g] = (ulong) (total * colLoad[highestbit(rgb[g])] + 0.5);
            if (tmrLoad[g] < TMR_MIN)
                tmrLoad[g] = TMR_MIN;
        }
    } else {
        total = TMR_1MS;
        for (g = 1; g <= gmax; ++g) {
            tmrLoad[g] = (ulong) (total * colLoad[highestbit(rgb[g])] + 0.5);
            if (tmrLoad[g] < TMR_MIN)
                tmrLoad[g] = TMR_MIN;
        }
    }
#ifdef DEBUG
    DBG( ESP_LOGD( TAG, "total phase duration: %6lu", total ) )
    for (g = 1; g <= gmax; ++g) {
        DBG( ESP_LOGD( TAG, "phase dur. pwm grp %d: %6lu", g, tmrLoad[g] ) )
    }
    DBG_DELAY
#endif

    // now join groups having less then TMR_MIN difference of tmrLoad values

    for (g = 2; g <= gmax; ++g) {
        long diff = tmrLoad[g] - tmrLoad[g-1];
        if (diff < TMR_MIN) {
            rgb[g-1] |= rgb[g];
            for (r = g; r < gmax; ++r) {
                rgb[r] = rgb[r+1];
                tmrLoad[r] = tmrLoad[r+1];
            }
            rgb[gmax] = 0;
            tmrLoad[gmax] = 0;
            --gmax;
            --g;
        }
    }
#ifdef DEBUG
    for (g = 1; g <= gmax; ++g) {
        DBG( ESP_LOGD( TAG, "joined ph. pwm grp %d: %6lu", g, tmrLoad[g] ) )
    }
    DBG_DELAY
#endif

    tmrLoad[0] = total - tmrLoad[gmax];  // last phase
    // tmrLoad[2,3] shall be differences
    for (g = 2; g <= gmax; ++g)
        tmrLoad[g] -= tmrLoad[g-1];

    DBG( ESP_LOGD( TAG, "off phase duration:    %6lu", tmrLoad[0] ) )
    for (g = 0; g <= gmax; ++g) {
        DBG( ESP_LOGD( TAG, "phase diff pwm grp %d: %6lu", g, tmrLoad[g] ) )
    }

    // which pins to toggle:
    set.mPinOnOff[0] = 0;
    set.tmrLoad[0] = tmrLoad[0];
    for (g = 1; g <= gmax; ++g) {
        uint16_t pins = pinsOfCols( rgb[g] );
        set.mPinOnOff[g] = pins;    // phase 1..3: switch off group of LEDs
        set.mPinOnOff[0] |= pins;   // phase 0: switch on all pwm LEDs
        set.tmrLoad[g] = tmrLoad[g];
    }
    if (gmax < 3) {
        set.mPinOnOff[gmax+1] = 0;
        set.tmrLoad[gmax + 1] = 0;
    }

    return true;
}
