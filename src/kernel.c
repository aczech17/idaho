#include "memory/phys_memory_map.h"
#include "memory/frame_allocator.h"
#include "memory/page_table.h"
#include "interrupt/idt.h"
#include "vga/vga.h"
#include "io/usb_controller.h"
#include "io/usb_device_manager.h"
#include "command_line.h"

Page_table* global_page_table;
static Idt_entry idt[256];
Frame_allocator global_frame_allocator;

static void measure_memory_regions(const Phys_memory_map* memory_regions, const char* name)
{
	u64 total_memory_available = 0;
	for (u32 i = 0; i < memory_regions->region_count; ++i)
	{
		total_memory_available += (memory_regions->end_addr[i] - memory_regions->start_addr[i] + 1);
	}

	vga_printf("%s size: %X bytes\n", name, total_memory_available);
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

	Command_line cmd = {0};
	cmd.prompt = "%";

	vga_printf("\n%s: ", cmd.prompt);
	for (;;)
	{
		char c = get_keyboard_key(&dvm);
		if (c == 0)
			continue;

		if (c == '\n')
			execute_command_line_command(&cmd);
		else
		{
			add_letter_to_command_line(&cmd, c);
			vga_printf("%c", c);
		}

	}

    for (;;);
}
