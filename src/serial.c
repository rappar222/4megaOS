#include "serial.h"
#include "io.h"

#define COM1 0x3F8

static bool serial_present = false;

void serial_init(void) {
    outb(COM1 + 1, 0x00);    // Disable all interrupts
    outb(COM1 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(COM1 + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
    outb(COM1 + 1, 0x00);    //                  (hi byte)
    outb(COM1 + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(COM1 + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    outb(COM1 + 4, 0x1E);    // Set in loopback mode, test the serial chip

    // Test serial chip: send byte 0xAE and check if received
    outb(COM1 + 0, 0xAE);
    if (inb(COM1 + 0) != 0xAE) {
        // Serial port is absent (e.g. VirtualBox without UART configured)
        serial_present = false;
        return;
    }

    // Serial port exists! Set to normal operation mode
    outb(COM1 + 4, 0x0F);
    serial_present = true;
}

static bool is_transmit_empty(void) {
    if (!serial_present) return false;
    return (inb(COM1 + 5) & 0x20) != 0;
}

void serial_putchar(char c) {
    if (!serial_present) return;
    while (!is_transmit_empty());
    outb(COM1, (uint8_t)c);
}

void serial_puts(const char* str) {
    if (!serial_present) return;
    while (*str) {
        if (*str == '\n') {
            serial_putchar('\r');
        }
        serial_putchar(*str++);
    }
}

bool serial_received(void) {
    if (!serial_present) return false;
    return (inb(COM1 + 5) & 1) != 0;
}

char serial_getchar(void) {
    if (!serial_present) return 0;
    while (!serial_received());
    return (char)inb(COM1);
}
