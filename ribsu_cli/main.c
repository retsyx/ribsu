/* Copyright (C) 2007 xyster.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdio.h>
#include <unistd.h>
#include <CoreFoundation/CoreFoundation.h>

#include "debug.h"
#include "ribsu-util.h"
#include "usb.h"
#include "tty.h"
#include "uirt-raw.h"
#include "uirt-sm.h"
#include "ribsu.h"

#define MODULE_NAME main
DBG_MODULE_DEFINE();

ribsu_ctx ribsu;

static void ribsu_read_callback(void *ctx0, buffer *buf);
void stdin_read_callback(CFSocketRef s, 
                         CFSocketCallBackType callbackType, 
                         CFDataRef address, 
                         const void *data, 
                         void *info);
void signal_handler(int sigraised);
void usage(void);

int 
main(int argc, char **argv)
{
    int f;
    ribsu_opts opts;
    sig_t old_handler;
    
    old_handler = signal(SIGINT, signal_handler);
    if (old_handler == SIG_ERR)
    {
        ERR("Could not establish new signal handler (err = %d)\n", errno);
    }
    
    bzero(&opts, sizeof(opts));
    
    while ((f = getopt(argc, argv, "ut:v:p:d")) >= 0)
    {
        switch (f)
        {
            case 'u':
                // Use USB
                opts.use_usb = 1;
                break;
            case 't':
                // Use tty device specified
                opts.use_tty = 1;
                if (optarg[0] != '-')
                {
                    strlcpy(opts.tty_dev_name, optarg, sizeof(opts.tty_dev_name));
                }
                break;
            case 'v':
                opts.vid = strtol(optarg, NULL, 0); 
                break;
            case 'p':
                opts.pid = strtol(optarg, NULL, 0);
                break;
            case 'd':
                dbg_level_main++;
                dbg_level_uirt_raw++;
                dbg_level_uirt_raw2++;
                dbg_level_uirt_pronto++;
                dbg_level_uirt_sm++;
                dbg_level_usb++;
                dbg_level_tty++;
                dbg_level_ribsu++;
                break;
            case '?':
                usage();
                return 1;
                break;
        }
    }

    if (!opts.use_usb  &&  !opts.use_tty)
    {
        opts.use_usb = opts.use_tty = 1;
    }
    
    if (add_fd_source(STDIN_FILENO, NULL, stdin_read_callback, NULL))
    {
        ERR("Failed to open stdin\n");
        return 1;
    }
        
    if (ribsu_init(&ribsu, &opts))
    {
        ERR("Failed to initialize ribsu\n");
        return 1;
    }
    
    ribsu_set_callback(&ribsu, ribsu_read_callback, NULL);
    
    CFRunLoopRun();
   
    ribsu_deinit(&ribsu);
    
    return 0;
}

void 
ribsu_read_callback(void *ctx0, buffer *buf)
{
    buffer *hex;
    UInt32 i;
    hex = buf_alloc(1025);
    if (!hex)
    {
        ERR("Failed to allocate buffer\n");
        return;
    }

    u_buf2hex(buf, hex);
    i = 0;
    while (hex->buf[i])
    {
        printf("%c", hex->buf[i]);
        if ((i & 3) == 3)
        {
            printf(" ");
        }
        i++;
    }
    printf("\n");
    
    buf_free(hex);
}

void 
stdin_read_callback(CFSocketRef s, 
                    CFSocketCallBackType callbackType, 
                    CFDataRef address, 
                    const void *data, 
                    void *info)
{
    FILE *fin;
    int c;
    buffer *hex, *raw;
    UInt32 n;
    
    raw = buf_alloc(512);
    if (!raw)
    {
        ERR("Failed to allocate buffer\n");
        return;
    }
    
    hex = buf_alloc(1025);
    if (!hex)
    {
        ERR("Failed to allocate buffer\n");
        return;
    }
    
    fin = info;
    
    n = 0;
    while ((c = fgetc(fin)) != '\n'  &&  c != ' ' &&  c != EOF  &&  n < hex->max)
    {
        hex->buf[n++] = c;
    }
    
    if (ferror(fin)  ||  feof(fin))
    {
        CFRunLoopStop(CFRunLoopGetCurrent());
        goto out;
    }
    
    if (!n) goto out;
 
    hex->buf[n++] = '\0';
    hex->len = n;
    
    switch (hex->buf[0])
    {
        case 'Q': // Quit
            CFRunLoopStop(CFRunLoopGetCurrent());
            break;
        case 'F': // Set freq. for RAW mode
            if (hex->len > 2)
            {
                n = strtol((char *)&hex->buf[1], NULL, 0);
            } else
            {
                n = 0;
            }
            ribsu_set_default_frequency(&ribsu, n);
            printf("Default frequency %dHz", (int)n);
            break;
        case 'I': // toggle interpretation
             if (hex->len > 2)
             {
                 n = strtol((char *)&hex->buf[1], NULL, 0);
             } else
             {
                 n = 0;
             }
            
            n = ribsu_toggle_interpretation(&ribsu, n);
            printf("I%d\n", (int)n); // echo the previous mode 
            break;
        default:
            u_hex2buf(hex, raw);
            ribsu_write(&ribsu, raw);
    }
    
out:
        
    buf_free(hex);
    buf_free(raw);
}

void 
signal_handler(int sigraised)
{
    CFRunLoopStop(CFRunLoopGetCurrent());
}

void
usage(void)
{
    USG("ribsu [-u] [-v VID] [-p PID] | [-t <device>] [-d]\n"
        "\t-u try direct USB using IOKit\n"
        "\t-t try TTY device specified, - to auto-detect device name (requires FTDI driver, version 2.0 or better)\n"
        "\t-v use USB VID\n"
        "\t-p use USB PID\n"
        "\t-d increment debug level\n");   
}

