#ifndef KHEAP_H
#define KHEAP_H

#include "types.h"

void kheap_init(uintptr_t start_addr, size_t size);
void* kmalloc(size_t size);
void kfree(void* ptr);
void* kcalloc(size_t num, size_t size);
void kheap_get_stats(size_t* total, size_t* used, size_t* free, uint32_t* alloc_count);
void kheap_print_info(void);

#endif
