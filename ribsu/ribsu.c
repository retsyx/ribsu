/* Copyright (C) 2007 xyster.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <CoreFoundation/CoreFoundation.h>

#include "debug.h"
#include "ribsu-util.h"
#include "tty.h"
#include "uirt.h"
#include "uirt-sm.h"
#include "usb.h"
#include "ribsu.h"

#define MODULE_NAME ribsu
DBG_MODULE_DEFINE();

static void ribsu_callback(void *ctx0, buffer *buf);

int
ribsu_init(ribsu_ctx *ctx, ribsu_opts *opts)
{
    ribsu_opts o;
    buffer tty_dev;
    UInt8 tty_dev_buf[RIBSU_TTY_MAX_NAME];
    
    buf_attach(&tty_dev, sizeof(tty_dev_buf), tty_dev_buf);
    
    bzero(ctx, sizeof(*ctx));
 
    ctx->interp = 1;
    
    // if no options given, use defaults
    if (!opts)
    {
        bzero(&o, sizeof(o));
        
        o.use_usb = 1; // try USB
        o.use_tty = 1; // try TTY
        o.tty_dev_name[0] = '\0'; // auto-detect TTY device name
    } else
    {
        o = *opts;
    }
    
    // default USB-UIRT VID/PID
    if (!o.vid)
    {
        o.vid = 0x0403;
    }
    
    if (!o.pid)
    {
        o.pid = 0xf850;
    }
    
    if (!o.use_usb  &&  !o.use_tty)
    {
        ERR("Illegal ribsu options, told to use neither USB nor TTY\n");
        return -1;
    }
    
    // First attempt to directly communicate with the device using I/O kit USB 
    if (o.use_usb)
    {
        DBG("VID/PID %X/%X\n", (unsigned)o.vid & 0xffff, (unsigned)o.pid & 0xffff);

        if (!usb_add_source(&ctx->drv, o.vid, o.pid)) 
        {
            ctx->drv_write = usb_write;
            ctx->drv_shutdown = usb_shutdown;
            usb_set_callback(ctx->drv, ribsu_callback, ctx);
        } else
        {
            o.use_usb = 0;
        }
    }
    
    // Attempt to use the TTY assuming an FTDI driver is installed
    if (!o.use_usb  &&  o.use_tty)
    {
        if (!o.tty_dev_name[0])
        {
           if (tty_find_device(&tty_dev))
           {
               ERR("Failed to find TTY device.\n");
               return -1;
           }
        } else
        {
            bcopy(o.tty_dev_name, tty_dev.buf, sizeof(o.tty_dev_name));
            tty_dev.len = sizeof(tty_dev_buf);
        }
        
        if (!tty_add_source(&ctx->drv, &tty_dev))
        {
            ctx->drv_write = tty_write;
            ctx->drv_shutdown = tty_shutdown;
            tty_set_callback(ctx->drv, ribsu_callback, ctx);
        } else
        {
            o.use_tty = 0;
        }
    }
    
    if (!o.use_usb  &&  !o.use_tty)
    {
        ERR("Failed to find USB-UIRT.\n");
        return -1;
    }
   
    usm_init(&ctx->usm);
    
    DBG("Init done\n");
    
    return 0;
}

int 
ribsu_deinit(ribsu_ctx *ctx)
{
    ctx->drv_shutdown(ctx->drv);
    
    return 0;
}

int 
ribsu_set_callback(ribsu_ctx *ctx, ribsu_callback_fn fn, void *fn_arg)
{
    ctx->callback_fn = fn;
    ctx->callback_arg = fn_arg;
    
    return 0;
}

int 
ribsu_write(ribsu_ctx *ctx, buffer *buf)
{
    buffer *out;
    int error;
    
    if (ctx->interp)
    {
        out = buf_alloc(512);
        if (!out)
        {
            ERR("Failed to allocate buffer\n");
            return -1;
        }
        
        usm_process_user(&ctx->usm, buf, out);
    } else
    {
        out = buf;
    }
    
    error = ctx->drv_write(ctx->drv, out);
    
    if (ctx->interp)
    {
        buf_free(out);
    }
    
    return error;
}

int 
ribsu_set_default_frequency(ribsu_ctx *ctx, UInt32 frequency)
{
    usm_set_default_frequency(&ctx->usm, frequency);
    
    return 0;
}

UInt32 
ribsu_toggle_interpretation(ribsu_ctx *ctx, UInt32 interp)
{
    UInt32 old;
    
    old = ctx->interp;
    ctx->interp = (interp && 1);
    
    return old;
}

void 
ribsu_callback(void *ctx0, buffer *buf)
{
    ribsu_ctx *ctx;
    buffer *out;
    
    ctx = ctx0;
    
    DMP("Got callback\n");
    
    if (!ctx->callback_fn) return;
    
    if (ctx->interp)
    {
        out = buf_alloc(512);
        if (!out)
        {
            ERR("Failed to allocate buffer\n");
            return;
        }
        
        usm_process_uirt(&ctx->usm, buf, out);
        do {
            if (out->len)
            {
                DMP("Propagating callback\n");
                ctx->callback_fn(ctx->callback_arg, out);
            }
            usm_process_uirt_more(&ctx->usm, out);
        } while (out->len);

        buf_free(out);   
    } else
    {
        out = buf;
    
        if (out->len)
        {
            DBG("Propagating callback\n");
            ctx->callback_fn(ctx->callback_arg, out);
        }
    
    }
}

int rlrn_init(ribsu_learn_ctx *ctx);
int rlrn_add_sequence(ribsu_learn_ctx *ctx, buffer *seq);
int rlrn_get_command(ribsu_learn_ctx *ctx, buffer *cmd);
int seq_cmp(buffer *a, buffer *b);

int 
ribsu_learn(ribsu_ctx *ctx)
{
    rlrn_init(&ctx->hi.lrn);
    
    ctx->hi.learning = 1;
    ctx->hi.state = 0;
    
    return 0;
}

enum {
    ribsu_lrn_sm_init = 0,
    ribsu_lrn_sm_status,
    ribsu_lrn_sm_ir,
};

int
ribsu_learn_sm(ribsu_ctx *ctx)
{
    switch (ctx->hi.state)
    {
        case ribsu_lrn_sm_init:
            break;
        case ribsu_lrn_sm_status:
            break;
        case ribsu_lrn_sm_ir:
            break;
    }
    
    return 0;
}

int 
ribsu_parrot(ribsu_ctx *ctx, buffer *cmd)
{
    return 0;
}

int
rlrn_init(ribsu_learn_ctx *ctx)
{
    int i;
    
    bzero(ctx, sizeof(*ctx));
    
    // initialize table buffers
    for (i = 0; i < RIBSU_LEARN_TABLE_SIZE; i++)
    {
        buf_attach(&ctx->table[i], RIBSU_LEARN_ROW_SIZE, ctx->table_buf[i]);    
    }
    
    return 0;
}

#define RIBSU_LEARN_SEQUENCES_TO_STOP 5

int 
rlrn_add_sequence(ribsu_learn_ctx *ctx, buffer *seq)
{
    int i;
    
    ctx->nof_seq++;
    
    // go over table rows and compare this sequence with previously
    // seen sequences. If the sequence is a repeat, increment a count.
    // Otherwise, copy the new sequence into the table 
    for (i = 0; i < RIBSU_LEARN_TABLE_SIZE; i++)
    {
        // this row is empty, copy the sequence in
        if (!ctx->table[i].len)
        {
            buf_copy(seq, &ctx->table[i]);
            ctx->count[i]++;
            break;
        } 
        
        // If the sequence is equal to what we have in this row, increment
        // the row count
        if (!seq_cmp(seq, &ctx->table[i]))
        {
            ctx->count[i]++;
            break;
        }
    }
    
    // Are we seeing too many sequences?
    if (i == RIBSU_LEARN_TABLE_SIZE)
    {
        return RIBSU_LEARN_ERROR_TOO_MANY_SEQUENCES;
    }
    
    // Did we see enough sequences to 'learn'?
    if (ctx->nof_seq == RIBSU_LEARN_SEQUENCES_TO_STOP)
    {
        return RIBSU_LEARN_DONE;
    }
    
    return RIBSU_LEARN_CONTINUE;
}

// At this point rlrn_add_sequence() returned RIBSU_LEARN_DONE and there is
// a table with some sequences and counts. Now just to decide what we are seeing
int 
rlrn_get_command(ribsu_learn_ctx *ctx, buffer *cmd)
{
    return 0;
}

// 5% threshold relative to the large one for equality
#define SEQ_CMP_THRESHOLD 5

// Yes, this a long, boring routine that could be simplified
// by converting the sequences into something that doesn't need
// as much interpretation. But why bother?
// note: assumes given sequences are valid!
int
seq_cmp(buffer *a, buffer *b)
{
    uirt_tx_cmd *ah, *bh;
    UInt32 at, bt, ao, bo, t;
    UInt8 *ad, *bd;
    int cmp;
  
    ah = (void *)a->buf;
    bh = (void *)b->buf;
    
    // first make sure that the frequency is the same
    cmp = (ah->freq != bh->freq);
    if (cmp) return cmp;
    
    ad = ah->data;
    bd = bh->data;
   
    ao = bo = 0;
    // jump over pulses and spaces and make sure they
    // agree (to within some tolerance)
    do {
        at = ad[ao++];
        if (at & 0x80)
        {
            at = ((at & 0x7f) << 8) | ad[ao++];
        }
        
        bt = bd[bo++];
        if (bt & 0x80)
        {
            bt = ((bt & 0x7f) << 8) | bd[bo++];
        }
        
        if (at == bt) continue;
        
        // check if equal to within tolerance
        if (at > bt)
        {
            t = (at << 8) * SEQ_CMP_THRESHOLD / 100; 
            
            // not equal
            if (((at - bt) << 8) > t)
            {
                return 1;
            }
        } else
        {
            t = (bt << 8) * SEQ_CMP_THRESHOLD / 100;
            // not equal
            if (((bt - at) << 8) > t) 
            {
                return 1;
            }
        }
    } while (ao < ah->data_len  &&  bo < bh->data_len);  
    
    // If we did not reach the end of either sequence,
    // for any reason, the sequences are not equal.
    if (ah->data_len - ao  ||  bh->data_len - bo)
    {
        return 1;
    }
    
    // the sequences are apparently equal!
    return 0;
}
