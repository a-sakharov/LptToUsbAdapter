#ifndef AVRLPT_H
#define AVRLPT_H

#include <stdbool.h>
#include <stdint.h>


typedef enum AVRLPT_MODE_t
{
    LPT_MODE_LEGACY = 0,
    LPT_MODE_PS2 = 1,
    LPT_MODE_EPP = 2
}AVRLPT_MODE;


typedef enum AVRLPT_REG_t
{
    AVRLPT_REG_PDATA = 0,
    AVRLPT_REG_PSTAT = 1,
    AVRLPT_REG_PCON = 2,
    AVRLPT_REG_ADDSTR = 3,
    AVRLPT_REG_DATASTR = 4
}AVRLPT_REG;

#pragma pack(push, 1)
typedef struct AVRLPT_version_t
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
} AVRLPT_version;
#pragma pack(pop)


typedef struct AVRLPT_t* AVRLPT;

AVRLPT AvrLpt_Open();
bool AvrLpt_Close(AVRLPT dev);
bool AvrLpt_SetMode(AVRLPT dev, AVRLPT_MODE mode);
bool AvrLpt_GetVersion(AVRLPT dev, AVRLPT_version *ver);
bool AvrLpt_SetPort8(AVRLPT dev, AVRLPT_REG reg, uint8_t byte);
bool AvrLpt_GetPort8(AVRLPT dev, AVRLPT_REG reg, uint8_t* byte);

#endif
