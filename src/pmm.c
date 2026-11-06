#include "pmm.h"
#include "string.h"
#include "vga.h"

// 128 bytes tracks all 1024 frames of 4MB physical RAM (1 bit per 4KB page)
// 1 = allocated / reserved, 0 = free
static uint8_t frame_bitmap[BITMAP_SIZE_BYTES];
static uint32_t k_end_aligned = 0;

static inline void set_frame(uint32_t frame) {
    if (frame < TOTAL_FRAMES_4MB) {
        frame_bitmap[frame / 8] |= (1 << (frame % 8));
    }
}

static inline void clear_frame(uint32_t frame) {
    if (frame < TOTAL_FRAMES_4MB) {
        frame_bitmap[frame / 8] &= ~(1 << (frame % 8));
    }
}

static inline bool test_frame(uint32_t frame) {
    if (frame >= TOTAL_FRAMES_4MB) return true;
    return (frame_bitmap[frame / 8] & (1 << (frame % 8))) != 0;
}

static uint32_t first_free_frame(void) {
    for (uint32_t i = 0; i < BITMAP_SIZE_BYTES; i++) {
        if (frame_bitmap[i] != 0xFF) {
            for (uint32_t j = 0; j < 8; j++) {
                uint32_t bit = 1 << j;
                if (!(frame_bitmap[i] & bit)) {
                    return i * 8 + j;
                }
            }
        }
    }
    return (uint32_t)-1;
}

void pmm_init(uint32_t kernel_end) {
    // Mark everything as reserved initially
    memset(frame_bitmap, 0xFF, BITMAP_SIZE_BYTES);

    // Align kernel end to 4KB boundary
    k_end_aligned = (kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    uint32_t kernel_end_frame   = k_end_aligned / PAGE_SIZE;

    // Frames below 1MB (0..255) remain reserved for IVT, BDA, VGA buffer, BIOS
    // Frames for kernel (256 .. kernel_end_frame) remain reserved

    // Free frames from kernel_end_frame up to 4MB (frame 1023)
    for (uint32_t f = kernel_end_frame; f < TOTAL_FRAMES_4MB; f++) {
        clear_frame(f);
    }
}

uint32_t pmm_alloc_frame(void) {
    uint32_t frame = first_free_frame();
    if (frame == (uint32_t)-1) {
        return 0; // Out of memory!
    }
    set_frame(frame);
    return frame * PAGE_SIZE;
}

void pmm_free_frame(uint32_t frame_addr) {
    uint32_t frame = frame_addr / PAGE_SIZE;
    if (frame >= (k_end_aligned / PAGE_SIZE) && frame < TOTAL_FRAMES_4MB) {
        clear_frame(frame);
    }
}

uint32_t pmm_get_total_frames(void) {
    return TOTAL_FRAMES_4MB;
}

uint32_t pmm_get_used_frames(void) {
    uint32_t count = 0;
    for (uint32_t f = 0; f < TOTAL_FRAMES_4MB; f++) {
        if (test_frame(f)) {
            count++;
        }
    }
    return count;
}

uint32_t pmm_get_free_frames(void) {
    return TOTAL_FRAMES_4MB - pmm_get_used_frames();
}

uint32_t pmm_get_kernel_end(void) {
    return k_end_aligned;
}

void pmm_print_info(void) {
    uint32_t used = pmm_get_used_frames();
    uint32_t free = TOTAL_FRAMES_4MB - used;
    uint32_t total_kb = TOTAL_MEMORY_4MB / 1024;
    uint32_t used_kb = (used * PAGE_SIZE) / 1024;
    uint32_t free_kb = (free * PAGE_SIZE) / 1024;

    kprintf("Physical Memory Manager (4MB Space):\n");
    kprintf("  Total RAM   : %u KB (4096 KB / 4 MB) [%u frames]\n", total_kb, TOTAL_FRAMES_4MB);
    kprintf("  Reserved/Used: %u KB [%u frames]\n", used_kb, used);
    kprintf("  Available   : %u KB [%u frames]\n", free_kb, free);
    kprintf("  Kernel End  : %p\n", k_end_aligned);
    kprintf("  Page Size   : 4096 bytes (4 KB)\n");
    kprintf("  Bitmap Size : 128 bytes (tracks all 1024 pages!)\n");

    // Visual bar of 40 segments
    kprintf("  Map: [");
    uint32_t used_bars = (used * 40) / TOTAL_FRAMES_4MB;
    for (uint32_t b = 0; b < 40; b++) {
        if (b < used_bars) {
            vga_putchar('#');
        } else {
            vga_putchar('.');
        }
    }
    kprintf("] %u%% used\n", (used * 100) / TOTAL_FRAMES_4MB);
}
