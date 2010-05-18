/* Copyright (C) 2007 xyster.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <CoreFoundation/CoreFoundation.h>
#include "debug.h"
#include "ribsu-util.h"
#include "uirt-pronto.h"
#include "uirt.h"

#define MODULE_NAME uirt_pronto
DBG_MODULE_DEFINE();

enum {
    DDS_INIT,
    DDS_PULSE,
    DDS_SPACE
};

// Parse RAW data and store internally
int
rp_parse(rp_ctx *ctx, UInt32 len, UInt8 *d)
{
    UInt32 n;
    int state;
    UInt32 (*fn)(rp_ctx *, UInt8 *);
    
    n = 0;
    
    state = DDS_INIT;
    
    while (n < len)
    {
        switch (state)
        {
            case DDS_INIT:
                fn = rp_init;
                state = DDS_PULSE;
                break;
            case DDS_PULSE:
                fn = rp_pulse;
                state = DDS_SPACE;
                break;
            case DDS_SPACE:
                fn = rp_space;
                state = DDS_PULSE;
                break;
            default:
                fn = NULL;
        }
        
        n += fn(ctx, &d[n]);
    }
    
    return 0;
}

// Use internal representation to generate RAW output good for
// giving to USB-UIRT
UInt32 
rp_output(rp_ctx *ctx, UInt8 *d)
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
            t = ctx->pulse[ctx->nof_pulses - nof_pulses];
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
            t = ctx->space[ctx->nof_spaces - nof_spaces];
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
    
    DMP("d[1] = %02X, d[6] = %02X, nof_fudge = %02X\n", d[1], d[6], (int)nof_fudge);
    d[1] += nof_fudge; // redo the length to take fudge into account
    d[6] += nof_fudge; 
    
    return n; 
}

UInt32
rp_init(rp_ctx *ctx, UInt8 *d)
{
    UInt32 n, f;
    
    bzero(ctx, sizeof(*ctx));
    
    ctx->interspace = 0;
    ctx->repeat_count = 1;
    
    n = 0;
        
    n += 2; // skip header (assume is 0000)
    
    // frequency
    f =  (UInt32)d[n++] << 8;
    f |= (UInt32)d[n++];
    // actual calculation is 4145146.44802 / f
    ctx->freq = 4145146 / f;
    
    // do we really need to differentiate between burst sequences?
    // XXX We do need to know the difference between sequences. The two sequences
    // are to support repeat.
    // for now assume we don't, skip over lengths
    n += 4;
    
    return n;
}

// PhPl
UInt32
rp_pulse(rp_ctx *ctx, UInt8 *d)
{
    UInt32 n, p;
    
    n = 0;
    
    // time in carrier cycles
    p =  (UInt32)d[n++] << 8; 
    p |= (UInt32)d[n++];
    
    // record the pulse
    ctx->pulse[ctx->nof_pulses++] = p;
    
   DBG("pulse = %02X\n", (unsigned)p);
    
    return n;
}

// ShSl
UInt32
rp_space(rp_ctx *ctx, UInt8 *d)
{
    UInt32 n, s;
    
    n = 0;
    
    // time in carrier cycles
    s =  (UInt32)d[n++] << 8; 
    s |= (UInt32)d[n++];
    
    // record the space
    ctx->space[ctx->nof_spaces++] = s;
  
    DBG("space = %02X\n", (unsigned)s);
    
    return n;
}
