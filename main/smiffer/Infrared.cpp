/* IR Rx handler
*/

#include "Infrared.h"

#include <driver/hw_timer.h>
#include <esp8266/gpio_struct.h>
#include <esp_log.h>


namespace {
const char * const TAG = "Infrared";
}


extern "C" void rx_start_callback( void * arg )
{
    ((Infrared *) arg)->RxStartISR();
}

extern "C" void rx_timer_callback( void * arg )
{
    ((Infrared *) arg)->RxTimerISR();
}

Infrared::Infrared( gpio_num_t rxPin, uint32_t baudrate )
    : mBitUsec { 1000000UL / baudrate }  // in µsecs
{
    rx.pin = rxPin;
}

Infrared::~Infrared()
{
    hw_timer_deinit();
}

void Infrared::Run()
{
    {
        gpio_config_t mConf;

        mConf.pin_bit_mask = 1 << rx.pin;           // pin
        mConf.mode         = GPIO_MODE_INPUT;       // input
        mConf.pull_up_en   = GPIO_PULLUP_DISABLE;   // pull-up mode
        mConf.pull_down_en = GPIO_PULLDOWN_DISABLE; // pull-down mode
        mConf.intr_type    = GPIO_INTR_NEGEDGE;     // interrupt on falling edge
        gpio_config( & mConf );
    }

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
    if (rx.mode != Mode::Wait)
        return;  // ignore interrupts in case not waiting for start

    // we got a start bit

    // further GPIO interrupts are ignored:
    // gpio_isr_handler_remove( rx.pin );

    // start condition:
    rx.byte = 0;
    rx.bits = 0;
    rx.mode = Mode::Read;

    // 1st bit is read after 1.x bit time
    hw_timer_alarm_us( mBitUsec, true );
}

void Infrared::RxTimerISR()
{
    // if (! rx.bits)
    //    hw_timer_alarm_us( mBitUsec, true );  // interval: one bit time / reload=true: cyclic

    if (rx.mode != Mode::Read) {
        hw_timer_enable( false );
        if (rx.mode == Mode::Stop)
            rx.mode = Mode::Wait;
        return;  // ignore interrupts in case not waiting timer interrupt
    }

    // timer (bit) interrupt

    rx.byte |= (((GPIO.in >> rx.pin) & 1) << rx.bits);  // last bit will be 0x80

    if (++rx.bits > 7)
    {
        // last bit read: write to buffer and wakeup thread
        if (! rx.full) {
            *rx.wp = rx.byte;
            if (++rx.wp == rx.end)
                rx.wp = rx.buf;
            if (rx.wp == rx.rp)
                rx.full = 1;
        }
        xSemaphoreGiveFromISR( rx.semaphore, 0 );

        // start again on next start bit
        hw_timer_enable( false );
        rx.mode = Mode::Wait;
    }
}
