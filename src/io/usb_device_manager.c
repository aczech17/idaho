#include "usb_device_manager.h"
#include "../memory/page_table.h"
#include "../common.h"
#include "usb_device.h"

typedef enum
{
	KEYBOARD_KEYS_PRESSED,
	KEYBOARD_NO_KEYS_PRESSED,
	KEYBOARD_REQUEST_ERROR,
	KEYBOARD_WAITING,
}Keyboard_response_status;

static i16 find_keyboard_interface_number(USB_Device* device)
{
	// USB_Interface* interface = NULL;

	const u8 HID_CLASS = 0x03;
	const u8 BOOT_INTERFACE_SUBCLASS = 0x01;
	const u8 KEYBOARD_PROTOCOL = 0x01;


	for (u8 intf_num = 0; intf_num < device->interfaces_count; ++intf_num)
	{
		USB_Interface* interface = &device->interfaces[intf_num];

		if (interface->class == HID_CLASS && interface->subclass == BOOT_INTERFACE_SUBCLASS && interface->protocol == KEYBOARD_PROTOCOL)
			return intf_num;
	}

	return -1;
}

bool setup_usb_device_manager(USB_Device_Manager* manager, USB_Controller* controller)
{
	memset(manager, 0, sizeof(USB_Device_Manager));

	manager->controller = controller;

	for (u8 port = 0; port < controller->max_ports; ++port)
	{
		if (controller->usb_port_states[port] != USB_PORT_CONNECTED)
			continue;

		USB_Device device = {0};
		if (!setup_usb_device(&device, manager->controller, port + 1))
		{
			vga_printf("Warning: USB device %X could not be set up.\n", port);
			continue;
		}

		i16 keyboard_interface_number = find_keyboard_interface_number(&device);
		if (keyboard_interface_number < 0)
			continue;


		manager->keyboard_device = device;
		manager->keyboard_interface_index = keyboard_interface_number;

		break;
	}
	if (manager->keyboard_interface_index < 0)
		return false;

	USB_Interface* keyboard_interface = &manager->keyboard_device.interfaces[manager->keyboard_interface_index];


	u8 interrupt_in_ep_num = 0;
	bool interrupt_in_ep_num_found = false;
	for (u8 ep_i = 0; ep_i < keyboard_interface->endpoint_count; ++ep_i)
	{
		u8 addr = keyboard_interface->endpoints[ep_i].endpoint_address;
		u8 attrs = keyboard_interface->endpoints[ep_i].attributes;

		if ((addr & 0x80) != 0 && (attrs & 0x03) == 0x03)
		{
			interrupt_in_ep_num = ep_i;
			interrupt_in_ep_num_found = true;
			break;
		}
	}

	if (!interrupt_in_ep_num_found)
		return false;

	USB_Interface_Endpoint* ep = &keyboard_interface->endpoints[interrupt_in_ep_num];
	manager->keyboard_interrupt_in_endpoint = ep;

	if (!set_up_usb_interface_ep_buffs(ep))
		return false;

	Input_context* keyboard_device_context = (Input_context*)(manager->keyboard_device.input_context_virt_addr);
	if (!set_input_context(&manager->keyboard_device, ep, keyboard_device_context))
		return false;

	if (!set_configuration(&manager->keyboard_device))
		return false;

	if (!set_protocol(&manager->keyboard_device, keyboard_interface->number))
		return false;

	manager->keyboard_transfer_pending = false;
	return true;
}

static bool request_from_keyboard(USB_Device_Manager* manager)
{
	if (manager->keyboard_transfer_pending == true)
		return true;

	USB_Device* device = &manager->keyboard_device;
	USB_Interface_Endpoint* ep = manager->keyboard_interrupt_in_endpoint;
	u64 buffer_addr = (u64)ep->data_buffer_phys_addr;
	u8 dci = 2 * (ep->endpoint_address & 0x0F) + 1;

	u32 normal_trb[4];
	normal_trb[0] = (u32)buffer_addr;
	normal_trb[1] = (u32)(buffer_addr >> 32);
	normal_trb[2] = 8; // transfer length
	normal_trb[3] =
		  (1U << 10) // TRB type: normal
		| (1U << 5); // IOC

	if (!write_to_endpoint_transfer_ring(ep, normal_trb))
		return false;

	manager->keyboard_transfer_pending = true;
	doorbell_ring(manager->controller, device->slot_id, dci);
	return true;
}

static Keyboard_response_status process_keyboard_respond(USB_Device_Manager* manager, u8* response)
{
	USB_Device* device = &manager->keyboard_device;
	USB_Interface_Endpoint* ep = manager->keyboard_interrupt_in_endpoint;
	u8 dci = 2 * (ep->endpoint_address & 0x0F) + 1;

	u32 event_trb[4];
	if (!read_event_trb(device->usb_controller, event_trb))
		return KEYBOARD_WAITING;

	update_event_ring_dequeue_pointer(device->usb_controller);

	u8 event_type = (event_trb[3] >> 10) & 0x3F;
	u8 endpoint_id = (event_trb[3] >> 16) & 0x1F;
	u8 slot_id = (event_trb[3] >> 24) & 0xFF;

	if (event_type != TRANSFER_EVENT || slot_id != device->slot_id || endpoint_id != dci)
		return KEYBOARD_WAITING;

	manager->keyboard_transfer_pending = false;

	u8 completion_code = (event_trb[2] >> 24) & 0xFF;
	u32 residual_length = event_trb[2] & 0xFFFFFF;

	if (completion_code != 1 || residual_length != 0)
		return KEYBOARD_REQUEST_ERROR;

	memcpy(response, (void*)(ep->data_buffer_virt_addr), 8);

	if (response[0] != 0)
		return KEYBOARD_KEYS_PRESSED;

	for (u8 i = 2; i < 8; i++)
	{
		if (response[i] != 0)
			return KEYBOARD_KEYS_PRESSED;
	}

	return KEYBOARD_NO_KEYS_PRESSED;
}

Key_report keyboard_step(USB_Device_Manager* manager)
{
	Key_report report = {0};
	u8 response[8] = {0};

	if (!request_from_keyboard(manager))
		panic("Could not send request to the keyboard.");

	Keyboard_response_status response_status = process_keyboard_respond(manager, response);
	if (response_status == KEYBOARD_REQUEST_ERROR)
		panic("keyboard request error");
	if (response_status == KEYBOARD_WAITING || response_status == KEYBOARD_NO_KEYS_PRESSED)
		return report;

	report.present = true;
	report.modifiers = response[0];

	for (u8 i = 0; i < 6; ++i)
		report.keycodes[i] = response[i + 2];

	return report;
}
