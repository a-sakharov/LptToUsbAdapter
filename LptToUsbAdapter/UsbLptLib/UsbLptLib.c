#include "UsbLptLib.h"
#include "UsbWrappers.h"
#include <stdlib.h>
#include <wchar.h>
#include <string.h>

#ifdef _WIN32
#include <Windows.h>
#else

#endif

#define USBLPT_VID 0x1209
#define USBLPT_PID 0x4153

struct USBLPT_t
{
    USBHANDLE usb_handles;
    USBLPT_version version;
    bool no_special_endpoint;
};

typedef enum USBLPT_COMMAND_t
{
    USBLPT_GET_VERSION = 0x07,
    USBLPT_SET_MODE = 0x08,
    USBLPT_SET_REG = 0x09,
    USBLPT_GET_REG = 0x0a,

    USBLPT_DFU_ERASE = 0x80,
    USBLPT_DFU_LDBLOCK = 0x81,
    USBLPT_DFU_FINALIZE = 0x82,
} USBLPT_COMMAND;

#define COMMAN_PIPE_OUT 1
#define COMMAN_PIPE_IN  2 //0x82?

#ifdef _DEBUG

#ifdef _WIN32
#define OUTPUT_WSTR(str) OutputDebugStringW(str)
#else
#define OUTPUT_WSTR(str) wprintf(L"%s", str)
#endif

void UsbLpt_DebugPrintUsbInfo(UsbDeviceInfo* dev)
{
    wchar_t buf[1024];
    UsbString* us;

#ifdef _WIN32
    swprintf(buf, sizeof(buf)/sizeof(*buf), L"%s\n\tVID %.4hX\n\tPID %.4hX\n\tbcdDevice %.4hX\n", 
        dev->deviceIdentifier,
        dev->vid,
        dev->pid,
        dev->bcdDevice);
#else
    swprintf(buf, sizeof(buf)/sizeof(*buf), L"%p\n\tVID %.4hX\n\tPID %.4hX\n\tbcdDevice %.4hX\n", 
        dev->deviceIdentifier,
        dev->vid,
        dev->pid,
        dev->bcdDevice);
#endif
    OUTPUT_WSTR(buf);

    if (dev->manufacturer)
    {
        OUTPUT_WSTR(L"\tManufacturer:\n");
        us = dev->manufacturer;
        while (us)
        {
            swprintf(buf, sizeof(buf) / sizeof(*buf), L"\t- [%.4hX] \"%s\"\n", us->lang, us->string);
            OUTPUT_WSTR(buf);
            us = us->next;
        }
    }

    if (dev->productName)
    {
        OUTPUT_WSTR(L"\tProduct name:\n");
        us = dev->productName;
        while (us)
        {
            swprintf(buf, sizeof(buf) / sizeof(*buf), L"\t- [%.4hX] \"%s\"\n", us->lang, us->string);
            OUTPUT_WSTR(buf);
            us = us->next;
        }
    }

    if (dev->serial)
    {
        OUTPUT_WSTR(L"\tSerial:\n");
        us = dev->serial;
        while (us)
        {
            swprintf(buf, sizeof(buf) / sizeof(*buf), L"\t- [%.4hX] \"%s\"\n", us->lang, us->string);
            OUTPUT_WSTR(buf);
            us = us->next;
        }
    }
    OUTPUT_WSTR(L"\n");
}
#endif


static bool UsbLpt_BulkCmd(USBLPT dev, uint8_t cmd, uint8_t* data_send, size_t data_send_count, uint8_t* data_recv, size_t* data_recv_cnt)
{
    if (data_send)
    {
        if (!USBWRAP_WritePipeDefaultInterface(dev->usb_handles, COMMAN_PIPE_OUT, data_send, data_send_count))
        {
            return false;
        }
    }

    if (data_recv)
    {
        if (!USBWRAP_ReadPipeDefaultInterface(dev->usb_handles, COMMAN_PIPE_IN, data_recv, data_recv_cnt))
        {
            return false;
        }
    }

    return true;
}

static bool UsbLpt_DFU_EraseFlash(USBLPT dev)
{
    uint8_t result;
    size_t size = 1;

    if(!UsbLpt_BulkCmd(dev, USBLPT_DFU_ERASE, NULL, 0, &result, &size))
    {
        return false;
    }

    if (result != 1 || size != 1)
    {
        return false;
    }

    return true;
}

