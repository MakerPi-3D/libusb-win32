

/* LIBUSB-WIN32, Generic Windows USB Driver
 * Copyright (C) 2002-2004 Stephan Meyer, <ste_meyer@web.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */



#ifndef __USB_REGISTRY_H__
#define __USB_REGISTRY_H__

#include <windows.h>
#include <setupapi.h>


#define LIBUSB_DRIVER_NAME_NT "libusb0"
#define LIBUSB_DRIVER_NAME_9X "libusb0.sys"

typedef int bool_t;

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE (!(FALSE)) 
#endif

#define REGISTRY_BUF_SIZE 512


bool_t usb_registry_is_nt(void);

bool_t usb_registry_get_property(DWORD which, HDEVINFO dev_info, 
                                 SP_DEVINFO_DATA *dev_info_data,
                                 char *buf, int size);
bool_t usb_registry_set_property(DWORD which, HDEVINFO dev_info, 
                                 SP_DEVINFO_DATA *dev_info_data, 
                                 char *buf, int size);

bool_t usb_registry_restart_device(HDEVINFO dev_info, 
                                   SP_DEVINFO_DATA *dev_info_data);
bool_t usb_registry_stop_device(HDEVINFO dev_info, 
                                SP_DEVINFO_DATA *dev_info_data);
bool_t usb_registry_start_device(HDEVINFO dev_info, 
                                 SP_DEVINFO_DATA *dev_info_data);

bool_t usb_registry_restart_root_hubs(void);

bool_t usb_registry_insert_filter(HDEVINFO dev_info,
                                  SP_DEVINFO_DATA *dev_info_data,
                                  char *filter_name);
bool_t usb_registry_remove_filter(HDEVINFO dev_info, 
                                  SP_DEVINFO_DATA *dev_info_data,
                                  char *filter_name);

bool_t usb_registry_is_service_libusb(HDEVINFO dev_info, 
                                      SP_DEVINFO_DATA *dev_info_data);

void usb_registry_start_filter(bool_t restart);
void usb_registry_stop_filter(bool_t restart);

void usb_registry_stop_libusb_devices(void);
void usb_registry_start_libusb_devices(void);


bool_t usb_registry_match(HDEVINFO dev_info, SP_DEVINFO_DATA *dev_info_data);
bool_t usb_registry_match_no_root_hubs(HDEVINFO dev_info, 
                                       SP_DEVINFO_DATA *dev_info_data);

int usb_registry_mz_string_size(char *src);
char *usb_registry_mz_string_find(char *src, char *str);
char *usb_registry_mz_string_find_sub(char *src, char *str);
bool_t usb_registry_mz_string_insert(char *src, char *str);
bool_t usb_registry_mz_string_remove(char *src, char *str);
void usb_registry_mz_string_lower(char *src);

int usb_registry_get_num_busses(void);

#endif