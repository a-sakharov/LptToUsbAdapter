/*
 * lpt.c
 *
 * Created: 2023-03-10 19:22:21
 *  Author: a.sakharov
 */ 

#include "lpt.h"
#include <avr/io.h>
#include <stdbool.h>
#include <util/delay.h>


/*
| Net             | Pin |
|-----------------|-----|
| D0              | PC4 |
| D1              | PC3 |
| D2              | PC2 |
| D3              | PC1 |
| D4              | PC0 |
| D5              | PB5 |
| D6              | PB4 |
| D7              | PB3 |
|                 |     |
| nSTROBE         | PC5 |
| nLINE_FEED      | PD0 |
| RESET           | PD3 |
| nSELECT_PRINTER | PD5 |
|                 |     |
| ERROR           | PD1 |
| ACK             | PB2 |
| nBUSY           | PB1 |
| PAPER_OUT       | PD6 |
| SELECT          | PD7 |
*/

//status
#define GET_ERROR()     (PIND & _BV(1))
#define GET_ACK()       (PINB & _BV(2))
#define GET_nBUSY()     (PINB & _BV(1))
#define GET_PAPER_OUT() (PIND & _BV(6))
#define GET_SELECT()    (PIND & _BV(7))

#define IN_ERROR()      (DDRD &= ~_BV(1))
#define IN_ACK()        (DDRB &= ~_BV(2))
#define IN_nBUSY()      (DDRB &= ~_BV(1))
#define IN_PAPER_OUT()  (DDRD &= ~_BV(6))
#define IN_SELECT()     (DDRD &= ~_BV(7))

//control
#define SET_nSTROBE(x)          if(x) PORTC |= _BV(5); else PORTC &= ~_BV(5)
#define SET_nLINE_FEED(x)       if(x) PORTD |= _BV(0); else PORTD &= ~_BV(0)
#define SET_RESET(x)            if(x) PORTD |= _BV(3); else PORTD &= ~_BV(3)
#define SET_nSELECT_PRINTER(x)  if(x) PORTD |= _BV(5); else PORTD &= ~_BV(5)

#define OUT_nSTROBE()          DDRC |= _BV(5)
#define OUT_nLINE_FEED()       DDRD |= _BV(0)
#define OUT_RESET()            DDRD |= _BV(3)
#define OUT_nSELECT_PRINTER()  DDRD |= _BV(5)

//data
#define OUT_D0() DDRC |= _BV(4)
#define OUT_D1() DDRC |= _BV(3)
#define OUT_D2() DDRC |= _BV(2)
#define OUT_D3() DDRC |= _BV(1)
#define OUT_D4() DDRC |= _BV(0)
#define OUT_D5() DDRB |= _BV(5)
#define OUT_D6() DDRB |= _BV(4)
#define OUT_D7() DDRB |= _BV(3)

#define IN_D0() (DDRC &= ~_BV(4))
#define IN_D1() (DDRC &= ~_BV(3))
#define IN_D2() (DDRC &= ~_BV(2))
#define IN_D3() (DDRC &= ~_BV(1))
#define IN_D4() (DDRC &= ~_BV(0))
#define IN_D5() (DDRB &= ~_BV(5))
#define IN_D6() (DDRB &= ~_BV(4))
#define IN_D7() (DDRB &= ~_BV(3))

#define OUT_DATA() OUT_D0(),OUT_D1(),OUT_D2(),OUT_D3(),OUT_D4(),OUT_D5(),OUT_D6(),OUT_D7()
#define IN_DATA()  IN_D0(),IN_D1(),IN_D2(),IN_D3(),IN_D4(),IN_D5(),IN_D6(),IN_D7()

