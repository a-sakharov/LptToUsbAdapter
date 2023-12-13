#include "UsbLptLib.h"
#include <Windows.h>
#include <strsafe.h>
#include <tchar.h>
#include <crtdbg.h>
#include "UsbWrappers.h"


#define USBLPT_VID 0x1209
#define USBLPT_PID 0x4153

struct USBLPT_t
{
    USBHANDLE usb_handles;
    USBLPT_version version;
};

typedef enum USBLPT_COMMAND_t
{
    USBLPT_GET_VERSION = 0x07,
    USBLPT_SET_MODE = 0x08,
    USBLPT_SET_REG = 0x09,
    USBLPT_GET_REG = 0x0a
} USBLPT_COMMAND;

#ifdef _DEBUG
void UsbLpt_DebugPrintUsbInfo(UsbDeviceInfo* dev)
{
    wchar_t buf[1024];
    UsbString* us;

    swprintf(buf, sizeof(buf)/sizeof(*buf), L"%s\n\tVID %.4hX\n\tPID %.4hX\n\tbcdDevice %.4hX\n", 
        dev->devicePath,
        dev->vid,
        dev->pid,
        dev->bcdDevice);
    OutputDebugStringW(buf);

    if (dev->manufacturer)
    {
        OutputDebugStringW(L"\tManufacturer:\n");
        us = dev->manufacturer;
        while (us)
        {
            swprintf(buf, sizeof(buf) / sizeof(*buf), L"\t- [%.4hX] \"%s\"\n", us->lang, us->string);
            OutputDebugStringW(buf);
            us = us->next;
        }
    }

    if (dev->productName)
    {
        OutputDebugStringW(L"\tProduct name:\n");
        us = dev->productName;
        while (us)
        {
            swprintf(buf, sizeof(buf) / sizeof(*buf), L"\t- [%.4hX] \"%s\"\n", us->lang, us->string);
            OutputDebugStringW(buf);
            us = us->next;
        }
    }

    if (dev->serial)
    {
        OutputDebugStringW(L"\tSerial:\n");
        us = dev->serial;
        while (us)
        {
            swprintf(buf, sizeof(buf) / sizeof(*buf), L"\t- [%.4hX] \"%s\"\n", us->lang, us->string);
            OutputDebugStringW(buf);
            us = us->next;
        }
    }
    OutputDebugStringW(L"\n");
}
#endif

bool UsbLpt_GetList(UsbLptDevice* devices, size_t devices_max, size_t* devices_used)
{
    UsbDeviceInfo* devlist;
    size_t devlistSize;
    size_t i;

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

            if(wcscmp(devlist[i].productName->string, L"Virtual EPP") || wcscmp(devlist[i].manufacturer->string, L"a.sakharov"))
            {
                continue;
            }

            struct USBLPT_t temp;
            if (!USBWRAP_OpenDeviceByPath(devlist[i].devicePath, &temp.usb_handles))
            {
                continue;
            }

            if (!UsbLpt_GetVersion(&temp, &devices[*devices_used].version))
            {
                USBWRAP_CloseDevice(temp.usb_handles);
                continue;
            }

            USBWRAP_CloseDevice(temp.usb_handles);
            
            wcscpy_s(devices[*devices_used].location, sizeof(devices[*devices_used].location)/sizeof(*devices[*devices_used].location), devlist[i].devicePath);
            if (devlist[i].serial)
            {
                wcscpy_s(devices[*devices_used].serial, sizeof(devices[*devices_used].serial) / sizeof(*devices[*devices_used].serial), devlist[i].serial->string);
            }
            else
            {
                devices[*devices_used].serial[0] = L'\0';
            }

            (*devices_used)++;
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

        if (selected_device == MAXSIZE_T)
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
    if (!USBWRAP_WriteVendorRequest(dev->usb_handles, USBLPT_SET_MODE, (uint16_t)mode, 0, NULL, 0))
    {
        return false;
    }

    return true;
}

bool UsbLpt_GetVersion(USBLPT dev, USBLPT_version* ver)
{
    uint16_t size = sizeof(USBLPT_version);

    memset(ver, 0, sizeof(USBLPT_version));

    if (!USBWRAP_ReadVendorRequest(dev->usb_handles, USBLPT_GET_VERSION, 0, 0, (uint8_t*)ver, &size, size - 32))
    {
        return false;
    }

    if (size != sizeof(USBLPT_version) && size != sizeof(USBLPT_version)-32)
    {
        return false;
    }

    return true;
}

bool UsbLpt_SetPort8(USBLPT dev, USBLPT_REG reg, uint8_t byte)
{
    if (!USBWRAP_WriteVendorRequest(dev->usb_handles, USBLPT_SET_REG, (uint16_t)reg, byte, NULL, 0))
    {
        return false;
    }

    return true;
}

bool UsbLpt_GetPort8(USBLPT dev, USBLPT_REG reg, uint8_t *byte)
{
    uint16_t iosz = 1;

    if (!USBWRAP_ReadVendorRequest(dev->usb_handles, USBLPT_GET_REG, (uint16_t)reg, 0, byte, &iosz, iosz))
    {
        return false;
    }

    return true;
}

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
            return MAXSIZE_T;
        }
    }

    WindowParams.SubmitBtnId = 77;

    WindowParams.MainWindow = CreateWindowEx(WS_EX_LEFT | WS_EX_TOPMOST, classname, L"Select USBLPT device", WS_BORDER | WS_SYSMENU | WS_CAPTION, CW_USEDEFAULT, CW_USEDEFAULT, 450, 120, NULL, NULL, GetModuleHandle(NULL), NULL);
    if (!WindowParams.MainWindow)
    {
        return MAXSIZE_T;
    }

    WindowParams.ListBox = CreateWindowEx(WS_EX_CLIENTEDGE, L"ComboBox", NULL, WS_CHILD | WS_VSCROLL | CBS_DROPDOWNLIST | WS_TABSTOP, 10, 10, 410, 190, WindowParams.MainWindow, NULL, GetModuleHandle(NULL), NULL);
    if (!WindowParams.ListBox)
    {
        return MAXSIZE_T;
    }

    WindowParams.SubmitButton = CreateWindowEx(WS_EX_CLIENTEDGE, L"Button", L"Submit", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 10, 40, 410, 25, WindowParams.MainWindow, (HMENU)WindowParams.SubmitBtnId, GetModuleHandle(NULL), NULL);
    if (!WindowParams.SubmitButton)
    {
        return MAXSIZE_T;
    }

    for (i = 0; i < devices_count; ++i)
    {
        wchar_t label[128];
        swprintf(label, sizeof(label)/sizeof(*label), L"[%s] v%hhu build %.4hu-%.2hhu-%.2hhu", devices[i].serial[0] == 0 ? L"NO SERIAL" : devices[i].serial, devices[i].version.revision + 1, devices[i].version.build_date.year, devices[i].version.build_date.month, devices[i].version.build_date.day);
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
        return MAXSIZE_T;
    }
}
