#ifndef USBLPT_H
#define USBLPT_H

#include <stdbool.h>
#include <stdint.h>


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
    uint8_t revision;
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
} USBLPT_version;
#pragma pack(pop)


typedef struct USBLPT_t* USBLPT;

#if defined __cplusplus
extern "C" {
#endif

    USBLPT UsbLpt_Open();
    bool UsbLpt_Close(USBLPT dev);
    bool UsbLpt_SetMode(USBLPT dev, USBLPT_MODE mode);
    bool UsbLpt_GetVersion(USBLPT dev, USBLPT_version* ver);
    bool UsbLpt_SetPort8(USBLPT dev, USBLPT_REG reg, uint8_t byte);
    bool UsbLpt_GetPort8(USBLPT dev, USBLPT_REG reg, uint8_t* byte);

#if defined __cplusplus
}
#endif

#endif
