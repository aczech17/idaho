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

static bool find_boot_keyboard_interface(const USB_Device* device, u8* interface_number)
{
	const u8* descriptor = (const u8*)device->config_descriptor;
	u32 offset = 0;
	u32 total_length = device->config_total_length;

	while (offset < total_length)
	{
		if (total_length - offset < 2)
			return false;

		u8 length = descriptor[offset];
		u8 type = descriptor[offset + 1];

		if (length < 2 || length > total_length - offset)
			return false;

		if (type == 4)
		{
			if (length < 9)
				return false;

			u8 interface_class = descriptor[offset + 5];
			u8 interface_subclass = descriptor[offset + 6];
			u8 interface_protocol = descriptor[offset + 7];

			if (interface_class == 0x03
				&& interface_subclass == 0x01
				&& interface_protocol == 0x01)
			{
				*interface_number = descriptor[offset + 2];
				return true;
			}
		}

		offset += length;
	}

	return false;
}

bool setup_usb_device_manager(USB_Device_Manager* manager, USB_Controller* controller)
{
	manager->controller = controller;
	manager->device_count = 0;

	// if (!setup_devices_buffer(manager))
	// 	return false;

	bool keyboard_found = false;
	for (u8 port = 0; port < controller->max_ports; ++port)
	{
		if (controller->usb_port_states[port] != USB_PORT_CONNECTED)
			continue;

		USB_Device new_device = {0};
		if (!setup_usb_device(&new_device, manager->controller, port + 1))
		{
			vga_printf("Warning: USB device %X could not be set up.\n", port);
			continue;
		}

		// add_device(manager, &new_device);

		u8 keyboard_interface_number;
		if (find_boot_keyboard_interface(&new_device, &keyboard_interface_number))
		{
			keyboard_found = true;
			break;
		}
	}

	if (!keyboard_found)
		return false;



	return true;
}
