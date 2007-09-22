
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <unistd.h>

#include "printInterpretedError.h"
#include "debug.h"
#include "ribsu-util.h"

#define MODULE_NAME usb
DBG_MODULE();

void printInterpretedError(char *s, IOReturn err)
{
// These should be defined somewhere, but I can't find them. These from Accessing hardware.

#if 0
static struct{int err; char *where;} systemSources[] = {
    {0, "kernel"},
    {1, "user space library"},
    {2, "user space servers"},
    {3, "old ipc errors"},
    {4, "mach-ipc errors"},
    {7, "distributed ipc"},
    {0x3e, "user defined errors"},
    {0x3f, "(compatibility) mach-ipc errors"}
    };
#endif

UInt32 system, sub, code;
    
    ERR("%s (0x%08X) ", s, err);
    
    system = err_get_system(err);
    sub = err_get_sub(err);
    code = err_get_code(err);
    
    if(system == err_get_system(sys_iokit))
    {
        if(sub == err_get_sub(sub_iokit_usb))
        {
            ERR("USB error %ld(0x%lX) ", code, code);
        }
        else if(sub == err_get_sub(sub_iokit_common))
        {
            ERR("IOKit common error %ld(0x%lX) ", code, code);
        }
        else
        {
            ERR("IOKit error %ld(0x%lX) from subsytem %ld(0x%lX) ", code, code, sub, sub);
        }
    }
    else
    {
        ERR("error %ld(0x%lX) from system %ld(0x%lX) - subsytem %ld(0x%lX) ", code, code, system, system, sub, sub);
    }
}
