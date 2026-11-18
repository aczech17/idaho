#ifndef MEMORY_BUMP_ALLOCATOR_H
#define MEMORY_BUMP_ALLOCATOR_H

#include "memory.h"

#define BUMP_ALLOCATOR_BASE_PAGE (1 << 18)
#define BUMP_ALLOCATOR_PAGE_COUNT (1 << 18)

typedef struct
{
	u64 next_page_number;
}Bump_allocator;

extern Bump_allocator global_bump_allocator;

Virt_addr bump_allocate_pages(u64 page_count);

#endif // MEMORY_BUMP_ALLOCATOR_H
