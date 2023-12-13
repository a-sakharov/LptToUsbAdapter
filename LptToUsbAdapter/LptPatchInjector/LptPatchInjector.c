#include <Windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <detours.h>
#include <UsbLptLib.h>
#include <shlwapi.h>
#include <psapi.h>
#include <stdarg.h>
#include "ld32.h"
#include "CommonCode.h"


#define FILL_INSTRUCTION_DATA(_instruction_sz, _port, _io_size, _out_direction) \
{\
instruction_sz = (_instruction_sz);\
port = (_port); \
io_size = (_io_size);\
out_direction = (_out_direction);\
}

#define CHECK_OPERATION_2B(byte0, byte1, _instruction_sz, _port, _io_size, _out_direction) \
if(inst_buf[0] == (byte0) && inst_buf[1] == (byte1)) FILL_INSTRUCTION_DATA(_instruction_sz, _port, _io_size, _out_direction)

#define CHECK_OPERATION_1B(byte0, _instruction_sz, _port, _io_size, _out_direction) \
if(inst_buf[0] == (byte0)) FILL_INSTRUCTION_DATA(_instruction_sz, _port, _io_size, _out_direction)

#define PATCH_DLL_NAME "LptPatchInjectee32.dll"

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

USBLPT UsbLpt;

struct LptPatchSettings_t Settings = { 0 };

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
        free(relative_addresses);
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

bool DecodeIoInstruction(uint8_t *inst_buf, size_t inst_len, uint32_t edx, uint16_t *used_port, uint8_t * used_io_size_bits, bool *is_direction_output, uint8_t *instruction_size)
{
    uint16_t port; //port number
    uint8_t io_size; //io data size, bits
    bool out_direction; //1 if OUT, 0 if IN
    uint8_t instruction_sz; //instruction length, bytes

         CHECK_OPERATION_2B(0x66, 0xE5, 3, inst_buf[2],  16, false) //IN  16 indirect
    else CHECK_OPERATION_2B(0x66, 0xED, 2, edx & 0xFFFF, 16, false) //IN  16 DX
    else CHECK_OPERATION_2B(0x66, 0xE7, 3, inst_buf[2],  16, true)  //OUT 16 indirect
    else CHECK_OPERATION_2B(0x66, 0xEF, 2, edx & 0xFFFF, 16, true)  //OUT 16 DX
    else CHECK_OPERATION_1B(0xE4, 2, inst_buf[1],  8,  false)       //IN  8  indirect
    else CHECK_OPERATION_1B(0xE5, 2, inst_buf[1],  32, false)       //IN  32 indirect
    else CHECK_OPERATION_1B(0xEC, 1, edx & 0xFFFF, 8,  false)       //IN  8  DX
    else CHECK_OPERATION_1B(0xED, 1, edx & 0xFFFF, 32, false)       //IN  32 DX
    else CHECK_OPERATION_1B(0xE6, 2, inst_buf[1],  8,  true)        //OUT 8  indirect
    else CHECK_OPERATION_1B(0xE7, 2, inst_buf[1],  32, true)        //OUT 32 indirect
    else CHECK_OPERATION_1B(0xEE, 1, edx & 0xFFFF, 8,  true)        //OUT 8  DX
    else CHECK_OPERATION_1B(0xEF, 1, edx & 0xFFFF, 32, true)        //OUT 32 DX
    else
    {
        if (Settings.logging.enabled)
        {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "Undefined instruction: %.2hhX %.2hhX %.2hhX", inst_buf[0], inst_buf[1], inst_buf[2]);
            LogLine(Settings.logging.file_errors, buffer);
        }
        return false;
    }

    *used_port = port;
    *used_io_size_bits = io_size;
    *is_direction_output = out_direction;
    *instruction_size = instruction_sz;

    return true;
}

