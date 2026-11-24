#ifndef IO_USB_DEVICE_MANAGER_H
#define IO_USB_DEVICE_MANAGER_H

#include "usb_controller.h"
#include "usb_device.h"

typedef struct
{
	USB_Controller* controller;
	USB_Device keyboard_device;
	i16 keyboard_interface_index;
	USB_Interface_Endpoint* keyboard_interrupt_in_endpoint;

	bool keyboard_transfer_pending;
}USB_Device_Manager;

typedef struct
{
	bool present;
	u8 modifiers;
	u8 keycodes[6];
}Key_report;


bool setup_usb_device_manager(USB_Device_Manager*, USB_Controller*);
Key_report keyboard_step(USB_Device_Manager* manager);

#endif // IO_USB_DEVICE_MANAGER_H
