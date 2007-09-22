/* Copyright (C) 2007 xyster.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#ifndef __RIBSU_H
#define __RIBSU_H

#include "debug.h"
#include "uirt-sm.h"

DBG_MODULE_OTHER(uirt_raw);
DBG_MODULE_OTHER(uirt_raw2);
DBG_MODULE_OTHER(uirt_pronto);
DBG_MODULE_OTHER(uirt_sm);
DBG_MODULE_OTHER(usb);
DBG_MODULE_OTHER(tty);
DBG_MODULE_OTHER(ribsu);

#define RIBSU_TTY_MAX_NAME 64

typedef void (*ribsu_callback_fn)(void *, buffer *);

typedef struct ribsu_opts
{
    int use_usb;
    int use_tty;
    UInt16 vid, pid;
    char tty_dev_name[RIBSU_TTY_MAX_NAME];
} ribsu_opts;

#define RIBSU_LEARN_ERROR_TOO_MANY_SEQUENCES (-1)
#define RIBSU_LEARN_DONE                     (0)
#define RIBSU_LEARN_CONTINUE                 (1)


#define RIBSU_LEARN_TABLE_SIZE 5
#define RIBSU_LEARN_ROW_SIZE   512

typedef struct ribsu_learn_ctx
{
    UInt32 nof_seq;
    UInt32 count[RIBSU_LEARN_TABLE_SIZE];
    buffer table[RIBSU_LEARN_TABLE_SIZE];
    UInt8 table_buf[RIBSU_LEARN_TABLE_SIZE][RIBSU_LEARN_ROW_SIZE];
} ribsu_learn_ctx;

typedef struct ribsu_ctx
{
    // low-level state
    usm_ctx usm;
    ribsu_callback_fn callback_fn;
    void *callback_arg;
    void *drv;
    int  (*drv_write)(void *ctx, buffer *buf);
    void (*drv_shutdown)(void *ctx);
    UInt32 interp : 1;
    
    // high-level state (in a struct in case this is broken out later)
    struct {
        int learning;
        int state;
        ribsu_learn_ctx lrn;
    } hi;
} ribsu_ctx;

// "low-level" API
int ribsu_init(ribsu_ctx *ctx, ribsu_opts *opts);
int ribsu_deinit(ribsu_ctx *ctx);
int ribsu_set_callback(ribsu_ctx *ctx, ribsu_callback_fn fn, void *fn_arg);
int ribsu_write(ribsu_ctx *ctx, buffer *buf);
int ribsu_set_default_frequency(ribsu_ctx *ctx, UInt32 frequency);
UInt32 ribsu_toggle_interpretation(ribsu_ctx *ctx, UInt32 interp);

// "high-level" API
int ribsu_learn(ribsu_ctx *ctx);
int ribsu_parrot(ribsu_ctx *ctx, buffer *cmd);

#endif
