#include <Windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdnoreturn.h>
#include <time.h>
#include <detours.h>
#include <UsbLptLib.h>
#include <shlwapi.h>
#include <psapi.h>
#include <stdarg.h>
#include "ld32.h"

    
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

void PrintInstructionDumpAt(HANDLE process, char* prefix, PVOID address, size_t len)
{
    uint8_t *instruction_buffer;
    size_t i;

    instruction_buffer = malloc(len);
    if (!instruction_buffer)
    {
        return;
    }

    SIZE_T readed;
    if (!ReadProcessMemory(process, address, instruction_buffer, len, &readed) || readed != len)
    {
        free(instruction_buffer);
        return;
    }

    char* instruction_buffer_hexline;// [sizeof(instruction_buffer) * 3 + 1 + 64] ;
    size_t instruction_buffer_hexline_len = len * 3 + 1 + strlen(prefix);

    instruction_buffer_hexline = malloc(instruction_buffer_hexline_len);
    if (!instruction_buffer_hexline_len)
    {
        free(instruction_buffer);
        return;
    }

    snprintf(instruction_buffer_hexline, instruction_buffer_hexline_len, "%s (at %p): ", prefix, address);

    for (i = 0; i < len; ++i)
    {
        snprintf(instruction_buffer_hexline + strlen(instruction_buffer_hexline), instruction_buffer_hexline_len - strlen(instruction_buffer_hexline), "%.2hhX ", instruction_buffer[i]);
    }

    free(instruction_buffer);

    LogLine(instruction_buffer_hexline);

    free(instruction_buffer_hexline);
}
#endif

USBLPT UsbLpt;
//#define MEMORY_PATCH_MODE

#if defined(MEMORY_PATCH_MODE)
#pragma pack(push, 1)
typedef union JmpFar_t
{
    uint8_t bytes[6];
    
    struct
    {
        uint8_t push;
        void* addr;
        uint8_t ret;
    };
    
} JmpFar;
#pragma pack(pop)

extern void* out_byte_fn;
extern uint32_t out_byte_fn_size;
extern void* in_byte_fn;
extern uint32_t in_byte_fn_size;

void* WritePort8;
void* ReadPort8;

bool FindExternalProcessDllFnAddresses(char* dll_name, HANDLE process, size_t functions, ...)
{
    va_list v;
    size_t module_i;
    size_t function_i;

    void** relative_addresses;

    relative_addresses = malloc(sizeof(void*) * functions);
    if (!relative_addresses)
    {
        return false;
    }

    //load relative addresses
    HMODULE mod = LoadLibraryExA(dll_name, NULL, DONT_RESOLVE_DLL_REFERENCES);
    if (mod == NULL)
    {
        return false;
    }

    va_start(v, functions);
    for (function_i = 0; function_i < functions; ++function_i)
    {
        relative_addresses[function_i] = GetProcAddress(mod, va_arg(v, char*));
        va_arg(v, void**);
        if (!relative_addresses[function_i])
        {
            free(relative_addresses);
            FreeLibrary(mod);
            va_end(v);
            return false;
        }
        relative_addresses[function_i] = (void*)((uint8_t*)relative_addresses[function_i] - (size_t)mod);
    }

    FreeLibrary(mod);
    va_end(v);

    //get absolute addresses in external process

    DWORD bytes_needed;
    if (!EnumProcessModules(process, NULL, 0, &bytes_needed))
    {
        free(relative_addresses);
        return false;
    }

    size_t bytes_allocated = bytes_needed;
    HMODULE* modules_array = malloc(bytes_allocated);
    if (!modules_array)
    {
        free(relative_addresses);
        return false;
    }

    if (!EnumProcessModules(process, modules_array, bytes_allocated, &bytes_needed))
    {
        free(relative_addresses);
        free(modules_array);
        return false;
    }

    bool found_module = false;
    va_start(v, functions);
    for (module_i = 0; module_i < bytes_allocated / sizeof(HMODULE); module_i++)
    {
        char module_path[MAX_PATH + 1];
        if (!GetModuleFileNameExA(process, modules_array[module_i], module_path, sizeof(module_path)))
        {
            break;
        }

        char module_path_again[sizeof(module_path)];
        char* filename;
        GetFullPathNameA(module_path, sizeof(module_path_again), module_path_again, &filename);

        if (!strcmp(filename, dll_name))
        {
            found_module = true;

            for (function_i = 0; function_i < functions; ++function_i)
            {
                va_arg(v, char*);
                *(va_arg(v, void**)) = (uint8_t*)modules_array[module_i] + (size_t)relative_addresses[function_i];
            }

            break;
        }
    }

    va_end(v);
    free(modules_array);
    free(relative_addresses);

    return found_module;
}
#endif

