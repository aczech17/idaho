#ifndef IO_USB_DEVICE_H
#define IO_USB_DEVICE_H

#include "usb_controller.h"
#include "usb_interface.h"

#define DEVICE_DESCRIPTOR 1
#define CONFIG_DESCRIPTOR 2
#define STRING_DESCRIPTOR 3
#define INTERFACE_DESCRIPTOR 4
#define ENDPOINT_DESCRIPTOR 5

#define TRANSFER_EVENT ((u32)(32))
#define COMMAND_COMPLETION_EVENT ((u32)(33))

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

typedef struct
{
	u32 input_control_context[8];
	u32 slot_context[8];
	u32 endpoint_contexts[31][8];
}Input_context;

void decode_event_trb(const u32 event_trb[4], u8* completion_code, u8* slot_id, u8* event_type, u8* endpoint_id);
bool setup_usb_device(USB_Device* device, USB_Controller* usb_controller, u8 port_id);
bool set_input_context(USB_Device* device, USB_Interface_Endpoint* ep, Input_context* ctxt);
bool set_configuration(USB_Device* device);
bool set_protocol(USB_Device* device, u8 interface_number);

#endif // IO_USB_DEVICE_H
