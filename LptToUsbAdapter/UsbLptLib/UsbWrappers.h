#ifndef USBWRAPPERS_H
#define USBWRAPPERS_H

#include <stdint.h>
#include <stdbool.h>
#include <wchar.h>


typedef struct USBHANDLE_t* USBHANDLE;

typedef struct UsbString_t UsbString;
struct UsbString_t
{
    UsbString* next;
    uint16_t lang;
    size_t length_chars;
    wchar_t* string;
};

typedef struct UsbDeviceInfo_t UsbDeviceInfo;
struct UsbDeviceInfo_t
{
    //generic usb device info
    uint16_t vid;
    uint16_t pid;
    uint16_t bcdDevice;
    UsbString* manufacturer;
    UsbString* productName;
    UsbString* serial;

    //os-specific usb device info
#ifdef _WIN32
    wchar_t* deviceIdentifier;
#else
    void *deviceIdentifier;//linusb_device*
#endif
};

#if defined __cplusplus
extern "C" {
#endif

    bool USBWRAP_Init();
    bool USBWRAP_DeInit();

    bool USBWRAP_CreateUsbDevicesInfoList(UsbDeviceInfo** devInfoList, size_t* devInfoListSize);
    bool USBWRAP_DestroyUsbDevicesInfoList(UsbDeviceInfo* devInfoList, size_t devInfoListSize);

    bool USBWRAP_OpenDeviceByPath(wchar_t* path, USBHANDLE* handle);
    bool USBWRAP_CloseDevice(USBHANDLE handle);
    
    bool USBWRAP_ReadVendorRequest(USBHANDLE dev, uint8_t request, uint16_t value, uint16_t index, uint8_t* data, size_t* size);
    bool USBWRAP_WriteVendorRequest(USBHANDLE dev, uint8_t request, uint16_t value, uint16_t index, uint8_t* data, size_t size);

    bool USBWRAP_ReadPipeDefaultInterface(USBHANDLE dev, uint8_t pipe, uint8_t* data, size_t* size);
    bool USBWRAP_WritePipeDefaultInterface(USBHANDLE dev, uint8_t pipe, uint8_t* data, size_t size);
#if defined __cplusplus
}
#endif

#endif
