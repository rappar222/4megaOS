#include "kheap.h"
#include "string.h"
#include "vga.h"

#define HEAP_MAGIC 0x4D454741 // 'MEGA'

struct heap_block {
    uint32_t magic;
    size_t size;            // Size of data area
    uint32_t is_free;
    struct heap_block* next;
} __attribute__((aligned(16)));

static struct heap_block* heap_start = NULL;
static size_t total_heap_bytes = 0;
static uint32_t active_allocations = 0;

void kheap_init(uintptr_t start_addr, size_t size) {
    // 8-byte align start
    start_addr = (start_addr + 7) & ~7;
    size = size & ~7;

    heap_start = (struct heap_block*)start_addr;
    heap_start->magic = HEAP_MAGIC;
    heap_start->size = size - sizeof(struct heap_block);
    heap_start->is_free = 1;
    heap_start->next = NULL;

    total_heap_bytes = size;
    active_allocations = 0;
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;

    // 8-byte align size
    size = (size + 7) & ~7;

    struct heap_block* curr = heap_start;
    while (curr != NULL) {
        if (curr->magic != HEAP_MAGIC) {
            kprintf("[KHEAP ERROR] Corrupt block magic at %p\n", curr);
            return NULL;
        }

        if (curr->is_free && curr->size >= size) {
            // Can we split?
            if (curr->size >= size + sizeof(struct heap_block) + 16) {
                struct heap_block* new_block = (struct heap_block*)((uintptr_t)curr + sizeof(struct heap_block) + size);
                new_block->magic = HEAP_MAGIC;
                new_block->size = curr->size - size - sizeof(struct heap_block);
                new_block->is_free = 1;
                new_block->next = curr->next;

                curr->size = size;
                curr->next = new_block;
            }

            curr->is_free = 0;
            active_allocations++;
            return (void*)((uintptr_t)curr + sizeof(struct heap_block));
        }

        curr = curr->next;
    }

    return NULL; // Out of heap memory!
}

void kfree(void* ptr) {
    if (!ptr) return;

    struct heap_block* block = (struct heap_block*)((uintptr_t)ptr - sizeof(struct heap_block));
    if (block->magic != HEAP_MAGIC) {
        kprintf("[KHEAP ERROR] Invalid free at %p (bad magic 0x%x)\n", ptr, block->magic);
        return;
    }

    if (block->is_free) {
        kprintf("[KHEAP WARNING] Double free at %p\n", ptr);
        return;
    }

    block->is_free = 1;
    if (active_allocations > 0) active_allocations--;

    // Coalesce adjacent free blocks
    struct heap_block* curr = heap_start;
    while (curr != NULL && curr->next != NULL) {
        if (curr->is_free && curr->next->is_free) {
            curr->size += sizeof(struct heap_block) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}

void* kcalloc(size_t num, size_t size) {
    size_t total = num * size;
    void* ptr = kmalloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void kheap_get_stats(size_t* total, size_t* used, size_t* free, uint32_t* alloc_count) {
    size_t u = 0;
    size_t f = 0;
    struct heap_block* curr = heap_start;

    while (curr != NULL) {
        if (curr->is_free) {
            f += curr->size;
        } else {
            u += curr->size + sizeof(struct heap_block);
        }
        curr = curr->next;
    }

    if (total) *total = total_heap_bytes;
    if (used) *used = u;
    if (free) *free = f;
    if (alloc_count) *alloc_count = active_allocations;
}

void kheap_print_info(void) {
    size_t total, used, free;
    uint32_t count;
    kheap_get_stats(&total, &used, &free, &count);

    kprintf("Kernel Heap Manager:\n");
    kprintf("  Base Address : %p\n", heap_start);
    kprintf("  Total Size   : %u KB (%u bytes)\n", total / 1024, total);
    kprintf("  Used Bytes   : %u bytes (%u KB)\n", used, used / 1024);
    kprintf("  Free Bytes   : %u bytes (%u KB)\n", free, free / 1024);
    kprintf("  Active Allocs: %u blocks\n", count);
}
