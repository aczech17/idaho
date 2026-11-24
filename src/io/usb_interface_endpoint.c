#include "usb_interface_endpoint.h"
#include "../memory/frame_allocator.h"
#include "../memory/arena_allocator.h"
#include "../memory/page_table.h"

bool set_up_usb_interface_ep_buffs(USB_Interface_Endpoint* ep)
{
    Phys_addr phys_addr = allocate_general_frame();
    Virt_addr virt_addr = arena_allocate_pages(&xhci_driver_page_allocator, 1);
    if (virt_addr == INVALID_ADDR || phys_addr == INVALID_ADDR)
        return false;

    map_address(virt_addr, phys_addr, PTE_WRITABLE);

    memset((void*)virt_addr, 0, FRAME_SIZE);

    ep->transfer_ring_virt_addr = virt_addr;
    ep->transfer_ring_phys_addr = phys_addr;
    ep->transfer_ring_index = 0;
    ep->transfer_ring_cycle_state = true;

    u32* link_trb = (u32*)(virt_addr + 255 * 16);

    link_trb[0] = (u32)ep->transfer_ring_phys_addr;
    link_trb[1] = (u32)((u64)ep->transfer_ring_phys_addr >> 32);
    link_trb[2] = 0;
    link_trb[3] =
        (6u << 10) |  // TRB Type: Link TRB
        (1u << 1)  |  // Toggle Cycle
        1u;           // Cycle State

    phys_addr = allocate_general_frame();
    virt_addr = arena_allocate_pages(&xhci_driver_page_allocator, 1);
    if (virt_addr == INVALID_ADDR || phys_addr == INVALID_ADDR)
        return false;

    map_address(virt_addr, phys_addr, PTE_WRITABLE);

    memset((void*)virt_addr, 0, FRAME_SIZE);

    ep->data_buffer_virt_addr = virt_addr;
    ep->data_buffer_phys_addr = phys_addr;

    return true;
}
