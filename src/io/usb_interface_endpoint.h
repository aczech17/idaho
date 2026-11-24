#ifndef IO_USB_INTERFACE_ENDPOINT_H
#define IO_USB_INTERFACE_ENDPOINT_H

#include "../common.h"
#include "../memory/memory.h"
#include <stdbool.h>

typedef struct
{
	u8 endpoint_address;
	u8 attributes;
	u16 max_packet_size;
	u8 interval;

	Virt_addr transfer_ring_virt_addr;
	Phys_addr transfer_ring_phys_addr;
	u16 transfer_ring_index;
	bool transfer_ring_cycle_state;

	Virt_addr data_buffer_virt_addr;
	Phys_addr data_buffer_phys_addr;	
}USB_Interface_Endpoint;

bool set_up_usb_interface_ep_buffs(USB_Interface_Endpoint*);
bool write_to_transfer_ring(USB_Interface_Endpoint*, const u32 trb[4]);

#endif // IO_USB_INTERFACE_ENDPOINT_H
