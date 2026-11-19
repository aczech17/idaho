#ifndef MEMORY_ARENA_ALLOCATOR_H
#define MEMORY_ARENA_ALLOCATOR_H

#include "memory.h"

typedef struct
{
	u64 first_page_number;
	u64 max_page_count;
	u64 next_page_number;
}Arena_allocator;

extern Arena_allocator xhci_driver_page_allocator;

Virt_addr arena_allocate_pages(Arena_allocator* allocator, u64 page_count);

#endif // MEMORY_ARENA_ALLOCATOR_H
