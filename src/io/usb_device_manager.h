#ifndef IO_USB_DEVICE_MANAGER_H
#define IO_USB_DEVICE_MANAGER_H

#include "usb_controller.h"
#include "usb_device.h"

typedef struct
{
	USB_Controller* controller;
	// USB_Device* devices;
	// u8 device_count;

	USB_Device keyboard_device;
	// USB_Interface* keyboard_interface;
	i16 keyboard_interface_index;
}USB_Device_Manager;

bool setup_usb_device_manager(USB_Device_Manager*, USB_Controller*);

#endif // IO_USB_DEVICE_MANAGER_H