bool PatchIoInstruction(HANDLE process, uint8_t* inst_buf, size_t inst_len, uint8_t io_size_bits, uint8_t instruction_size_bytes, void *instruction_address, bool is_out_instruction)
{
    if (Settings.logging.enabled)
    {
        PrintInstructionDumpAt(Settings.logging.file_patched_areas, process, "pre-patched IO call area", instruction_address, 32);
    }

    if (io_size_bits != 8 || instruction_size_bytes != 1) //only 8 bit io, only port address in DX, only LPT1
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
        len_this = length_disasm(inst_buf + len_total, io_size_bits - len_total);
        if (len_this < 1)
        {
            return false;//eh
        }
        len_total += len_this;
    }

    PVOID worker_fn = is_out_instruction ? &out_byte_fn : &in_byte_fn;
    uint32_t worker_size = is_out_instruction ? out_byte_fn_size : in_byte_fn_size;
    void* remote_io_fn = is_out_instruction ? WritePort8 : ReadPort8;

    remoteInoutWorker = VirtualAllocEx(process, NULL, (len_total - instruction_size_bytes) + sizeof(JmpFar) + worker_size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
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
    if (!WriteProcessMemory(process, (PVOID)((uint8_t*)remoteInoutWorker + worker_size), inst_buf + instruction_size_bytes, len_total - instruction_size_bytes, &written) || written != len_total - instruction_size_bytes)
    {
        return false;
    }

    //patch old IO call
    if (len_total > inst_len)
    {
        return false;
    }
    memset(inst_buf, 0x90, len_total);
    JmpFar* jumpToWorker = (JmpFar*)inst_buf;
    jumpToWorker->push = 0x68;
    jumpToWorker->ret = 0xc3;
    jumpToWorker->addr = (uint8_t*)remoteInoutWorker + 4;//first 4 bytes - real worker address

    if (!WriteProcessMemory(process, instruction_address, inst_buf, len_total, &written) || written != len_total)
    {
        return false;
    }

    //and finally, write jump to old address
    jumpToWorker->addr = (uint8_t*)instruction_address + len_total;
    if (!WriteProcessMemory(process, (PVOID)((uint8_t*)remoteInoutWorker + (len_total - instruction_size_bytes) + worker_size), jumpToWorker, sizeof(JmpFar), &written) || written != sizeof(JmpFar))
    {
        return false;
    }

    if (Settings.logging.enabled)
    {
        PrintInstructionDumpAt(Settings.logging.file_patched_areas, process, "patched IO call area", instruction_address, 32);
        PrintInstructionDumpAt(Settings.logging.file_patched_areas, process, "worker area", remoteInoutWorker, 32);
    }

    return true;
}

