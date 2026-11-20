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
}USB_Device;

bool setup_usb_device(USB_Device*, XHCI_Info*);

#endif // IO_USB_DEVICE_H
