#include <Windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdnoreturn.h>
#include <time.h>
#include <detours.h>
#include <AVRLPT.h>
#include <shlwapi.h>

#define FILL_INSTRUCTION_DATA(_instruction_sz, _port, _io_size, _out_direction) \
{\
instruction_sz = (_instruction_sz);\
port = (_port); \
io_size = (_io_size);\
out_direction = (_out_direction);\
}

#define CHECK_OPERATION_2B(byte0, byte1, _instruction_sz, _port, _io_size, _out_direction) \
if(instr_ptr[0] == (byte0) && instr_ptr[1] == (byte1)) FILL_INSTRUCTION_DATA(_instruction_sz, _port, _io_size, _out_direction)

#define CHECK_OPERATION_1B(byte0, _instruction_sz, _port, _io_size, _out_direction) \
if(instr_ptr[0] == (byte0)) FILL_INSTRUCTION_DATA(_instruction_sz, _port, _io_size, _out_direction)

#ifdef _DEBUG
void LogLine(char* line)
{
    OutputDebugStringA(line);
    OutputDebugStringA("\n");

    FILE* logFile;
    if (fopen_s(&logFile, "OmegaLptPatchInjector.log", "a"))
    {
        return;
    }

    char timebuffer[128];
    time_t utime;
    struct tm utm;

    utime = time(0);

    gmtime_s(&utm, &utime);

    //asctime_s(timebuffer, sizeof(timebuffer), &utm);
    strftime(timebuffer, sizeof(timebuffer), "%Y-%m-%d %H:%M:%S", &utm);

    fprintf(logFile, "%s: %s\n", timebuffer, line);

    fclose(logFile);
}
#endif

AVRLPT AvrLpt;

