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
