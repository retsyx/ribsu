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
#include "uirt-raw2.h"

#define MODULE_NAME uirt_raw2
DBG_MODULE_DEFINE();

enum {
    DDS_INIT,
    DDS_INTERSPACE,
    DDS_PULSE,
    DDS_SPACE
};


static UInt32 rr2_interspace(rr2_ctx *ctx, UInt32 len, UInt8 *d);
static UInt32 rr2_pulse(rr2_ctx *ctx, UInt32 len, UInt8 *d);
static UInt32 rr2_space(rr2_ctx *ctx, UInt32 len, UInt8 *d);
static UInt32 rr2_final(rr2_ctx *ctx, UInt32 len, UInt8 *d);

// Parse RAW2 data and store internally
rr2_ret
rr2_parse(rr2_ctx *ctx, UInt32 len, UInt8 *d)
{
    rr2_ret ret;
    UInt32 n, m;
    UInt32 (*fn)(rr2_ctx *, UInt32, UInt8 *);
    int nextState;
    
    m = n = 0;
    
    DMP("state, n, len, ctx->done = %d, %d, %d, %d",
        (int)ctx->state, (int)n, (int)len, ctx->done);
    while (n < len  &&  !ctx->done)
    {
        DMP("state, n, len, ctx->done = %d, %d, %d, %d",
            (int)ctx->state, (int)n, (int)len, ctx->done);
        switch (ctx->state)
        {
            case DDS_INIT:
                DMP("DDS_INIT");
                fn = rr2_init;
                nextState = DDS_INTERSPACE;
                break;
            case DDS_INTERSPACE:
                DMP("DDS_INTERSPACE");
                fn = rr2_interspace;
                nextState = DDS_PULSE;
                break;
            case DDS_PULSE:
                DMP("DDS_PULSE");
                fn = rr2_pulse;
                nextState = DDS_SPACE;
                break;
            case DDS_SPACE:
                DMP("DDS_SPACE");
                fn = rr2_space;
                nextState = DDS_PULSE;
                break;
            default:
                fn = NULL;
        }

        m = fn(ctx, len - n, &d[n]);
        ctx->state = nextState;
        DMP("m %d", (int)m);
        if (m > len - n) break;
        n += m;
        DMP("state, n, len, ctx->done = %d, %d, %d, %d",
            (int)ctx->state, (int)n, (int)len, ctx->done);
    }
    DMP("state, n, len, ctx->done = %d, %d, %d, %d",
        (int)ctx->state, (int)n, (int)len, ctx->done);
 
    ret.n = n;
    if (ctx->done)
    {
        rr2_final(ctx, len - n, &d[n]);
        
        ret.done = 1;
        if (ret.n >= 2)
        {
            ret.m = (ctx->done == 1 ? n - 2 : n); // partial
        } else
        {
            ret.m = ret.n;
        }

        ret.d[0] = ret.d[1] = 0;
        ctx->state = DDS_INIT;
        ctx->done = 0;
    } else
    {
        ret.done = 0;
        ret.m = ret.n;
    }
    
    return ret;
}

// Use internal representation to generate Pronto output
UInt32 
rr2_output_pronto(rr2_ctx *ctx, UInt8 *d)
{
    UInt32 n, t;
    UInt32 nof_pulses, nof_spaces;
    
    n = 0;
    
    d[n++] = 0;
    d[n++] = 0; // learned command
    d[n++] = 0;
    d[n++] = 4145146 / ctx->calc_freq; // frequency
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
            t = 560 * ctx->pulse[ctx->nof_pulses - nof_pulses] / ctx->calc_freq;
            d[n++] = t >> 8;
            d[n++] = t & 0xff;
            nof_pulses--;
        }
        
        if (nof_spaces)
        {
            t = 560 * ctx->space[ctx->nof_spaces - nof_spaces] / ctx->calc_freq;
            d[n++] = t >> 8;
            d[n++] = t & 0xff;
            nof_spaces--;
        }
    }

    // Add 10ms as the trailer (the USB-UIRT end of code gap definition)
    t = ctx->calc_freq / 100;
    d[n++] = t >> 8;
    d[n++] = t & 0xff;
    
    return n;
}