bool process_io_exception(HANDLE process, HANDLE thread, void* exception_address)
{
    //instrucion details
    uint8_t instr_ptr[3]; //bytes readed from exception ptr
    uint8_t instruction_sz; //instruction length, bytes
    uint16_t port; //port number
    uint8_t io_size; //io data size, bits
    bool out_direction; //1 if OUT, 0 if IN
    uint32_t edx = 0; //ExceptionInfo->ContextRecord->Edx
    uint32_t eax = 0; //ExceptionInfo->ContextRecord->Eax
    CONTEXT threadContext = { .ContextFlags = WOW64_CONTEXT_INTEGER | WOW64_CONTEXT_CONTROL };
    bool bypassMode = false;
    uint16_t bypassPorts[] = {
        0x77A
    };
    size_t i;
    uint32_t data = 0; //data to write

    SIZE_T readed;
    if (!ReadProcessMemory(process, exception_address, instr_ptr, sizeof(instr_ptr), &readed) || readed != sizeof(instr_ptr))
    {
        return false;
    }

    if (!GetThreadContext(thread, &threadContext))
    {
        return false;
    }

    edx = threadContext.Edx;
    eax = threadContext.Eax;

    //and only for in/out instructions
    CHECK_OPERATION_2B(0x66, 0xE5, 3, instr_ptr[2], 16, false)      //IN  16 indirect
    else CHECK_OPERATION_2B(0x66, 0xED, 2, edx & 0xFFFF, 16, false) //IN  16 DX
    else CHECK_OPERATION_2B(0x66, 0xE7, 3, instr_ptr[2], 16, true)  //OUT 16 indirect
    else CHECK_OPERATION_2B(0x66, 0xEF, 2, edx & 0xFFFF, 16, true)  //OUT 16 DX
    else CHECK_OPERATION_1B(0xE4, 2, instr_ptr[1], 8, false)        //IN  8  indirect
    else CHECK_OPERATION_1B(0xE5, 2, instr_ptr[1], 32, false)       //IN  32 indirect
    else CHECK_OPERATION_1B(0xEC, 1, edx & 0xFFFF, 8, false)        //IN  8  DX
    else CHECK_OPERATION_1B(0xED, 1, edx & 0xFFFF, 32, false)       //IN  32 DX
    else CHECK_OPERATION_1B(0xE6, 2, instr_ptr[1], 8, true)         //OUT 8  indirect
    else CHECK_OPERATION_1B(0xE7, 2, instr_ptr[1], 32, true)        //OUT 32 indirect
    else CHECK_OPERATION_1B(0xEE, 1, edx & 0xFFFF, 8, true)         //OUT 8  DX
    else CHECK_OPERATION_1B(0xEF, 1, edx & 0xFFFF, 32, true)        //OUT 32 DX
    else
    {
#ifdef _DEBUG
        char buffer[128];
        snprintf(buffer, sizeof(buffer), "Undefined instruction: %.2hhX %.2hhX %.2hhX", instr_ptr[0], instr_ptr[1], instr_ptr[2]);
        LogLine(buffer);
#endif
        return false;
    }

    for (i = 0; i < sizeof(bypassPorts)/sizeof(*bypassPorts); ++i)
    {
        if (port == bypassPorts[i])
        {
            bypassMode = true;
            break;
        }
    }

    if (!bypassMode)
    {
        if (io_size != 8 || port < 0x378 || port > 0x37c) //only 8 bit io, only LPT1
        {
            return false;
        }

        if (out_direction)
        {
            if (io_size == 32)
            {
                data = eax;
            }
            else if (io_size == 16)
            {
                data = eax & 0xFFFF;
            }
            else
            {
                data = eax & 0xFF;
            }

            if (!AvrLpt_SetPort8(AvrLpt, port - 0x378, (uint8_t)data))
            {
                return false;
            }
        }
        else
        {
            uint8_t temp;
            if (!AvrLpt_GetPort8(AvrLpt, port - 0x378, &temp))
            {
                return false;
            }

            data = temp;

            if (io_size == 32)
            {
                eax = data;
            }
            else if (io_size == 16)
            {
                eax = (eax & 0xFFFF0000) | (data & 0x0000FFFF);
            }
            else
            {
                eax = (eax & 0xFFFFFF00) | (data & 0x000000FF);
            }
        }
    }
    threadContext.Eip += instruction_sz; //move EIP +n bytes
    threadContext.Eax = eax;

    if (!SetThreadContext(thread, &threadContext))
    {
        return false;
    }

#ifdef _DEBUG
    char buffer[128];
    if (out_direction)
    {
        snprintf(buffer, sizeof(buffer), "%p OUT.%c 0x%hX, 0x%X", exception_address, io_size == 8 ? 'b' : io_size == 16 ? 'w' : 'd', port, data);
    }
    else
    {
        snprintf(buffer, sizeof(buffer), "%p IN.%c 0x%X ; got %X", exception_address, io_size == 8 ? 'b' : io_size == 16 ? 'w' : 'd', port, data);
    }

    LogLine(buffer);
#endif

    return true;
}

#define PATCH_DLL_NAME "OmegaLptPatch32.dll"
#define APPLICATION_NAME L"Orange.exe"
//#define APPLICATION_NAME L"OmegaLptPatchDemo.exe"

bool GetOmegaExePath(wchar_t* path, size_t max_len)
{
    if (wcscpy_s(path, max_len, APPLICATION_NAME) != 0)
    {
        return false;
    }

    if (PathFileExistsW(path) == TRUE)
    {
        return true;
    }

    //search alrgest EXE but not self?

    return false;
}

bool GetPatchDllPath(char* path, size_t max_len)
{
    if (strcpy_s(path, max_len, PATCH_DLL_NAME) != 0)
    {
        return false;
    }
    
    return PathFileExistsA(path) == TRUE;
}

