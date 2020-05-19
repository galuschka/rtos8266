/*
 * Monitor.h
 *
 *  Created on: 06.05.2020
 *      Author: holger
 */

#ifndef MAIN_MONITOR_H_
#define MAIN_MONITOR_H_

#include "AnalogReader.h"

struct httpd_req;

class Monitor
{
public:
    Monitor( AnalogReader & analog );
    ~Monitor();

    void Show( struct httpd_req * req );
private:
    AnalogReader &Reader;
};

#endif /* MAIN_MONITOR_H_ */
