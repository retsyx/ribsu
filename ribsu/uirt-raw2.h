/* Copyright (C) 2007 xyster.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __UIRT_RAW2_H
#define __UIRT_RAW2_H

#define RR2_MAX_PULSES 128

typedef struct rr2_ctx
{
    UInt8 repeat_count;
    UInt32 interspace; // interspace in 50us
    UInt32 freq_total; // frequency total
    UInt32 nof_freq_samples; // number of frequency samples
    UInt32 calc_freq; // calculated frequency
    UInt32 nof_pulses;
    UInt32 pulse[RR2_MAX_PULSES]; // pulse times in 400ns
    UInt32 nof_spaces;
    UInt32 space[RR2_MAX_PULSES]; // space times in 400ns
    int done;
} rr2_ctx;

typedef struct rr2_ret
{
    UInt32 n : 16;
    UInt32 m : 16;
    UInt8 d[2];
} rr2_ret;

rr2_ret rr2_parse(rr2_ctx *ctx, UInt32 len, UInt8 *d);
UInt32 rr2_output(rr2_ctx *ctx, UInt8 *d);

#endif