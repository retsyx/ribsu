/* Copyright (C) 2007 xyster.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <paths.h>
#include <termios.h>
#include <sysexits.h>
#include <sys/param.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>

#ifdef __MWERKS__
#define __CF_USE_FRAMEWORK_INCLUDES__
#endif

#include <CoreFoundation/CoreFoundation.h>

#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>
#include <IOKit/IOBSD.h>

#include "debug.h"
#include "ribsu-util.h"
#include "tty.h"

#define MODULE_NAME tty
DBG_MODULE_DEFINE();

typedef struct tty_ctx
{
    int fd;
    FILE *fp;
    void (*callback_fn)(void *, buffer *);
    void *callback_arg;  
} tty_ctx;

// Hold the original termios attributes so we can reset them
static struct termios gOriginalTTYAttrs;

// Function prototypes
static void tty_read_callback(CFSocketRef s, 
                              CFSocketCallBackType callbackType, 
                              CFDataRef address, 
                              const void *data, 
                              void *info);
static kern_return_t FindModems(io_iterator_t *matchingServices);
static kern_return_t GetModemPath(io_iterator_t serialPortIterator, buffer *dev_name);
static int OpenSerialPort(const char *bsdPath);
static void CloseSerialPort(int fileDescriptor);


int 
tty_find_device(buffer *dev_name)
{
    kern_return_t	kernResult;
    io_iterator_t	serialPortIterator;
    
    kernResult = FindModems(&serialPortIterator);
    if (kernResult != KERN_SUCCESS)
    {
        return -1;
    }
    
    kernResult = GetModemPath(serialPortIterator, dev_name);
    
    IOObjectRelease(serialPortIterator);	// Release the iterator.
    
    if (kernResult != KERN_SUCCESS)
    {
        return -1;
    }
    
    return 0;
}

int 
tty_add_source(void **ctx0, buffer *dev_name)
{
    tty_ctx *ctx;
    int error;
    
    *ctx0 = NULL;
    
    ctx = malloc(sizeof(*ctx));
    if (!ctx)
    {
        ERR("No memory\n");
        error = -1;
        goto out;
    }
    
    ctx->fd = OpenSerialPort((char *)dev_name->buf);
    if (ctx->fd < 0)
    {
        ERR("Failed to open device %s\n", dev_name->buf);
        error = -1;
        goto out;
    }
    
    add_fd_source(ctx->fd, &ctx->fp, tty_read_callback, ctx);
    
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
tty_set_callback(void *ctx0, void (*fn)(void *, buffer *), void *fn_arg)
{
    tty_ctx *ctx;
    
    ctx = ctx0;
    
    ctx->callback_fn = fn;
    ctx->callback_arg = fn_arg;
    
    return 0;
}

void 
tty_read_callback(CFSocketRef s, 
                  CFSocketCallBackType callbackType, 
                  CFDataRef address, 
                  const void *data, 
                  void *info)
{
    tty_ctx *ctx;
    buffer *raw;
    int n;
    
    raw = buf_alloc(512);
    
    ctx = info;
    
    n = fread(raw->buf, 1, raw->max, ctx->fp);
    if (n < 1)
    {
        ERR("Failed to read from device\n");
        return;
    }
    raw->len = n;
    
    if (ctx->callback_fn)
    {
        ctx->callback_fn(ctx->callback_arg, raw);
    }
    
    buf_free(raw);
}

int
tty_write(void *ctx0, buffer *buf)
{
    tty_ctx *ctx;
    int n, sent;
    
    ctx = ctx0;
    
    n = sent = 0;
    do {
        n = write(ctx->fd, &buf->buf[sent], buf->len - sent);
        if (n < 0)
        {
           return -1;
        }
        sent += n;
    } while ((UInt32)sent < buf->len);
        
    return 0;
}

void
tty_shutdown(void *ctx0)
{
    tty_ctx *ctx;
    
    ctx = ctx0;
    
    CloseSerialPort(ctx->fd);
    
    fclose(ctx->fp);
    
    free(ctx);
}


// Returns an iterator across all known modems. Caller is responsible for
// releasing the iterator when iteration is complete.
static kern_return_t FindModems(io_iterator_t *matchingServices)
{
    kern_return_t		kernResult; 
    mach_port_t			masterPort;
    CFMutableDictionaryRef	classesToMatch;

/*! @function IOMasterPort
    @abstract Returns the mach port used to initiate communication with IOKit.
    @discussion Functions that don't specify an existing object require the IOKit master port to be passed. This function obtains that port.
    @param bootstrapPort Pass MACH_PORT_NULL for the default.
    @param masterPort The master port is returned.
    @result A kern_return_t error code. */

    kernResult = IOMasterPort(MACH_PORT_NULL, &masterPort);
    if (KERN_SUCCESS != kernResult)
    {
        ERR("IOMasterPort returned %d\n", kernResult);
        goto exit;
    }
        
