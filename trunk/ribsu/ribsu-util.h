/* Copyright (C) 2007 xyster.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#ifndef __RIBSU_UTIL_H
#define __RIBSU_UTIL_H

typedef struct buffer {
    UInt32 max;
    UInt32 len;
    UInt8  *buf;
    UInt8  lcl[0];
} buffer;

int add_fd_source(int fd, FILE **cfp, CFSocketCallBack callback, void *callback_arg);

buffer *buf_alloc(UInt32 max);
buffer *buf_init(buffer *buf, UInt32 max);
buffer *buf_attach(buffer *buff, UInt32 max, UInt8 *buf);
buffer *buf_copy(buffer *src, buffer *dst);
buffer *buf_append(buffer *dst, buffer *src);
buffer *buf_slide(buffer *buf, UInt32 len);
void    buf_free(buffer *buf);

void  u_buf2hex(buffer *buf, buffer *hex);
void  u_hex2buf(buffer *hex, buffer *buf);
UInt8 u_hex2val(UInt8 hex);

#endif

