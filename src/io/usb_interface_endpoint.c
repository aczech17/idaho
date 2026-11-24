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

bool write_to_endpoint_transfer_ring(USB_Interface_Endpoint* ep, const u32 trb[4])
{
    if (ep == NULL || trb == NULL)
        return false;

    if (ep->transfer_ring_virt_addr == INVALID_ADDR || ep->transfer_ring_index >= 255)
        return false;

    volatile u32* transfer_ring = (volatile u32*)(ep->transfer_ring_virt_addr);
    u16 index = ep->transfer_ring_index;
    volatile u32* destination = transfer_ring + index * 4;

    destination[0] = trb[0];
    destination[1] = trb[1];
    destination[2] = trb[2];

    u32 control = ep->transfer_ring_cycle_state ? (trb[3] | 1U) : (trb[3] & ~1U);
    destination[3] = control;

    if (index == 254)
    {
        volatile u32* link_trb = transfer_ring + 255 * 4;

        u32 link_control = ep->transfer_ring_cycle_state ? (link_trb[3] | 1U) : (link_trb[3] & ~1U);
        link_trb[3] = link_control;

        ep->transfer_ring_cycle_state = !ep->transfer_ring_cycle_state;
        ep->transfer_ring_index = 0;

        return true;
    }

    ++ep->transfer_ring_index;
    return true;
}