/*! @function IOServiceMatching
    @abstract Create a matching dictionary that specifies an IOService class match.
    @discussion A very common matching criteria for IOService is based on its class. IOServiceMatching will create a matching dictionary that specifies any IOService of a class, or its subclasses. The class is specified by C-string name.
    @param name The class name, as a const C-string. Class matching is successful on IOService's of this class or any subclass.
    @result The matching dictionary created, is returned on success, or zero on failure. The dictionary is commonly passed to IOServiceGetMatchingServices or IOServiceAddNotification which will consume a reference, otherwise it should be released with CFRelease by the caller. */

    // Serial devices are instances of class IOSerialBSDClient
    classesToMatch = IOServiceMatching(kIOSerialBSDServiceValue);
    if (classesToMatch == NULL)
    {
        ERR("IOServiceMatching returned a NULL dictionary.\n");
    }
    else {
/*!
	@function CFDictionarySetValue
	Sets the value of the key in the dictionary.
	@param theDict The dictionary to which the value is to be set. If this
		parameter is not a valid mutable CFDictionary, the behavior is
		undefined. If the dictionary is a fixed-capacity dictionary and
		it is full before this operation, and the key does not exist in
		the dictionary, the behavior is undefined.
	@param key The key of the value to set into the dictionary. If a key 
		which matches this key is already present in the dictionary, only
		the value is changed ("add if absent, replace if present"). If
		no key matches the given key, the key-value pair is added to the
		dictionary. If added, the key is retained by the dictionary,
		using the retain callback provided
		when the dictionary was created. If the key is not of the sort
		expected by the key retain callback, the behavior is undefined.
	@param value The value to add to or replace into the dictionary. The value
		is retained by the dictionary using the retain callback provided
		when the dictionary was created, and the previous value if any is
		released. If the value is not of the sort expected by the
		retain or release callbacks, the behavior is undefined.
*/
        CFDictionarySetValue(classesToMatch,
                             CFSTR(kIOSerialBSDTypeKey),
                             CFSTR(kIOSerialBSDRS232Type));
        // Each serial device object has a property with key
        // kIOSerialBSDTypeKey and a value that is one of kIOSerialBSDAllTypes,
        // kIOSerialBSDModemType, or kIOSerialBSDRS232Type. You can experiment with the
        // matching by changing the last parameter in the above call to CFDictionarySetValue.
       
        // As shipped, this sample is only interested in modems,
        // so add this property to the CFDictionary we're matching on. 
        // This will find devices that advertise themselves as modems,
        // such as built-in and USB modems. However, this match won't find serial modems.
    }
    
    /*! @function IOServiceGetMatchingServices
        @abstract Look up registered IOService objects that match a matching dictionary.
        @discussion This is the preferred method of finding IOService objects currently registered by IOKit. IOServiceAddNotification can also supply this information and install a notification of new IOServices. The matching information used in the matching dictionary may vary depending on the class of service being looked up.
        @param masterPort The master port obtained from IOMasterPort().
        @param matching A CF dictionary containing matching information, of which one reference is consumed by this function. IOKitLib can contruct matching dictionaries for common criteria with helper functions such as IOServiceMatching, IOOpenFirmwarePathMatching.
        @param existing An iterator handle is returned on success, and should be released by the caller when the iteration is finished.
        @result A kern_return_t error code. */

    kernResult = IOServiceGetMatchingServices(masterPort, classesToMatch, matchingServices);    
    if (KERN_SUCCESS != kernResult)
    {
        ERR("IOServiceGetMatchingServices returned %d\n", kernResult);
        goto exit;
    }
        
