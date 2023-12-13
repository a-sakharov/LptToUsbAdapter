#include <Windows.h>
#include <detours.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdnoreturn.h>
#include "UsbLptLib.h"

typedef HANDLE(WINAPI* CreateFileA_t)(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
static CreateFileA_t TrueCreateFileA;

typedef BOOL (WINAPI* DeviceIoControl_t)(HANDLE hDevice, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped);
static DeviceIoControl_t TrueDeviceIoControl;

typedef LSTATUS (APIENTRY* RegOpenKeyExA_t)(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult);
static RegOpenKeyExA_t TrueRegOpenKeyExA;

typedef LSTATUS (APIENTRY* RegEnumValueA_t)(HKEY hKey, DWORD dwIndex, LPSTR lpValueName, LPDWORD lpcchValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);
static RegEnumValueA_t TrueRegEnumValueA;

typedef LSTATUS (APIENTRY* RegQueryValueExA_t)( HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);
static RegQueryValueExA_t TrueRegQueryValueExA;

static HANDLE WinIoHookHandle;

HANDLE WINAPI CreateFileA_filter(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    bool winIoAccess = false;
    if (lpFileName)
    {
        if (!strcmp(lpFileName, "\\\\.\\WINIO"))
        {
            WinIoHookHandle = TrueCreateFileA("winio.hook", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, NULL);
            return WinIoHookHandle;
        }
    }
    
    return TrueCreateFileA(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

BOOL WINAPI DeviceIoControl_filter(HANDLE hDevice, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped)
{
    if (WinIoHookHandle == hDevice)
    {
        return TRUE;
    }

    return TrueDeviceIoControl(hDevice, dwIoControlCode, lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize, lpBytesReturned, lpOverlapped);
}

#define LPT0_KEY "\\device\\parallel0"
#define LPT0_VALUE "LPT0"
HKEY ParallelPortsKey;
LSTATUS APIENTRY RegEnumValueA_filter(HKEY hKey, DWORD dwIndex, LPSTR lpValueName, LPDWORD lpcchValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
{
    if (hKey == ParallelPortsKey)
    {
        if (dwIndex > 0)
        {
            return ERROR_NO_MORE_ITEMS;
        }

        //fill item 0
        if (lpValueName)
        {
            if (*lpcchValueName >= sizeof(LPT0_KEY))
            {
                memcpy(lpValueName, LPT0_KEY, sizeof(LPT0_KEY));
            }
            *lpcchValueName = sizeof(LPT0_KEY) - 1;
        }

		*lpType = REG_SZ;

		if (lpData)
		{
			if (*lpcbData >= sizeof(LPT0_VALUE))
			{
				memcpy(lpData, LPT0_VALUE, sizeof(LPT0_VALUE));
			}
			*lpcbData = sizeof(LPT0_VALUE);
		}
		return ERROR_SUCCESS;
	}

    return TrueRegEnumValueA(hKey, dwIndex, lpValueName, lpcchValueName, lpReserved, lpType, lpData, lpcbData);
}

LSTATUS APIENTRY RegOpenKeyExA_filter(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult)
{
    LSTATUS result;

    if (hKey == HKEY_LOCAL_MACHINE && strcmp(lpSubKey, "HARDWARE\\DEVICEMAP\\PARALLEL PORTS") == 0 && ulOptions == 0 && samDesired == (KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE))
    {
        result = TrueRegOpenKeyExA(hKey, "HARDWARE\\DEVICEMAP", ulOptions, samDesired, phkResult);
        ParallelPortsKey = *phkResult;
        return result;
    }

    return TrueRegOpenKeyExA(hKey, lpSubKey, ulOptions, samDesired, phkResult);
}

LSTATUS APIENTRY RegQueryValueExA_filter(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
{
    if (hKey == ParallelPortsKey && strcmp(lpValueName, LPT0_KEY) == 0)
    {
        if (lpType)
        {
            *lpType = REG_SZ;
        }

        if (lpData)
        {
            if (*lpcbData >= sizeof(LPT0_VALUE))
            {
                memcpy(lpData, LPT0_VALUE, sizeof(LPT0_VALUE));
            }
            *lpcbData = sizeof(LPT0_VALUE);
        }

        return ERROR_SUCCESS;
    }

    return TrueRegQueryValueExA(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (DetourIsHelperProcess()) 
    {
        return TRUE;
    }

    // Perform actions based on the reason for calling.
    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
            DetourRestoreAfterWith();

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            TrueCreateFileA = DetourFindFunction("Kernel32.dll", "CreateFileA");
            DetourAttach(&(PVOID)TrueCreateFileA, CreateFileA_filter);

            TrueDeviceIoControl = DetourFindFunction("Kernel32.dll", "DeviceIoControl");
            DetourAttach(&(PVOID)TrueDeviceIoControl, DeviceIoControl_filter);

			TrueRegOpenKeyExA = DetourFindFunction("Advapi32.dll", "RegOpenKeyExA");
            DetourAttach(&(PVOID)TrueRegOpenKeyExA, RegOpenKeyExA_filter);

			TrueRegEnumValueA = DetourFindFunction("Advapi32.dll", "RegEnumValueA");
            DetourAttach(&(PVOID)TrueRegEnumValueA, RegEnumValueA_filter);

			TrueRegQueryValueExA = DetourFindFunction("Advapi32.dll", "RegQueryValueExA");
            DetourAttach(&(PVOID)TrueRegQueryValueExA, RegQueryValueExA_filter);

            DetourTransactionCommit();
            
            break;

        case DLL_THREAD_ATTACH:
            // Do thread-specific initialization.
            break;

        case DLL_THREAD_DETACH:
            // Do thread-specific cleanup.
            break;

        case DLL_PROCESS_DETACH:
            
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourDetach(&(PVOID)TrueCreateFileA, CreateFileA_filter);
            DetourDetach(&(PVOID)TrueDeviceIoControl, DeviceIoControl_filter);
            DetourDetach(&(PVOID)TrueRegOpenKeyExA, RegOpenKeyExA_filter);
            DetourDetach(&(PVOID)TrueRegEnumValueA, RegEnumValueA_filter);
            DetourDetach(&(PVOID)TrueRegQueryValueExA, RegQueryValueExA_filter);
            DetourTransactionCommit();
            
            // Perform any necessary cleanup.
            break;
    }
    return TRUE;  // Successful DLL_PROCESS_ATTACH.
}

_declspec(dllexport) void dummy()
{

}

noreturn void Die(wchar_t* reason, bool isSystemFail)
{
    DWORD error;

    if (isSystemFail)
    {
        error = GetLastError();

        wchar_t* errorDescription = NULL;
        if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&errorDescription, 0, 0) != 0)
        {
            MessageBox(NULL, errorDescription, reason, MB_OK | MB_ICONERROR);
            HeapFree(GetProcessHeap(), 0, errorDescription);
        }
        else
        {
            MessageBox(NULL, reason, L"Can not get error details", MB_OK | MB_ICONERROR);
        }
    }
    else
    {
        MessageBox(NULL, reason, L"Internal error", MB_OK | MB_ICONERROR);
    }

    //exit(-1);
    ExitProcess(-1);
}


USBLPT UsbLpt;
uint16_t bypassPorts[] = {
        0x77A
};

void OpenUsbLpt()
{
    if (UsbLpt)
    {
        return;
    }

    UsbLpt = UsbLpt_OpenAuto();
    if (!UsbLpt)
    {
        Die(L"Can't open USBLPT", false);
    }

    if (!UsbLpt_SetMode(UsbLpt, LPT_MODE_EPP))
    {
        Die(L"Can't configure USBLPT", false);
    }
}

__declspec(dllexport) void _cdecl WritePort8(uint16_t port, uint8_t data)
{
    OpenUsbLpt();

    size_t i;
    for (i = 0; i < sizeof(bypassPorts) / sizeof(*bypassPorts); ++i)
    {
        if (port == bypassPorts[i])
        {
            return;
        }
    }

    port -= 0x378;
    if (port > 4)
    {
        Die(L"Invalid port passed", false);
    }

    if (!UsbLpt_SetPort8(UsbLpt, port, data))
    {
        Die(L"Error performing UsbLpt_SetPort8", false);
    }
}

 __declspec(dllexport) uint8_t _cdecl ReadPort8(uint16_t port)
{
    OpenUsbLpt();

    size_t i;
    for (i = 0; i < sizeof(bypassPorts) / sizeof(*bypassPorts); ++i)
    {
        if (port == bypassPorts[i])
        {
            return 0;
        }
    }

    port -= 0x378;
    if (port > 4)
    {
        Die(L"Invalid port passed", false);
    }

    uint8_t result;
    if (!UsbLpt_GetPort8(UsbLpt, port, &result))
    {
        Die(L"Error performing UsbLpt_GetPort8", false);
    }

    return result;
}