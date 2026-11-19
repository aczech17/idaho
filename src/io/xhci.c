#include "xhci.h"
#include "../memory/page_table.h"
#include "../memory/bump_allocator.h"

static XHCI_Info find_xhci_info(void)
{
	XHCI_Info xhci_info = {0};

	for (u16 bus = 0; bus < 256; ++bus)
		for (u8 device = 0; device < 32; ++device)
			for (u8 function = 0; function < 8; ++function)
			{
				u32 ident = pci_read_config(bus, device, function, 0);

				u16 vendor_id = ident & 0xFFFF;
				if (vendor_id == 0xFFFF)
					continue; // nonexistent

				u32 class_info = pci_read_config(bus, device, function, 0x08);
				u32 class_code = class_info >> 8;

				if (class_code == XHCI_SIGNATURE)
				{
					xhci_info.bus = bus;
					xhci_info.device = device;
					xhci_info.function = function;
					xhci_info.found = true;

					return xhci_info;
				}
			}

	return xhci_info;
}

static u64 get_mmio_xhci_size(const XHCI_Info* info)
{
	u32 bar0 = pci_read_config(info->bus, info->device, info->function, 0x10);
	u32 bar1 = pci_read_config(info->bus, info->device, info->function, 0x14);

	u32 command_status = pci_read_config(info->bus, info->device, info->function, 0x04);
	u16 command = command_status & 0xFFFF;

	// Reset memory space enable for bar measure.
	pci_write_config_16_bit(info->bus, info->device, info->function, 0x04, command & ~(1 << 1));

	// Write ones to BAR
	pci_write_config_32_bit(info->bus, info->device, info->function, 0x10, 0xFFFFFFFF);
	pci_write_config_32_bit(info->bus, info->device, info->function, 0x14, 0xFFFFFFFF);

	u32 bar0_mask = pci_read_config(info->bus, info->device, info->function, 0x10);
	u32 bar1_mask = pci_read_config(info->bus, info->device, info->function, 0x14);

	// Restore original BAR
	pci_write_config_32_bit(info->bus, info->device, info->function, 0x10, bar0);
	pci_write_config_32_bit(info->bus, info->device, info->function, 0x14, bar1);

	// Restore command register
	pci_write_config_16_bit(info->bus, info->device, info->function, 0x04, command);

	u64 address_mask = (((u64)bar1_mask) << 32) | (bar0_mask & 0xFFFFFFF0U);
	u64 size = ~address_mask + 1;

	return size;
}

static void enable_xhci_memory_space(const XHCI_Info* info)
{
	u32 command_status = pci_read_config(info->bus, info->device, info->function, 0x04);

	u16 command = command_status & 0xFFFF;
	command |= (1 << 1); // Set memory space enable bit
	command |= (1 << 2);  // Set bus master enable bit

	pci_write_config_16_bit(info->bus, info->device, info->function, 0x04, command);
}

static u64 get_mmio_xhci_address(const XHCI_Info* info)
{
	u32 bar0 = pci_read_config(info->bus, info->device, info->function, 0x10);
	u32 bar1 = pci_read_config(info->bus, info->device, info->function, 0x14);

	u32 bar0_addr = bar0 & 0xFFFFFFF0;

	u64 mmio_addr = ((u64)bar1 << 32) | (u64)bar0_addr;
	return mmio_addr;
}

static void take_ownership(const u64 mmio_addr)
{
	volatile u32* regs = (u32*)mmio_addr;

	u32 hccparams1 = regs[4];
	u32 xecp = ((hccparams1 >> 16) & 0xFFFF);

	u32 xecp_offset = xecp * 4;

	if (xecp == 0)
		return;

	for (;;)
	{
		volatile u32* header_pointer = (u32*)(mmio_addr + xecp_offset);
		u32 header = *header_pointer;

		u8 capability_id = header & 0xFF;
		if (capability_id == 1) // USB legacy support found
		{
			((volatile u8*)header_pointer)[3] |= 1U;

			// Wait until the BIOS releases the controller.
			while (((*header_pointer) & (1U << 16)) != 0) {}
			break;
		}

		u8 next_offset_pointer = (header >> 8) & 0xFF;
		if (next_offset_pointer == 0)
			break;

		xecp_offset += next_offset_pointer * 4;
	}
}

static void stop(const u64 mmio_addr)
{
	volatile u32* regs = (u32*)(mmio_addr);
	u32 operational_offset = regs[0] & 0xFF;

	volatile u32* operational_regs = (volatile u32*)(mmio_addr + operational_offset);
	volatile u32* usbcmd = &operational_regs[0];
	volatile u32* usbsts = &operational_regs[1];

	*usbcmd &= ~1U;

	while (((*usbsts) & 1U) == 0) {}
}

static void restart(const u64 mmio_addr)
{
	volatile u32* regs = (u32*)(mmio_addr);
	u32 operational_offset = regs[0] & 0xFF;

	volatile u32* operational_regs = (volatile u32*)(mmio_addr + operational_offset);
	volatile u32* usbcmd = &operational_regs[0];
	volatile u32* usbsts = &operational_regs[1];

	*usbcmd |= (1U << 1);

	while (((*usbcmd) & (1U << 1)) != 0) {} // host controller reset (HCRST)
	while (((*usbsts) & (1U << 11)) != 0) {} // controller not ready (CNR)
}