exit:
    return kernResult;
}
    
// Given an iterator across a set of modems, return the BSD path to the first one.
// If no modems are found the path name is set to an empty string.
static kern_return_t GetModemPath(io_iterator_t serialPortIterator, buffer *dev_name)
{
    io_object_t		modemService;
    kern_return_t	kernResult = KERN_FAILURE;
    Boolean		modemFound = false;
    
    // Initialize the returned path
    *dev_name->buf = '\0';
    
    // Iterate across all modems found. In this example, we bail after finding the first modem.
    
    while ((modemService = IOIteratorNext(serialPortIterator)) && !modemFound)
    {
        CFTypeRef	bsdPathAsCFString;
        
        // Get the callout device's path (/dev/cu.xxxxx). The callout device should almost always be
        // used: the dialin device (/dev/tty.xxxxx) would be used when monitoring a serial port for
        // incoming calls, e.g. a fax listener.
        
        bsdPathAsCFString = IORegistryEntryCreateCFProperty(modemService,
                                                            CFSTR(kIOCalloutDeviceKey),
                                                            kCFAllocatorDefault,
                                                            0);
        if (bsdPathAsCFString)
        {
            Boolean result;
            
            // Convert the path from a CFString to a C (NUL-terminated) string for use
            // with the POSIX open() call.
            
            result = CFStringGetCString(bsdPathAsCFString,
                                        (char *)dev_name->buf,
                                        dev_name->max, 
                                        kCFStringEncodingASCII);
            CFRelease(bsdPathAsCFString);
            dev_name->len = strlen((char *)dev_name->buf);
            
            if (result)
            {
                if (strstr((char *)dev_name->buf, "usbserial"))
                {
                    modemFound = true;
                    kernResult = KERN_SUCCESS;
                }
                
            }
        }
        // Release the io_service_t now that we are done with it.
        
        (void) IOObjectRelease(modemService);
    }
    
    return kernResult;
}

