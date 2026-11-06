#include "timer.h"
#include "idt.h"
#include "io.h"

static volatile uint32_t timer_ticks = 0;
static uint32_t timer_frequency = 100;

static void timer_callback(struct registers* regs) {
    (void)regs;
    timer_ticks++;
}

void timer_init(uint32_t frequency) {
    timer_frequency = frequency;
    register_interrupt_handler(32, timer_callback);

    uint32_t divisor = 1193180 / frequency;

    // Send command byte: Channel 0, lobyte/hibyte, Mode 3 (Square wave generator), binary
    outb(0x43, 0x36);

    uint8_t l = (uint8_t)(divisor & 0xFF);
    uint8_t h = (uint8_t)((divisor >> 8) & 0xFF);

    outb(0x40, l);
    outb(0x40, h);
}

uint32_t timer_get_ticks(void) {
    return timer_ticks;
}

uint32_t timer_get_uptime_seconds(void) {
    return timer_ticks / timer_frequency;
}

void timer_sleep(uint32_t ticks) {
    uint32_t target = timer_ticks + ticks;
    while (timer_ticks < target) {
        hlt();
    }
}
