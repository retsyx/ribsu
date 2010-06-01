/* Copyright (C) 2007 xyster.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <CoreFoundation/CoreFoundation.h>
#include "debug.h"
#include "ribsu-util.h"
#include "uirt.h"
#include "uirt-raw.h"

#define MODULE_NAME uirt_raw
DBG_MODULE_DEFINE();

enum {
    DDS_INIT,
    DDS_INTERSPACE,
    DDS_PULSE,
    DDS_SPACE
};


static UInt32 rr_interspace(rr_ctx *ctx, UInt32 len, UInt8 *d);
static UInt32 rr_pulse(rr_ctx *ctx, UInt32 len, UInt8 *d);
static UInt32 rr_space(rr_ctx *ctx, UInt32 len, UInt8 *d);

// Parse RAW data and store internally
rr_ret
rr_parse(rr_ctx *ctx, UInt32 len, UInt8 *d)
{
    rr_ret ret;
    UInt32 n, m;
    UInt32 (*fn)(rr_ctx *, UInt32, UInt8 *);
    int nextState;
    
    n = 0;
    
    while (n < len  &&  !ctx->done)
    {
        switch (ctx->state)
        {
            case DDS_INIT:
                DMP("INIT\n");
                fn = rr_init;
                nextState = DDS_INTERSPACE;
                break;
            case DDS_INTERSPACE:
                DMP("INTERSPACE\n");
                fn = rr_interspace;
                nextState = DDS_PULSE;
                break;
            case DDS_PULSE:
                DMP("PULSE\n");
                fn = rr_pulse;
                nextState = DDS_SPACE;
                break;
            case DDS_SPACE:
                DMP("SPACE\n");
                fn = rr_space;
                nextState = DDS_PULSE;
                break;
            default:
                fn = NULL;
        }
        
        m = fn(ctx, len - n, &d[n]);
        ctx->state = nextState;
        if (m > len - n) break;
        n += m;
    }
 
    ret.n = n;
    if (ctx->done)
    {
        ret.done = 1;
        if (ret.n >= 2)
        {
            ret.m = (ctx->done == 1 ? n - 2 : n); // partial
        } else
        {
            ret.m = ret.n;
        }
        ret.d[0] = ret.d[1] = 0; // 0 inter-space delay
        ctx->state = DDS_INIT;
        ctx->done = 0;
    } else
    {
        ret.done = 0;
        ret.m = ret.n;
    }
    
    return ret;
}

void 
rr_set_frequency(rr_ctx *ctx, UInt32 freq)
{
    ctx->freq = freq;
}

// Use internal representation to generate Pronto output
UInt32 
rr_output_pronto(rr_ctx *ctx, UInt8 *d)
{
    UInt32 n, t;
    UInt32 nof_pulses, nof_spaces;

    n = 0;
    
    d[n++] = 0;
    d[n++] = 0; // learned command
    d[n++] = 0;
    d[n++] = 4145146 / ctx->freq; // frequency
    d[n++] = 0;
    d[n++] = 0; // once burst-pair count
    d[n++] = ctx->nof_pulses >> 8;
    d[n++] = ctx->nof_pulses & 0xff; // repeat burst-pair count
    
    nof_pulses = ctx->nof_pulses;
    nof_spaces = ctx->nof_spaces;

    while (nof_pulses  ||  nof_spaces)
    {
        if (nof_pulses)
        {
            t = 78000 * ctx->pulse[ctx->nof_pulses - nof_pulses] / ctx->freq;
            d[n++] = t >> 8;
            d[n++] = t & 0xff;
            nof_pulses--;
        }
        
        if (nof_spaces)
        {
            t = 78000 * ctx->space[ctx->nof_spaces - nof_spaces] / ctx->freq;
            d[n++] = t >> 8;
            d[n++] = t & 0xff;
            nof_spaces--;
        }
    }
    
    // Add 10ms as the trailer (the USB-UIRT end of code gap definition)
    t = ctx->freq / 100;
    d[n++] = t >> 8;
    d[n++] = t & 0xff;
    
    return n;    
}

// Use internal representation to generate RAW output good for
// giving the USB-UIRT
UInt32 
rr_output(rr_ctx *ctx, UInt8 *d)
{
    UInt32 n, v, t;
    UInt32 nof_pulses, nof_spaces, nof_fudge;
    
    n = 0;
    
    DBG("calculated frequency %u\n", (unsigned)ctx->freq);
    
    v = 2500000 / ctx->freq;
    if (v >= 0x80)
    {
        ERR("invalid freq byte value = %Xh\n", (unsigned)v); 
    } else
    {
        DBG("freq byte value = %Xh\n", (unsigned)v);
    }
    
    d[n++] = UIRT_CMD_TX_RAW; // RAW command
    
    d[n++] = 6 + (UInt8)(ctx->nof_pulses + ctx->nof_spaces); // RAW command length
    
    d[n++] = v; // frequency
    
    d[n++] = ctx->repeat_count; // repeat count
    
    d[n++] = (UInt8)(ctx->interspace >> 8); // interspace hi
    d[n++] = (UInt8)(ctx->interspace & 0xff); // interspace low
    
    d[n++] = (UInt8)(ctx->nof_pulses + ctx->nof_spaces); 
    
    nof_pulses = ctx->nof_pulses;
    nof_spaces = ctx->nof_spaces;
    nof_fudge = 0;
    
    while (nof_pulses  ||  nof_spaces)
    {
        if (nof_pulses)
        {
            t = 78000 * ctx->pulse[ctx->nof_pulses - nof_pulses] / ctx->freq;
            if (t >= 0x80)
            {
                d[n++] = 0x80 | (t >> 8);
                d[n++] = (t & 0xFF);
                nof_fudge++;
            } else
            {
                d[n++] = t;
            }
            
            nof_pulses--;
        }
        
        if (nof_spaces)
        {
            t = 80000 * ctx->space[ctx->nof_spaces - nof_spaces] / ctx->freq;
            
            if (t >= 0x80)
            {
                d[n++] = 0x80 | (t >> 8);
                d[n++] = (t & 0xFF);
                nof_fudge++;
            } else
            {
                d[n++] = t;
            }
            
            nof_spaces--;
        }
    }
    
    DMP("d[1] = %02X, d[6] = %02X, nof_fudge = %02X\n", 
        d[UIRT_CMD_O_LENGTH], d[UIRT_CMD_TX_RAW_O_LENGTH], (int)nof_fudge);
    d[UIRT_CMD_O_LENGTH] += nof_fudge; // redo the length to take fudge into account
    d[UIRT_CMD_TX_RAW_O_LENGTH] += nof_fudge; 
    
    return n; 
}

UInt32
rr_init(rr_ctx *ctx, UInt32 len, UInt8 *d)
{
    bzero(ctx, sizeof(*ctx));
    
    ctx->freq = 38461; // pick a default that divides 2500000 semi-nicely for no particular reason
    ctx->repeat_count = 1;
    
    return 0;
}

UInt32
rr_interspace(rr_ctx *ctx, UInt32 len, UInt8 *d)
{
    UInt32 n, i;
    
    n = 0;

    if (len < 2) return -len;
    i = (UInt32)d[n++] << 8;
    i |= (UInt32)d[n++];
    
    ctx->interspace = i;
    
    return n;
}

UInt32
rr_pulse(rr_ctx *ctx, UInt32 len, UInt8 *d)
{
    UInt32 n, p;
    
    n = 0;

    // time in 50us units
    p = (UInt32)d[n++];
    
    // record the pulse
    ctx->pulse[ctx->nof_pulses++] = p;
    
    return n;
}

UInt32
rr_space(rr_ctx *ctx, UInt32 len, UInt8 *d)
{
    UInt32 n, s;
    static UInt32 max_s = 0;
    
    n = 0;
    
    s = (UInt32)d[n++];
    if (s == 0xff) 
    {
        ctx->done = 2;
        return n;
    }

    if (s > max_s)
    {
        max_s = s;
        DBG("max_s = %X\n", (int)max_s);
    }
    
    if (s > 0x80)
    {
        ctx->done = 1;
        return n;
    }
   
    // record the space
    ctx->space[ctx->nof_spaces++] = s;
    
    return n;
}
