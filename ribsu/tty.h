/* Copyright (C) 2007 xyster.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#ifndef __TTY_H
#define __TTY_H

int  tty_find_device(buffer *dev_name);
int  tty_add_source(void **ctx, buffer *dev_name);
int  tty_set_callback(void *ctx, void (*fn)(void *, buffer *), void *fn_arg);
int  tty_write(void *ctx, buffer *buf);
void tty_shutdown(void *ctx);

#endif