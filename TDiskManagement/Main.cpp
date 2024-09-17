#include <windows.h>
#include <iostream>
#include <winioctl.h>

#define IOCTL_GET_READY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

int main() {
    HANDLE hDevice = CreateFile(
        L"\\\\.\\diskctrl",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hDevice == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to open device. Error: " << GetLastError() << std::endl;
        return 1;
    }

    char buffer[6];
    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_GET_READY,
        NULL,
        0,
        buffer,
        sizeof(buffer),
        &bytesReturned,
        NULL
    );

    if (result) {
        std::cout << "Driver response: " << buffer << std::endl;
    }
    else {
        std::cerr << "Failed to get ready status. Error: " << GetLastError() << std::endl;
    }

    CloseHandle(hDevice);
    return 0;
}