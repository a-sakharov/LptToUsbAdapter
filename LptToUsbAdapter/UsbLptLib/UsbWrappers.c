#include <Windows.h>
#include <Usbioctl.h>
#include <cfgmgr32.h>
#include <wchar.h>
#include <tchar.h>
#include <stdbool.h>
#include <Winusb.h>
#include <SetupAPI.h>
#include <initguid.h>
#include <Usbiodef.h>
#include <Devpkey.h>
#include <usbspec.h>
#include "UsbWrappers.h"

/* f18a0e88-c30c-11d0-8815-00a0c906bed8 */
DEFINE_GUID(GUID_DEVINTERFACE_USB_HUB, 0xf18a0e88, 0xc30c, 0x11d0, 0x88, 0x15, 0x00, \
    0xa0, 0xc9, 0x06, 0xbe, 0xd8);

/* A5DCBF10-6530-11D2-901F-00C04FB951ED */
DEFINE_GUID(GUID_DEVINTERFACE_USB_DEVICE, 0xA5DCBF10L, 0x6530, 0x11D2, 0x90, 0x1F, 0x00, \
    0xC0, 0x4F, 0xB9, 0x51, 0xED);

#define USB_VENDOR_REQUEST 0x40
#define USB_PACKET_IN 0x80
#define USB_PACKET_OUT 0x00

struct USBHANDLE_t
{
    HANDLE devHandle;
    WINUSB_INTERFACE_HANDLE devWinUsbHandle;
};

