#include "usb_device_manager.h"
#include "../memory/frame_allocator.h"
#include "../memory/arena_allocator.h"
#include "../memory/page_table.h"
#include "../common.h"


// static bool setup_devices_buffer(USB_Device_Manager* manager)
// {
// 	u64 buffer_size = (u64)(manager->controller->max_ports) * sizeof(USB_Device);
// 	u64 frames_to_allocate = (buffer_size + FRAME_SIZE - 1) / FRAME_SIZE;
//
// 	Phys_addr mem_buffer_phys_addr = allocate_contiguous_general_frames(frames_to_allocate, MAX_FRAME_NUM);
// 	Virt_addr mem_buffer_virt_addr = arena_allocate_pages(&xhci_driver_page_allocator, frames_to_allocate);
//
// 	if (mem_buffer_phys_addr == INVALID_ADDR || mem_buffer_virt_addr == INVALID_ADDR)
// 		return false;
//
// 	for (u64 frame = 0; frame < frames_to_allocate; ++frame)
// 	{
// 		Virt_addr virt = mem_buffer_virt_addr + frame * FRAME_SIZE;
// 		Phys_addr phys = mem_buffer_phys_addr + frame * FRAME_SIZE;
// 		map_address(virt, phys, PTE_WRITABLE);
// 	}
// 	memset((void*)mem_buffer_virt_addr, 0, frames_to_allocate * FRAME_SIZE);
//
// 	manager->devices = (USB_Device*)mem_buffer_virt_addr;
// 	manager->device_count = 0;
//
// 	return true;
// }
//
// static void add_device(USB_Device_Manager* manager, USB_Device* device)
// {
// 	USB_Device* dst = &manager->devices[manager->device_count];
// 	memcpy(dst, device, sizeof(USB_Device));
//
// 	++manager->device_count;
// }

// static bool find_boot_keyboard_interface(const USB_Device* device, u8* interface_number)
// {
// 	const u8* descriptor = (const u8*)device->config_descriptor;
// 	u32 offset = 0;
// 	u32 total_length = device->config_total_length;
//
// 	while (offset < total_length)
// 	{
// 		if (total_length - offset < 2)
// 			return false;
//
// 		u8 length = descriptor[offset];
// 		u8 type = descriptor[offset + 1];
//
// 		if (length < 2 || length > total_length - offset)
// 			return false;
//
// 		if (type == 4)
// 		{
// 			if (length < 9)
// 				return false;
//
// 			u8 interface_class = descriptor[offset + 5];
// 			u8 interface_subclass = descriptor[offset + 6];
// 			u8 interface_protocol = descriptor[offset + 7];
//
// 			if (interface_class == 0x03
// 				&& interface_subclass == 0x01
// 				&& interface_protocol == 0x01)
// 			{
// 				*interface_number = descriptor[offset + 2];
// 				return true;
// 			}
// 		}
//
// 		offset += length;
// 	}
//
// 	return false;
// }

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
