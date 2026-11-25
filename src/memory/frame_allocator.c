#include "frame_allocator.h"
#include "../common.h"
#include "memory.h"
#include <stdbool.h>


/**
 *
 * Determine which frames are free. Free frames are those available and not belonging to the kernel.
 **/
void setup_global_frame_allocator
(
    Phys_memory_map* available_regions,
    Phys_memory_map* kernel_regions,
    u64 kernel_memory_size
)
{
    memset(&global_frame_allocator, 0, sizeof(Frame_allocator));
    
    // Mark available memory regions as accessible.
    for (u32 region = 0; region < available_regions->region_count; ++region)
    {
        Phys_addr region_start = available_regions->start_addr[region];
        Phys_addr region_end   = available_regions->end_addr[region];

        u64 first_frame = (region_start + FRAME_SIZE - 1) / FRAME_SIZE; // Round up
        u64 last_frame = (region_end - FRAME_SIZE + 1) / FRAME_SIZE; // Round down

        for (u64 frame_number = first_frame; frame_number <= last_frame; ++frame_number)
        {
            u64 byte_number = frame_number / 8;
            u64 bit_number = frame_number % 8;
            
            global_frame_allocator.accessible[byte_number] |= 1 << (7 - bit_number);
        }
    }

    // Mark all frames belonging to kernel as kernel space.
    u64 kernel_frames_count = kernel_memory_size / FRAME_SIZE;
    u64 full_kernel_blocks = kernel_frames_count / 8;
    memset(global_frame_allocator.kernel_space, 0xFF, full_kernel_blocks);

    u64 remaining_frames = kernel_frames_count % 8;
    for (u64 i = 0; i < remaining_frames; ++i)
    {
        global_frame_allocator.kernel_space[full_kernel_blocks] |= (1 << (7 - i));
    }


    // Mark all the frames already used by the kernel as taken.
    for (u32 region = 0; region < kernel_regions->region_count; ++region)
    {
        Phys_addr region_start = kernel_regions->start_addr[region];
        Phys_addr region_end   = kernel_regions->end_addr[region];

        u64 first_frame = region_start / FRAME_SIZE;
        u64 last_frame = region_end / FRAME_SIZE;

        // It's not so efficient.
        for (u64 frame_number = first_frame; frame_number <= last_frame; ++frame_number)
        {
            u64 byte_number = frame_number / 8;
            u64 bit_number = frame_number % 8;
            
            global_frame_allocator.taken[byte_number] |= (1 << (7 - bit_number));
        }
    }
}

static Frame_address allocate_frame(bool kernel_request)
{
    u64 frame_block = kernel_request ? global_frame_allocator.latest_allocated_kernel_block
        : global_frame_allocator.latest_allocated_general_block;
    u8 unwanted_block_kernelness = kernel_request ? 0 : 0xFF;

    for (u64 attempt = 0; attempt < FRAME_BITMAP_SIZE; ++attempt)
    {
        u8 block_accessible = global_frame_allocator.accessible[frame_block];
        u8 block_kernelness = global_frame_allocator.kernel_space[frame_block];
        u8 block_taken = global_frame_allocator.taken[frame_block];

        if (block_accessible == 0 || block_kernelness == unwanted_block_kernelness || block_taken == 0xFF)
        {
            frame_block = (frame_block + 1) % FRAME_BITMAP_SIZE;
            continue;
        }

        for (u64 i = 0; i < 8; ++i)
        {
            u8 mask = 0x80 >> i;

            bool frame_accessible = global_frame_allocator.accessible[frame_block] & mask;
            bool frame_kernelness = global_frame_allocator.kernel_space[frame_block] & mask;
            bool frame_taken = global_frame_allocator.taken[frame_block] & mask;

            if (frame_accessible && frame_kernelness == kernel_request && !frame_taken)
            {
                // We've found a proper frame.
                global_frame_allocator.taken[frame_block] |= mask; // Mark as used

                if (kernel_request)
                    global_frame_allocator.latest_allocated_kernel_block = frame_block;
                else
                    global_frame_allocator.latest_allocated_general_block = frame_block;

                u64 frame_number = 8 * frame_block + i;
                return frame_number * FRAME_SIZE;
            }
        }

        frame_block = (frame_block + 1) % FRAME_BITMAP_SIZE;
    }

    return INVALID_ADDR;
}

Frame_address allocate_kernel_frame(void)
{
    return allocate_frame(true);
}

Frame_address allocate_general_frame(void)
{
    return allocate_frame(false);
}

Frame_address allocate_contiguous_general_frames(u64 frames_count, u64 max_frame_number)
{
    // It's not so efficient.

    if (frames_count == 0)
        return INVALID_ADDR;

    u64 first_frame = 0, last_frame = 0;
    bool found = false;

    for (u64 attempt = 0; attempt < FRAME_BITMAP_SIZE * 8; ++attempt)
    {
        u64 index = last_frame / 8;
        u64 mask = 1 << (7 - (last_frame % 8));

        if (last_frame > max_frame_number)
            return INVALID_ADDR;

        bool good_frame = (global_frame_allocator.accessible[index] & mask) && !(global_frame_allocator.kernel_space[index] & mask) &&
            !(global_frame_allocator.taken[index] & mask);

        if (good_frame)
        {
            if (last_frame - first_frame + 1 == frames_count)
            {
                found = true;
                global_frame_allocator.latest_allocated_general_block = last_frame / 8;
                break;
            }
            ++last_frame;
        }
        else
        {
            ++last_frame;
            first_frame = last_frame;
        }
    }

    if (!found)
        return INVALID_ADDR;

    for (u64 frame = first_frame; frame <= last_frame; ++frame)
    {
        u64 index = frame / 8;
        u64 mask = 1 << (7 - (frame % 8));

        global_frame_allocator.taken[index] |= mask;
    }

    return first_frame * FRAME_SIZE;
}

void deallocate_frame(Frame_address frame_address)
{
    u64 frame_number = frame_address / FRAME_SIZE;
    u64 frame_block = frame_number / 8;
    u64 frame_in_block = frame_number % 8;
    global_frame_allocator.taken[frame_block] &= ~(1 << (7 - frame_in_block));
}
