#include "memory/phys_memory_map.h"
#include "memory/frame_allocator.h"
#include "memory/page_table.h"
#include "interrupt/idt.h"
#include "io/usb_device.h"
#include "vga/vga.h"
#include "io/usb_controller.h"
#include "io/usb_device_manager.h"

Page_table* global_page_table;
static Idt_entry idt[256];
Frame_allocator global_frame_allocator;

// static void print_memory_map(const Phys_memory_map* memory_regions, const char* name)
// {
//     vga_printf("%Z%s:%z\n", 0x09 ,name);
//     u64 total_memory_available = 0;
//     for (u32 i = 0; i < memory_regions->region_count; ++i)
//     {
//         total_memory_available += (memory_regions->end_addr[i] - memory_regions->start_addr[i] + 1);
//         vga_printf("start = %X, end = %X\n", memory_regions->start_addr[i], memory_regions->end_addr[i]);
//     }
//     // u64 total_frames_available = total_memory_available / FRAME_SIZE;
//
//     vga_printf("%s: %X bytes\n", name, total_memory_available);
//     // vga_printf("Total frames in %s: %X\n%z\n", name, total_frames_available);
// }


static void measure_memory_regions(const Phys_memory_map* memory_regions, const char* name)
{
	u64 total_memory_available = 0;
	for (u32 i = 0; i < memory_regions->region_count; ++i)
	{
		total_memory_available += (memory_regions->end_addr[i] - memory_regions->start_addr[i] + 1);
	}

	vga_printf("%s size: %X bytes\n", name, total_memory_available);
}

static bool present_in_report(const Key_report* key_report, u8 hid_code)
{
	for (u8 i = 0; i < 6; ++i)
		if (key_report->keycodes[i] == hid_code)
			return true;

	return false;
}

void kernel_main(const Kernel_mem_info* kernel_mem_info)
{
    const Phys_addr LOW_MEM_SIZE = 1 << 20;
    const u64 KERNEL_MEM_SIZE = 1 << 30; // The government will feed itself.

    disable_bios_cursor();
    clear_screen();
    vga_printf("%ZIdaho OS. Witamy!\n%z\n", 0x1F);


    Phys_memory_map phys_memory_regions;
    determine_available_phys_addresses(&phys_memory_regions, kernel_mem_info, LOW_MEM_SIZE);

    Phys_memory_map kernel_regions;
    register_kernel_regions(&kernel_regions, kernel_mem_info);

    measure_memory_regions(&phys_memory_regions, "available memory");
    measure_memory_regions(&kernel_regions, "kernel memory");


    setup_frame_allocator(&phys_memory_regions, &kernel_regions, KERNEL_MEM_SIZE);

    global_page_table = (Page_table*)(kernel_mem_info->page_table_address);

    setup_idt(idt);
    vga_printf("\nIDT set up.%z\n");

    USB_Controller usb_controller = setup_usb_controller();
	if (!usb_controller.found)
	{
		panic("Could not configure USB controller.");
	}
    vga_printf("USB controller set up.\n\n");

	USB_Device_Manager dvm;
	if (setup_usb_device_manager(&dvm, &usb_controller))
		vga_printf("%ZKeyboard configuration done.\n%z", 0x0A);
	else
		vga_printf("%ZKeyboard configuration failed.\n%z", 0x04);


	Key_report prev_report = {0};
	for (;;)
	{
		Key_report new_report = keyboard_step(&dvm);
		if (!new_report.present)
			continue;

		if (!prev_report.present)
		{
			prev_report = new_report;
			continue;
		}

		for (u8 kc = 0; kc < 6; ++kc)
		{
			u8 hid_code = prev_report.keycodes[kc];
			if (hid_code == 0)
				continue;

			if (!present_in_report(&new_report, hid_code))
			{
				char ascii_code = 'A' + (char)hid_code - 0x04;
				vga_printf("%c", ascii_code);
			}
		}

		prev_report = new_report;
	}

    for (;;);
}
