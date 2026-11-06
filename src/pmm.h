#ifndef PMM_H
#define PMM_H

#include "types.h"

#define PAGE_SIZE          4096
#define TOTAL_MEMORY_4MB   (4 * 1024 * 1024)
#define TOTAL_FRAMES_4MB   (TOTAL_MEMORY_4MB / PAGE_SIZE) // 1024 frames
#define BITMAP_SIZE_BYTES  (TOTAL_FRAMES_4MB / 8)          // 128 bytes

void pmm_init(uint32_t kernel_end);
uint32_t pmm_alloc_frame(void);
void pmm_free_frame(uint32_t frame_addr);
uint32_t pmm_get_total_frames(void);
uint32_t pmm_get_used_frames(void);
uint32_t pmm_get_free_frames(void);
uint32_t pmm_get_kernel_end(void);
void pmm_print_info(void);

#endif
