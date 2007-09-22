/* Copyright (C) 2007 xyster.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#ifndef __UIRT_PRONTO_H
#define __UIRT_PRONTO_H

#define RP_MAX_PULSES 128

typedef struct rp_ctx
{
    UInt8 repeat_count;
    UInt32 interspace; // interspace in 50us (pulled from thin air)
    UInt32 freq; 
    UInt32 nof_pulses;
    UInt32 pulse[RP_MAX_PULSES]; // pulse times in carrier cycles
    UInt32 nof_spaces;
    UInt32 space[RP_MAX_PULSES]; // space times in carrier cycles
} rp_ctx;

int rp_parse(rp_ctx *ctx, UInt32 len, UInt8 *d);
UInt32 rp_output(rp_ctx *ctx, UInt8 *d); 
UInt32 rp_init(rp_ctx *ctx, UInt8 *d);
UInt32 rp_pulse(rp_ctx *ctx, UInt8 *d);
UInt32 rp_space(rp_ctx *ctx, UInt8 *d);

#endif