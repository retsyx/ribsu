/* Copyright (C) 2007 xyster.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>

#include <IOKit/usb/IOUSBLib.h>

#include <unistd.h>

#include "printInterpretedError.h"
#include "debug.h"

#define MODULE_NAME usb
DBG_MODULE_DEFINE();

// Set this flag to get more status messages
#define VERBOSE 0

/*
 * IN Endpoint
 * 
 * The device reserves the first two bytes of data on this endpoint to contain the current
 * values of the modem and line status registers. In the absence of data, the device 
 * generates a message consisting of these two status bytes every 40 ms
 *
 * Byte 0: Modem Status
 * 
 * Offset       Description
 * B0   Reserved - must be 1
 * B1   Reserved - must be 0
 * B2   Reserved - must be 0
 * B3   Reserved - must be 0
 * B4   Clear to Send (CTS)
 * B5   Data Set Ready (DSR)
 * B6   Ring Indicator (RI)
 * B7   Receive Line Signal Detect (RLSD)
 * 
 * Byte 1: Line Status
 * 
 * Offset       Description
 * B0   Data Ready (DR)
 * B1   Overrun Error (OE)
 * B2   Parity Error (PE)
 * B3   Framing Error (FE)
 * B4   Break Interrupt (BI)
 * B5   Transmitter Holding Register (THRE)
 * B6   Transmitter Empty (TEMT)
 * B7   Error in RCVR FIFO
 * 
 */
#define FTDI_RS0_CTS    (0x10)
#define FTDI_RS0_DSR    (0x20)
#define FTDI_RS0_RI     (0x40)
#define FTDI_RS0_RLSD   (0x80)

#define FTDI_RS_DR   (0x01)
#define FTDI_RS_OE   (0x02)
#define FTDI_RS_PE   (0x04)
#define FTDI_RS_FE   (0x08)
#define FTDI_RS_BI   (0x10)
#define FTDI_RS_THRE (0x20)
#define FTDI_RS_TEMT (0x40)
#define FTDI_RS_FIFO (0x80)

/*
 Linux 
 int usb_control_msg(struct usb_device *dev, unsigned int pipe, __u8 request, __u8 requesttype,
                     __u16 value, __u16 index, void *data, __u16 size, int timeout)
 */

typedef struct usb_ctx
{
    IOUSBInterfaceInterface **intf;
    buffer buf;
    UInt8 bufbuf[64];
    void (*callback_fn)(void *, buffer *);
    void *callback_arg;
} usb_ctx;

static int getInterface(io_iterator_t interfaceIterator, IOUSBInterfaceInterface ***intf0);
static IOUSBInterfaceInterface **getUSBInterfaceInterface(io_service_t usbInterface);
static Boolean isThisTheInterfaceYoureLookingFor(IOUSBInterfaceInterface **intf);
static int openUSBInterface(IOUSBInterfaceInterface **intf);
static int initFTDI(IOUSBInterfaceInterface **intf);
static int initUIRT(usb_ctx *ctx);
static unsigned ftdi_232bm_baud_base_to_divisor(unsigned baud, int base);

static int async_read(usb_ctx *ctx);
static void usb_read_callback(void *refCon, IOReturn result, void *arg0);

CFSocketContext stdinContext;

int 
usb_add_source(void **ctx0, UInt32 vid, UInt32 pid)
{
    int error;
    IOReturn err;
    mach_port_t masterPort;
    CFMutableDictionaryRef dict;
    IOUSBInterfaceInterface **intf;
    SInt32 usbConfig, usbIntNum;
    io_iterator_t anIterator;
    usb_ctx *ctx;
        
    *ctx0 = NULL;
    
    ctx = malloc(sizeof(*ctx));
    if (!ctx)
    {
        ERR("No memory\n");
        error = -1;
        goto out;
    }
    bzero(ctx, sizeof(*ctx));
    
    buf_attach(&ctx->buf, sizeof(ctx->bufbuf), ctx->bufbuf);
   
    err = IOMasterPort(MACH_PORT_NULL, &masterPort);
    if (err != kIOReturnSuccess)
    {
        printInterpretedError("Could not get master port", err);
        error = -1;
        goto out;
    }
    
    // Attempt to detect the device. Match against the interface
    dict = IOServiceMatching("IOUSBInterface");
    
    if (dict == nil)
    {
        ERR("Could not create matching dictionary\n");
        error = -1;
        goto out;
    }
    
    // setup vid/pid to look for
	CFDictionarySetValue(dict, 
                         CFSTR(kUSBVendorID), 
                         CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &vid));
	CFDictionarySetValue(dict, CFSTR(kUSBProductID), 
                         CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pid));
    
    // Look for interface 0 in config 1.
    // These should really come from parameters.
    usbConfig = 1;
	usbIntNum = 0;
	CFDictionarySetValue(dict, CFSTR(kUSBConfigurationValue), 
                         CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &usbConfig));
	CFDictionarySetValue(dict, CFSTR(kUSBInterfaceNumber), 
                         CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &usbIntNum));
    
    // Get all the matching services
    err = IOServiceGetMatchingServices(masterPort, dict, &anIterator);
    if (err != kIOReturnSuccess)
    {
        // Do I need to release dict here, the call (if sucessfull??) consumes one, if so how??
        printInterpretedError("Could not get device iterator", err);
        error = -1;
        goto out;
    }
    
    err = getInterface(anIterator, &intf);
    
    IOObjectRelease(anIterator);

    if (err != kIOReturnSuccess)
    {
        ERR("Failed to find interface\n");
        error = -1;
        goto out;
    }
    
    // setup the FTDI bridge and the UIRT itself

    LOG("Setting up FTDI\n");
    err = initFTDI(intf);
    if (err != kIOReturnSuccess)
    {
        error = -1;
        goto out;
    }

    ctx->intf = intf;

    LOG("Setting up UIRT\n");
    err = initUIRT(ctx);
    if (err != kIOReturnSuccess)
    {
        error = -1;
        goto out;
    }
    
    *ctx0 = ctx;
    
    error = 0;
    
