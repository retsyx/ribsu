/* Copyright (C) 2007 xyster.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __UIRT_SM_H
#define __UIRT_SM_H

#define USM_AGG_MAX (4096) 

typedef struct usm_ctx
{
    UInt32 mode; // master mode (USM_M_RAW2/USM_M_RAW/USM_M_UIR)
    UInt32 state;
    UInt32 default_frequency;
    buffer agg;
    UInt8  agg_buf[USM_AGG_MAX];
} usm_ctx;

void usm_init(usm_ctx *ctx);
void usm_process_uirt(usm_ctx *ctx, buffer *in, buffer *out);
void usm_process_uirt_more(usm_ctx *ctx, buffer *out);
void usm_process_user(usm_ctx *ctx, buffer *in, buffer *out);


void usm_set_default_frequency(usm_ctx *ctx, UInt32 frequency);

#endif