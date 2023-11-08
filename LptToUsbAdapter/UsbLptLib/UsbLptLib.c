#include "UsbLptLib.h"
#include <Windows.h>
#include <Winusb.h>
#include <SetupAPI.h>
#include <initguid.h>
#include <Usbiodef.h>
#include <strsafe.h>
#include <tchar.h>
#include <Devpkey.h>


#define USBLPT_VID 0x1209
#define USBLPT_PID 0x4153

struct USBLPT_t
{
    HANDLE devHandle;
    WINUSB_INTERFACE_HANDLE devWinUsbHandle;
    USB_INTERFACE_DESCRIPTOR iface0;
};

typedef enum USBLPT_COMMAND_t
{
    USBLPT_GET_VERSION = 0x07,
    USBLPT_SET_MODE = 0x08,
    USBLPT_SET_REG = 0x09,
    USBLPT_GET_REG = 0x0a
} USBLPT_COMMAND;

static bool DevicePathToVidPid(PTSTR path, uint16_t *vid, uint16_t *pid)
{
    PTSTR vid_p;
    PTSTR pid_p;

    vid_p = _tcsstr(path, TEXT("vid_"));
    pid_p = _tcsstr(path, TEXT("pid_"));

    if (!vid_p || _tcslen(vid_p) < 8) //no vid
    {
        return false;
    }

    if (!pid_p || _tcslen(pid_p) < 8) //no vid
    {
        return false;
    }

    vid_p += 4;
    pid_p += 4;

    _stscanf_s(vid_p, TEXT("%4hx"), vid);
    _stscanf_s(pid_p, TEXT("%4hx"), pid);

    return true;
}

static PSP_DEVICE_INTERFACE_DETAIL_DATA SetupDiGetDeviceInterfaceDetailAlloc(HDEVINFO DeviceInfoSet, PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData, PSP_DEVINFO_DATA DeviceInfoData)
{
    ULONG requiredLength = 0;
    PSP_DEVICE_INTERFACE_DETAIL_DATA device_detail;

    //get size
    if (!SetupDiGetDeviceInterfaceDetail(DeviceInfoSet, DeviceInterfaceData, NULL, 0, &requiredLength, NULL)
        && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        return NULL;
    }

    //alloc
    device_detail = malloc(requiredLength);
    if (!device_detail)
    {
        return NULL;
    }

    device_detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

    //get
    if (!SetupDiGetDeviceInterfaceDetail(DeviceInfoSet, DeviceInterfaceData, device_detail, requiredLength, &requiredLength, DeviceInfoData))
    {
        free(device_detail);
        return NULL;
    }

    return device_detail;
}

