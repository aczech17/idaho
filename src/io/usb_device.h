#ifndef IO_USB_DEVICE_H
#define IO_USB_DEVICE_H

#include "usb_controller.h"
#include "usb_interface.h"

// USB_Controller → USB_Device → USB_Interface → USB_Interface_Endpoint

typedef struct
{
	USB_Controller* usb_controller;

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

	Phys_addr received_descriptor_phys_addr;
	Virt_addr received_descriptor_virt_addr;

	// Device Descriptor
	u16 vendor_id;
	u16 product_id;

	u8 device_class;
	u8 device_subclass;
	u8 device_protocol;

	u8 num_configurations;

	// Configuration Descriptor
	u16 config_total_length;
	u8 config_value;
	u8 interfaces_count;

	USB_Interface* interfaces;
	Virt_addr config_descriptor;
}USB_Device;

bool setup_usb_device(USB_Device* device, USB_Controller* usb_controller, u8 port_id);
bool set_configuration(USB_Device* device);
bool set_protocol(USB_Device* device, u8 interface_number);

#endif // IO_USB_DEVICE_H
