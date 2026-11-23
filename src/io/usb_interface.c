#include "usb_interface.h"
#include "../memory/memory.h"
#include "../memory/frame_allocator.h"
#include "../memory/arena_allocator.h"
#include "../memory/page_table.h"

bool allocate_interface_endpoints(USB_Interface* interface)
{
    u8 mem_needed = interface->endpoint_count * sizeof(USB_Interface_Endpoint);
    u8 frames_to_allocate = (mem_needed + FRAME_SIZE - 1) / FRAME_SIZE;

    Phys_addr phys_addr = allocate_contiguous_general_frames(frames_to_allocate, MAX_FRAME_NUM);
    Virt_addr virt_addr = arena_allocate_pages(&xhci_driver_page_allocator, frames_to_allocate);

    if (phys_addr == INVALID_ADDR || virt_addr == INVALID_ADDR)
        return false;

    map_address(virt_addr, phys_addr, PTE_WRITABLE);

    memset((void*)virt_addr, 0, frames_to_allocate * FRAME_SIZE);

    interface->endpoints = (USB_Interface_Endpoint*)virt_addr;

    return true;
}
