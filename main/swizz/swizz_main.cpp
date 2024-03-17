/*
 * swizz_main.cpp
 *
 * D1 mini's usable GPIOs: 4,5, 12,13,14
 *                         ___   _   _
 *                        | | |_| |_| |
 *                        | |        o|           <- wifi antenna and LED
 *
 *               /RST   - RST      X TX - GPIO1
 *               ADC0   - A0       X RX - GPIO3
 * relay 4 <---  GPIO16 - D0 !    ok D1 - GPIO5  ---> relay 6
 * relay 3 <---  GPIO14 - D5 ok   ok D2 - GPIO4  ---> relay 5
 * relay 2 <---  GPIO12 - D6 ok    ! D3 - GPIO0
 * relay 1 <---  GPIO13 - D7 ok    ! D4 - GPIO2  (onboard LED)
 *               GPIO15 - D8 !        G  - GND
 *               Aref   - 3V3        5V - power supply
 */

#include "Init.h"
#include "Relay.h"
#include "Swizz.h"

extern "C" void app_main()
{
    Init::Init();

    // GPIO_NUM_15 does not work - boot error when used

                    // open drain mode and low active
    Relay relay[] { Relay( GPIO_NUM_13, true, true, Relay::MODE_OFF ),
                    Relay( GPIO_NUM_12, true, true, Relay::MODE_OFF ),
                    Relay( GPIO_NUM_14, true, true, Relay::MODE_OFF ),
                    Relay( GPIO_NUM_16, true, true, Relay::MODE_OFF ),
                    Relay( GPIO_NUM_4,  true, true, Relay::MODE_OFF ),
                    Relay( GPIO_NUM_5,  true, true, Relay::MODE_OFF ),
                  };
#define NELEMENTS(x) (sizeof(x)/sizeof(x[0]))
    Swizz swizz { relay, NELEMENTS(relay) };
    swizz.Run();
}