// Use internal representation to generate RAW output good for
// giving the USB-UIRT
UInt32 
rr2_output(rr2_ctx *ctx, UInt8 *d)
{
    UInt32 n, v, t;
    UInt32 nof_pulses, nof_spaces, nof_fudge;
    
    n = 0;
    
    v = 2500000 / ctx->calc_freq;
    if (v >= 0x80)
    {
       ERR("invalid freq byte value = %Xh", (unsigned)v); 
    } else
    {
       DBG("freq byte value = %Xh", (unsigned)v);
    }
    
    d[n++] = 0x36; // RAW command
    
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
            t = 560 * ctx->pulse[ctx->nof_pulses - nof_pulses] / ctx->calc_freq;
            DMP("pulse %02Xh", (int)t);
            
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
            t = 560 * ctx->space[ctx->nof_spaces - nof_spaces] / ctx->calc_freq;
            DMP("space %02Xh", (int)t);
            
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
    
    DMP("d[1] = %02X, d[6] = %02X, nof_fudge = %02X", 
        d[UIRT_CMD_O_LENGTH], d[UIRT_CMD_TX_RAW_O_LENGTH], (int)nof_fudge);
    d[UIRT_CMD_O_LENGTH] += nof_fudge; // redo the length to take fudge into account
    d[UIRT_CMD_TX_RAW_O_LENGTH] += nof_fudge; 
    
    return n; 
}

UInt32
rr2_init(rr2_ctx *ctx, UInt32 len, UInt8 *d)
{
    bzero(ctx, sizeof(*ctx));
    
    ctx->repeat_count = 1;
    
    return 0;
}

UInt32
rr2_interspace(rr2_ctx *ctx, UInt32 len, UInt8 *d)
{
    UInt32 n, i;

    if (len < 2) return -len;

    n = 0;
    
    i = (UInt32)d[n++] << 8;
    i |= (UInt32)d[n++];
    i = i * 500 / 512; // convert from 51.2us to 50us
    
    ctx->interspace = i;
    
    return n;
}

// PhPlCl or PhPlChCl
UInt32
rr2_pulse(rr2_ctx *ctx, UInt32 len, UInt8 *d)
{
    UInt32 n, p, c, f;
    
    n = 0;

    if (len < 3) return -len;
    
    // time in 400ns units
    p = (UInt32)d[n++] << 8;
    p |= (UInt32)d[n++];
        
    // number of carrier cycles 
    c = (UInt32)d[n++];
    if (c & 0x80)
    {
        if (len < 4) return -len;
        c = ((c & 0x7f) << 8) | (UInt32)d[n++];
    }
    
    // add the grokked frequency to the running total for later averaging
    if (c != 0)
    {
        f = (250000 / p) * (10 * c - 5); 
        DMP("p = %u, c = %u, f = %u", (unsigned)p, (unsigned)c, (unsigned)f); 
        
        ctx->freq_total += f;
        ctx->nof_freq_samples++;
    }
    
    // record the pulse
    ctx->pulse[ctx->nof_pulses++] = p;
    
    return n;
}

UInt32
rr2_space(rr2_ctx *ctx, UInt32 len, UInt8 *d)
{
    UInt32 n, s;
    
    n = 0;
    
    if (len < 1) return -len;
    // time in 400ns units
    s = (UInt32)d[n++];
    if (s == 0xff)
    {
        ctx->done = 2;
        return n;
    }

    if (len < 2) return -len;
    s = (s << 8) | (UInt32)d[n++];
    
    if (s > 0x3e80)
    {
        DBG("s = %X", (int)s);
        ctx->done = 1;
        return n;
    }
    
    // record the space
    ctx->space[ctx->nof_spaces++] = s;
    
    return n;
}

UInt32
rr2_final(rr2_ctx *ctx, UInt32 len, UInt8 *d)
{
    if (ctx->nof_freq_samples)
    {
        ctx->calc_freq = ctx->freq_total / ctx->nof_freq_samples;
    } else
    {
        ctx->calc_freq = 1; // Just set it to something that won't crash
    }

    DBG("calc_freq = %u", (unsigned)ctx->calc_freq);
    
    return 0;
}

