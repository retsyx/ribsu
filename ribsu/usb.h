/* Copyright (C) 2007 xyster.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#ifndef __USB_H
#define __USB_H

int  usb_add_source(void **ctx, UInt32 vid, UInt32 pid);
int  usb_set_callback(void *ctx, void (*fn)(void *, buffer *), void *fn_arg);
int  usb_write(void *ctx, buffer *buf);
void usb_shutdown(void *ctx);

#endif