out:
        
    if (error)
    {
        if (ctx) free(ctx);
    }
        
    return error;
}

int
usb_set_callback(void *ctx0, void (*fn)(void *, buffer *), void *fn_arg)
{
    usb_ctx *ctx;
    
    ctx = ctx0;
    
    DBG("%p setting callback to %p %p\n", ctx, fn, fn_arg);
    
    ctx->callback_fn = fn;
    ctx->callback_arg = fn_arg;
    
    return 0;
}

int  
usb_write(void *ctx0, buffer *buf)
{
    usb_ctx *ctx;
    IOReturn kr;
    UInt32 i, ie;
    
    ctx = ctx0;
    
    kr = kIOReturnSuccess;
    
    for (i = 0; i < buf->len; i += 64)
    {
        if (i + 64 <= buf->len)
        {
            ie = 64;
        } else
        {
            ie = buf->len - i;
        }
        
        kr = (*ctx->intf)->WritePipe(ctx->intf, 2, &buf->buf[i], ie);
        if (kr != kIOReturnSuccess)
        {
            ERR("failed %u\n", (unsigned)i);
            break; 
        }    
    }
    
    return (kr ? -1 : kr);  
}

void
usb_shutdown(void *ctx0)
{
    usb_ctx *ctx;
    
    ctx = ctx0;
    
    if (!ctx) return;
    
    (*ctx->intf)->USBInterfaceClose(ctx->intf);
    (*ctx->intf)->Release(ctx->intf);
    
    free(ctx);
}

int 
getInterface(io_iterator_t interfaceIterator, IOUSBInterfaceInterface ***intf0)
{
    IOUSBInterfaceInterface **intf;
    io_service_t usbInterface;
    int err = 0;
    
    *intf0 = NULL;
    intf = NULL;
    
    usbInterface = IOIteratorNext(interfaceIterator);
    if (usbInterface == nil)
    {
        ERR("Unable to find an interface\n");
        return -1;
    }
    
    while (usbInterface != nil)
    {
        intf = getUSBInterfaceInterface(usbInterface);
        
        if (intf != nil)
        {
            // Don't release the interface here. That's one too many releases and causes set alt interface to fail
            if (isThisTheInterfaceYoureLookingFor(intf))
            {
                err = openUSBInterface(intf);
                *intf0 = intf;
                if (!err) return err;
            }
        }
        usbInterface = IOIteratorNext(interfaceIterator);
    }
    
    ERR("No interesting interfaces found\n");
    IOObjectRelease(usbInterface);
    
    return -1;
}

IOUSBInterfaceInterface **
getUSBInterfaceInterface(io_service_t usbInterface)
{
    IOReturn err;
    IOCFPlugInInterface **plugInInterface=NULL;
    IOUSBInterfaceInterface **intf=NULL;
    SInt32 score;
    HRESULT res;
    
    // There is no documentation for IOCreatePlugInInterfaceForService or QueryInterface, you have to use sample code.
	err = IOCreatePlugInInterfaceForService(usbInterface, 
                                            kIOUSBInterfaceUserClientTypeID, 
                                            kIOCFPlugInInterfaceID,
                                            &plugInInterface, 
                                            &score);
    (void)IOObjectRelease(usbInterface);				// done with the usbInterface object now that I have the plugin
    if ((kIOReturnSuccess != err) || (plugInInterface == nil) )
    {
        printInterpretedError("Unable to create plug in interface for USB interface", err);
        return (nil);
    }
    
    res = (*plugInInterface)->QueryInterface(plugInInterface, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID), (LPVOID)&intf);
    (void)(*plugInInterface)->Release(plugInInterface);			// done with this
    if (res  ||  !intf)
    {
        ERR("Unable to create interface with QueryInterface %lX\n", res);
        return (nil);
    }
    
    return (intf);
}