static bool FindOpenDevice(uint16_t dev_vid, uint16_t dev_pid, wchar_t *devManufacturer, wchar_t *devProductName, HANDLE *devHandle, WINUSB_INTERFACE_HANDLE *devWinUsbHandle)
{
    HDEVINFO deviceInfo;
    SP_DEVICE_INTERFACE_DATA interfaceData = { .cbSize = sizeof(SP_DEVICE_INTERFACE_DATA) };
    bool dev_found = false;
    PSP_DEVICE_INTERFACE_DETAIL_DATA device_detail = NULL;
    HANDLE device = 0;
    WINUSB_INTERFACE_HANDLE wih = 0;

    deviceInfo = SetupDiGetClassDevs(&GUID_DEVINTERFACE_USB_DEVICE, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfo == INVALID_HANDLE_VALUE) 
    {
        return false;
    }

    DWORD dev_index = 0;
    while (SetupDiEnumDeviceInterfaces(deviceInfo, NULL, &GUID_DEVINTERFACE_USB_DEVICE, dev_index++, &interfaceData))
    {
        if (device_detail)
        {
            free(device_detail);
            device_detail = NULL;
        }

        if (device)
        {
            CloseHandle(device);
            device = 0;
        }

        if (wih)
        {
            WinUsb_Free(wih);
            wih = 0;
        }

        device_detail = SetupDiGetDeviceInterfaceDetailAlloc(deviceInfo, &interfaceData, NULL);
        if (!device_detail)
        {
            continue;
        }

        uint16_t vid;
        uint16_t pid;

        if (!DevicePathToVidPid(device_detail->DevicePath, &vid, &pid) || vid != dev_vid || pid != dev_pid)
        {
            continue;
        }

        device = CreateFile(device_detail->DevicePath, GENERIC_WRITE | GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
        if (device == INVALID_HANDLE_VALUE)
        {
            device = 0;
            continue;
        }

        if(!WinUsb_Initialize(device, &wih))
        {
            continue;
        }

        LONG transferred;
        if (devManufacturer)
        {
            wchar_t device_manufacturer[1024];
            if (!WinUsb_GetDescriptor(wih, USB_STRING_DESCRIPTOR_TYPE, 0x01, 0, (PUCHAR)device_manufacturer, sizeof(device_manufacturer), &transferred))
            {
                continue;
            }

            if ((transferred >= sizeof(device_manufacturer)) || (transferred < 0))
            {
                continue;
            }
            device_manufacturer[transferred / 2] = L'\0';
            memmove(device_manufacturer, device_manufacturer + 1, transferred);

            if (wcscmp(device_manufacturer, devManufacturer))
            {
                continue;
            }
        }

        if (devProductName)
        {
            wchar_t device_product_name[1024];
            if (!WinUsb_GetDescriptor(wih, USB_STRING_DESCRIPTOR_TYPE, 0x02, 0, (PUCHAR)device_product_name, sizeof(device_product_name), &transferred) || (transferred / 2 >= sizeof(device_product_name)) || (transferred < 0))
            {
                continue;
            }

            if ((transferred >= sizeof(device_product_name)) || (transferred < 0))
            {
                continue;
            }
            device_product_name[transferred / 2] = L'\0';
            memmove(device_product_name, device_product_name + 1, transferred - 1);

            if (wcscmp(device_product_name, devProductName))
            {
                continue;
            }
        }

        dev_found = true;
        break;

    }

    if (device_detail)
    {
        free(device_detail);
    }

    if (dev_found)
    {
        *devHandle = device;
        *devWinUsbHandle = wih;
    }
    else
    {
        if (device)
        {
            CloseHandle(device);
        }

        if (wih)
        {
            WinUsb_Free(wih);
        }
    }

    SetupDiDestroyDeviceInfoList(deviceInfo);

    return dev_found;
}

#define VENDOR_REQUEST 0x40
static bool Write(USBLPT dev, USBLPT_COMMAND request, uint16_t value, uint16_t index, uint8_t *data, uint16_t size)
{
    LONG transferred;
    WINUSB_SETUP_PACKET packet =
    {
        .RequestType = VENDOR_REQUEST | 0x00,
        .Request = request,
        .Value = value,
        .Index = index,
        .Length = size,
    };

    if (!WinUsb_ControlTransfer(dev->devWinUsbHandle, packet, data, size, &transferred, NULL))
    {
        return false;
    }

    if (transferred != size)
    {
        return false;
    }

    return true;
}

static bool Read(USBLPT dev, USBLPT_COMMAND request, uint16_t value, uint16_t index, uint8_t* data, uint16_t *size)
{
    LONG transferred;
    WINUSB_SETUP_PACKET packet =
    {
        .RequestType = VENDOR_REQUEST | 0x80,
        .Request = request,
        .Value = value,
        .Index = index,
        .Length = *size,
    };

    if (!WinUsb_ControlTransfer(dev->devWinUsbHandle, packet, data, *size, &transferred, NULL))
    {
        return false;
    }

    if (transferred != *size)
    {
        return false;//maybe not?
    }

    return true;
}

USBLPT UsbLpt_Open()
{
    USBLPT result = NULL;

    HANDLE devHandle;
    WINUSB_INTERFACE_HANDLE devWinUsbHandle;

    if (FindOpenDevice(USBLPT_VID, USBLPT_PID, L"a.sakharov", L"Virtual EPP", &devHandle, &devWinUsbHandle))
    {
        result = malloc(sizeof(struct USBLPT_t));
        if(!result)
        {
            WinUsb_Free(devWinUsbHandle);
            CloseHandle(devHandle);
            return false;
        }

        result->devHandle = devHandle;
        result->devWinUsbHandle = devWinUsbHandle;
#if 0
        if (!WinUsb_QueryInterfaceSettings(result->devWinUsbHandle, 0, &result->iface0))
        {
            UsbLpt_Close(result);
            return false;
        }
#endif
    }

    return result;
}

bool UsbLpt_Close(USBLPT dev)
{
    WinUsb_Free(dev->devWinUsbHandle);
    CloseHandle(dev->devHandle);

    free(dev);

    return true;
}

bool UsbLpt_SetMode(USBLPT dev, USBLPT_MODE mode)
{
    if (!Write(dev, USBLPT_SET_MODE, (uint16_t)mode, 0, NULL, 0))
    {
        return false;
    }

    return true;
}

bool UsbLpt_GetVersion(USBLPT dev, USBLPT_version* ver)
{
    uint16_t size = sizeof(USBLPT_version);

    if (!Read(dev, USBLPT_GET_VERSION, 0, 0, (uint8_t*)ver, &size))
    {
        return false;
    }

    return true;
}

bool UsbLpt_SetPort8(USBLPT dev, USBLPT_REG reg, uint8_t byte)
{
    if (!Write(dev, USBLPT_SET_REG, (uint16_t)reg, byte, NULL, 0))
    {
        return false;
    }

    return true;
}

bool UsbLpt_GetPort8(USBLPT dev, USBLPT_REG reg, uint8_t *byte)
{
    uint16_t iosz = 1;

    if (!Read(dev, USBLPT_GET_REG, (uint16_t)reg, 0, byte, &iosz))
    {
        return false;
    }

    return true;
}
