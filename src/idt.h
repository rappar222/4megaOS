#ifndef IDT_H
#define IDT_H

#include "types.h"

struct registers {
    uint32_t ds;                                     // Data segment selector
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; // Pushed by pusha
    uint32_t int_no, err_code;                      // Interrupt number and error code
    uint32_t eip, cs, eflags, useresp, ss;          // Pushed by the processor automatically
} __attribute__((packed));

typedef void (*isr_t)(struct registers*);

struct idt_entry {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_hi;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

void idt_init(void);
void register_interrupt_handler(uint8_t n, isr_t handler);
void isr_handler(struct registers* regs);
void irq_handler(struct registers* regs);

#endif
