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
#include "uirt-raw2.h"
#include "uirt-pronto.h"
#include "uirt-sm.h"

#define MODULE_NAME uirt_sm
DBG_MODULE_DEFINE();

// 00 is a pseudo command we use only here
#define UIRT_CMD_TX_PRONTO     (0x00)

enum {
    USM_M_ECHO,
    USM_M_UIR,
    USM_M_RAW,
    USM_M_RAW2,
};

enum {
    USM_W_STATUS, // waiting for UIRT to return status
    USM_W_CODE,   // waiting for UIRT to issue codes
    USM_W_VER,    // waiting for UIRT version info
};

static void usm_process_uir(usm_ctx *ctx, buffer *in, buffer *out);
static void usm_process_raw(usm_ctx *ctx, buffer *in, buffer *out);
static void usm_process_raw2(usm_ctx *ctx, buffer *in, buffer *out);
static void usm_process_thru(usm_ctx *ctx, buffer *in, buffer *out);
static int  usm_checksum(buffer *buf);

void 
usm_init(usm_ctx *ctx)
{
    bzero(ctx, sizeof(*ctx));
    
    buf_attach(&ctx->agg, USM_AGG_MAX, ctx->agg_buf);
    
    // power on defaults
    ctx->mode = USM_M_UIR;
    ctx->state = USM_W_CODE;
}

void
usm_process_uirt(usm_ctx *ctx, buffer *in, buffer *out)
{
    out->len = 0;
    
    if (!in->len) return;
    
    switch (ctx->state)
    {
        case USM_W_STATUS:
        case USM_W_VER:
            usm_process_thru(ctx, in, out);
            break;
        case USM_W_CODE:
            switch (ctx->mode)
            {
                case USM_M_UIR:
                    usm_process_uir(ctx, in, out);
                    break;
                case USM_M_ECHO:
                    usm_process_thru(ctx, in, out);
                    break;
                case USM_M_RAW:
                    usm_process_raw(ctx, in, out);
                    break;
                case USM_M_RAW2:
                    usm_process_raw2(ctx, in, out);
                    break;
            }
            break;
    }
}

void
usm_process_uirt_more(usm_ctx *ctx, buffer *out)
{
    switch (ctx->mode)
    {
        case USM_M_UIR:
            usm_process_uir(ctx, NULL, out);
            break;
        case USM_M_RAW:
            usm_process_raw(ctx, NULL, out);
            break;
        default:
            out->len = 0;
    }
}

void
usm_process_user(usm_ctx *ctx, buffer *in, buffer *out)
{
    rp_ctx rp;
   
    switch (in->buf[0])
    {
        case UIRT_CMD_MODE_UIR:
            ctx->mode = USM_M_UIR;
            ctx->state = USM_W_STATUS;
            break;
        case UIRT_CMD_MODE_RAW:
            ctx->mode = USM_M_RAW;
            ctx->state = USM_W_STATUS;
            break;
        case UIRT_CMD_GET_VERSION:
            ctx->state = USM_W_VER;
            break;
        case UIRT_CMD_MODE_RAW2:
            ctx->mode = USM_M_RAW2;
            ctx->state = USM_W_STATUS;
            break;
        case UIRT_CMD_GET_GPIO_CAPS:
        case UIRT_CMD_GET_GPIO_CFG:
        case UIRT_CMD_SET_GPIO_CFG:
        case UIRT_CMD_GET_GPIO:
        case UIRT_CMD_SET_GPIO:
        case UIRT_CMD_REFRESH_GPIO:
        case UIRT_CMD_GET_CFG:
        case UIRT_CMD_TX_RAW:
        case UIRT_CMD_TX_STRUCT:
        case UIRT_CMD_TX_PRONTO:
            ctx->state = USM_W_STATUS;
            break;
        default:
            ERR("Unknown command, stopping interpreatation!\n");
            ctx->mode = USM_M_ECHO;
            ctx->state = USM_W_STATUS;
            break;
    }
    
    ctx->agg.len = 0;
    
    if (in->buf[0] == UIRT_CMD_TX_PRONTO)
    {
        // futz with the pronto encoding and make it RAW
        rp_parse(&rp, in->len, in->buf);
        out->len = rp_output(&rp, out->buf);
        
        {
            /* output generated RAW for debug purposes
            */
            buffer buf;
            UInt8 bufbuf[1024];
            
            buf_attach(&buf, sizeof(bufbuf), bufbuf);
            
            u_buf2hex(out, &buf);
            DMP("%s\n", buf.buf);
        }
        
    } else
    {
        // pass-thru
        buf_copy(in, out);
    }

    usm_checksum(out);
}

