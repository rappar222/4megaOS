#include "keyboard.h"
#include "idt.h"
#include "io.h"
#include "serial.h"

#define KEYBOARD_BUFFER_SIZE 256

static char key_buffer[KEYBOARD_BUFFER_SIZE];
static volatile uint32_t buf_read_idx = 0;
static volatile uint32_t buf_write_idx = 0;
static bool shift_pressed = false;
static bool extended = false;

static const char scancode_ascii_lower[128] = {
    [0x01] = 27,   // ESC
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0A] = '9', [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
    [0x0E] = '\b', // Backspace
    [0x0F] = '\t', // Tab
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
    [0x1C] = '\n', // Enter
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
    [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
    [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
    [0x34] = '.', [0x35] = '/',
    [0x37] = '*',
    [0x39] = ' ',  // Space
    [0x47] = '7', [0x48] = '8', [0x49] = '9',
    [0x4B] = '4', [0x4C] = '5', [0x4D] = '6',
    [0x4F] = '1', [0x50] = '2', [0x51] = '3',
    [0x52] = '0', [0x53] = '.'
};

static const char scancode_ascii_upper[128] = {
    [0x01] = 27,   // ESC
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
    [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
    [0x0A] = '(', [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
    [0x0E] = '\b', // Backspace
    [0x0F] = '\t', // Tab
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
    [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
    [0x18] = 'O', [0x19] = 'P', [0x1A] = '{', [0x1B] = '}',
    [0x1C] = '\n', // Enter
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F',
    [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
    [0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~',
    [0x2B] = '|',
    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V',
    [0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = '<',
    [0x34] = '>', [0x35] = '?',
    [0x37] = '*',
    [0x39] = ' ',  // Space
    [0x47] = '7', [0x48] = '8', [0x49] = '9',
    [0x4B] = '4', [0x4C] = '5', [0x4D] = '6',
    [0x4F] = '1', [0x50] = '2', [0x51] = '3',
    [0x52] = '0', [0x53] = '.'
};

static void buffer_put(char c) {
    uint32_t next = (buf_write_idx + 1) % KEYBOARD_BUFFER_SIZE;
    if (next != buf_read_idx) {
        key_buffer[buf_write_idx] = c;
        buf_write_idx = next;
    }
}

static void process_scancode(uint8_t scancode) {
    if (scancode == 0xE0) {
        extended = true;
        return;
    }

    if (extended) {
        extended = false;
        // Extended keys (Arrow keys mapping)
        if (scancode == 0x48) { buffer_put('w'); return; } // Up
        if (scancode == 0x50) { buffer_put('s'); return; } // Down
        if (scancode == 0x4B) { buffer_put('a'); return; } // Left
        if (scancode == 0x4D) { buffer_put('d'); return; } // Right
        return;
    }

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

    // Ignore key release events (highest bit set)
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

static void keyboard_callback(struct registers* regs) {
    (void)regs;
    while (inb(0x64) & 1) {
        uint8_t scancode = inb(0x60);
        process_scancode(scancode);
    }
}

void keyboard_init(void) {
    register_interrupt_handler(33, keyboard_callback);

    // 1. Drain any pending data in 8042 buffer
    while (inb(0x64) & 1) {
        inb(0x60);
    }

    // 2. Enable keyboard interface
    outb(0x64, 0xAE);

    // 3. Read configuration byte
    outb(0x64, 0x20);
    for (volatile int i = 0; i < 2000; i++);
    if (inb(0x64) & 1) {
        uint8_t cfg = inb(0x60);
        cfg |= 0x01;  // Enable IRQ1 interrupt
        cfg &= ~0x10; // Enable keyboard clock line
        outb(0x64, 0x60);
        for (volatile int i = 0; i < 2000; i++);
        outb(0x60, cfg);
    }

    // 4. Enable scanning on keyboard device
    outb(0x60, 0xF4);
    for (volatile int i = 0; i < 2000; i++);
    while (inb(0x64) & 1) {
        inb(0x60);
    }
}

bool keyboard_has_char(void) {
    // Check if hardware 8042 buffer has unread scancodes (polling fallback)
    if (inb(0x64) & 1) {
        uint8_t scancode = inb(0x60);
        process_scancode(scancode);
    }

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
        if (c == 127) c = '\b';
        return c;
    }

    return 0;
}