static void FreeUsbString(UsbString *str)
{
    UsbString* next;

    while (str)
    {
        next = str->next;
        free(str->string);
        free(str);
        str = next;
    }
}
static bool DevicePathToVidPid(PTSTR path, uint16_t* vid, uint16_t* pid)
{
    PTSTR vid_p;
    PTSTR pid_p;

    vid_p = _tcsstr(path, TEXT("vid_"));
    pid_p = _tcsstr(path, TEXT("pid_"));

    if (!vid_p || _tcslen(vid_p) < 8) //no vid
    {
        return false;
    }

    if (!pid_p || _tcslen(pid_p) < 8) //no pid
    {
        return false;
    }

    vid_p += 4;
    pid_p += 4;

    _stscanf_s(vid_p, TEXT("%4hx"), vid);
    _stscanf_s(pid_p, TEXT("%4hx"), pid);

    return true;
}
static bool DeviceLocationToPortHub(PTSTR path, uint16_t* port, uint16_t* hub)
{
    PTSTR port_p;
    PTSTR hub_p;

    port_p = _tcsstr(path, TEXT("Port_#"));
    hub_p = _tcsstr(path, TEXT("Hub_#"));

    if (!port_p || _tcslen(port_p) < 10) //no port
    {
        return false;
    }

    if (!hub_p || _tcslen(hub_p) < 9) //no hub
    {
        return false;
    }

    port_p += 6;
    hub_p += 5;

    _stscanf_s(port_p, TEXT("%4hu"), port);
    _stscanf_s(hub_p, TEXT("%4hu"), hub);

    return true;
}
static size_t DeviceIdToDevicePathBufferLength(PTSTR id)
{
    return 4 + _tcslen(id) + 1 + 38 + 1;
}
static bool DeviceIdToDevicePath(PTSTR id, PTSTR path, size_t path_len, const GUID *device_guid)
{
    int r;

    r = _stprintf_s(path, path_len, TEXT("\\\\?\\%s#{%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX}"), 
        id,
        device_guid->Data1, device_guid->Data2, device_guid->Data3,
        device_guid->Data4[0], device_guid->Data4[1], device_guid->Data4[2], device_guid->Data4[3],
        device_guid->Data4[4], device_guid->Data4[5], device_guid->Data4[6], device_guid->Data4[7]);

    if (r == -1)
    {
        return false;
    }

    size_t i;
    for (i = 4; i < path_len; ++i)
    {
        if (path[i] == TEXT('\\'))
        {
            path[i] = TEXT('#');
        }
    }

    return true;
}
static bool DeviceGetPortHub(HDEVINFO deviceInfo, PSP_DEVINFO_DATA devinfo, uint16_t* port, uint16_t* hub)
{
    DWORD requedBuffer = 0;
    if (SetupDiGetDeviceRegistryProperty(deviceInfo, devinfo, SPDRP_LOCATION_INFORMATION, NULL, NULL, 0, &requedBuffer) != TRUE &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        return false;
    }

    PTSTR devLocation;
    DWORD allocatedBuffer = requedBuffer;
    devLocation = malloc(allocatedBuffer);

    if(!devLocation)
    {
        return false;
    }

    if (SetupDiGetDeviceRegistryProperty(deviceInfo, devinfo, SPDRP_LOCATION_INFORMATION, NULL, (PBYTE)devLocation, allocatedBuffer, &requedBuffer) != TRUE)
    {
        free(devLocation);
        return false;
    }

    if (!DeviceLocationToPortHub(devLocation, port, hub))
    {
        free(devLocation);
        return false;
    }

    free(devLocation);

    return true;
}
static bool GetUsbDeviceHubHandle(PSP_DEVINFO_DATA devinfo, HANDLE *handle)
{
    DEVINST parent;
    if (CM_Get_Parent(&parent, devinfo->DevInst, 0) != CR_SUCCESS)
    {
        return false;
    }

    ULONG devid_len;
    if (CM_Get_Device_ID_Size(&devid_len, parent, 0) != CR_SUCCESS)
    {
        return false;
    }
    devid_len++;

    PTSTR parent_id = malloc(sizeof(TCHAR) * devid_len);
    if (!parent_id)
    {
        return false;
    }

    if (CM_Get_Device_ID(parent, parent_id, devid_len, 0) != CR_SUCCESS)
    {
        free(parent_id);
        return false;
    }

    PTSTR parent_path;
    size_t parent_path_len = DeviceIdToDevicePathBufferLength(parent_id);
    parent_path = malloc(parent_path_len * sizeof(TCHAR));
    if (!parent_path)
    {
        free(parent_id);
        return false;
    }

    if (!DeviceIdToDevicePath(parent_id, parent_path, parent_path_len, &GUID_DEVINTERFACE_USB_HUB))
    {
        free(parent_id);
        free(parent_path);
        return false;
    }

    free(parent_id);

    *handle = CreateFile(parent_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (*handle == INVALID_HANDLE_VALUE)
    {
        free(parent_path);
        return false;
    }

    free(parent_path);

    return true;
}
static PUSB_DESCRIPTOR_REQUEST GetConfigDescriptor(HANDLE hHubDevice, ULONG ConnectionIndex, UCHAR DescriptorIndex)
{
    BOOL    success = 0;
    ULONG   nBytes = 0;
    ULONG   nBytesReturned = 0;

    UCHAR   configDescReqBuf[sizeof(USB_DESCRIPTOR_REQUEST) + sizeof(USB_CONFIGURATION_DESCRIPTOR)];

    PUSB_DESCRIPTOR_REQUEST         configDescReq = NULL;
    PUSB_CONFIGURATION_DESCRIPTOR   configDesc = NULL;


    // Request the Configuration Descriptor the first time using our
    // local buffer, which is just big enough for the Cofiguration
    // Descriptor itself.
    //
    nBytes = sizeof(configDescReqBuf);

    configDescReq = (PUSB_DESCRIPTOR_REQUEST)configDescReqBuf;
    configDesc = (PUSB_CONFIGURATION_DESCRIPTOR)(configDescReq + 1);

    // Zero fill the entire request structure
    //
    memset(configDescReq, 0, nBytes);

    // Indicate the port from which the descriptor will be requested
    //
    configDescReq->ConnectionIndex = ConnectionIndex;

    //
    // USBHUB uses URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE to process this
    // IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION request.
    //
    // USBD will automatically initialize these fields:
    //     bmRequest = 0x80
    //     bRequest  = 0x06
    //
    // We must inititialize these fields:
    //     wValue    = Descriptor Type (high) and Descriptor Index (low byte)
    //     wIndex    = Zero (or Language ID for String Descriptors)
    //     wLength   = Length of descriptor buffer
    //
    configDescReq->SetupPacket.wValue = (USB_CONFIGURATION_DESCRIPTOR_TYPE << 8) | DescriptorIndex;

    configDescReq->SetupPacket.wLength = (USHORT)(nBytes - sizeof(USB_DESCRIPTOR_REQUEST));

    // Now issue the get descriptor request.
    //
    success = DeviceIoControl(hHubDevice,
        IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,
        configDescReq,
        nBytes,
        configDescReq,
        nBytes,
        &nBytesReturned,
        NULL);

    if (!success)
    {
        return NULL;
    }

    if (nBytes != nBytesReturned)
    {
        return NULL;
    }

    if (configDesc->wTotalLength < sizeof(USB_CONFIGURATION_DESCRIPTOR))
    {
        return NULL;
    }

    // Now request the entire Configuration Descriptor using a dynamically
    // allocated buffer which is sized big enough to hold the entire descriptor
    //
    nBytes = sizeof(USB_DESCRIPTOR_REQUEST) + configDesc->wTotalLength;

    configDescReq = (PUSB_DESCRIPTOR_REQUEST)malloc(nBytes);

    if (configDescReq == NULL)
    {
        return NULL;
    }

    configDesc = (PUSB_CONFIGURATION_DESCRIPTOR)(configDescReq + 1);

    // Indicate the port from which the descriptor will be requested
    //
    configDescReq->ConnectionIndex = ConnectionIndex;

    //
    // USBHUB uses URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE to process this
    // IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION request.
    //
    // USBD will automatically initialize these fields:
    //     bmRequest = 0x80
    //     bRequest  = 0x06
    //
    // We must inititialize these fields:
    //     wValue    = Descriptor Type (high) and Descriptor Index (low byte)
    //     wIndex    = Zero (or Language ID for String Descriptors)
    //     wLength   = Length of descriptor buffer
    //
    configDescReq->SetupPacket.wValue = (USB_CONFIGURATION_DESCRIPTOR_TYPE << 8)
        | DescriptorIndex;

    configDescReq->SetupPacket.wLength = (USHORT)(nBytes - sizeof(USB_DESCRIPTOR_REQUEST));

    // Now issue the get descriptor request.
    //

    success = DeviceIoControl(hHubDevice,
        IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,
        configDescReq,
        nBytes,
        configDescReq,
        nBytes,
        &nBytesReturned,
        NULL);

    if (!success)
    {
        free(configDescReq);
        return NULL;
    }

    if (nBytes != nBytesReturned)
    {
        free(configDescReq);
        return NULL;
    }

    if (configDesc->wTotalLength != (nBytes - sizeof(USB_DESCRIPTOR_REQUEST)))
    {
        free(configDescReq);
        return NULL;
    }

    return configDescReq;
}
static UsbString *GetStringDescriptor(HANDLE hHubDevice, ULONG ConnectionIndex, uint8_t DescriptorIndex, uint16_t LanguageID)
{
    BOOL    success = 0;
    ULONG   nBytes = 0;
    ULONG   nBytesReturned = 0;

    UCHAR   stringDescReqBuf[sizeof(USB_DESCRIPTOR_REQUEST) + MAXIMUM_USB_STRING_LENGTH];

    PUSB_DESCRIPTOR_REQUEST stringDescReq = NULL;
    PUSB_STRING_DESCRIPTOR  stringDesc = NULL;

    nBytes = sizeof(stringDescReqBuf);

    stringDescReq = (PUSB_DESCRIPTOR_REQUEST)stringDescReqBuf;
    stringDesc = (PUSB_STRING_DESCRIPTOR)(stringDescReq + 1);

    // Zero fill the entire request structure
    //
    memset(stringDescReq, 0, nBytes);

    // Indicate the port from which the descriptor will be requested
    //
    stringDescReq->ConnectionIndex = ConnectionIndex;

    //
    // USBHUB uses URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE to process this
    // IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION request.
    //
    // USBD will automatically initialize these fields:
    //     bmRequest = 0x80
    //     bRequest  = 0x06
    //
    // We must inititialize these fields:
    //     wValue    = Descriptor Type (high) and Descriptor Index (low byte)
    //     wIndex    = Zero (or Language ID for String Descriptors)
    //     wLength   = Length of descriptor buffer
    //
    stringDescReq->SetupPacket.wValue = (USB_STRING_DESCRIPTOR_TYPE << 8) | DescriptorIndex;

    stringDescReq->SetupPacket.wIndex = LanguageID;

    stringDescReq->SetupPacket.wLength = (USHORT)(nBytes - sizeof(USB_DESCRIPTOR_REQUEST));

    // Now issue the get descriptor request.
    //
    success = DeviceIoControl(hHubDevice,
        IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,
        stringDescReq,
        nBytes,
        stringDescReq,
        nBytes,
        &nBytesReturned,
        NULL);

    //
    // Do some sanity checks on the return from the get descriptor request.
    //

    if (!success)
    {
        return NULL;
    }

    if (nBytesReturned < 2)
    {
        return NULL;
    }

    if (stringDesc->bDescriptorType != USB_STRING_DESCRIPTOR_TYPE)
    {
        return NULL;
    }

    if (stringDesc->bLength != nBytesReturned - sizeof(USB_DESCRIPTOR_REQUEST))
    {
        return NULL;
    }

    if (stringDesc->bLength % 2 != 0)
    {
        return NULL;
    }

    //
    // Looks good, allocate some (zero filled) space for the string descriptor
    // node and copy the string descriptor to it.
    //

    UsbString *result = malloc(sizeof(UsbString));
    if (result == NULL)
    {
        return NULL;
    }

    result->next = NULL;
    result->lang = LanguageID;
    result->length_chars = (stringDesc->bLength - 2) / 2;
    result->string = malloc(stringDesc->bLength);
    if (result->string == NULL)
    {
        free(result);
        return NULL;
    }

    memcpy(result->string, stringDesc->bString, stringDesc->bLength);

    return result;
}
static UsbString *GetStringDescriptors(HANDLE hHubDevice, ULONG ConnectionIndex, uint8_t DescriptorIndex, uint16_t* LanguageIDs, size_t LanguageIDsCount)
{
    size_t i;
    UsbString* result = NULL;
    UsbString* current;

    for (i = 0; i < LanguageIDsCount; ++i)
    {
        current = GetStringDescriptor(hHubDevice, ConnectionIndex, DescriptorIndex, LanguageIDs[i]);
        if (current)
        {
            if (result)
            {
                result->next = current;
            }
            else
            {
                result = current;
            }
        }
    }

    return result;
}
static bool GatherUsbDeviceInfo(HANDLE hHubDevice, ULONG ConnectionIndex, UsbDeviceInfo *info)
{
    PUSB_NODE_CONNECTION_INFORMATION    connectionInfo = NULL;
    BOOL success = 0;
    ULONG nBytes = 0;

    nBytes = sizeof(USB_NODE_CONNECTION_INFORMATION) + sizeof(USB_PIPE_INFO) * 30;

    connectionInfo = (PUSB_NODE_CONNECTION_INFORMATION)malloc(nBytes);

    if (connectionInfo == NULL)
    {
        return false;
    }

    connectionInfo->ConnectionIndex = ConnectionIndex;
    
    success = DeviceIoControl(hHubDevice,
        IOCTL_USB_GET_NODE_CONNECTION_INFORMATION,
        connectionInfo,
        nBytes,
        connectionInfo,
        nBytes,
        &nBytes,
        NULL);

    if (!success)
    {
        free(connectionInfo);
        return false;
    }

    info->bcdDevice = connectionInfo->DeviceDescriptor.bcdDevice;
    info->pid = connectionInfo->DeviceDescriptor.idProduct;
    info->vid = connectionInfo->DeviceDescriptor.idVendor;
    info->manufacturer = NULL;
    info->serial = NULL;
    info->productName = NULL;
    info->devicePath = NULL;
    UsbString *supportedLanguagesString = NULL;
    supportedLanguagesString = GetStringDescriptor(hHubDevice, ConnectionIndex, 0, 0);
    if (!supportedLanguagesString)
    {
        free(connectionInfo);
        return true;
    }

    info->manufacturer = connectionInfo->DeviceDescriptor.iManufacturer ? GetStringDescriptors(hHubDevice, ConnectionIndex, connectionInfo->DeviceDescriptor.iManufacturer, supportedLanguagesString->string, supportedLanguagesString->length_chars) : NULL;
    info->serial = connectionInfo->DeviceDescriptor.iSerialNumber ? GetStringDescriptors(hHubDevice, ConnectionIndex, connectionInfo->DeviceDescriptor.iSerialNumber, supportedLanguagesString->string, supportedLanguagesString->length_chars) : NULL;
    info->productName = connectionInfo->DeviceDescriptor.iProduct ? GetStringDescriptors(hHubDevice, ConnectionIndex, connectionInfo->DeviceDescriptor.iProduct, supportedLanguagesString->string, supportedLanguagesString->length_chars) : NULL;
    
    FreeUsbString(supportedLanguagesString);
    free(connectionInfo);

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

/* PUBLIC FUNCTIONS */

bool USBWRAP_CreateUsbDevicesInfoList(UsbDeviceInfo** devInfoList, size_t* devInfoListSize)
{
    HDEVINFO deviceInfo;
    SP_DEVINFO_DATA devInfoData = { .cbSize = sizeof(SP_DEVINFO_DATA) };

    deviceInfo = SetupDiGetClassDevs(&GUID_DEVINTERFACE_USB_DEVICE, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
    if (deviceInfo == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    UsbDeviceInfo* result = NULL;
    size_t length = 0;

    DWORD dev_index = 0;
    while (SetupDiEnumDeviceInfo(deviceInfo, dev_index++, &devInfoData))
    {
        uint16_t port;
        uint16_t hub;
        if(!DeviceGetPortHub(deviceInfo, &devInfoData, &port, &hub))
        {
            continue;
        }

        HANDLE hub_handle;
        if (!GetUsbDeviceHubHandle(&devInfoData, &hub_handle))
        {
            continue;
        }
        
        SP_DEVICE_INTERFACE_DATA interfaceData = { .cbSize = sizeof(SP_DEVICE_INTERFACE_DATA) };
        if (!SetupDiEnumDeviceInterfaces(deviceInfo, NULL, &GUID_DEVINTERFACE_USB_DEVICE, dev_index-1, &interfaceData))
        {
            continue;
        }

        PSP_DEVICE_INTERFACE_DETAIL_DATA device_detail;
        device_detail = SetupDiGetDeviceInterfaceDetailAlloc(deviceInfo, &interfaceData, NULL);
        if (!device_detail)
        {
            continue;
        }
        
        UsbDeviceInfo info;
        if (GatherUsbDeviceInfo(hub_handle, port, &info))
        {
            length++;
            void* temp = result;
            temp = realloc(result, length * sizeof(UsbDeviceInfo));
            if (temp)
            {
                result = temp;
                memcpy(&result[length - 1], &info, sizeof(UsbDeviceInfo));
                result[length - 1].devicePath = _tcsdup(device_detail->DevicePath);
            }
            else
            {
                length--;
            }
        }

        free(device_detail);

        CloseHandle(hub_handle);
    }

    SetupDiDestroyDeviceInfoList(deviceInfo);

    *devInfoListSize = length;
    *devInfoList = result;

    return true;
}
bool USBWRAP_DestroyUsbDevicesInfoList(UsbDeviceInfo* devInfoList, size_t devInfoListSize)
{
    size_t i;

    for (i = 0; i < devInfoListSize; ++i)
    {
        FreeUsbString(devInfoList[i].manufacturer);
        FreeUsbString(devInfoList[i].productName);
        FreeUsbString(devInfoList[i].serial);
        free(devInfoList[i].devicePath);
    }

    free(devInfoList);

    return true;
}
bool USBWRAP_OpenDeviceByPath(wchar_t *path, USBHANDLE *handle)
{
    HANDLE device;
    device = CreateFile(path, GENERIC_WRITE | GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (device == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    WINUSB_INTERFACE_HANDLE wih;
    if (!WinUsb_Initialize(device, &wih))
    {
        CloseHandle(device);
        return false;
    }

    *handle = malloc(sizeof(struct USBHANDLE_t));
    if (!*handle)
    {
        WinUsb_Free(wih);
        CloseHandle(device);
        return false;
    }

    (*handle)->devHandle = device;
    (*handle)->devWinUsbHandle = wih;

    return true;
}
bool USBWRAP_CloseDevice(USBHANDLE handle)
{
    WinUsb_Free(handle->devWinUsbHandle);
    CloseHandle(handle->devHandle);

    free(handle);

    return true;
}
bool USBWRAP_WriteVendorRequest(USBHANDLE dev, uint8_t request, uint16_t value, uint16_t index, uint8_t* data, uint16_t size)
{
    LONG transferred;
    WINUSB_SETUP_PACKET packet =
    {
        .RequestType = USB_VENDOR_REQUEST | USB_PACKET_OUT,
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
bool USBWRAP_ReadVendorRequest(USBHANDLE dev, uint8_t request, uint16_t value, uint16_t index, uint8_t* data, uint16_t* size)
{
    LONG transferred;
    WINUSB_SETUP_PACKET packet =
    {
        .RequestType = USB_VENDOR_REQUEST | USB_PACKET_IN,
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