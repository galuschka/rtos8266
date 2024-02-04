/* compatibility header to map misc of esp-sdk to esp8266-rtos-sdk
*/
#ifndef __ESP_MISC_H__
#define __ESP_MISC_H__

#include <stdint.h>
#include <rom/ets_sys.h>

#ifdef	__cplusplus
extern "C" {
#endif

static inline void sdk_os_delay_us( uint16_t us )
{
    ets_delay_us( us );
}

#ifdef	__cplusplus
}
#endif

#endif
