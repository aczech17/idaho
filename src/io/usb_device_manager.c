#include "usb_device_manager.h"
#include "../memory/frame_allocator.h"
#include "../memory/arena_allocator.h"
#include "../memory/page_table.h"
#include "../common.h"

static bool setup_devices_buffer(USB_Device_Manager* manager)
{
	u64 buffer_size = (u64)(manager->controller->max_ports) * sizeof(USB_Device);
	u64 frames_to_allocate = (buffer_size + FRAME_SIZE - 1) / FRAME_SIZE;

	Phys_addr mem_buffer_phys_addr = allocate_contiguous_general_frames(frames_to_allocate, MAX_FRAME_NUM);
	Virt_addr mem_buffer_virt_addr = arena_allocate_pages(&xhci_driver_page_allocator, frames_to_allocate);

	if (mem_buffer_phys_addr == INVALID_ADDR || mem_buffer_virt_addr == INVALID_ADDR)
		return false;

	for (u64 frame = 0; frame < frames_to_allocate; ++frame)
	{
		Virt_addr virt = mem_buffer_virt_addr + frame * FRAME_SIZE;
		Phys_addr phys = mem_buffer_phys_addr + frame * FRAME_SIZE;
		map_address(virt, phys, PTE_WRITABLE);
	}
	memset((void*)mem_buffer_virt_addr, 0, frames_to_allocate * FRAME_SIZE);

	manager->devices = (USB_Device*)mem_buffer_virt_addr;
	manager->device_count = 0;

	return true;
}

static void add_device(USB_Device_Manager* manager, USB_Device* device)
{
	USB_Device* dst = &manager->devices[manager->device_count];
	memcpy(dst, device, sizeof(USB_Device));

	++manager->device_count;
}

bool setup_usb_device_manager(USB_Device_Manager* manager, USB_Controller* controller)
{
	manager->controller = controller;
	manager->device_count = 0;

	if (!setup_devices_buffer(manager))
		return false;

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

		add_device(manager, &new_device);
	}



	return true;
}
