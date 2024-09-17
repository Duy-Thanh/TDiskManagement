//#pragma warning(push,3)
//#pragma warning(disable:4115)  // named typedef in parenthesis
//#pragma warning(disable:4200)  // nameless struct/union
//#pragma warning(disable:4201)  // nameless struct/union
//#pragma warning(disable:4214)  // bit field types other than int

#pragma warning(disable:4101)

#define MAX_MOUNT_NAME_SIZE 4096  // Adjust as needed

// IOCTLs
#define IOCTL_GET_READY			CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_DISK_INFO		CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

// DEFINITIONS
#define MAX_DISK_NAME			256
#define MAX_MANUFACTURER		256
#define MAX_PATH				256

#include <initguid.h>
#include <ntddk.h>
#include <wdf.h>
//#include <ntddstor.h>
#include <ntdddisk.h>
#include <mountmgr.h>
#include <limits.h>
#include <ntddscsi.h>

//#pragma warning(pop)

typedef struct _PARTITION_INFO {
	WCHAR PartitionName[MAX_PATH];
	WCHAR DriveLetter;
} PARTITION_INFO, *PPARTITION_INFO;

typedef struct _DISK_INFO {
	WCHAR DiskName[MAX_DISK_NAME];
	WCHAR Manufacturer[MAX_MANUFACTURER];
	ULONG PartitionCount;
	PARTITION_INFO Partitions[1];
} DISK_INFO, *PDISK_INFO;

DRIVER_INITIALIZE DriverEntry;

/// <summary>
/// Declarations
/// </summary>
/// <param name="DriverObject"></param>
/// <param name="RegistryPath"></param>
/// <returns></returns>
NTSTATUS DriverEntry(
	_In_ PDRIVER_OBJECT DriverObject,
	_In_ PUNICODE_STRING RegistryPath
);

NTSTATUS DeviceCreate(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP Irp
);

NTSTATUS DeviceClose(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP Irp
);

NTSTATUS DeviceControl(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP Irp
);

VOID DeviceUnload(
	_In_ PDRIVER_OBJECT DriverObject
);

/// Core function declarations
NTSTATUS EnumerateDisks();
NTSTATUS GetDiskInformation(PDEVICE_OBJECT DeviceObject);
NTSTATUS QueryDiskPartitions(PDEVICE_OBJECT DeviceObject);
NTSTATUS QueryPartitionMountPoint(PDEVICE_OBJECT DeviceObject, ULONG PartitionNumber);
NTSTATUS GetPartitionInfo(PDEVICE_OBJECT DeviceObject, PDISK_INFO DiskInfo);

/// <summary>
/// DEFINITIONS
/// </summary>
/// <param name="DeviceObject"></param>
/// <param name="Irp"></param>
/// <returns></returns>
NTSTATUS DeviceCreate(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP Irp
) {
	UNREFERENCED_PARAMETER(DeviceObject);

	NTSTATUS status = STATUS_SUCCESS;

	Irp->IoStatus.Status = STATUS_SUCCESS;

	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return status;
}

NTSTATUS DeviceClose(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP Irp
) {
	UNREFERENCED_PARAMETER(DeviceObject);

	NTSTATUS status = STATUS_SUCCESS;

	Irp->IoStatus.Status = STATUS_SUCCESS;

	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return status;
}

NTSTATUS DeviceControl(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP Irp
) {
	UNREFERENCED_PARAMETER(DeviceObject);

	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
	ULONG ioControlCode = irpSp->Parameters.DeviceIoControl.IoControlCode;
	//ULONG inputBufferLength = irpSp->Parameters.DeviceIoControl.InputBufferLength;
	ULONG outputBufferLength = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
	PVOID ioBuffer = Irp->AssociatedIrp.SystemBuffer;

	switch (ioControlCode) {
		case IOCTL_GET_READY: {
			if (outputBufferLength < 6) {
				status = STATUS_BUFFER_TOO_SMALL;
				break;
			}

			RtlCopyMemory(ioBuffer, "ready", 6);
			Irp->IoStatus.Information = 6;
			break;
		}
		
		default:
			status = STATUS_INVALID_DEVICE_REQUEST;
			break;
	}

	Irp->IoStatus.Status = STATUS_SUCCESS;

	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return status;
}

