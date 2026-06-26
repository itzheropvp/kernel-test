/*
 * memory.c - Bump allocator implementation
 */

#include "memory.h"
#include "../libc/string.h"

/*
 * kernel_end is defined by the linker script (linker.ld) as a symbol
 * placed at the very end of the kernel binary in memory.
 *
 * We use it as our initial heap pointer.
 */
extern uint32_t kernel_end;

/* Current heap top — starts right after the kernel binary */
static uint32_t heap_ptr = 0;

/* Maximum heap size: we allow up to 4 MB of physical memory */
#define HEAP_MAX 0x400000  /* 4 MB */

void memory_init(void) {
    /* Set the starting heap address to right after the kernel code/data */
    heap_ptr = (uint32_t)&kernel_end;
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;

    /* Safety check: don't allocate past 4 MB */
    if (heap_ptr + size > HEAP_MAX) return NULL;

    /* Remember where this allocation starts */
    uint32_t addr = heap_ptr;

    /* Advance the pointer */
    heap_ptr += size;

    /* Align the next allocation to a 4-byte boundary.
     * (addr + 3) & ~3 rounds UP to the next multiple of 4.
     * This ensures efficient memory access on x86.
     */
    heap_ptr = (heap_ptr + 3) & ~3U;

    return (void*)addr;
}

void* kzalloc(size_t size) {
    void* ptr = kmalloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void kfree(void* ptr) {
    /* Bump allocator: we can't actually free.
     * In a real OS you'd return memory to a free list here.
     */
    (void)ptr;  /* suppress "unused variable" warning */
}

size_t memory_used(void) {
    return heap_ptr - (uint32_t)&kernel_end;
}
