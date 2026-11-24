#include "usb_device_manager.h"
#include "../memory/page_table.h"
#include "../common.h"

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
	// manager->device_count = 0;

	// if (!setup_devices_buffer(manager))
	// 	return false;


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

	return true;
}