bool process_io_exception(HANDLE process, HANDLE thread, void* exception_address)
{
    //instrucion details
    uint8_t instr_ptr[16]; //bytes readed from exception ptr
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

#if defined(MEMORY_PATCH_MODE)
    //create mempatch

#ifdef _DEBUG
    PrintInstructionDumpAt(process, "pre-patched IO call area", exception_address, 32);
#endif

    if (io_size != 8 || instruction_sz != 1) //only 8 bit io, only port address in DX, only LPT1
    {
        return false;
    }

    JmpFar jmp_far;

    int len_this;
    SIZE_T len_total = 0;
    size_t x = sizeof(JmpFar);
    LPVOID remoteInoutWorker;
    while (len_total < sizeof(jmp_far))
    {
        len_this = length_disasm(instr_ptr + len_total, sizeof(instr_ptr) - len_total);
        if (len_this < 1)
        {
            return false;//eh
        }
        len_total += len_this;
    }

    PVOID worker_fn = out_direction ? &out_byte_fn : &in_byte_fn;
    uint32_t worker_size = out_direction ? out_byte_fn_size : in_byte_fn_size;
    void* remote_io_fn = out_direction ? WritePort8 : ReadPort8;

    remoteInoutWorker = VirtualAllocEx(process, NULL, (len_total - instruction_sz) + sizeof(JmpFar) + worker_size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!remoteInoutWorker)
    {
        return false;
    }

    SIZE_T written;

    //copy worker function
    if (!WriteProcessMemory(process, remoteInoutWorker, worker_fn, worker_size, &written) || written != worker_size)
    {
        return false;
    }

    //copy calling ptr to worker fn
    if (!WriteProcessMemory(process, remoteInoutWorker, &remote_io_fn, sizeof(remote_io_fn), &written) || written != sizeof(remote_io_fn))
    {
        return false;
    }

    //copy old instructions, but not IO call
    if (!WriteProcessMemory(process, (PVOID)((uint8_t*)remoteInoutWorker + worker_size), instr_ptr + instruction_sz, len_total - instruction_sz, &written) || written != len_total - instruction_sz)
    {
        return false;
    }

    //patch old IO call
    if (len_total > sizeof(instr_ptr))
    {
        return false;
    }
    memset(instr_ptr, 0x90, len_total);
    JmpFar* jumpToWorker = (JmpFar*)instr_ptr;
    jumpToWorker->push = 0x68;
    jumpToWorker->ret = 0xc3;
    jumpToWorker->addr = (uint8_t*)remoteInoutWorker + 4;//first 4 bytes - real worker address

    if (!WriteProcessMemory(process, exception_address, instr_ptr, len_total, &written) || written != len_total)
    {
        return false;
    }

    //and finally, write jump to old address
    jumpToWorker->addr = (uint8_t*)exception_address + len_total;
    if (!WriteProcessMemory(process, (PVOID)((uint8_t*)remoteInoutWorker + (len_total - instruction_sz) + worker_size), jumpToWorker, sizeof(JmpFar), &written) || written != sizeof(JmpFar))
    {
        return false;
    }

#ifdef _DEBUG
    PrintInstructionDumpAt(process, "patched IO call area", exception_address, 32);
    PrintInstructionDumpAt(process, "worker area", remoteInoutWorker, 32);
#endif

#else

    for (i = 0; i < sizeof(bypassPorts) / sizeof(*bypassPorts); ++i)
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

            if (!UsbLpt_SetPort8(UsbLpt, port - 0x378, (uint8_t)data))
            {
                return false;
            }
        }
        else
        {
            uint8_t temp;
            if (!UsbLpt_GetPort8(UsbLpt, port - 0x378, &temp))
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

#endif


    return true;
}

#define PATCH_DLL_NAME "LptPatchInjectee32.dll"
//#define APPLICATION_NAME L"Orange.exe"
#define APPLICATION_NAME L"LptPortAccessDemo.exe"

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
#if 0
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
#endif
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

    if (sizeof(void*) != 4)
    {
        Die(L"Only x86 support! No 64-bit mode!", false);
    }

#if !defined(MEMORY_PATCH_MODE)
    UsbLpt = UsbLpt_OpenAuto();
    if (!UsbLpt)
    {
        Die(L"Can't open USBLPT", false);
    }

    if (!UsbLpt_SetMode(UsbLpt, LPT_MODE_EPP))
    {
        Die(L"Can't configure USBLPT", false);
    }
#else
    MessageBoxA(NULL, "Highly experimental mode!\r\nUse for your own risk!", "Warning", MB_ICONWARNING);
#endif

    if (!GetOmegaExePath(appPath, sizeof(appPath) / sizeof(*appPath)))
    {
        Die(L"Start file can not be located", false);
    }

    if (!GetPatchDllPath(patchDllPath, sizeof(patchDllPath) / sizeof(*patchDllPath)))
    {
        Die(L"patch dll file can not be located", false);
    }

#if 0
    if (!CreateProcess(appPath, NULL, NULL, NULL, FALSE, DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS, NULL, NULL, &si, &pi))
    {
        Die(L"error creating process", true);
    }
    if (!InjectLibrary(pi.hProcess, patchDllPath))
    {
        Die(L"error injecting patch dll", true);
    }
#else
    if (!DetourCreateProcessWithDllExW(appPath, NULL, NULL, NULL, FALSE, DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS, NULL, NULL, &si, &pi, patchDllPath, NULL))
    {
        Die(L"error creating process with injected dll patch", true);
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
#if defined(MEMORY_PATCH_MODE)
                        if (!WritePort8 || !ReadPort8)
                        {
                            //load addresses of WriteReg8/ReadReg8 inside dst process. only now, when process really loaded.
                            if (!FindExternalProcessDllFnAddresses(patchDllPath, pi.hProcess, 2, "ReadPort8", &ReadPort8, "WritePort8", &WritePort8))
                            {
                                Die(L"error loading addresses of patch dll io functions", false);
                            }
                        }
#endif
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


#if !defined(MEMORY_PATCH_MODE)
    UsbLpt_Close(UsbLpt);
#endif

    return 0;
}
