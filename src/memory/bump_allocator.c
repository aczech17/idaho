#include "bump_allocator.h"

Bump_allocator global_bump_allocator =
{
	.next_page_number = BUMP_ALLOCATOR_BASE_PAGE,
};

Virt_addr bump_allocate_pages(u64 page_count)
{
	if (page_count == 0)
		return INVALID_ADDR;

	u64 new_next_page_number = global_bump_allocator.next_page_number + page_count;
	if (new_next_page_number > BUMP_ALLOCATOR_BASE_PAGE + BUMP_ALLOCATOR_PAGE_COUNT)
		return INVALID_ADDR;

	u64 allocated_page_number = global_bump_allocator.next_page_number;
	global_bump_allocator.next_page_number = new_next_page_number;

	return allocated_page_number * FRAME_SIZE;
}