static void set_up_scratchpad(XHCI_Info* info, u64 max_frame_number)
{
	volatile u32* regs = (u32*)(info->mmio_addr);
	u32 operational_offset = regs[0] & 0xFF;

	volatile u32* op_regs = (u32*)(info->mmio_addr + operational_offset);
	u32 page_size = op_regs[2];

	if (!(page_size & 1))
		panic("xHCI does not support 4 KiB pages.");

	u64 scratchpad_array_size = info->scratchpad_count * 8;
	u64 scratchpad_array_page_count = (scratchpad_array_size + FRAME_SIZE - 1) / FRAME_SIZE;

	Phys_addr scratchpad_start_frame_phys_addr =
		allocate_contiguous_general_frames(&global_frame_allocator, scratchpad_array_page_count, max_frame_number);
	Virt_addr scratchpad_start_page_virt_addr = bump_allocate_pages(scratchpad_array_page_count);


	u64 page_addr = scratchpad_start_page_virt_addr;
	u64 frame_addr = scratchpad_start_frame_phys_addr;
	for (u64 i = 0; i < scratchpad_array_page_count; ++i)
	{
		map_address(page_addr, frame_addr, PTE_WRITABLE);

		page_addr += FRAME_SIZE;
		frame_addr += FRAME_SIZE;
	}

	memset((void*)scratchpad_start_page_virt_addr, 0, scratchpad_array_size);

	*(u64*)(info->dcbaa_virt_addr) = scratchpad_start_frame_phys_addr;
	info->scratchpad_virt_addr = scratchpad_start_page_virt_addr;

	u32 n = info->scratchpad_count;
	for (u32 i = 0; i < n; ++i)
	{
		Phys_addr phys_addr = allocate_contiguous_general_frames(&global_frame_allocator, 1, max_frame_number);
		Virt_addr virt_addr = bump_allocate_pages(1);
		map_address(virt_addr, phys_addr, PTE_WRITABLE);
		memset((void*)virt_addr, 0, FRAME_SIZE);

		u64* scratchpad_addresses = (u64*)(info->scratchpad_virt_addr);
		scratchpad_addresses[i] = phys_addr;
	}
}

XHCI_Info setup_xhci(void)
{
	XHCI_Info xhci_info = find_xhci_info();
	if (!xhci_info.found)
	{
		panic("Could not find XHCI info.");
	}

	u64 mmio_size = get_mmio_xhci_size(&xhci_info);
	enable_xhci_memory_space(&xhci_info);

	u64 mmio_addr = get_mmio_xhci_address(&xhci_info);
	xhci_info.mmio_addr = mmio_addr;

	u64 mmio_page_addr = mmio_addr & ~(FRAME_SIZE - 1ULL);
	u64 mmio_offset = mmio_addr - mmio_page_addr;

	u64 mmio_page_count =
		(mmio_offset + mmio_size + FRAME_SIZE - 1) / FRAME_SIZE;

	for (u64 i = 0; i < mmio_page_count; ++i)
	{
		u64 page_addr = mmio_page_addr + i * FRAME_SIZE;

		map_address(page_addr, (Phys_addr)page_addr,
					PTE_WRITABLE | PTE_CACHE_DISABLE);
	}

	volatile u32* xhci_regs = (u32*)(mmio_addr);

	u32 cap_length = xhci_regs[0] & 0xFF;
	u32 hcsparams1 = xhci_regs[1];
	u32 hcsparams2 = xhci_regs[2];
	u32 hccparams1 = xhci_regs[4];

	volatile u32* op_regs = (volatile u32*)(mmio_addr + cap_length);
	volatile u32* config = &op_regs[14];

	xhci_info.max_slots = hcsparams1 & 0xFF;
	xhci_info.max_interrupters = (hcsparams1 >> 8) & 0x7FF;
	xhci_info.max_ports = (hcsparams1 >> 24) & 0xFF;
	xhci_info.scratchpad_count = (((hcsparams2 >> 21) & 0x1F) << 5) | ((hcsparams2 >> 27) & 0x1F);

	take_ownership(mmio_addr);
	stop(mmio_addr);
	restart(mmio_addr);

	// set max slots shared with kernel to max value possible
	u32 config_val = *config;
	config_val = (config_val & ~0xFF) | xhci_info.max_slots;
	*config = config_val;

	bool supports_64bit_dma = (hccparams1 & 1) != 0;
	u64 max_frame_number = supports_64bit_dma ? FRAME_BITMAP_SIZE * 8 - 1 : 0xFFFFFFFFULL / FRAME_SIZE;

	u64 dcbaa_size = (xhci_info.max_slots + 1) * 8;

	// Allocate memory for DCBAA. DCBAA has maximum of 256 entries, that makes 256 * 8 = 2048 bytes.
	// 2048 < 4096, so one page is enough.
	Phys_addr dcbaa_phys_addr = allocate_contiguous_general_frames(&global_frame_allocator, 1, max_frame_number);
	if (dcbaa_phys_addr == INVALID_ADDR)
		panic("Could not allocate mem for DCBAA.");

	Virt_addr dcbaa_virt_addr = bump_allocate_pages(1);
	map_address(dcbaa_virt_addr, dcbaa_phys_addr, PTE_WRITABLE);
	memset((void*)dcbaa_virt_addr, 0, dcbaa_size);

	xhci_info.dcbaa_virt_addr = dcbaa_virt_addr;


	if (xhci_info.scratchpad_count > 0)
		set_up_scratchpad(&xhci_info, max_frame_number);

	volatile u64* dcbaap = (volatile u64*)&op_regs[12];

	if (supports_64bit_dma)
		*dcbaap = dcbaa_phys_addr;
	else
		*(volatile u32*)(dcbaap) = (u32)(dcbaa_phys_addr);

	return xhci_info;
}