VOID DeviceUnload(
	_In_ PDRIVER_OBJECT DriverObject
) {
	UNICODE_STRING symbolicLinkName;

	RtlInitUnicodeString(&symbolicLinkName, L"\\DosDevices\\diskctrl");

	IoDeleteSymbolicLink(&symbolicLinkName);

	// Delete device
	IoDeleteDevice(DriverObject->DeviceObject);

	DbgPrint("diskctrl: Driver unloaded\n");
}

/// Core function definitions

NTSTATUS EnumerateDisks() {
	NTSTATUS status;
	PWSTR deviceInterfaceList = NULL;
	UNICODE_STRING deviceName;
	PFILE_OBJECT fileObject = NULL;
	PDEVICE_OBJECT deviceObject = NULL;
	ULONG deviceCount = 0;
	PWSTR currentDevice = NULL;

	// Query the list of device interfaces for disk devices
	status = IoGetDeviceInterfaces(
		&GUID_DEVINTERFACE_DISK,
		NULL,
		0,
		&deviceInterfaceList
	);

	if (!NT_SUCCESS(status)) {
		DbgPrint("diskctrl: Failed to retrieve device interface list (status: 0x%x)\n", status);
		return status;
	}

	if (deviceInterfaceList == NULL) {
		DbgPrint("diskctrl: Device interface list is null\n");
		return STATUS_UNSUCCESSFUL;
	}

	currentDevice = deviceInterfaceList;
	while (*currentDevice != UNICODE_NULL && deviceCount < 100) {  // Limit to 100 devices as a safety measure
		RtlInitUnicodeString(&deviceName, currentDevice);
		DbgPrint("diskctrl: Found disk interface: %wZ\n", &deviceName);

		status = IoGetDeviceObjectPointer(
			&deviceName,
			FILE_READ_DATA,
			&fileObject,
			&deviceObject
		);

		if (NT_SUCCESS(status)) {
			status = GetDiskInformation(deviceObject);
			if (!NT_SUCCESS(status)) {
				DbgPrint("diskctrl: Failed to query disk properties (status: 0x%x)\n", status);
			}

			status = QueryDiskPartitions(deviceObject);
			if (!NT_SUCCESS(status)) {
				DbgPrint("diskctrl: Failed to query disk partitions (status: 0x%x)\n", status);
			}

			ObDereferenceObject(fileObject);
		}
		else {
			DbgPrint("diskctrl: Failed to get device object pointer for: %wZ (status: 0x%X)\n",
				&deviceName, status);
		}

		// Move to the next device interface string
		currentDevice += wcslen(currentDevice) + 1;
		deviceCount++;
	}

	DbgPrint("diskctrl: Total disks: %d\n", deviceCount);

	if (deviceInterfaceList) {
		ExFreePool(deviceInterfaceList);
	}

	return STATUS_SUCCESS;
}

NTSTATUS GetDiskInformation(PDEVICE_OBJECT DeviceObject) {
	NTSTATUS status = STATUS_SUCCESS;
	STORAGE_PROPERTY_QUERY query;
	STORAGE_DEVICE_DESCRIPTOR* deviceDescriptor = NULL;
	ULONG deviceDescriptorSize = sizeof(STORAGE_DEVICE_DESCRIPTOR) + 256;
	//ULONG returnedLength = 0;
	KEVENT event;
	PIRP irp;
	IO_STATUS_BLOCK ioStatusBlock;

	// Allocate buffer for the device descriptor
	deviceDescriptor = (STORAGE_DEVICE_DESCRIPTOR*)ExAllocatePool2(POOL_FLAG_NON_PAGED, deviceDescriptorSize, 'DISK');
	if (!deviceDescriptor) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	// Initialize the query
	RtlZeroMemory(&query, sizeof(STORAGE_PROPERTY_QUERY));
	query.PropertyId = StorageDeviceProperty;
	query.QueryType = PropertyStandardQuery;

	// Initialize the event for synchorization
	KeInitializeEvent(&event, NotificationEvent, FALSE);

	irp = IoBuildDeviceIoControlRequest(
		IOCTL_STORAGE_QUERY_PROPERTY,
		DeviceObject,
		&query,
		sizeof(STORAGE_PROPERTY_QUERY),
		deviceDescriptor,
		deviceDescriptorSize,
		FALSE,
		&event,
		&ioStatusBlock
	);

	if (irp == NULL) {
		ExFreePool(deviceDescriptor);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	// Send the IRP to the driver
	status = IoCallDriver(DeviceObject, irp);

	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
		status = ioStatusBlock.Status;
	}

	if (NT_SUCCESS(status)) {
		// Determine whether the device is external or internal, SSD or HDD
		if (deviceDescriptor->BusType == BusTypeUsb) {
			DbgPrint("diskctrl: Device is an external USB\n");
		}
		else {
			DbgPrint("diskctrl: Device is an internal HDD or SSD\n");
		}

		// Check if HDD/SSD distinction is available
		if (deviceDescriptor->DeviceType == FILE_DEVICE_DISK) {
			if (deviceDescriptor->RemovableMedia) {
				DbgPrint("diskctrl: Device is an internal HDD\n");
			}
			else {
				DbgPrint("diskctrl: Device is an internal SSD\n");
			}
		}
	}
	else {
		DbgPrint("diskctrl: Device is an internal SSD\n");
	}

	// Cleanup
	ExFreePool(deviceDescriptor);

	return status;
}