bool InjectLibrary(HANDLE process, char* lib_path)
{
    size_t lib_path_size = strlen(lib_path) + 1;
    void* remotePatchDllPath;

    remotePatchDllPath = VirtualAllocEx(process, NULL, lib_path_size, MEM_COMMIT, PAGE_READWRITE);
    if (!remotePatchDllPath)
    {
        return false;
    }

    DWORD written;
    if (!WriteProcessMemory(process, remotePatchDllPath, lib_path, lib_path_size, &written) || written != lib_path_size)
    {
        return false;
    }

    DWORD patcher_thread_id;
    HANDLE patcher_thread;
    patcher_thread = CreateRemoteThread(process, NULL, 0, (LPTHREAD_START_ROUTINE)LoadLibraryA, (LPVOID)remotePatchDllPath, 0, &patcher_thread_id);
    if (patcher_thread == NULL)
    {
        return false;
    }
    CloseHandle(patcher_thread);

    return true;
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

    exit(-1);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    DebugSetProcessKillOnExit(TRUE);

    STARTUPINFO si = { .cb = sizeof(STARTUPINFO) };
    PROCESS_INFORMATION pi;
    wchar_t appPath[MAX_PATH];
    char patchDllPath[MAX_PATH];
    AVRLPT_version av;

    AvrLpt = AvrLpt_Open();
    if (!AvrLpt)
    {
        Die(L"AVRLPT device not found", false);
    }

    if (!AvrLpt_GetVersion(AvrLpt, &av))
    {
        Die(L"Can't get AVRLPT version", false);
    }

    if (av.revision != 0)
    {
        if (MessageBox(NULL, L"Undefined AVRLPT revesion. Continue?", L"Warning", MB_YESNO | MB_ICONWARNING) == IDNO)
        {
            exit(-1);
        }
    }

    if (!AvrLpt_SetMode(AvrLpt, LPT_MODE_EPP))
    {
        Die(L"Can't configure AVRLPT", false);
    }

    if (!GetOmegaExePath(appPath, sizeof(appPath) / sizeof(*appPath)))
    {
        Die(L"Omega start file can not be located", false);
    }

    if (!GetPatchDllPath(patchDllPath, sizeof(patchDllPath) / sizeof(*patchDllPath)))
    {
        Die(L"patch dll file can not be located", false);
    }

#if 0
    if (!CreateProcess(appPath, NULL, NULL, NULL, FALSE, DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS, NULL, NULL, &si, &pi))
    {
        Die(L"error creating omega process", true);
    }
    if (!InjectLibrary(pi.hProcess, patchDllPath))
    {
        Die(L"error injecting patch dll", true);
    }
#else
    LPCSTR dlls[2] = { patchDllPath };
    if (!DetourCreateProcessWithDllExW(appPath, NULL, NULL, NULL, FALSE, DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS, NULL, NULL, &si, &pi, patchDllPath, NULL))
    {
        Die(L"error creating omega process with injected dll patch", true);
    }
#endif
    CloseHandle(pi.hThread);

    DEBUG_EVENT de;
    bool process_alive = true;

    while (process_alive && WaitForDebugEvent(&de, INFINITE))
    {
        DWORD continue_type = DBG_CONTINUE;

        switch (de.dwDebugEventCode)
        {
            case CREATE_PROCESS_DEBUG_EVENT:
            {
                CloseHandle(de.u.CreateProcessInfo.hFile);
            }
            break;

            case LOAD_DLL_DEBUG_EVENT:
            {
                CloseHandle(de.u.LoadDll.hFile);
            }
            break;

            case EXCEPTION_DEBUG_EVENT:
            {
                switch (de.u.Exception.ExceptionRecord.ExceptionCode)
                {
                    case EXCEPTION_PRIV_INSTRUCTION:
                    {
                        HANDLE thread = OpenThread(THREAD_ALL_ACCESS, FALSE, de.dwThreadId);
                        if (!process_io_exception(pi.hProcess, thread, de.u.Exception.ExceptionRecord.ExceptionAddress))
                        {
                            continue_type = DBG_EXCEPTION_NOT_HANDLED;
                        }
                    }
                    break;

                    default:
                    {
                        continue_type = DBG_EXCEPTION_NOT_HANDLED;
                    }
                    break;
                }
            }
            break;

            case EXIT_PROCESS_DEBUG_EVENT:
            {
                process_alive = false;
            }
            break;

            default:
            {
                continue_type = DBG_EXCEPTION_NOT_HANDLED;
            }
            break;
        }

        ContinueDebugEvent(de.dwProcessId, de.dwThreadId, continue_type);
    }

    CloseHandle(pi.hProcess);
    AvrLpt_Close(AvrLpt);
    
    return 0;
}
