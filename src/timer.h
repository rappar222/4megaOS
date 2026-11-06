#ifndef TIMER_H
#define TIMER_H

#include "types.h"

void timer_init(uint32_t frequency);
uint32_t timer_get_ticks(void);
uint32_t timer_get_uptime_seconds(void);
void timer_sleep(uint32_t ticks);

#endif
