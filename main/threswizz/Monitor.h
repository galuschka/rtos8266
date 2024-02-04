/*
 * Monitor.h
 */

#ifndef MAIN_MONITOR_H_
#define MAIN_MONITOR_H_

#include "AnalogReader.h"

struct httpd_req;

class Monitor
{
public:
    typedef AnalogReader::value_t value_t;

    Monitor( AnalogReader & analog );
    ~Monitor();

    void SetThres( value_t off, value_t on ) {
        ThresOff = off;
        ThresOn  = on;
    };

    void Show( struct httpd_req * req ) const;
private:
    AnalogReader & Reader;
    value_t        ThresOff { 0x8000 };
    value_t        ThresOn  { 0x8000 };
};

#endif /* MAIN_MONITOR_H_ */
