/* IR Rx handler
*/

#include "Infrared.h"

#include <driver/hw_timer.h>
#include <esp8266/gpio_struct.h>
#include <esp_log.h>


namespace {
const char * const TAG = "Infrared";
}

extern "C" {
void InfraredTask( void * infrared )
{
    ((Infrared*) infrared)->Run();
}

void rx_start_callback( void * arg )
{
    ((Infrared *) arg)->RxStartISR();
}

void rx_timer_callback( void * arg )
{
    ((Infrared *) arg)->RxTimerISR();
}
}

Infrared::Infrared( gpio_num_t rxPin, uint32_t baudrate )
 //   mBaudrate { baudrate }
 // , mBitUsec { (1000000UL + baudrate/2) / baudrate }  // in µsecs
    : mTimerDiv       { 4 }
    , mTimerLoadStart { (((CPU_CLK_FREQ) >> 4) + baudrate/2) / baudrate }
    , mTimerLoadData  { (((CPU_CLK_FREQ) >> 4) + baudrate/2) / baudrate }
{
    rx.pin = rxPin;
}

Infrared::~Infrared()
{
    hw_timer_deinit();
}

bool Infrared::Init()
{
    xTaskCreate( InfraredTask, "Infrared", /*stack size*/1024, this, /*prio*/ configMAX_PRIORITIES - 1, &mTaskHandle );
    if (!mTaskHandle) {
        ESP_LOGE( TAG, "xTaskCreate failed" );
        return false;
    }
    return true;
}

void Infrared::Run()
{
    /* we get following signal:
     * __________          _          _          _          ____________
     *           |_XXXXXXXX |_XXXXXXXX |_XXXXXXXX |_XXXXXXXX
     *           | |      | | |      | | |      | | |      | |
     * ---idle-->|b|<data>|e|b|<data>|e|b|<data>|e|b|<data>|e|<--idle---
     *
     *      b: start (begin) -> low active  -> falling edge will discover
     *      e: stop  (end)   -> high active -> no edge or raising edge after bit 7
     *   data: data (not invers - high active)
     *   idle: idle
     */
    mConf.pin_bit_mask = 1 << rx.pin;           // pin
    mConf.mode         = GPIO_MODE_INPUT;       // input
    mConf.pull_up_en   = GPIO_PULLUP_DISABLE;   // pull-up mode
    mConf.pull_down_en = GPIO_PULLDOWN_DISABLE; // pull-down mode
    mConf.intr_type    = GPIO_INTR_NEGEDGE;     // interrupt on falling edge
    gpio_config( & mConf );

    // timer interrupt callback:
    hw_timer_init( rx_timer_callback, this );

    // GIO interrupt handler (falling edge of start bit)
    gpio_install_isr_service( 0 );  // so we can use gpio_isr_handler_add()
    rx.semaphore = xSemaphoreCreateBinary();  // where we get waked up
    esp_err_t err = gpio_isr_handler_add( rx.pin, rx_start_callback, this );
    if (err != ESP_OK) {
        ESP_LOGE( TAG, "gpio_isr_register() failed with error %d", err );
        return;
    }

    rx.mode = Mode::Wait;
    rx.bits = 8;

    while (1) {
        xSemaphoreTake( rx.semaphore, portMAX_DELAY );
        while (rx.full || (rx.rp != rx.wp)) {
            rxFunc( *rx.rp, rx.full );
            if (++rx.rp == rx.end)
                rx.rp = rx.buf;
            rx.full = 0;
        }
    }
}

void Infrared::RxStartISR()
{
    // further GPIO interrupts are ignored:
    mConf.intr_type = GPIO_INTR_DISABLE;  // no more PIN interrupts while reading byte
    gpio_config( & mConf );

    if (rx.mode != Mode::Wait)
        return;  // ignore interrupts in case not waiting for start

    // we got a start bit

    // start condition:
    rx.byte = 0;
    rx.bits = 0;
    rx.mode = Mode::Read;

    // 1st bit should be read after 1.5 bit time, but timer seems to be inaccurate.
    // We even need a shorter delay on first bit.
    hw_timer_alarm_ticks( mTimerDiv, mTimerLoadStart, false );
}

void Infrared::RxTimerISR()
{
    // timer (bit) interrupt

    if (rx.mode != Mode::Read) {
        hw_timer_enable( false );
        if (rx.mode == Mode::Stop) {
            rx.mode = Mode::Wait;
            mConf.intr_type = GPIO_INTR_NEGEDGE;     // interrupt on falling edge
            gpio_config( & mConf );
        }
        return;  // ignore interrupts in case not waiting timer interrupt
    }

    rx.byte |= (((GPIO.in >> rx.pin) & 1) << rx.bits);  // last bit will be 0x80

    if (! rx.bits)
        hw_timer_alarm_ticks( mTimerDiv, mTimerLoadData, true );

    if (++rx.bits > 7)
    {
        // start again on next start bit and save current byte
        uint8_t byte = rx.byte;
        rx.mode = Mode::Wait;
        mConf.intr_type = GPIO_INTR_NEGEDGE;     // interrupt on falling edge
        gpio_config( & mConf );

        hw_timer_enable( false );

        // last bit read: write to buffer and wakeup thread
        if (! rx.full) {
            *rx.wp = byte;
            if (++rx.wp == rx.end)
                rx.wp = rx.buf;
            if (rx.wp == rx.rp)
                rx.full = 1;
        }
        xSemaphoreGiveFromISR( rx.semaphore, 0 );
    }
}
