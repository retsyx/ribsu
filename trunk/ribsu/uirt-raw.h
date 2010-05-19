/* Copyright (C) 2007 xyster.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __UIRT_RAW_H
#define __UIRT_RAW_H

#define RR_MAX_PULSES 128

typedef struct rr_ctx
{
    int state;
    int done;
    UInt8 repeat_count;
    UInt32 interspace; // interspace in 50us
    UInt32 freq; // frequency (pulled from thin air)
    UInt32 nof_pulses;
    UInt32 pulse[RR_MAX_PULSES]; // pulse times in 50us
    UInt32 nof_spaces;
    UInt32 space[RR_MAX_PULSES]; // space times in 50us
} rr_ctx;

typedef struct rr_ret
{
    int done;
    UInt32 n : 16;
    UInt32 m : 16;
    UInt8 d[2];
} rr_ret;

UInt32 rr_init(rr_ctx *ctx, UInt32 len, UInt8 *d);
rr_ret rr_parse(rr_ctx *ctx, UInt32 len, UInt8 *d);
void rr_set_frequency(rr_ctx *ctx, UInt32 freq);
UInt32 rr_output(rr_ctx *ctx, UInt8 *d);

#endif
