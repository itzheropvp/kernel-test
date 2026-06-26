/*
 * memory.h - Simple Kernel Memory Allocator
 *
 * WHAT IS A BUMP ALLOCATOR?
 *
 * The simplest possible memory allocator. It works like a pointer
 * that starts at the end of the kernel binary and only moves forward:
 *
 *   ┌──────────────────────────────────────────────────────────┐
 *   │  Kernel code/data  │  Allocated  │    Free heap   │      │
 *   │  (loaded by GRUB)  │  objects    │    space       │      │
 *   └──────────────────────────────────────────────────────────┘
 *   0x100000          kernel_end   heap_ptr        end_of_RAM
 *
 * Allocation:
 *   1. Save current heap_ptr
 *   2. Advance heap_ptr by the requested size
 *   3. Return the saved pointer
 *
 * Deallocation:
 *   Not supported! (In our kernel, we don't need to free memory.)
 *
 * ADVANTAGES:
 *   - Extremely fast (just a pointer increment)
 *   - Zero fragmentation
 *   - No bookkeeping overhead
 *
 * DISADVANTAGES:
 *   - Can't free individual allocations
 *   - Runs out of memory over time (fine for a kernel that allocates
 *     data structures at boot and never frees them)
 *
 * For a more complete kernel you'd implement a slab allocator, buddy
 * allocator, or dlmalloc-style allocator.
 *
 * KERNEL_END SYMBOL:
 *   Defined in linker.ld as a label at the very end of the kernel binary.
 *   We use it as the starting point of our heap.
 */

#ifndef MEMORY_H
#define MEMORY_H

#include "../kernel/kernel.h"

/*
 * kmalloc - Allocate 'size' bytes of kernel memory.
 *
 * Returns a pointer to the allocated block (always 4-byte aligned).
 * Returns NULL if out of memory (address > 4 MB limit we impose).
 * The memory is NOT zeroed — use memset() if you need zeroed memory.
 */
void* kmalloc(size_t size);

/*
 * kzalloc - Allocate 'size' bytes, zeroed.
 */
void* kzalloc(size_t size);

/*
 * kfree - No-op for a bump allocator.
 * In a real kernel this would return memory to a free list.
 */
void kfree(void* ptr);

/*
 * memory_used - Return how many bytes have been allocated.
 */
size_t memory_used(void);

/*
 * memory_init - Initialize the allocator (must call before kmalloc).
 */
void memory_init(void);

#endif /* MEMORY_H */