Boolean 
isThisTheInterfaceYoureLookingFor(IOUSBInterfaceInterface **intf)
{
    //	Check to see if this is the interface you're interested in
    //  This code is only expecting one interface, so returns true
    //  the first time.
    //  You code could check the nature and type of endpoints etc
    
    static Boolean foundOnce  = false;
    if (foundOnce)
    {
        LOG("Subsequent interface found, we're only intersted in 1 of them\n");
        return false;
    }
    
    foundOnce = true;
    return true;
}

int 
openUSBInterface(IOUSBInterfaceInterface **intf)
{
    IOReturn ret;
    
#if VERBOSE
    UInt8 n;
    int i;
    UInt8 direction;
    UInt8 number;
    UInt8 transferType;
    UInt16 maxPacketSize;
    UInt8 interval;
    static char *types[]={
        "Control",
        "Isochronous",
        "Bulk",
        "Interrupt"};
    static char *directionStr[]={
        "In",
        "Out",
        "Control"};
#endif
    
	ret = (*intf)->USBInterfaceOpen(intf);
    if (ret != kIOReturnSuccess)
    {
        printInterpretedError("Could not set configuration on device", ret);
        return -1;
    }
	
#if VERBOSE
    // We don't use the endpoints in our device, but it has some anyway
    
    ret = (*intf)->GetNumEndpoints(intf, &n);
    if (ret != kIOReturnSuccess)
    {
        printInterpretedError("Could not get number of endpoints in interface", ret);
        return -1;
    }
    
    LOG("%d endpoints found\n", n);
    
    for (i = 1; i<=n; i++)
    {
        ret = (*intf)->GetPipeProperties(intf, i, &direction, &number, &transferType, &maxPacketSize, &interval);
        if (ret != kIOReturnSuccess)
        {
            ERR("Endpoint %d -", n);
            printInterpretedError("Could not get endpoint properties", ret);
            return -1;
        }
        LOG("Endpoint %d: %s %s %d, max packet %d, interval %d\n", i, types[transferType], directionStr[direction], number, maxPacketSize, interval);
    }
#endif
    
    return 0;
}

unsigned 
ftdi_232bm_baud_base_to_divisor(unsigned baud, int base)
{
    static const unsigned char divfrac[8] = { 0, 3, 2, 4, 1, 5, 6, 7 };
    unsigned divisor;
    int divisor3 = base / 2 / baud; // divisor shifted 3 bits to the left
    divisor = divisor3 >> 3;
    divisor |= (unsigned)divfrac[divisor3 & 0x7] << 14;
    /* Deal with special cases for highest baud rates. */
    if (divisor == 1) divisor = 0; else     // 1.0
        if (divisor == 0x4001) divisor = 1;     // 1.5
    return divisor;
}

