/* Name: main.c
 * Project: hid-data, example how to use HID for data transfer
 * Author: Christian Starkjohann
 * Creation Date: 2008-04-11
 * Tabsize: 4
 * Copyright: (c) 2008 by OBJECTIVE DEVELOPMENT Software GmbH
 * License: GNU GPL v2 (see License.txt), GNU GPL v3 or proprietary (CommercialLicense.txt)
 */

/*
This example should run on most AVRs with only little changes. No special
hardware resources except INT0 are used. You may have to change usbconfig.h for
different I/O pins for USB. Please note that USB D+ must be the INT0 pin, or
at least be connected to INT0 as well.
*/

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>  /* for sei() */
#include <util/delay.h>     /* for _delay_ms() */
#include <avr/eeprom.h>

#include <avr/pgmspace.h>   /* required by usbdrv.h */
#include "usbdrv.h"
#include "oddebug.h"        /* This is also an example for using debug macros */
#include "build_defs.h"
#include "lpt.h"

/* ------------------------------------------------------------------------- */
/* ----------------------------- USB interface ----------------------------- */
/* ------------------------------------------------------------------------- */

//PROGMEM const char usbHidReportDescriptor[22] = {    /* USB report descriptor */
//    0x06, 0x00, 0xff,              // USAGE_PAGE (Generic Desktop)
//    0x09, 0x01,                    // USAGE (Vendor Usage 1)
//    0xa1, 0x01,                    // COLLECTION (Application)
//    0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
//    0x26, 0xff, 0x00,              //   LOGICAL_MAXIMUM (255)
//    0x75, 0x08,                    //   REPORT_SIZE (8)
//    0x95, 0x80,                    //   REPORT_COUNT (128)
//    0x09, 0x00,                    //   USAGE (Undefined)
//    0xb2, 0x02, 0x01,              //   FEATURE (Data,Var,Abs,Buf)
//    0xc0                           // END_COLLECTION
//};

/* Since we define only one feature report, we don't use report-IDs (which
 * would be the first byte of the report). The entire report consists of 128
 * opaque data bytes.
 */

/* The following variables store the status of the current data transfer */
static uchar    currentAddress;
static uchar    bytesRemaining;

/* ------------------------------------------------------------------------- */

/* usbFunctionRead() is called when the host requests a chunk of data from
 * the device. For more information see the documentation in usbdrv/usbdrv.h.
 */
uchar   usbFunctionRead(uchar *data, uchar len)
{
    if(len > bytesRemaining)
        len = bytesRemaining;
    eeprom_read_block(data, (uchar *)0 + currentAddress, len);
    currentAddress += len;
    bytesRemaining -= len;
    return len;
}

/* usbFunctionWrite() is called when the host sends a chunk of data to the
 * device. For more information see the documentation in usbdrv/usbdrv.h.
 */
uchar   usbFunctionWrite(uchar *data, uchar len)
{
    if(bytesRemaining == 0)
        return 1;               /* end of transfer */
    if(len > bytesRemaining)
        len = bytesRemaining;
    eeprom_write_block(data, (uchar *)0 + currentAddress, len);
    currentAddress += len;
    bytesRemaining -= len;
    return bytesRemaining == 0; /* return 1 if this was the last chunk */
}

/* ------------------------------------------------------------------------- */
typedef struct msUsbExtCompatHeader_t
{
    uint32_t dwLength;
    uint16_t bcdVersion;
    uint16_t wIndex;
    uint8_t bCount;
    uint8_t reserved[7];
} msUsbExtCompatHeader;

typedef struct usbExtCompatDescriptor_t
{
    msUsbExtCompatHeader header;
    uint8_t bFirstInterfaceNumber;
    uint8_t reserved1;
    char compatibleID[8];
    char subCompatibleID[8];
    uint8_t reserved2[6];
} msUsbExtCompatDescriptor;