void 
usm_set_default_frequency(usm_ctx *ctx, UInt32 frequency)
{
    ctx->default_frequency = frequency;   
}

void
usm_process_uir(usm_ctx *ctx, buffer *in, buffer *out)
{
    if (in)
    {
        buf_append(&ctx->agg, in);
    }
    
    if (ctx->agg.len >= UIRT_UIR_CODE_LEN)
    {
        bcopy(ctx->agg.buf, out->buf, UIRT_UIR_CODE_LEN);
        out->len = UIRT_UIR_CODE_LEN;
        if (ctx->agg.len > UIRT_UIR_CODE_LEN)
        {
            bcopy(&ctx->agg.buf[UIRT_UIR_CODE_LEN], ctx->agg.buf, ctx->agg.len - UIRT_UIR_CODE_LEN);
        }
        ctx->agg.len -= UIRT_UIR_CODE_LEN;
    } else
    {
        out->len = 0;
    }
}

void
usm_process_raw(usm_ctx *ctx, buffer *in, buffer *out)
{
    rr_ctx rr;
    rr_ret ret;
  
    if (in)
    {
        if (ctx->agg.len + in->len > USM_AGG_MAX)
        {
            ERR("agg max passed, need %d\n", (int)(ctx->agg.len + in->len));
        } else
        {
            buf_append(&ctx->agg, in);
        }
    }
    
    ret = rr_parse(&rr, ctx->agg.len, ctx->agg.buf);
    if (ret.n)
    {
        // process only if something useful was found
        
        if (ctx->default_frequency)
        {
            rr_set_frequency(&rr, ctx->default_frequency);
        }
        
        out->len = rr_output(&rr, out->buf);
    
        // fix up the aggregation buffer
        if (ret.m != ret.n)
        {
            buf_slide(&ctx->agg, ret.m);
            bcopy(ret.d, ctx->agg.buf, ret.n - ret.m);
        } else
        {
            ctx->agg.len = 0;
        }
    } else
    {
        out->len = 0;
    }
}

void
usm_process_raw2(usm_ctx *ctx, buffer *in, buffer *out)
{
    rr2_ctx rr;
    rr2_ret ret;
    if (in)
    {
        if (ctx->agg.len + in->len > USM_AGG_MAX)
        {
            ERR("agg max passed, need %d\n", (int)(ctx->agg.len + in->len));
        } else
        {
            buf_append(&ctx->agg, in);
        }
    }
    
    // parse the data 
    ret = rr2_parse(&rr, ctx->agg.len, ctx->agg.buf);
    if (ret.n)
    {
        // prettify the data
        out->len = rr2_output(&rr, out->buf);
        
        // fix up the aggregation buffer
        if (ret.m != ret.n)
        {
            buf_slide(&ctx->agg, ret.m);
            bcopy(ret.d, ctx->agg.buf, ret.n - ret.m);
        } else
        {
            ctx->agg.len = 0;
        }
    } else
    {
        out->len = 0;
    }
}

void
usm_process_thru(usm_ctx *ctx, buffer *in, buffer *out)
{
    // passthru response
    buf_copy(in, out);
    
    ctx->state = USM_W_CODE;
}

int
usm_checksum(buffer *buf)
{
	int check = 0;
	UInt32 i;
    
    if (buf->len == buf->max)
    {
        ERR("Unable to append checksum, buffer not big enough\n");
        return -1;
    }
    
    check = 0;
	for (i = 0; i < buf->len; i++) 
    {
		check = check - buf->buf[i];
	}
	
    buf->buf[buf->len] = check & 0xff;
    buf->len++;
    
    return 0;
}

