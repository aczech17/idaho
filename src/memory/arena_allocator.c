#include "arena_allocator.h"

Arena_allocator xhci_driver_page_allocator =
{
	.first_page_number = 1 << 17,
	.max_page_count = 1 << 18,
	.next_page_number = 1 << 17,
};

Virt_addr arena_allocate_pages(Arena_allocator* allocator, u64 page_count)
{
	if (page_count == 0)
		return INVALID_ADDR;

	u64 new_next_page_number = allocator->next_page_number + page_count;
	if (new_next_page_number - allocator->first_page_number > allocator->max_page_count)
		return INVALID_ADDR;

	u64 allocated_page_number = allocator->next_page_number;
	allocator->next_page_number = new_next_page_number;

	return allocated_page_number * FRAME_SIZE;
}
