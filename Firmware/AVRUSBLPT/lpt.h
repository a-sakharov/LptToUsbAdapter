/*
 * lpt.h
 *
 * Created: 2023-03-10 19:22:02
 *  Author: a.sakharov
 */ 


#ifndef LPT_H
#define LPT_H

#include <stdint.h>

typedef enum AVRLPT_MODE_t
{
    LPT_MODE_LEGACY = 0,
    LPT_MODE_PS2 = 1,
    LPT_MODE_EPP = 2
} AVRLPT_MODE;

typedef enum AVRLPT_REG_t
{
    AVRLPT_REG_PDATA = 0,
    AVRLPT_REG_PSTAT = 1,
    AVRLPT_REG_PCON = 2,
    AVRLPT_REG_ADDSTR = 3,
    AVRLPT_REG_DATASTR = 4
} AVRLPT_REG;

void LPT_Init();
void LPT_SetMode(AVRLPT_MODE mode);
void LPT_SetReg(AVRLPT_REG reg, uint8_t value);
uint8_t LPT_GetReg(AVRLPT_REG reg);



#endif /* LPT_H_ */