/* Copyright (C) 2007 xyster.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <stdio.h>
#include <unistd.h>
#include <CoreFoundation/CoreFoundation.h>

#include "debug.h"
#include "ribsu-util.h"

#define MODULE_NAME ribsu_util
DBG_MODULE_DEFINE();

int
add_fd_source(int fd, FILE **cfp, CFSocketCallBack callback, void *callback_arg)
{
    CFSocketRef sockRef;
    CFRunLoopSourceRef runLoopSource;
    FILE *fp;
    CFSocketContext context;
    
    /* Create the CF socket
        */
    fp = fdopen(fd, "r");
    if (!fp)
    {
        ERR("Failed to open fd stream?\n");
        return -1;
    }

    bzero(&context, sizeof(context));
    if (callback_arg)
    {
        context.info = callback_arg;
    } else
    {
        context.info = fp;
    }
    sockRef = CFSocketCreateWithNative(kCFAllocatorDefault, fd, kCFSocketReadCallBack, callback, &context);
    
    if (!sockRef)
    {
        ERR("Failed to obtain socket reference!\n");
        return -1;
    }
    
    /* Add the CF socket as a source
        */
    runLoopSource = CFSocketCreateRunLoopSource(NULL, sockRef, 10);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopSource, 
                       kCFRunLoopDefaultMode);

    if (cfp) *cfp = fp;
    
    return 0;    
}


buffer *
buf_alloc(UInt32 max)
{
    buffer *buf;
    
    buf = malloc(sizeof(*buf) + max);
    if (!buf) return buf;
    
    buf->max = max;
    buf->len = 0;
    buf->buf = buf->lcl;
    
    return buf;
}

buffer *
buf_init(buffer *buf, UInt32 max)
{
    buf->buf = malloc(max);
    if (!buf->buf)
    {
        return NULL;
    }
    
    buf->max = max;
    buf->len = 0;
    
    return buf;
}

buffer *
buf_attach(buffer *buff, UInt32 max, UInt8 *buf)
{
    buff->buf = buf;
    buff->max = max;
    buff->len = 0;
    
    return buff;
}

buffer *
buf_copy(buffer *src, buffer *dst)
{
    if (!src  ||  !dst) return NULL;
    
    dst->len = src->len;
    
    if (dst->len > dst->max)
    {
        return NULL;
    }

    if (dst->len)
    {
        bcopy(src->buf, dst->buf, dst->len);   
    }
    
    return dst;
}

buffer *
buf_append(buffer *dst, buffer *src)
{
    if (!src  ||  !dst) return NULL;
    
    if (dst->len + src->len > dst->max)
    {
        return NULL;
    }
    
   if (src->len)
   {
       bcopy(src->buf, &dst->buf[dst->len], src->len);
       dst->len += src->len;
   }
    
    return dst;
}

buffer *
buf_slide(buffer *buf, UInt32 len)
{
    if (!buf) return buf;
    
    if (len >= buf->len)
    {
        buf->len = 0;
        return buf;
    }
        
    bcopy(&buf->buf[len], buf->buf, buf->len - len);
   
    buf->len -= len;
    
    return buf;
}

void
buf_free(buffer *buf)
{
    if (buf->buf  &&  buf->buf != buf->lcl)
    {
        free(buf->buf);
    } else
    {
        free(buf);
    }
}

void 
u_buf2hex(buffer *buf, buffer *hex)
{
    UInt32 n;
    UInt8 *hexb;

    hexb = hex->buf;
    
    for (n = 0; n < buf->len; n++)
    {
        sprintf((char *)hexb, "%02X", (unsigned)buf->buf[n]);
        hexb += 2;
    }
    
    *hexb = '\0';
}

void 
u_hex2buf(buffer *hex, buffer *buf)
{
    UInt32 n, l;
    UInt8 *bufb;
    
    n = l = 0;
    bufb = buf->buf;
    
    while (hex->buf[n])
    {
        if (hex->buf[n] == ' ') 
        {
            n++;
            continue;
        }
        
        *bufb++ = (u_hex2val(hex->buf[n]) << 4) | u_hex2val(hex->buf[n+1]);
        l++;
        n += 2;
    }
    
    buf->len = l;
}

UInt8 
u_hex2val(UInt8 hex)
{
    if (hex >= '0'  &&  hex <= '9')
    {
        return hex - '0';
    } else if (hex >= 'A'  &&  hex <= 'F')
    {
        return hex - 'A' + 10;
    } else if (hex >= 'a'  &&  hex <= 'f')
    {
        return hex - 'a' + 10;
    } else
    {
        return 0;
    }
}