static const msUsbExtCompatDescriptor msExtCompatDescriptor =
{
    .header = { 
        .dwLength = sizeof(msUsbExtCompatDescriptor), 
        .bcdVersion = 0x0100, 
        .wIndex = 0x0004, 
        .bCount = 1 
        },
    .bFirstInterfaceNumber = 0,
    .reserved1 = 1,
    .compatibleID = "WINUSB\0",
    .subCompatibleID = "\0\0\0\0\0\0\0"
};

typedef struct UsbLptVersion
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
} UsbLptVersion;

UsbLptVersion Version;

typedef enum AVRLPT_COMMAND_t
{
    USBLPT_GET_VERSION = 0x07,
    USBLPT_SET_MODE = 0x08,
    USBLPT_SET_REG = 0x09,
    USBLPT_GET_REG = 0x0a
} AVRLPT_COMMAND;


volatile static uint8_t IoByte;

usbMsgLen_t usbFunctionSetup(uchar data[8])
{
    usbRequest_t    *rq = (void *)data;
    
    if((rq->bmRequestType & 0x7F) != 0x40) //only vendor requests
    {
        return 0;
    }
    
    switch(rq->bRequest)
    {
        case GET_MS_DESCRIPTOR:
        {
            if (rq->wIndex.word == 0x0004)
            {
                usbMsgPtr = (usbMsgPtr_t)&msExtCompatDescriptor;
                return sizeof(msExtCompatDescriptor);
            }
            return 0;
        }
        break;
        
        case USBLPT_GET_VERSION:
        {
            usbMsgPtr = (usbMsgPtr_t)&Version;
            return sizeof(UsbLptVersion);
        }
        break;
        
        case USBLPT_SET_MODE:
        {
            if(rq->wValue.word > LPT_MODE_EPP)
            {
                rq->wValue.word = LPT_MODE_EPP;//LPT_MODE_EPP if invalid
            }
            
            LPT_SetMode((uint8_t)(rq->wValue.word));
            return 0;
        }
        break;
        
        case USBLPT_SET_REG:
        {
            if(rq->wValue.word <= AVRLPT_REG_DATASTR)
            {
                LPT_SetReg((uint8_t)(rq->wValue.word), (uint8_t)(rq->wIndex.word));    
            }
            return 0;
        }
        break;
        
        case USBLPT_GET_REG:
        {
            if(rq->wValue.word <= AVRLPT_REG_DATASTR)
            {
                IoByte = LPT_GetReg((uint8_t)(rq->wValue.word));  
            }
            else
            {
                IoByte = 0;
            }
            usbMsgPtr = (usbMsgPtr_t)&IoByte;
            return sizeof(IoByte);
        }
        break;
        
        default:
        {
            return 0;
        }
        break;
    }


    return 0;
}

/* ------------------------------------------------------------------------- */

int main(void)
{
    uchar   i;


    Version.revision = 0;
    
    Version.build_date.day = BUILD_DAY;
    Version.build_date.month = BUILD_MONTH;
    Version.build_date.year = BUILD_YEAR;
    
    Version.build_time.hour = BUILD_HOUR;
    Version.build_time.minute = BUILD_MIN;
    Version.build_time.second = BUILD_SEC;

    LPT_SetMode(LPT_MODE_LEGACY);
    LPT_Init();

    wdt_enable(WDTO_1S);
    /* Even if you don't use the watchdog, turn it off here. On newer devices,
     * the status of the watchdog (on/off, period) is PRESERVED OVER RESET!
     */
    /* RESET status: all port bits are inputs without pull-up.
     * That's the way we need D+ and D-. Therefore we don't need any
     * additional hardware initialization.
     */
    odDebugInit();
    DBG1(0x00, 0, 0);       /* debug output: main starts */
    usbInit();
    usbDeviceDisconnect();  /* enforce re-enumeration, do this while interrupts are disabled! */
    i = 0;
    while(--i){             /* fake USB disconnect for > 250 ms */
        wdt_reset();
        _delay_ms(1);
    }
    usbDeviceConnect();
    sei();
    DBG1(0x01, 0, 0);       /* debug output: main loop starts */
    for(;;){                /* main event loop */
        DBG1(0x02, 0, 0);   /* debug output: main loop iterates */
        wdt_reset();
        usbPoll();
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