static bool UsbLpt_DFU_SendFirmware(USBLPT dev, uint32_t address, uint8_t* data, size_t data_size)
{
    uint8_t* send_fw;
    size_t send_size = 4 + data_size;

    send_fw = malloc(send_size);
    if (!send_fw)
    {
        return false;
    }

    memcpy(send_fw + 0, &address, sizeof(address));
    memcpy(send_fw + 4, data, data_size);

    uint8_t result;
    size_t size = 1;

    if (!UsbLpt_BulkCmd(dev, USBLPT_DFU_FINALIZE, send_fw, send_size, &result, &size))
    {
        free(send_fw);
        return false;
    }

    free(send_fw);
    if (result != 1 || size != 1)
    {
        return false;
    }

    return true;
}

static bool UsbLpt_DFU_Finalize(USBLPT dev, uint32_t firmware_size, uint8_t sha512[64])
{
#pragma pack(push, 1)
    struct
    {
        uint32_t firmware_size;
        uint8_t sha512[64];
    } finalize_data;
#pragma pack(pop)

    finalize_data.firmware_size = firmware_size;
    memcpy(finalize_data.sha512, sha512, sizeof(finalize_data.sha512));

    uint8_t result;
    size_t size = 1;

    if (!UsbLpt_BulkCmd(dev, USBLPT_DFU_FINALIZE, (uint8_t *) & finalize_data, sizeof(finalize_data), &result, &size))
    {
        return false;
    }

    if (result != 1 || size != 1)
    {
        return false;
    }

    return true;
}

static bool UsbLpt_FirmwareUpdate(USBLPT dev)
{
    if (!(dev->version.version& 0x80))
    {
        return false;
    }

    if ((dev->version.version & 0x7F) == 2 && (dev->version.v3.revision == 0)) //v3r0
    {
        if ((dev->version.v3.chip_id & 0xFFFFFF0F) != 0x30500508) //only CH32V305RBT6
        {
            return false;
        }

        //erase
        if (!UsbLpt_DFU_EraseFlash(dev))
        {
            return false;
        }

        uint8_t fw_block[512];

        //send firmware
        if (!UsbLpt_DFU_SendFirmware(dev, 0x00009000, fw_block, sizeof(fw_block)))
        {
            return false;
        }

        //finalize
        uint8_t sha512[64] = {
            1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,'h','e','l','l','o','!'
        };
        if (!UsbLpt_DFU_Finalize(dev, 0x12345678, sha512))
        {
            return false;
        }
    }

    return false;
}

bool UsbLpt_Init()
{
    return USBWRAP_Init();
}

bool UsbLpt_DeInit()
{
    return USBWRAP_DeInit();
}

bool UsbLpt_GetList(UsbLptDevice* devices, size_t devices_max, size_t* devices_used)
{
    UsbDeviceInfo* devlist;
    size_t devlistSize;
    size_t i;
    UsbLptDevice device_this;

    *devices_used = 0;

    if (USBWRAP_CreateUsbDevicesInfoList(&devlist, &devlistSize))
    {
        for (i = 0; i < devlistSize && *devices_used < devices_max; ++i)
        {
#ifdef _DEBUG
            UsbLpt_DebugPrintUsbInfo(&devlist[i]);
#endif
            if (devlist[i].vid != USBLPT_VID || devlist[i].pid != USBLPT_PID)
            {
                continue;
            }

            if (devlist[i].manufacturer == NULL || devlist[i].productName == NULL)
            {
                continue;
            }

            if(wcscmp(devlist[i].manufacturer->string, L"a.sakharov"))
            {
                continue;
            }

            if (wcscmp(devlist[i].productName->string, L"Virtual EPP") && wcscmp(devlist[i].productName->string, L"Virtual EPP DFU"))
            {
                continue;
            }

            wcsncpy(device_this.location, devlist[i].deviceIdentifier, sizeof(device_this.location) / sizeof(*device_this.location));
            if (devlist[i].serial)
            {
                wcsncpy(device_this.serial, devlist[i].serial->string, sizeof(device_this.serial) / sizeof(*device_this.serial));
            }
            else
            {
                device_this.serial[0] = L'\0';
            }

            USBLPT temp;
            temp = UsbLpt_Open(&device_this);
            if (!temp)
            {
                continue;
            }
            memcpy(&device_this.version, &temp->version, sizeof(USBLPT_version));

            if (!wcscmp(devlist[i].productName->string, L"Virtual EPP DFU"))
            {
                UsbLpt_FirmwareUpdate(temp);
            }
            else
            {
                memcpy(&devices[*devices_used], &device_this, sizeof(device_this));

                (*devices_used)++;
            }
            UsbLpt_Close(temp);
        }

        USBWRAP_DestroyUsbDevicesInfoList(devlist, devlistSize);
    }

    return true;
}