NTSTATUS QueryDiskPartitions(PDEVICE_OBJECT DeviceObject) {
	NTSTATUS status = STATUS_SUCCESS;
	DRIVE_LAYOUT_INFORMATION_EX* layoutInfo = NULL;
	ULONG layoutInfoSize = sizeof(DRIVE_LAYOUT_INFORMATION_EX) + 10 * sizeof(PARTITION_INFORMATION_EX);
	KEVENT event;
	PIRP irp;
	IO_STATUS_BLOCK ioStatusBlock;

	// Allocate memory for partition layout info
	layoutInfo = (DRIVE_LAYOUT_INFORMATION_EX*)ExAllocatePool2(POOL_FLAG_NON_PAGED, layoutInfoSize, 'LAYT');

	if (!layoutInfo) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	// Initialize the event for synchorization
	KeInitializeEvent(&event, NotificationEvent, FALSE);

	// Build the IRP for the IOCTL_DISK_GET_DRIVE_LAYOUT_EX
	irp = IoBuildDeviceIoControlRequest(
		IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
		DeviceObject,
		NULL,
		0,
		layoutInfo,
		layoutInfoSize,
		FALSE,
		&event,
		&ioStatusBlock
	);

	if (irp == NULL) {
		ExFreePool(layoutInfo);
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	
	// Send the IRP to the driver
	status = IoCallDriver(DeviceObject, irp);

	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
		status = ioStatusBlock.Status;
	}

	if (NT_SUCCESS(status)) {
		ULONG partitionCount = layoutInfo->PartitionCount;
		DbgPrint("diskctrl: Number of partitions: %lu\n", partitionCount);

		for (ULONG i = 0; i < partitionCount; ++i) {
			PARTITION_INFORMATION_EX partition = layoutInfo->PartitionEntry[i];
			LARGE_INTEGER partitionSize = partition.PartitionLength;

			DbgPrint("diskctrl: Partition %lu have size %llu bytes (%llu GB)\n",
				i + 1, partitionSize.QuadPart,
				partitionSize.QuadPart / (1024 * 1024 * 1024));

			// Retrieve partition mount point (if any)
			QueryPartitionMountPoint(DeviceObject, i);
		}
	}
	else {
		DbgPrint("diskctrl: Failed to get drive layout (status = 0x%x)\n", status);
	}

	ExFreePool(layoutInfo);

	return status;
}

