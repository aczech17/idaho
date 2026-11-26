#include "memory/phys_memory_map.h"
#include "memory/frame_allocator.h"
#include "memory/page_table.h"
#include "interrupt/idt.h"
#include "vga/vga.h"
#include "io/usb_controller.h"
#include "io/usb_device_manager.h"
#include "command_line.h"
#include "acpi/rsdp.h"
#include "acpi/rsdt.h"

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

static void format_build_date(char output[11])
{
	const char* date = __DATE__;
	u8 month = 0;

	switch (date[0])
	{
		case 'J':
			if (date[1] == 'a')
				month = 1;
			else if (date[2] == 'n')
				month = 6;
			else
				month = 7;
			break;

		case 'F': month = 2; break;
		case 'M': month = date[2] == 'r' ? 3 : 5; break;
		case 'A': month = date[1] == 'p' ? 4 : 8; break;
		case 'S': month = 9; break;
		case 'O': month = 10; break;
		case 'N': month = 11; break;
		case 'D': month = 12; break;

		default: break;
	}

	output[0] = date[4] == ' ' ? '0' : date[4];
	output[1] = date[5];
	output[2] = '.';

	output[3] = '0' + month / 10;
	output[4] = '0' + month % 10;
	output[5] = '.';

	for (u8 i = 0; i < 4; ++i)
		output[6 + i] = date[7 + i];

	output[10] = '\0';
}

void kernel_main(const Kernel_mem_info* kernel_mem_info)
{
    const Phys_addr LOW_MEM_SIZE = 1 << 20;
    const u64 KERNEL_MEM_SIZE = 1 << 29; // The government will feed itself.

    disable_bios_cursor();
    clear_screen();
    vga_printf("%ZIdaho OS. Witamy!\n%z", 0x1F);

	char build_date[11] = {0};
	format_build_date(build_date);

	vga_printf("Build date: %s\n\n", build_date);

    Phys_memory_map phys_memory_regions;
    determine_available_phys_addresses(&phys_memory_regions, kernel_mem_info, LOW_MEM_SIZE);

    Phys_memory_map kernel_regions;
    register_kernel_regions(&kernel_regions, kernel_mem_info);

    measure_memory_regions(&phys_memory_regions, "available memory");
    measure_memory_regions(&kernel_regions, "kernel memory");


    setup_global_frame_allocator(&phys_memory_regions, &kernel_regions, KERNEL_MEM_SIZE);
    global_page_table = (Page_table*)(kernel_mem_info->page_table_address);

    setup_idt(idt);
    vga_printf("\nIDT set up.%z\n");

    USB_Controller usb_controller = setup_usb_controller();
	if (!usb_controller.found)
	{
		panic("Could not configure USB controller.");
	}
    vga_printf("USB controller set up.\n\n");

	RSDP rsdp;
	setup_rsdp(&rsdp);



	u64 rsdt_address = 0;
	if (rsdp.revision >= 2)
		rsdt_address = rsdp.xsdt_address;
	else
		rsdt_address = (u64)rsdp.rsdt_address;

	// vga_printf("RSDP revision: %u\n", rsdp.revision);
	// vga_printf("RSDT addr: %x\n", (u64)rsdp.rsdt_address);
	// vga_printf("XSDT addr: %x\n", rsdp.xsdt_address);
	// vga_printf("Using:     %x\n", rsdt_address);

	RSDT rsdt;
	setup_rsdt(&rsdt, rsdt_address);


	USB_Device_Manager dvm;
	if (setup_usb_device_manager(&dvm, &usb_controller))
		vga_printf("%ZKeyboard configuration done.\n%z", 0x0A);
	else
		panic("Keyboard configuration failed.");

	Command_line cmd = {0};
	cmd.prompt = "%";

	vga_printf("\n%s: ", cmd.prompt);
	for (;;)
	{
		u8 hid_code = get_keyboard_hid_code(&dvm);
		process_cmd(&cmd, hid_code);
	}

    for (;;);
}
