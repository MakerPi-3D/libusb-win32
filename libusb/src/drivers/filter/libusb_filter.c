/* LIBUSB-WIN32, Generic Windows USB Driver
 * Copyright (C) 2002-2003 Stephan Meyer, <ste_meyer@web.de>
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


#include "libusb_filter.h"
#include <wchar.h>


static struct {
  KMUTEX mutex;
  int is_used[LIBUSB_MAX_NUMBER_OF_DEVICES];
} device_ids;

NTSTATUS __stdcall DriverEntry(DRIVER_OBJECT *driver_object,
			       UNICODE_STRING *registry_path)
{
  PDRIVER_DISPATCH *dispatch_function = driver_object->MajorFunction;
  int i;

  KeInitializeMutex(&device_ids.mutex, 0);

  for(i = 0; i < LIBUSB_MAX_NUMBER_OF_DEVICES; i++)
    {
      device_ids.is_used[i] = 0;
    }

  for(i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++, dispatch_function++) 
    {
      *dispatch_function = dispatch;
    }
  
  driver_object->DriverExtension->AddDevice = add_device;
  driver_object->DriverUnload = unload;

  return STATUS_SUCCESS;
}

NTSTATUS __stdcall add_device(DRIVER_OBJECT *driver_object, 
			      DEVICE_OBJECT *physical_device_object)
{
  NTSTATUS status;
  DEVICE_OBJECT *device_object;
  libusb_device_extension *device_extension;
  int i, j;

  status = IoCreateDevice(driver_object, sizeof(libusb_device_extension), 
			  NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, 
			  &device_object);
  if(!NT_SUCCESS(status))
    {
      KdPrint(("LIBUSB_FLTR - add_device(): creating device failed\n"));
      return status;
    }

  device_extension = (libusb_device_extension *)device_object->DeviceExtension;

  device_extension->self = device_object;
  device_extension->physical_device_object = physical_device_object;
  device_extension->next_stack_device = NULL;
  device_extension->control_device_object = NULL;
  device_extension->main_device_object = NULL;
  device_extension->driver_object = driver_object;

  device_extension->configuration_handle = NULL;
  device_extension->current_configuration = 0;

  for(i = 0; i < LIBUSB_MAX_NUMBER_OF_INTERFACES; i++)
    {
      for(j = 0; j < LIBUSB_MAX_NUMBER_OF_ENDPOINTS; j++)
	{
	  device_extension->pipe_info[i][j].endpoint_address = 0;	
	  device_extension->pipe_info[i][j].pipe_handle = NULL;
	} 
    }


  device_extension->next_stack_device = 
    IoAttachDeviceToDeviceStack(device_object, physical_device_object);
  
  initialize_remove_lock(&device_extension->remove_lock);

  
  device_object->Flags |= DO_DIRECT_IO | DO_POWER_PAGABLE;
  device_object->Flags &= ~DO_DEVICE_INITIALIZING;

  status = create_control_object(device_extension);

  return STATUS_SUCCESS;
}


VOID __stdcall unload(DRIVER_OBJECT *driver_object)
{

}

NTSTATUS pass_irp_down(libusb_device_extension *device_extension, IRP *irp)
{
  NTSTATUS status = STATUS_SUCCESS;

  IoCopyCurrentIrpStackLocationToNext(irp);
  status = IoCallDriver(device_extension->next_stack_device, irp);
  
  irp->IoStatus.Status = status;
  
  return status;
}


NTSTATUS complete_irp(IRP *irp, NTSTATUS status, ULONG info)
{
  irp->IoStatus.Status = status;
  irp->IoStatus.Information = info;
  IoCompleteRequest(irp, IO_NO_INCREMENT);
  
  return status;
}


NTSTATUS on_io_completion(DEVICE_OBJECT *device_object, 
			  IRP *irp, void *event)
{
  KeSetEvent((KEVENT *)event, IO_NO_INCREMENT, FALSE);
  return STATUS_MORE_PROCESSING_REQUIRED;
}


NTSTATUS call_usbd(libusb_device_extension *device_extension, void *urb, 
		   ULONG control_code, int timeout)
{
  IO_STATUS_BLOCK io_status;
  KEVENT event;
  NTSTATUS status = STATUS_SUCCESS;
  IRP *irp;
  IO_STACK_LOCATION *next_irp_stack;
  
  KeInitializeEvent(&event, NotificationEvent, FALSE);

  irp = IoBuildDeviceIoControlRequest(control_code, 
				      device_extension->next_stack_device,
				      NULL, 0, NULL, 0, TRUE, 
				      &event, &io_status);

  next_irp_stack = IoGetNextIrpStackLocation(irp);
  next_irp_stack->Parameters.Others.Argument1 = urb;
  next_irp_stack->Parameters.Others.Argument2 = NULL;

  IoSetCompletionRoutine(irp, (PIO_COMPLETION_ROUTINE)on_io_completion, 
 			 (void *)&event, TRUE, TRUE, TRUE); 

  status = IoCallDriver(device_extension->next_stack_device, irp);
  

  if(status == STATUS_PENDING)
    {
      LARGE_INTEGER m_timeout;
      m_timeout.QuadPart = -(timeout * 10000);
      
      if(KeWaitForSingleObject(&event, Executive, KernelMode,
			       FALSE, &m_timeout) == STATUS_TIMEOUT)
	{
	  KdPrint(("LIBUSB_FILTER - call_usbd(): request timed out\n"));
	  
	  IoCancelIrp(irp);
	  KeWaitForSingleObject(&event, Executive, KernelMode, 
				FALSE, NULL);
	}
    }

  KeClearEvent(&event);
  IoCompleteRequest(irp, IO_NO_INCREMENT);
  KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL); 
   
  status = io_status.Status;

  return status;
}


int get_pipe_handle(libusb_device_extension *device_extension, 
		    int endpoint_address, USBD_PIPE_HANDLE *pipe_handle)
{
  int i, j;

  *pipe_handle = NULL;

  for(i = 0; i < LIBUSB_MAX_NUMBER_OF_INTERFACES; i++)
    {
      for(j = 0; j < LIBUSB_MAX_NUMBER_OF_ENDPOINTS; j++)
	{
	  if(device_extension->pipe_info[i][j].endpoint_address 
	     == endpoint_address)
	    {
	      *pipe_handle = device_extension->pipe_info[i][j].pipe_handle;
	      return TRUE;
	    }
	}
    }
  return FALSE;
}

int update_pipe_info(libusb_device_extension *device_extension, int interface,
		     USBD_INTERFACE_INFORMATION *interface_info)
{
  int i;

  if(interface > LIBUSB_MAX_NUMBER_OF_INTERFACES)
    {
      return FALSE;
    }

  for(i = 0; i < LIBUSB_MAX_NUMBER_OF_ENDPOINTS ; i++)
    {
      device_extension->pipe_info[interface][i].endpoint_address = 0;	
      device_extension->pipe_info[interface][i].pipe_handle = NULL;	
    } 

  if(interface_info)
    {
      KdPrint(("LIBUSB_FILTER - update_pipe_info(): interface %d\n",
	       interface));
      for(i = 0; (i < (int)interface_info->NumberOfPipes) 
	    && (i < LIBUSB_MAX_NUMBER_OF_ENDPOINTS); i++) 
	{
	  if(i == LIBUSB_MAX_NUMBER_OF_ENDPOINTS)
	    {
	      return FALSE;
	    }

	  KdPrint(("LIBUSB_FILTER - update_pipe_info(): endpoint "
		   "address %02xh\n",
		   interface_info->Pipes[i].EndpointAddress));	  

	  device_extension->pipe_info[interface][i].pipe_handle = 
	    interface_info->Pipes[i].PipeHandle;	
	  device_extension->pipe_info[interface][i].endpoint_address = 
	    interface_info->Pipes[i].EndpointAddress;	
	}
    }
  return TRUE;
}


NTSTATUS create_control_object(libusb_device_extension *device_extension)
{
  NTSTATUS status = STATUS_SUCCESS;
  UNICODE_STRING nt_device_name;
  UNICODE_STRING symbolic_link_name;
  WCHAR tmp_name_0[64];
  WCHAR tmp_name_1[64];
  libusb_device_extension *my_device_extension;


  if(!get_device_id(device_extension))
    {
      KdPrint(("LIBUSB_FILTER - create_control_object(): getting device id"
	       " failed\n"));
      return STATUS_UNSUCCESSFUL;
    }

  KdPrint(("LIBUSB_FILTER - create_control_object(): creating device"
	   " %d\n",device_extension->device_id));

  _snwprintf(tmp_name_0, sizeof(tmp_name_0)/sizeof(WCHAR), L"%s%04d", 
	     LIBUSB_NT_DEVICE_NAME,
	     device_extension->device_id);
  
  RtlInitUnicodeString(&nt_device_name, tmp_name_0);

  _snwprintf(tmp_name_1, sizeof(tmp_name_1)/sizeof(WCHAR), L"%s%04d", 
	     LIBUSB_SYMBOLIC_LINK_NAME,
	     device_extension->device_id);
  
  RtlInitUnicodeString(&symbolic_link_name, tmp_name_1);

  status = IoCreateDevice(device_extension->driver_object,
			  sizeof(libusb_device_extension), 
			  &nt_device_name,
			  FILE_DEVICE_UNKNOWN, 0, FALSE, 
			  &device_extension->control_device_object);
  if(!NT_SUCCESS(status))
    {
      KdPrint(("LIBUSB_FILTER - create_control_object(): creating device"
	       " %d failed\n", device_extension->device_id));
      device_extension->control_device_object = NULL;

      return status;
    }

  
  my_device_extension = (libusb_device_extension *)device_extension
    ->control_device_object->DeviceExtension;


  status = IoCreateSymbolicLink(&symbolic_link_name, &nt_device_name );

  if(!NT_SUCCESS(status))
    {
      KdPrint(("LIBUSB_FILTER - create_control_object(): creating symbolic "
	       "link failed\n"));
      IoDeleteDevice(device_extension->control_device_object);
      device_extension->control_device_object = NULL;
      return status;
    }
  
  my_device_extension->self = device_extension->control_device_object;
  my_device_extension->main_device_object = device_extension->self;
  my_device_extension->control_device_object = NULL;
  my_device_extension->physical_device_object 
    = device_extension->physical_device_object;


  device_extension->control_device_object->Flags &= ~DO_DEVICE_INITIALIZING;

  return status;
}

void delete_control_object(libusb_device_extension *device_extension)
{
  UNICODE_STRING symbolic_link_name;
  WCHAR tmp_name[64];
 

  if(device_extension->control_device_object)
    {
      KdPrint(("LIBUSB_FILTER - delete_control_object(): deleting device"
	       " %d\n",device_extension->device_id));
      
      _snwprintf(tmp_name, sizeof(tmp_name)/sizeof(WCHAR), L"%s%04d", 
		 LIBUSB_SYMBOLIC_LINK_NAME,
		 device_extension->device_id);
      
      RtlInitUnicodeString(&symbolic_link_name, tmp_name);
      
      IoDeleteSymbolicLink(&symbolic_link_name);
      
      IoDeleteDevice(device_extension->control_device_object);
      release_device_id(device_extension);
    }
}


void initialize_remove_lock(libusb_remove_lock *remove_lock)
{
  KeInitializeEvent(&remove_lock->event, NotificationEvent, FALSE);
  remove_lock->usage_count = 1;
  remove_lock->remove_pending = FALSE;
}


NTSTATUS acquire_remove_lock(libusb_remove_lock *remove_lock)
{
  long usage_count = InterlockedIncrement(&remove_lock->usage_count);

  if(remove_lock->remove_pending)
    {
      if(InterlockedDecrement(&remove_lock->usage_count) == 0)
	{
	  KeSetEvent(&remove_lock->event, 0, FALSE);
	}      
      return STATUS_DELETE_PENDING;
    }
  return STATUS_SUCCESS;
}


void release_remove_lock(libusb_remove_lock *remove_lock)
{
  if(InterlockedDecrement(&remove_lock->usage_count) == 0)
    {
      KeSetEvent(&remove_lock->event, 0, FALSE);
    }
}


void release_remove_lock_and_wait(libusb_remove_lock *remove_lock)
{
  remove_lock->remove_pending = TRUE;
  release_remove_lock(remove_lock);
  release_remove_lock(remove_lock);
  KeWaitForSingleObject(&remove_lock->event, Executive, KernelMode,
			FALSE, NULL);
}

int get_device_id(libusb_device_extension *device_extension)
{
  int ret = 0;
  int i;
  
  KeWaitForSingleObject(&device_ids.mutex, Executive, KernelMode,
			FALSE, NULL);
  
  for(i = 0; i < LIBUSB_MAX_NUMBER_OF_DEVICES; i++)
    {
      if(!device_ids.is_used[i])
	{
	  device_ids.is_used[i] = 1;
	  device_extension->device_id = i;
	  ret = 1;
	  break;
	}
    }

  KeReleaseMutex(&device_ids.mutex, FALSE);
  return ret;
}

void release_device_id(libusb_device_extension *device_extension)
{
  KeWaitForSingleObject(&device_ids.mutex, Executive, KernelMode,
			FALSE, NULL);
  
  if(device_extension->device_id < LIBUSB_MAX_NUMBER_OF_DEVICES)
    device_ids.is_used[device_extension->device_id] = 0;
  
  KeReleaseMutex(&device_ids.mutex, FALSE);
}
