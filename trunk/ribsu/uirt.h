/* Copyright (C) 2007 xyster.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#ifndef __UIRT_H
#define __UIRT_H

#define UIRT_CMD_MODE_UIR      (0x20)
#define UIRT_CMD_MODE_RAW      (0x21)
#define UIRT_CMD_GET_VERSION   (0x23)
#define UIRT_CMD_MODE_RAW2     (0x24)
#define UIRT_CMD_GET_GPIO_CAPS (0x30)
#define UIRT_CMD_GET_GPIO_CFG  (0x31)
#define UIRT_CMD_SET_GPIO_CFG  (0x32)
#define UIRT_CMD_GET_GPIO      (0x33)
#define UIRT_CMD_SET_GPIO      (0x34)
#define UIRT_CMD_REFRESH_GPIO  (0x35)
#define UIRT_CMD_TX_RAW        (0x36)
#define UIRT_CMD_TX_STRUCT     (0x37)
#define UIRT_CMD_GET_CFG       (0x38)

#define UIRT_STATUS_TXING      (0x20)
#define UIRT_STATUS_OK         (0x21)
#define UIRT_STATUS_CSUM_ERROR (0x80)
#define UIRT_STATUS_TO_ERROR   (0x81)
#define UIRT_STATUS_CMD_ERROR  (0x82)

#define UIRT_UIR_CODE_LEN    (6)

#define UIRT_CMD_O_LENGTH        (1)
#define UIRT_CMD_TX_RAW_O_LENGTH (6)

#pragma pack(1)

typedef struct uirt_tx_cmd
{
    UInt8  op; // UIRT_CMD_TX_RAW
    UInt8  len;
    UInt8  freq;
    UInt8  repeat_count;
    UInt16 interspace; // big endian!
    UInt8  data_len; // pulse/space data len
    UInt8  data[0];
} uirt_tx_cmd;

#pragma pack()

#endif