#define SET_D0(x) if(x) PORTC |= _BV(4); else PORTC &= ~_BV(4)
#define SET_D1(x) if(x) PORTC |= _BV(3); else PORTC &= ~_BV(3)
#define SET_D2(x) if(x) PORTC |= _BV(2); else PORTC &= ~_BV(2)
#define SET_D3(x) if(x) PORTC |= _BV(1); else PORTC &= ~_BV(1)
#define SET_D4(x) if(x) PORTC |= _BV(0); else PORTC &= ~_BV(0)
#define SET_D5(x) if(x) PORTB |= _BV(5); else PORTB &= ~_BV(5)
#define SET_D6(x) if(x) PORTB |= _BV(4); else PORTB &= ~_BV(4)
#define SET_D7(x) if(x) PORTB |= _BV(3); else PORTB &= ~_BV(3)

#define GET_D0() (PINC & _BV(4))
#define GET_D1() (PINC & _BV(3))
#define GET_D2() (PINC & _BV(2))
#define GET_D3() (PINC & _BV(1))
#define GET_D4() (PINC & _BV(0))
#define GET_D5() (PINB & _BV(5))
#define GET_D6() (PINB & _BV(4))
#define GET_D7() (PINB & _BV(3))


static volatile AVRLPT_MODE ActiveMode;

//store last written values
static volatile uint8_t PCON;
static volatile uint8_t PDATA;
static volatile uint8_t PSTAT;
static volatile uint8_t ADDSTR;
static volatile uint8_t DATASTR;

static uint8_t DataGet()
{
    uint8_t value = 0;
    
    if(GET_D0()) value |= _BV(0);
    if(GET_D1()) value |= _BV(1);
    if(GET_D2()) value |= _BV(2);
    if(GET_D3()) value |= _BV(3);
    if(GET_D4()) value |= _BV(4);
    if(GET_D5()) value |= _BV(5);
    if(GET_D6()) value |= _BV(6);
    if(GET_D7()) value |= _BV(7);
    
    return value;
}

static void DataSet(uint8_t value)
{
    SET_D0(value & _BV(0));
    SET_D1(value & _BV(1));
    SET_D2(value & _BV(2));
    SET_D3(value & _BV(3));
    SET_D4(value & _BV(4));
    SET_D5(value & _BV(5));
    SET_D6(value & _BV(6));
    SET_D7(value & _BV(7));
}

static bool EppWrite(uint8_t data, bool AddrMode)
{
    bool success = true;
    
    if(GET_nBUSY() != 0)
    {
        return false;
    }
    
    //setup
    OUT_DATA();
    
    //perform
    SET_nSTROBE(0); //write=0
    DataSet(data); //DATA set
    
    SET_nLINE_FEED(AddrMode==true); //set data strobe 
    SET_nSELECT_PRINTER(AddrMode==false); //set adddr strobe
    
    _delay_us(1);
    
    //waiting for WAIT#
    uint8_t timeout = 10;
    while(timeout != 0 && GET_nBUSY() == 0)
    {
        timeout--;
        _delay_us(1);
    }
    
    if(GET_nBUSY() == 0)
    {
        success = false;
    }
    
    SET_nLINE_FEED(1); //set data strobe
    SET_nSELECT_PRINTER(1); //set adddr strobe
    SET_nSTROBE(1); //write=1
    
    //unsetup
    if(!(PCON & _BV(5)))
    {
        //pdata output
        OUT_DATA();
    }
    else
    {
        //pdata input
        IN_DATA();
    }
    
    return success;
}

static bool EppRead(uint8_t *data, bool AddrMode)
{
    bool success = true;
    
    if(GET_nBUSY() != 0)
    {
        return false;
    }
        
    //setup
    IN_DATA();
    
    //perform
    SET_nSTROBE(1); //write=1
    
    SET_nLINE_FEED(AddrMode==true); //set data strobe
    SET_nSELECT_PRINTER(AddrMode==false); //set adddr strobe
    
    //waiting for WAIT#
    uint8_t timeout = 10;
    while(timeout != 0 && GET_nBUSY() == 0)
    {
        timeout--;
        _delay_us(1);
    }
    
    if(GET_nBUSY() == 0)
    {
        success = false;
    }
    else
    {
        *data = DataGet();
    }
    
    SET_nLINE_FEED(1); //set data strobe
    SET_nSELECT_PRINTER(1); //set addr strobe
    
    //unsetup
    if(GET_nBUSY() != 0)
    {
        PCON |= _BV(5); //bad idea but maybe safe
    }
    
    return success;
}