USBLPT UsbLpt_OpenAuto()
{
    USBLPT UsbLpt;
    UsbLptDevice usblptdevs[16];
    size_t usblptdevs_count;
    if (!UsbLpt_GetList(usblptdevs, sizeof(usblptdevs) / sizeof(*usblptdevs), &usblptdevs_count))
    {
        return NULL;
    }

    if (usblptdevs_count < 1)
    {
        return NULL;
    }

    size_t selected_device = 0;

    if (usblptdevs_count > 1)
    {
        selected_device = UsbLpt_BuildSimpleGuiDeviceSelection(usblptdevs, usblptdevs_count);

        if (selected_device == SIZE_MAX)
        {
            return NULL;
        }
    }

    UsbLpt = UsbLpt_Open(&usblptdevs[selected_device]);
    if (!UsbLpt)
    {
        return NULL;
    }

    return UsbLpt;
}

USBLPT UsbLpt_Open(UsbLptDevice *dev)
{
    USBLPT result;
    
    result = malloc(sizeof(struct USBLPT_t));
    if (!result)
    {
        return NULL;
    }

    if (!USBWRAP_OpenDeviceByPath(dev->location, &result->usb_handles))
    {
        free(result);
        return NULL;
    }

    if (!UsbLpt_GetVersion(result, &result->version))
    {
        UsbLpt_Close(result);
        return NULL;
    }

    if (result->version.version < 2)
    {
        result->no_special_endpoint = true;
    }
    else
    {
        result->no_special_endpoint = false;
    }

    return result;
}

bool UsbLpt_Close(USBLPT dev)
{
    USBWRAP_CloseDevice(dev->usb_handles);
    
    free(dev);

    return true;
}

bool UsbLpt_SetMode(USBLPT dev, USBLPT_MODE mode)
{
	if (dev->no_special_endpoint)
	{
		if (!USBWRAP_WriteVendorRequest(dev->usb_handles, USBLPT_SET_MODE, (uint16_t)mode, 0, NULL, 0))
		{
			return false;
		}
	}
	else
	{
#pragma pack(push, 1)
		struct
		{
			uint8_t mode;
		}
		set_mode_cmd =
		{
			mode,
		};
#pragma pack(pop)

		if (!UsbLpt_BulkCmd(dev, USBLPT_SET_MODE, (uint8_t*) & set_mode_cmd, sizeof(set_mode_cmd), NULL, NULL))
		{
			return false;
		}
	}
	return true;
}

bool UsbLpt_GetVersion(USBLPT dev, USBLPT_version* ver)
{
    size_t size = sizeof(USBLPT_version);

	memset(ver, 0, sizeof(USBLPT_version));
	if (dev->no_special_endpoint)
	{
		if (!USBWRAP_ReadVendorRequest(dev->usb_handles, USBLPT_GET_VERSION, 0, 0, (uint8_t*)ver, &size))
		{
			return false;
		}
	}
	else
	{
        if (!UsbLpt_BulkCmd(dev, USBLPT_GET_VERSION, NULL, 0, (uint8_t*)ver, &size))
        {
            return false;
        }
	}

	if (size != sizeof(USBLPT_version) && size != sizeof(USBLPT_version) - 32)
	{
		return false;
	}

	return true;
}

bool UsbLpt_SetPort8(USBLPT dev, USBLPT_REG reg, uint8_t byte)
{
	if (dev->no_special_endpoint)
	{
		if (!USBWRAP_WriteVendorRequest(dev->usb_handles, USBLPT_SET_REG, (uint16_t)reg, byte, NULL, 0))
		{
			return false;
		}
	}
	else
	{
#pragma pack(push, 1)
        struct
        {
            uint8_t reg;
            uint8_t byte;
        } 
        set_port_cmd =
        {
            reg,
            byte
        };
#pragma pack(pop)

		if (!UsbLpt_BulkCmd(dev, USBLPT_SET_REG, (uint8_t*) & set_port_cmd, sizeof(set_port_cmd), NULL, NULL))
		{
			return false;
		}
	}
	return true;
}

bool UsbLpt_GetPort8(USBLPT dev, USBLPT_REG reg, uint8_t *byte)
{
    size_t recv = 1;
    if (dev->no_special_endpoint)
    {
        if (!USBWRAP_ReadVendorRequest(dev->usb_handles, USBLPT_GET_REG, (uint16_t)reg, 0, byte, &recv))
        {
            return false;
        }
    }
    else
    {
        if (!UsbLpt_BulkCmd(dev, USBLPT_GET_REG, NULL, 0, byte, &recv))
        {
            return false;
        }
    }

    if (recv != 1)
    {
        return false;
    }

    return true;
}

#ifdef _WIN32
/* QUICK AND DIRTY WINDOWS GUI BUILDING */

