#include "vga.h"
#include "io.h"
#include "string.h"
#include "serial.h"

#define VGA_MEMORY (volatile uint16_t*)0xB8000

static size_t vga_row;
static size_t vga_column;
static uint8_t vga_color;
static volatile uint16_t* vga_buffer;

static void update_hardware_cursor(void) {
    uint16_t pos = vga_row * VGA_WIDTH + vga_column;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_init(void) {
    vga_row = 0;
    vga_column = 0;
    vga_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_buffer = VGA_MEMORY;
    vga_clear();
}

void vga_set_color(uint8_t color) {
    vga_color = color;
}

uint8_t vga_get_color(void) {
    return vga_color;
}

void vga_clear(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            vga_buffer[index] = vga_entry(' ', vga_color);
        }
    }
    vga_row = 0;
    vga_column = 0;
    update_hardware_cursor();
}

static void scroll(void) {
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = vga_buffer[(y + 1) * VGA_WIDTH + x];
        }
    }
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', vga_color);
    }
    vga_row = VGA_HEIGHT - 1;
}

void vga_putentry_at(char c, uint8_t color, size_t x, size_t y) {
    if (x < VGA_WIDTH && y < VGA_HEIGHT) {
        const size_t index = y * VGA_WIDTH + x;
        vga_buffer[index] = vga_entry(c, color);
    }
}

void vga_putchar(char c) {
    serial_putchar(c);

    if (c == '\n') {
        vga_column = 0;
        if (++vga_row == VGA_HEIGHT) {
            scroll();
        }
        update_hardware_cursor();
        return;
    }

    if (c == '\r') {
        vga_column = 0;
        update_hardware_cursor();
        return;
    }

    if (c == '\t') {
        vga_column = (vga_column + 8) & ~7;
        if (vga_column >= VGA_WIDTH) {
            vga_column = 0;
            if (++vga_row == VGA_HEIGHT) {
                scroll();
            }
        }
        update_hardware_cursor();
        return;
    }

    const size_t index = vga_row * VGA_WIDTH + vga_column;
    vga_buffer[index] = vga_entry(c, vga_color);
    if (++vga_column == VGA_WIDTH) {
        vga_column = 0;
        if (++vga_row == VGA_HEIGHT) {
            scroll();
        }
    }
    update_hardware_cursor();
}

void vga_backspace(void) {
    if (vga_column > 0) {
        vga_column--;
        const size_t index = vga_row * VGA_WIDTH + vga_column;
        vga_buffer[index] = vga_entry(' ', vga_color);
        update_hardware_cursor();
        serial_putchar('\b');
        serial_putchar(' ');
        serial_putchar('\b');
    }
}

void vga_puts(const char* str) {
    while (*str) {
        vga_putchar(*str++);
    }
}

void vga_set_cursor(size_t x, size_t y) {
    if (x < VGA_WIDTH && y < VGA_HEIGHT) {
        vga_column = x;
        vga_row = y;
        update_hardware_cursor();
    }
}

void vga_get_cursor(size_t* x, size_t* y) {
    if (x) *x = vga_column;
    if (y) *y = vga_row;
}

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

void kprintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    char buf[32];
    for (size_t i = 0; fmt[i] != '\0'; i++) {
        if (fmt[i] != '%') {
            vga_putchar(fmt[i]);
            continue;
        }

        i++;
        if (fmt[i] == '0' && fmt[i+1] == '2' && (fmt[i+2] == 'u' || fmt[i+2] == 'd' || fmt[i+2] == 'x')) {
            uint32_t val = va_arg(args, uint32_t);
            int base = (fmt[i+2] == 'x') ? 16 : 10;
            utoa(val, buf, base);
            if (val < (uint32_t)base) vga_putchar('0');
            vga_puts(buf);
            i += 2;
            continue;
        }

        switch (fmt[i]) {
            case 'c': {
                char c = (char)va_arg(args, int);
                vga_putchar(c);
                break;
            }
            case 's': {
                const char* str = va_arg(args, const char*);
                if (!str) str = "(null)";
                vga_puts(str);
                break;
            }
            case 'd':
            case 'i': {
                int32_t val = va_arg(args, int32_t);
                itoa(val, buf, 10);
                vga_puts(buf);
                break;
            }
            case 'u': {
                uint32_t val = va_arg(args, uint32_t);
                utoa(val, buf, 10);
                vga_puts(buf);
                break;
            }
            case 'x': {
                uint32_t val = va_arg(args, uint32_t);
                utoa(val, buf, 16);
                vga_puts(buf);
                break;
            }
            case 'p': {
                uint32_t val = va_arg(args, uint32_t);
                vga_puts("0x");
                utoa(val, buf, 16);
                vga_puts(buf);
                break;
            }
            case '%': {
                vga_putchar('%');
                break;
            }
            default: {
                vga_putchar('%');
                vga_putchar(fmt[i]);
                break;
            }
        }
    }

    va_end(args);
}