void LPT_Init()
{
    if((ActiveMode == LPT_MODE_LEGACY) || !(PCON & _BV(5)))
    {
        //pdata output
        OUT_DATA();
    }
    else
    {
        //pdata input
        IN_DATA();
    }
    
    //conrol always output
    OUT_nSTROBE();
    OUT_nLINE_FEED();
    OUT_RESET();
    OUT_nSELECT_PRINTER();
    
    //status always input
    IN_ERROR();
    IN_ACK();
    IN_nBUSY();
    IN_PAPER_OUT();
    IN_SELECT();
}

void LPT_SetMode(AVRLPT_MODE mode)
{
    if(mode == ActiveMode)
    {
        return;
    }
    
    ActiveMode = mode;
    
    LPT_Init();
}

void LPT_SetReg(AVRLPT_REG reg, uint8_t value)
{
    switch(reg)
    {
        case AVRLPT_REG_PDATA:
        if((ActiveMode == LPT_MODE_LEGACY) || !(PCON & _BV(5)))//if DATA is output 
        {
            DataSet(value);
            PDATA = value;
        }
        break;
        
        case AVRLPT_REG_PSTAT:
        //PSTAT = value; //read-only reg
        break;
        
        case AVRLPT_REG_PCON:
        if(ActiveMode == LPT_MODE_LEGACY)
        {
            PCON = value & 0x1F;
        }
        else
        {
            PCON = value & 0x3F;
            if(PCON & _BV(5))
            {
                IN_DATA();
            }
            else
            {
                OUT_DATA();
            }
        }
        
        SET_nSTROBE(!(value & _BV(0)));//inverted
        SET_nLINE_FEED(!(value & _BV(1)));//inverted
        SET_RESET(value & _BV(2));
        SET_nSELECT_PRINTER(!(value & _BV(3)));//inverted
        break;
        
        case AVRLPT_REG_ADDSTR:
        if(ActiveMode == LPT_MODE_EPP)
        {
            ADDSTR = value;
            if(!EppWrite(ADDSTR, true))
            {
                PSTAT |= 1;
            }
        }
        break;
        
        case AVRLPT_REG_DATASTR:
        if(ActiveMode == LPT_MODE_EPP)
        {
            ADDSTR = value;
            if(!EppWrite(ADDSTR, false))
            {
                PSTAT |= 1;
            }
        }

        break;
    }
}

uint8_t LPT_GetReg(AVRLPT_REG reg)
{
    uint8_t value = 0;
    
    switch(reg)
    {
        case AVRLPT_REG_PDATA:
        if((ActiveMode == LPT_MODE_LEGACY) || !(PCON & _BV(5)))//if DATA is output
        {
            return PDATA;
        }
        else
        {
            return DataGet();
        }
        break;
        
        case AVRLPT_REG_PSTAT:
        value = PSTAT & 0x01; //save time-out bit
        PSTAT = 0; //clear time-out bit
        if(!GET_nBUSY()) value |= _BV(7);//inverted
        if(GET_ACK()) value |= _BV(6);
        if(GET_PAPER_OUT()) value |= _BV(5);
        if(GET_SELECT()) value |= _BV(4);
        if(GET_ERROR()) value |= _BV(3);
        return value;
        break;
        
        case AVRLPT_REG_PCON:
        return PCON;
        break;
        
        case AVRLPT_REG_ADDSTR:
        if(!EppRead(&value, true))
        {
            PSTAT |= 0x01;
        }
        return value;
        break;
        
        case AVRLPT_REG_DATASTR:
        if(!EppRead(&value, false))
        {
            PSTAT |= 0x01;
        }
        return value;
        break;
    }
    
    return 0;
}