struct WindowParams_t
{
    HWND ListBox;
    HWND MainWindow;
    HWND SubmitButton;
    LRESULT ListBoxSelection;
    WORD SubmitBtnId;
}WindowParams;
static LRESULT CALLBACK ChoiceWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    RECT rcOwner;
    RECT rcDlg;
    RECT rc;
    HWND hwndOwner;
    CREATESTRUCT* cs;

    switch (uMsg)
    {
        case WM_CREATE:
        cs = (CREATESTRUCT*)lParam;
        WindowParams.ListBoxSelection = LB_ERR;

        if ((hwndOwner = GetParent(hwnd)) == NULL)
        {
            hwndOwner = GetDesktopWindow();
        }

        GetWindowRect(hwndOwner, &rcOwner);
        GetWindowRect(hwnd, &rcDlg);
        CopyRect(&rc, &rcOwner);

        OffsetRect(&rcDlg, -rcDlg.left, -rcDlg.top);
        OffsetRect(&rc, -rc.left, -rc.top);
        OffsetRect(&rc, -rcDlg.right, -rcDlg.bottom);

        SetWindowPos(hwnd, HWND_TOP, rcOwner.left + (rc.right / 2), rcOwner.top + (rc.bottom / 2), 0, 0, SWP_NOSIZE);
        break;

        case WM_COMMAND:
        {
            if(LOWORD(wParam) == WindowParams.SubmitBtnId)
            {
                WindowParams.ListBoxSelection = SendMessage(WindowParams.ListBox, CB_GETCURSEL, 0, 0);
                DestroyWindow(WindowParams.SubmitButton);
                DestroyWindow(WindowParams.ListBox);
                DestroyWindow(WindowParams.MainWindow);
            }
        }
        break;

        case WM_DESTROY:
        PostQuitMessage((int)WindowParams.ListBoxSelection);
        return 0;

        default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

size_t UsbLpt_BuildSimpleGuiDeviceSelection(UsbLptDevice* devices, size_t devices_count)
{
    WNDCLASSEX wc = { 0 };
    MSG msg;
    size_t i;
    const wchar_t classname[] = L"ChoiceDialogClass";

    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ChoiceWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
    wc.lpszClassName = classname;

    if (!RegisterClassEx(&wc))
    {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            return SIZE_MAX;
        }
    }

    WindowParams.SubmitBtnId = 77;

    WindowParams.MainWindow = CreateWindowEx(WS_EX_LEFT | WS_EX_TOPMOST, classname, L"Select USBLPT device", WS_BORDER | WS_SYSMENU | WS_CAPTION, CW_USEDEFAULT, CW_USEDEFAULT, 450, 120, NULL, NULL, GetModuleHandle(NULL), NULL);
    if (!WindowParams.MainWindow)
    {
        return SIZE_MAX;
    }

    WindowParams.ListBox = CreateWindowEx(WS_EX_CLIENTEDGE, L"ComboBox", NULL, WS_CHILD | WS_VSCROLL | CBS_DROPDOWNLIST | WS_TABSTOP, 10, 10, 410, 190, WindowParams.MainWindow, NULL, GetModuleHandle(NULL), NULL);
    if (!WindowParams.ListBox)
    {
        return SIZE_MAX;
    }

    WindowParams.SubmitButton = CreateWindowEx(WS_EX_CLIENTEDGE, L"Button", L"Submit", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 10, 40, 410, 25, WindowParams.MainWindow, (HMENU)WindowParams.SubmitBtnId, GetModuleHandle(NULL), NULL);
    if (!WindowParams.SubmitButton)
    {
        return SIZE_MAX;
    }

    for (i = 0; i < devices_count; ++i)
    {
        wchar_t label[128];
        swprintf(label, sizeof(label)/sizeof(*label), L"[%s] v%hhu build %.4hu-%.2hhu-%.2hhu", devices[i].serial[0] == 0 ? L"NO SERIAL" : devices[i].serial, devices[i].version.version + 1, devices[i].version.build_date.year, devices[i].version.build_date.month, devices[i].version.build_date.day);
        SendMessage(WindowParams.ListBox, CB_ADDSTRING, 0, (LPARAM)label);
    }

    SendMessage(WindowParams.ListBox, CB_SETCURSEL, 0, 0);

    ShowWindow(WindowParams.SubmitButton, SW_SHOW);
    ShowWindow(WindowParams.ListBox, SW_SHOW);
    ShowWindow(WindowParams.MainWindow, SW_SHOW);

    UpdateWindow(WindowParams.MainWindow);
    UpdateWindow(WindowParams.ListBox);
    UpdateWindow(WindowParams.SubmitButton);

    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (msg.wParam != LB_ERR)
    {
        return msg.wParam;
    }
    else
    {
        return SIZE_MAX;
    }
}
#else
size_t UsbLpt_BuildSimpleGuiDeviceSelection(UsbLptDevice* devices, size_t devices_count)
{
    return SIZE_MAX;
}
#endif