int 
initFTDI(IOUSBInterfaceInterface **intf)
{
    IOReturn kr;  
    IOUSBDevRequest req;
    unsigned baud;
    
    bzero(&req, sizeof(req));
    
    // FTDI serial/USB chip initialization 
    req.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBVendor, kUSBDevice);
    req.bRequest = 0; // Reset
    req.wValue = 0; // Reset
    req.wIndex = 0;
    req.wLength = 0;
    req.pData = NULL;
    kr = (*intf)->ControlRequest(intf, 0, &req);
    
    // clear DTR
    req.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBVendor, kUSBDevice);
    req.bRequest = 1; // Set modem control register
    //req.wValue = (1 | (1 << 8)); // to clear DTR use (1 << 8)
    req.wValue = (1 << 8); // clear DTR
    req.wIndex = 0;
    req.wLength = 0;
    req.pData = NULL;
    kr = (*intf)->ControlRequest(intf, 0, &req);

    // set RTS
    req.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBVendor, kUSBDevice);
    req.bRequest = 1; // Set modem control register
    req.wValue = (2 | (2 << 8)); // to clear RTS use (2 << 8)
    req.wIndex = 0;
    req.wLength = 0;
    req.pData = NULL;
    kr = (*intf)->ControlRequest(intf, 0, &req);
    
    if (kr != kIOReturnSuccess)
    {
        ERR("Failed with error %Xh\n", kr);
        return -1;
    }
    
    #define FTDI_SIO_SET_DATA_PARITY_NONE (0x0 << 8 )
    #define FTDI_SIO_SET_DATA_PARITY_ODD (0x1 << 8 )
    #define FTDI_SIO_SET_DATA_PARITY_EVEN (0x2 << 8 )
    #define FTDI_SIO_SET_DATA_PARITY_MARK (0x3 << 8 )
    #define FTDI_SIO_SET_DATA_PARITY_SPACE (0x4 << 8 )
    #define FTDI_SIO_SET_DATA_STOP_BITS_1 (0x0 << 11 )
    #define FTDI_SIO_SET_DATA_STOP_BITS_15 (0x1 << 11 )
    #define FTDI_SIO_SET_DATA_STOP_BITS_2 (0x2 << 11 )
    #define FTDI_SIO_SET_BREAK (0x1 << 14)
    // set data bits, stop bits and parity
    req.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBVendor, kUSBDevice);
    req.bRequest = 4; // set data characteristics of port
    // OR FTDIO_SIO defines with 5, 6, 7, 8 for data bit numbers.
    req.wValue = FTDI_SIO_SET_DATA_STOP_BITS_1 | FTDI_SIO_SET_DATA_PARITY_NONE | 8;
    req.wIndex = 0;
    req.wLength = 0;
    req.pData = NULL;
    kr = (*intf)->ControlRequest(intf, 0, &req);
    if (kr != kIOReturnSuccess)
    {
        ERR("Failed to set data/stop/parity bits with error %Xh\n", kr); 
        return -1;
    } 
    
    baud = 312500; // this is the only one that works with USB-UIRT
    
    baud = ftdi_232bm_baud_base_to_divisor(baud, 48000000);
    
    // baud rate
    req.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBVendor, kUSBDevice);
    req.bRequest = 3; // set baud rate
    req.wValue = (unsigned short)baud; 
    req.wIndex = (unsigned short)(baud >> 16);
    req.wLength = 0;
    req.pData = NULL;
    kr = (*intf)->ControlRequest(intf, 0, &req);
    if (kr != kIOReturnSuccess)
    {
        ERR("Failed to set baud rate with error %Xh\n", kr);
        return -1;
    } 

    return 0;
}
    
int 
initUIRT(usb_ctx *ctx)
{
    IOReturn kr;
    mach_port_t             masterPort;
    IONotificationPortRef   notifyPort;
    CFRunLoopSourceRef      runLoopSource;

    // Initialize async I/O
    
    kr = IOMasterPort(MACH_PORT_NULL, &masterPort);
    if (kr != kIOReturnSuccess)
    {
        printInterpretedError("Could not get master port", kr);
        return -1;
    }
        
    notifyPort = IONotificationPortCreate(masterPort);
    runLoopSource = IONotificationPortGetRunLoopSource(notifyPort);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopSource, 
                       kCFRunLoopDefaultMode);
        
        
    kr = (*ctx->intf)->CreateInterfaceAsyncEventSource(ctx->intf, &runLoopSource);
    if (kr != kIOReturnSuccess)
    {
        ERR("Failed to create run loop source %08X\n", kr);
        (void) (*ctx->intf)->USBInterfaceClose(ctx->intf);
        (void) (*ctx->intf)->Release(ctx->intf);
        return -1;   
    }
        
    CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopSource,
                       kCFRunLoopDefaultMode);
    DBG("Added async event source\n");
    bzero(ctx->buf.buf, ctx->buf.max);

    // Prime the async reads
    async_read(ctx);
    
    return 0;
}

int 
async_read(usb_ctx *ctx)
{
    IOReturn kr;
    
    kr = (*ctx->intf)->ReadPipeAsync(ctx->intf, 1, ctx->buf.buf, ctx->buf.max, usb_read_callback, ctx);
    if (kr != kIOReturnSuccess)
    {
      ERR("Async read failed with error %08Xh\n", kr); 
      return -1;
    }
    
  return 0;  
 }

void 
usb_read_callback(void *refCon, IOReturn result, void *arg0)
{
    usb_ctx *ctx = refCon;
    UInt32  i, size = (UInt32) arg0;
        
    if (result != kIOReturnSuccess) 
    {
        ERR("error from async bulk read (%08Xh)\n", result);
    }
     
    if (size > 2)
    {
        // shift the buffer to get rid of the two bytes
        bcopy(&ctx->buf.buf[2], ctx->buf.buf, size - 2); 
        ctx->buf.len = size - 2;
        
        DMP("%p Got some data, callback %p %p\n", ctx, ctx->callback_fn, ctx->callback_arg);
        if (ctx->callback_fn)
        {
            ctx->callback_fn(ctx->callback_arg, &ctx->buf);
        }
    }
    
    i = async_read(ctx);
    if (i) 
    {
        ERR("Error reading from USB device\n");
        CFRunLoopStop(CFRunLoopGetCurrent());
    }
}



