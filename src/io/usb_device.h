#ifndef IO_USB_DEVICE_H
#define IO_USB_DEVICE_H

#include "xhci.h"

typedef struct
{
	XHCI_Info* xhci_info;

	u8 port_id;
	u8 slot_id;
	u8 port_speed;

	Virt_addr device_context_virt_addr;
	Phys_addr device_context_phys_addr;

	Virt_addr input_context_virt_addr;
	Phys_addr input_context_phys_addr;

	// Endpoint 0 transfer ring
	Virt_addr ep0_transfer_ring_virt_addr;
	Phys_addr ep0_transfer_ring_phys_addr;
	u16 ep0_transfer_ring_trb_index;
	bool ep0_transfer_ring_cycle_state;

	// Endpoint 0
	u16 ep0_max_packet_size;

}USB_Device;

bool setup_usb_device(USB_Device*, XHCI_Info*);

#endif // IO_USB_DEVICE_H
