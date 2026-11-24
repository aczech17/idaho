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
}USB_Device_Manager;

bool setup_usb_device_manager(USB_Device_Manager*, USB_Controller*);

#endif // IO_USB_DEVICE_MANAGER_H
