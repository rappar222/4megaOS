#include "types.h"
#include "multiboot.h"
#include "vga.h"
#include "serial.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "timer.h"
#include "keyboard.h"
#include "pmm.h"
#include "kheap.h"
#include "shell.h"
#include "io.h"
#include "net.h"

extern uint32_t _kernel_start;
extern uint32_t _kernel_end;

void kernel_main(uint32_t magic, struct multiboot_info* mbi) {
    // 1. Initialize serial port (COM1) for diagnostics / headless output
    serial_init();
    serial_puts("\n[KERNEL] 4MegaOS COM1 Serial Initialized\n");

    // 2. Initialize VGA console
    vga_init();

    // 3. Print Welcome Banner
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    kprintf("  ___ _ __  __                  ___  ____  \n");
    kprintf(" | _ ) |  \\/  |___ __ _ __ _   / _ \\/ ___| \n");
    kprintf(" | _ \\ | |\\/| / -_) _` / _` | | (_) \\___ \\ \n");
    kprintf(" |___/_|_|  |_\\___\\__, \\__,_|  \\___/|____/ \n");
    kprintf("                  |___/                    \n");
    vga_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    kprintf(" The 4MB RAM Operating System for x86 (IA-32)\n");
    kprintf("--------------------------------------------------------------------\n");
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    // 4. Verify Multiboot
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        kprintf("[WARN] Invalid Multiboot magic: 0x%x (expected 0x%x)\n", magic, MULTIBOOT_BOOTLOADER_MAGIC);
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    } else {
        kprintf("[OK] Multiboot header valid. Bootloader: %s\n",
                mbi->flags & (1 << 9) ? (const char*)mbi->boot_loader_name : "GRUB");
    }

    // 5. Initialize GDT
    gdt_init();
    kprintf("[OK] Global Descriptor Table (GDT) loaded\n");

    // 6. Initialize IDT
    idt_init();
    kprintf("[OK] Interrupt Descriptor Table (IDT) loaded\n");

    // 7. Initialize Timer & Keyboard
    timer_init(100);
    kprintf("[OK] Programmable Interval Timer (PIT) at 100Hz\n");

    keyboard_init();
    kprintf("[OK] PS/2 Keyboard driver initialized\n");

    // 8. Unmask Timer (IRQ0) and Keyboard (IRQ1)
    pic_clear_mask(0);
    pic_clear_mask(1);

    // 9. Initialize Physical Memory Manager (PMM)
    uint32_t k_start = (uint32_t)&_kernel_start;
    uint32_t k_end   = (uint32_t)&_kernel_end;
    pmm_init(k_end);

    kprintf("[OK] Physical Memory Manager: 4096 KB (1024 frames) managed\n");
    kprintf("     Kernel location: %p - %p (%u KB)\n", k_start, k_end, (k_end - k_start + 1023) / 1024);

    // 10. Initialize Kernel Heap
    // Use memory from aligned kernel_end to 0x3F0000 (reserving 64KB at top for kernel stack)
    uint32_t heap_start = (k_end + 4095) & ~4095;
    uint32_t heap_end   = 0x3F0000;
    size_t heap_size    = heap_end - heap_start;
    kheap_init(heap_start, heap_size);
    kprintf("[OK] Kernel Heap initialized: %u KB available (%p - %p)\n",
            heap_size / 1024, heap_start, heap_end);

    // 11. Enable Interrupts
    sti();
    kprintf("[OK] CPU Interrupts enabled (STI)\n");

    // 12. Initialize Network (PCI E1000)
    net_init();
    if (net_is_available()) {
        kprintf("[OK] Intel E1000 Gigabit Ethernet initialized\n");
    } else {
        kprintf("[INFO] No network interface found on PCI bus\n");
    }

    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    kprintf("\nBoot complete! System running cleanly within 4MB physical RAM.\n");
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprintf("Type 'help' for available commands, or 'mem' to view 4MB memory map.\n\n");

    // 12. Run Interactive Shell
    shell_init();
    shell_run();

    // In case shell ever returns
    for (;;) {
        hlt();
    }
}
