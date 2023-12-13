#pragma once
#include <stdnoreturn.h>
#include <stdbool.h>
#include <stdint.h>
#include <Windows.h>


#define SETTINGS_FILE_NAME "LptPatch.ini"
#define DEFAULT_APPLICATION_NAME "Orange.exe"
//#define DEFAULT_APPLICATION_NAME "LptPortAccessDemo.exe"

struct LptPatchSettings_t
{
    struct
    {
        char* application_path;

    }target;

    struct
    {
        bool use_mempatch;
        uint16_t* bypass_ports;
        uint16_t bypass_ports_cnt;
        bool patch_winio_load;
        bool emulate_lpt_in_registry;
    }behavior;

    struct
    {
        uint16_t base_address;
        char* mode;
    }lpt;

    struct
    {
        bool enabled;
        char* file_intercepted_calls;
        char* file_patched_areas;
        char* file_errors;
    }logging;
};

void LoadSettings(char* settings_file, struct LptPatchSettings_t *settings);
noreturn void Die(wchar_t* reason, bool isSystemFail);
char* strdup_die(const char* str);
void LogLine(const char* file, char* line);
void PrintInstructionDumpAt(const char* file, HANDLE process, char* prefix, PVOID address, size_t len);