// Given the path to a serial device, open the device and configure it.
// Return the file descriptor associated with the device.
static int OpenSerialPort(const char *bsdPath)
{
    int 		fileDescriptor = -1;
    int 		handshake;
    struct termios	options;
    
    // Open the serial port read/write, with no controlling terminal, and don't wait for a connection.
    // The O_NONBLOCK flag also causes subsequent I/O on the device to be non-blocking.
    // See open(2) ("man 2 open") for details.
    
    fileDescriptor = open(bsdPath, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fileDescriptor == -1)
    {
        ERR("Error opening serial port %s - %s(%d).\n",
            bsdPath, strerror(errno), errno);
        goto error;
    }

    // Note that open() follows POSIX semantics: multiple open() calls to the same file will succeed
    // unless the TIOCEXCL ioctl is issued. This will prevent additional opens except by root-owned
    // processes.
    // See tty(4) ("man 4 tty") and ioctl(2) ("man 2 ioctl") for details.
    
    if (ioctl(fileDescriptor, TIOCEXCL) == -1)
    {
        ERR("Error setting TIOCEXCL on %s - %s(%d).\n",
            bsdPath, strerror(errno), errno);
        goto error;
    }
    
    // Now that the device is open, clear the O_NONBLOCK flag so subsequent I/O will block.
    // See fcntl(2) ("man 2 fcntl") for details.
    /*
    if (fcntl(fileDescriptor, F_SETFL, 0) == -1)
    {
        ERR("Error clearing O_NONBLOCK %s - %s(%d).\n",
            bsdPath, strerror(errno), errno);
        goto error;
    }
    */
    
    // Get the current options and save them so we can restore the default settings later.
    if (tcgetattr(fileDescriptor, &gOriginalTTYAttrs) == -1)
    {
        ERR("Error getting tty attributes %s - %s(%d).\n",
            bsdPath, strerror(errno), errno);
        goto error;
    }

    // The serial port attributes such as timeouts and baud rate are set by modifying the termios
    // structure and then calling tcsetattr() to cause the changes to take effect. Note that the
    // changes will not become effective without the tcsetattr() call.
    // See tcsetattr(4) ("man 4 tcsetattr") for details.
    
    options = gOriginalTTYAttrs;
    
    // Print the current input and output baud rates.
    // See tcsetattr(4) ("man 4 tcsetattr") for details.
    
    DBG("Current input baud rate is %d\n", (int) cfgetispeed(&options));
    DBG("Current output baud rate is %d\n", (int) cfgetospeed(&options));
    
    // Set raw input (non-canonical) mode, with reads blocking until either a single character 
    // has been received or a one second timeout expires.
    // See tcsetattr(4) ("man 4 tcsetattr") and termios(4) ("man 4 termios") for details.
    
    cfmakeraw(&options);
    options.c_cc[VMIN] = 1;
    options.c_cc[VTIME] = 10;
        
    // The baud rate, word length, and handshake options can be set as follows:
    
    cfsetspeed(&options, B38400); 	// Set 312500 baud    
    options.c_cflag = (CS8 	 | 	// Use 8 bit words, no parity, one stop
			CCTS_OFLOW | 	// CTS flow control of output
			CRTS_IFLOW);	// RTS flow control of input
    
    // Print the new input and output baud rates.
    
    DBG("Input baud rate changed to %d\n", (int) cfgetispeed(&options));
    DBG("Output baud rate changed to %d\n", (int) cfgetospeed(&options));
    
    // Cause the new options to take effect immediately.

    if (tcsetattr(fileDescriptor, TCSANOW, &options) == -1)
    {
        ERR("Error setting tty attributes %s - %s(%d).\n",
            bsdPath, strerror(errno), errno);
        goto error;
    }

    // To set the modem handshake lines, use the following ioctls.
    // See tty(4) ("man 4 tty") and ioctl(2) ("man 2 ioctl") for details.
    
    if (ioctl(fileDescriptor, TIOCSDTR) == -1) // Assert Data Terminal Ready (DTR)
    {
        ERR("Error asserting DTR %s - %s(%d).\n",
            bsdPath, strerror(errno), errno);
    }
    
    if (ioctl(fileDescriptor, TIOCCDTR) == -1) // Clear Data Terminal Ready (DTR)
    {
        ERR("Error clearing DTR %s - %s(%d).\n",
            bsdPath, strerror(errno), errno);
    }
    
    handshake = TIOCM_DTR | TIOCM_RTS | TIOCM_CTS | TIOCM_DSR | TIOCM_SR | TIOCM_ST;
    if (ioctl(fileDescriptor, TIOCMSET, &handshake) == -1)
    // Set the modem lines depending on the bits set in handshake
    {
        ERR("Error setting handshake lines %s - %s(%d).\n",
            bsdPath, strerror(errno), errno);
    }
    
    // To read the state of the modem lines, use the following ioctl.
    // See tty(4) ("man 4 tty") and ioctl(2) ("man 2 ioctl") for details.
    
    if (ioctl(fileDescriptor, TIOCMGET, &handshake) == -1)
    // Store the state of the modem lines in handshake
    {
        ERR("Error getting handshake lines %s - %s(%d).\n",
            bsdPath, strerror(errno), errno);
    }
    
    DBG("Handshake lines currently set to %d\n", handshake);    
    
    // Success
    return fileDescriptor;
    
    // Failure path
error:
    if (fileDescriptor != -1)
    {
        close(fileDescriptor);
    }
    
    return -1;
}

// Given the file descriptor for a serial device, close that device.
void CloseSerialPort(int fileDescriptor)
{
    // Block until all written output has been sent from the device.
    // Note that this call is simply passed on to the serial device driver.
    // See tcsendbreak(3) ("man 3 tcsendbreak") for details.
    if (tcdrain(fileDescriptor) == -1)
    {
        ERR("Error waiting for drain - %s(%d).\n",
            strerror(errno), errno);
    }
    
    // Traditionally it is good practice to reset a serial port back to
    // the state in which you found it. This is why the original termios struct
    // was saved.
    if (tcsetattr(fileDescriptor, TCSANOW, &gOriginalTTYAttrs) == -1)
    {
        ERR("Error resetting tty attributes - %s(%d).\n",
            strerror(errno), errno);
    }

    close(fileDescriptor);
}

