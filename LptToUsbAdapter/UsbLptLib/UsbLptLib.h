#ifndef USBLPTLIB_H
#define USBLPTLIB_H

#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>


typedef enum USBLPT_MODE_t
{
    LPT_MODE_LEGACY = 0,
    LPT_MODE_PS2 = 1,
    LPT_MODE_EPP = 2
}USBLPT_MODE;


typedef enum USBLPT_REG_t
{
    USBLPT_REG_PDATA = 0,
    USBLPT_REG_PSTAT = 1,
    USBLPT_REG_PCON = 2,
    USBLPT_REG_ADDSTR = 3,
    USBLPT_REG_DATASTR = 4
}USBLPT_REG;

#pragma pack(push, 1)
typedef struct USBLPT_version_t
{
    uint8_t implementation; //count from 0, while public version count from 1. so, this implementation 0 == version 1 and so on
    struct
    {
        uint8_t day;
        uint8_t month;
        uint16_t year;
    } build_date;

    struct 
    {
        uint8_t hour;
        uint8_t minute;
        uint8_t second;
    } build_time;

    union
    {
        struct
        {
            uint8_t dummy[32];
        } v1;

        struct
        {
            uint8_t dummy[32];
        } v2;

        struct
        {
            uint8_t revision;
            uint32_t chip_id;
            uint8_t uuid[12];
            uint8_t dummy[15];
        } v3;
    };
} USBLPT_version;
#pragma pack(pop)

struct UsbLptDevice_t
{
    USBLPT_version version;
    wchar_t serial[128];
    wchar_t location[128]; //will be enough?
};
typedef struct UsbLptDevice_t UsbLptDevice;


typedef struct USBLPT_t *USBLPT;

#if defined __cplusplus
extern "C" {
#endif

    bool UsbLpt_GetList(UsbLptDevice *devices, size_t devices_max, size_t *devices_used);
    size_t UsbLpt_BuildSimpleGuiDeviceSelection(UsbLptDevice* devices, size_t devices_count);

    bool UsbLpt_Init();
    bool UsbLpt_DeInit();

    USBLPT UsbLpt_Open(UsbLptDevice* dev);
    USBLPT UsbLpt_OpenAuto();
    
    bool UsbLpt_Close(USBLPT dev);
    bool UsbLpt_Reset(USBLPT dev);
    bool UsbLpt_SetMode(USBLPT dev, USBLPT_MODE mode);
    bool UsbLpt_GetVersion(USBLPT dev, USBLPT_version* ver);
    bool UsbLpt_SetPort8(USBLPT dev, USBLPT_REG reg, uint8_t byte);
    bool UsbLpt_GetPort8(USBLPT dev, USBLPT_REG reg, uint8_t* byte);

#if defined __cplusplus
}
#endif

#endif