NTSTATUS QueryPartitionMountPoint(PDEVICE_OBJECT DeviceObject, ULONG PartitionNumber) {
	NTSTATUS status;
	PMOUNTDEV_NAME mountName = NULL;
	ULONG mountNameSize = sizeof(MOUNTDEV_NAME) + 256;  // Initial buffer size
	KEVENT event;
	PIRP irp;
	IO_STATUS_BLOCK ioStatusBlock;

	while (TRUE) {
		if (mountNameSize > MAX_MOUNT_NAME_SIZE) {
			DbgPrint("diskctrl: Mount name size exceeded maximum allowed\n");
			status = STATUS_BUFFER_OVERFLOW;
			goto Cleanup;
		}

		mountName = (PMOUNTDEV_NAME)ExAllocatePool2(POOL_FLAG_NON_PAGED, mountNameSize, 'MNTN');
		if (!mountName) {
			DbgPrint("diskctrl: Failed to allocate memory for mount point info\n");
			status = STATUS_INSUFFICIENT_RESOURCES;
			goto Cleanup;
		}

		KeInitializeEvent(&event, NotificationEvent, FALSE);

		irp = IoBuildDeviceIoControlRequest(
			IOCTL_MOUNTDEV_QUERY_DEVICE_NAME,
			DeviceObject,
			NULL,
			0,
			mountName,
			mountNameSize,
			FALSE,
			&event,
			&ioStatusBlock
		);

		if (irp == NULL) {
			DbgPrint("diskctrl: Failed to build IRP for IOCTL_MOUNTDEV_QUERY_DEVICE_NAME\n");
			status = STATUS_INSUFFICIENT_RESOURCES;
			goto Cleanup;
		}

		status = IoCallDriver(DeviceObject, irp);

		if (status == STATUS_PENDING) {
			KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
			status = ioStatusBlock.Status;
		}

		if (status == STATUS_BUFFER_OVERFLOW || status == STATUS_BUFFER_TOO_SMALL) {
			ExFreePool(mountName);
			mountName = NULL;
			mountNameSize = (ULONG)ioStatusBlock.Information;
			continue;
		}

		if (!NT_SUCCESS(status)) {
			DbgPrint("diskctrl: Failed to query partition mount point (status = 0x%X)\n", status);
			goto Cleanup;
		}

		if (ioStatusBlock.Information < sizeof(MOUNTDEV_NAME)) {
			DbgPrint("diskctrl: Insufficient data returned from IOCTL_MOUNTDEV_QUERY_DEVICE_NAME\n");
			status = STATUS_BUFFER_TOO_SMALL;
			goto Cleanup;
		}

		if (mountName->NameLength > 0) {
			UNICODE_STRING mountPointName;
			mountPointName.Length = mountName->NameLength;
			mountPointName.MaximumLength = mountName->NameLength;
			mountPointName.Buffer = mountName->Name;
			DbgPrint("diskctrl: Partition %lu has mount point: %wZ\n", PartitionNumber, &mountPointName);
		}
		else {
			DbgPrint("diskctrl: Partition %lu does not have a mount point\n", PartitionNumber);
		}

		break;
	}

Cleanup:
	if (mountName) {
		ExFreePool(mountName);
	}
	return status;
}

/// <summary>
/// Driver entry point
/// </summary>
/// <param name="DriverObject"></param>
/// <param name="RegistryPath"></param>
/// <returns></returns>
NTSTATUS DriverEntry(
	_In_ PDRIVER_OBJECT DriverObject,
	_In_ PUNICODE_STRING RegistryPath
) {
	UNREFERENCED_PARAMETER(RegistryPath);

	NTSTATUS status = STATUS_SUCCESS;

	UNICODE_STRING deviceName;
	UNICODE_STRING symbolicLinkName;
	PDEVICE_OBJECT DeviceObject = NULL;

	RtlInitUnicodeString(&deviceName, L"\\Device\\diskctrl");

	status = IoCreateDevice(
		DriverObject,
		0,								// Device extension type
		&deviceName,
		FILE_DEVICE_UNKNOWN,			// Device Type
		FILE_DEVICE_SECURE_OPEN,		// Characteristics
		FALSE,							// Exclusive Access
		&DeviceObject
	);
	
	if (!NT_SUCCESS(status)) {
		DbgPrint("diskctrl: Failed to create device");
		return status;
	}

	// Create symbolic link to access the device from usermode
	RtlInitUnicodeString(&symbolicLinkName, L"\\DosDevices\\diskctrl");
	status = IoCreateSymbolicLink(
		&symbolicLinkName,
		&deviceName
	);

	if (!NT_SUCCESS(status)) {
		DbgPrint("diskctrl: Failed to create symbolic device");
		IoDeleteDevice(DeviceObject);
		return status;
	}

	// Functions
	DriverObject->DriverUnload = DeviceUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = DeviceCreate;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = DeviceClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceControl;

	DbgPrint("diskctrl: Driver loaded\n");

	DbgPrint("diskctrl: Started enumerating disks\n");

	EnumerateDisks();

	return STATUS_SUCCESS;
}