bool ProcessIoException(HANDLE process, HANDLE thread, void* exception_address)
{
    //instrucion details
    uint8_t instr_ptr[16]; //bytes readed from exception ptr
    uint8_t instruction_sz; //instruction length, bytes
    uint16_t port; //port number
    uint8_t io_size; //io data size, bits
    bool out_direction; //1 if OUT, 0 if IN
    uint32_t eax = 0; //ExceptionInfo->ContextRecord->Eax
    CONTEXT threadContext = { .ContextFlags = WOW64_CONTEXT_INTEGER | WOW64_CONTEXT_CONTROL };
    bool bypassMode = false;
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

    eax = threadContext.Eax;

    //and only for in/out instructions
    if(!DecodeIoInstruction(instr_ptr, sizeof(instr_ptr), threadContext.Edx, &port, &io_size, &out_direction, &instruction_sz))
    {
        return false;
    }

    if (Settings.behavior.use_mempatch)
    {
        PatchIoInstruction(process, instr_ptr, sizeof(instr_ptr), io_size, instruction_sz, exception_address, out_direction);
    }
    else
    {
        for (i = 0; i < Settings.behavior.bypass_ports_cnt; ++i)
        {
            if (port == Settings.behavior.bypass_ports[i])
            {
                bypassMode = true;
                break;
            }
        }

        if (!bypassMode)
        {
            if (io_size != 8 || port < Settings.lpt.base_address || port >(Settings.lpt.base_address + 4)) //only 8 bit io
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

                if (!UsbLpt_SetPort8(UsbLpt, port - Settings.lpt.base_address, (uint8_t)data))
                {
                    return false;
                }
            }
            else
            {
                uint8_t temp;
                if (!UsbLpt_GetPort8(UsbLpt, port - Settings.lpt.base_address, &temp))
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

        if (Settings.logging.enabled)
        {
            char buffer[128];
            if (out_direction)
            {
                snprintf(buffer, sizeof(buffer), "%p OUT.%c 0x%hX, 0x%X", exception_address, io_size == 8 ? 'b' : io_size == 16 ? 'w' : 'd', port, data);
            }
            else
            {
                snprintf(buffer, sizeof(buffer), "%p IN.%c 0x%X ; got %X", exception_address, io_size == 8 ? 'b' : io_size == 16 ? 'w' : 'd', port, data);
            }

            LogLine(Settings.logging.file_intercepted_calls, buffer);
        }

    }

    return true;
}

bool GetTargetExePath(wchar_t* path, size_t max_len)
{
    size_t converted;

    if (mbstowcs_s(&converted, path, max_len, Settings.target.application_path, strlen(Settings.target.application_path)) != 0)
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

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    DebugSetProcessKillOnExit(TRUE);
    STARTUPINFO si = { .cb = sizeof(STARTUPINFO) };
    PROCESS_INFORMATION pi;
    wchar_t appPath[MAX_PATH];
    char patchDllPath[MAX_PATH];

    LoadSettings(SETTINGS_FILE_NAME, &Settings);

    if (sizeof(void*) != 4)
    {
        Die(L"Only x86 support! No 64-bit mode!", false);
    }

    if (Settings.behavior.use_mempatch)
    {
        MessageBoxA(NULL, "Highly experimental mode!\r\nUse for your own risk!", "Warning", MB_ICONWARNING);
    }
    else
    {
        UsbLpt = UsbLpt_OpenAuto();
        if (!UsbLpt)
        {
            Die(L"Can't open USBLPT", false);
        }

        USBLPT_MODE m;
        if (!_stricmp(Settings.lpt.mode, "EPP"))
        {
            m = LPT_MODE_EPP;
        }
        else if (!_stricmp(Settings.lpt.mode, "LEGACY"))
        {
            m = LPT_MODE_LEGACY;
        }
        else if (!_stricmp(Settings.lpt.mode, "PS/2") || !_stricmp(Settings.lpt.mode, "PS2"))
        {
            m = LPT_MODE_PS2;
        }
        else
        {
            Die(L"Invalid lpt mode set in configuration file", false);
        }

        if (!UsbLpt_SetMode(UsbLpt, m))
        {
            Die(L"Can't configure USBLPT", false);
        }
    }

    if (!GetTargetExePath(appPath, sizeof(appPath) / sizeof(*appPath)))
    {
        Die(L"Start file can not be located", false);
    }

    if (!GetPatchDllPath(patchDllPath, sizeof(patchDllPath) / sizeof(*patchDllPath)))
    {
        Die(L"patch dll file can not be located", false);
    }

    if (!DetourCreateProcessWithDllExW(appPath, NULL, NULL, NULL, FALSE, DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS, NULL, NULL, &si, &pi, patchDllPath, NULL))
    {
        Die(L"error creating process with injected dll patch", true);
    }

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
                if (Settings.behavior.use_mempatch)
                {
                    if (!WritePort8 || !ReadPort8)
                    {
                        //load addresses of WriteReg8/ReadReg8 inside dst process. only now, when process really loaded.
                        if (!FindExternalProcessDllFnAddresses(patchDllPath, pi.hProcess, 2, "ReadPort8", &ReadPort8, "WritePort8", &WritePort8))
                        {
                            Die(L"error loading addresses of patch dll io functions", false);
                        }
                    }
                }
                HANDLE thread = OpenThread(THREAD_ALL_ACCESS, FALSE, de.dwThreadId);
                if (!ProcessIoException(pi.hProcess, thread, de.u.Exception.ExceptionRecord.ExceptionAddress))
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

    if (!Settings.behavior.use_mempatch)
    {
        UsbLpt_Close(UsbLpt);
    }

    FreeSettings(&Settings);

    return 0;
}
