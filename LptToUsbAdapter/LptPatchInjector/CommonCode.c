#include "CommonCode.h"
#include <iniparser.h>
#include <Windows.h>
#include <time.h>


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

char* strdup_die(const char* str)
{
    char* r = _strdup(str);
    if (!r)
    {
        Die(L"Error: cant allocate memory for string", true);
    }
    return r;
}

void LoadSettings(char *settings_file, struct LptPatchSettings_t *settings)
{
    dictionary* lpt_patch_settings;
    struct
    {
        char* key;
        char* default_value;
    }
    defaults[] =
    {
        {"target", NULL},
        {"target:application_path", DEFAULT_APPLICATION_NAME},

        {"behavior", NULL},
        {"behavior:use_mempatch", "True"},
        {"behavior:bypass_ports", "0x77A 0x2F8 0x2F9 0x2FA 0x2FB 0x2FC 0x2FD 0x2FE 0x2FF"},
        {"behavior:patch_winio_load", "True"},          //TODO: implement
        {"behavior:emulate_lpt_in_registry", "True"},   //TODO: implement

        {"lpt", NULL},
        {"lpt:base_address", "0x378"},
        {"lpt:mode", "EPP"},

        {"logging", NULL},
        {"logging:enabled", "False"},
        {"logging:file_intercepted_calls", "calls.log"},
        {"logging:file_patched_areas", "patches.log"},
        {"logging:file_errors", "errors.log"},
    };
    bool update_required = false;

    lpt_patch_settings = iniparser_load(settings_file);
    if (!lpt_patch_settings)
    {
        lpt_patch_settings = dictionary_new(0);
    }

    if (!lpt_patch_settings)
    {
        Die(L"Error create/load default settings", false);
    }

    size_t i;
    for (i = 0; i < sizeof(defaults) / sizeof(*defaults); ++i)
    {
        if (!iniparser_find_entry(lpt_patch_settings, defaults[i].key))
        {
            iniparser_set(lpt_patch_settings, defaults[i].key, defaults[i].default_value);
            update_required = true;
        }
    }

    if (update_required)
    {
        FILE* f;
        if (fopen_s(&f, settings_file, "wt") == 0)
        {
            iniparser_dump_ini(lpt_patch_settings, f);
            fclose(f);
        }
        else
        {
            Die(L"Error save default settings", true);
        }
    }

    settings->target.application_path = strdup_die(iniparser_getstring(lpt_patch_settings, "target:application_path", ""));

    settings->behavior.use_mempatch = iniparser_getboolean(lpt_patch_settings, "behavior:use_mempatch", true);
    const char* bypass_ports = iniparser_getstring(lpt_patch_settings, "behavior:bypass_ports", NULL);
    settings->behavior.patch_winio_load = iniparser_getboolean(lpt_patch_settings, "behavior:patch_winio_load", true);
    settings->behavior.emulate_lpt_in_registry = iniparser_getboolean(lpt_patch_settings, "behavior:emulate_lpt_in_registry", true);

    settings->lpt.base_address = iniparser_getint(lpt_patch_settings, "lpt:base_address", 0x378);
    settings->lpt.mode = strdup_die(iniparser_getstring(lpt_patch_settings, "lpt:mode", NULL));

    settings->logging.enabled = iniparser_getboolean(lpt_patch_settings, "logging:enabled", true);
    settings->logging.file_intercepted_calls = strdup_die(iniparser_getstring(lpt_patch_settings, "logging:file_intercepted_calls", NULL));
    settings->logging.file_patched_areas = strdup_die(iniparser_getstring(lpt_patch_settings, "logging:file_patched_areas", NULL));
    settings->logging.file_errors = strdup_die(iniparser_getstring(lpt_patch_settings, "logging:file_errors", NULL));

    char* strtok_ctx = NULL;
    char* port;
    port = strtok_s((char*)bypass_ports, " ,", &strtok_ctx);
    while (port)
    {
        settings->behavior.bypass_ports_cnt++;
        void* tmp = realloc(settings->behavior.bypass_ports, sizeof(*settings->behavior.bypass_ports) * settings->behavior.bypass_ports_cnt);
        if (!tmp)
        {
            Die(L"Error memory allocation while reading bypass ports", true);
        }
        settings->behavior.bypass_ports = tmp;
        settings->behavior.bypass_ports[settings->behavior.bypass_ports_cnt - 1] = (uint16_t)strtoul(port, NULL, 0);

        port = strtok_s(NULL, " ,", &strtok_ctx);
    }

    iniparser_freedict(lpt_patch_settings);
}

void FreeSettings(struct LptPatchSettings_t* settings)
{
    free(settings->target.application_path);
    free(settings->lpt.mode);
    free(settings->logging.file_intercepted_calls);
    free(settings->logging.file_patched_areas);
    free(settings->logging.file_errors);
    free(settings->behavior.bypass_ports);
}

void LogLine(const char* file, char* line)
{
#ifdef _DEBUG
    OutputDebugStringA(line);
    OutputDebugStringA("\n");
#endif

    static struct
    {
        FILE* file;
        char name[MAX_PATH];
        bool used;
    } opened_logs_cache[5] = {0};
    size_t i;
    size_t index_in_cache;
    bool found_in_cache = false;

    for (i = 0; i < sizeof(opened_logs_cache) / sizeof(*opened_logs_cache); ++i)
    {
        if (opened_logs_cache[i].used && !strcmp(opened_logs_cache[i].name, file))
        {
            found_in_cache = true;
            index_in_cache = i;
            break;
        }
    }

    if (!found_in_cache)
    {
        for (i = 0; i < sizeof(opened_logs_cache) / sizeof(*opened_logs_cache); ++i)
        {
            if (!opened_logs_cache[i].used)
            {
                found_in_cache = true;

                if (fopen_s(&opened_logs_cache[i].file, file, "a"))
                {
                    return;
                }
                index_in_cache = i;
                opened_logs_cache[i].used = true;
                strncpy_s(opened_logs_cache[i].name, sizeof(opened_logs_cache[i].name), file, strlen(file));
                break;
            }
        }
    }

    if (!found_in_cache)
    {
        return; //no more place in cache...
    }

    char timebuffer[128];
    time_t utime;
    struct tm utm;

    utime = time(0);

    gmtime_s(&utm, &utime);

    //asctime_s(timebuffer, sizeof(timebuffer), &utm);
    strftime(timebuffer, sizeof(timebuffer), "%Y-%m-%d %H:%M:%S", &utm);

    fprintf(opened_logs_cache[index_in_cache].file, "%s: %s\n", timebuffer, line);

    fflush(opened_logs_cache[index_in_cache].file);
}

void PrintInstructionDumpAt(const char* file, HANDLE process, char* prefix, PVOID address, size_t len)
{
    uint8_t* instruction_buffer;
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
    if (!instruction_buffer_hexline)
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

    LogLine(file, instruction_buffer_hexline);

    free(instruction_buffer_hexline);
}
