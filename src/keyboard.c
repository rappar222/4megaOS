#include "keyboard.h"
#include "idt.h"
#include "io.h"
#include "serial.h"

#define KEYBOARD_BUFFER_SIZE 256

static char key_buffer[KEYBOARD_BUFFER_SIZE];
static volatile uint32_t buf_read_idx = 0;
static volatile uint32_t buf_write_idx = 0;
static bool shift_pressed = false;

static const char scancode_ascii_lower[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*',  0,   ' '
};

static const char scancode_ascii_upper[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*',  0,   ' '
};

static void buffer_put(char c) {
    uint32_t next = (buf_write_idx + 1) % KEYBOARD_BUFFER_SIZE;
    if (next != buf_read_idx) {
        key_buffer[buf_write_idx] = c;
        buf_write_idx = next;
    }
}

static void keyboard_callback(struct registers* regs) {
    (void)regs;
    uint8_t scancode = inb(0x60);

    // Left shift (0x2A), Right shift (0x36) pressed
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = true;
        return;
    }
    // Shift released (0xAA, 0xB6)
    if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = false;
        return;
    }

    // Ignore release events (highest bit set)
    if (scancode & 0x80) {
        return;
    }

    if (scancode < 128) {
        char c = shift_pressed ? scancode_ascii_upper[scancode] : scancode_ascii_lower[scancode];
        if (c != 0) {
            buffer_put(c);
        }
    }
}

void keyboard_init(void) {
    register_interrupt_handler(33, keyboard_callback);
}

bool keyboard_has_char(void) {
    if (buf_read_idx != buf_write_idx) {
        return true;
    }
    if (serial_received()) {
        return true;
    }
    return false;
}

char keyboard_getchar(void) {
    while (!keyboard_has_char()) {
        hlt();
    }

    if (buf_read_idx != buf_write_idx) {
        char c = key_buffer[buf_read_idx];
        buf_read_idx = (buf_read_idx + 1) % KEYBOARD_BUFFER_SIZE;
        return c;
    }

    if (serial_received()) {
        char c = serial_getchar();
        if (c == '\r') c = '\n';
        return c;
    }

    return 0;
}
