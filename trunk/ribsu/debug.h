/* Copyright (C) 2007 xyster.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __DEBUG_H
#define __DEBUG_H


#define DBG_ERR_LVL 0
#define DBG_LOG_LVL 1
#define DBG_DBG_LVL 2
#define DBG_DMP_LVL 3

#define DBG_MODULE() extern DBG_MODULE_DEFINE()
#define DBG_MODULE_OTHER(NAME) extern DBG_MODULE_DEFINE2(NAME)
#define DBG_MODULE_DEFINE() DBG_MODULE_DEFINE2(MODULE_NAME)
#define DBG_MODULE_DEFINE2(module) DBG_MODULE_DEFINE3(module)
#define DBG_MODULE_DEFINE3(module) int dbg_level_##module

#define OUT(type, level, x...) do { \
    OUT2(MODULE_NAME, __LINE__, type, level, x); \
} while (0)

#define OUT2(module, line, type, level, x...) OUT3(module, line, type, level, x)

#define OUT3(module, line, type, level, x...) if (dbg_level_##module >= level) \
{ \
    OUT4(type": "#module": "__FILE__"("#line"): "x); \
} \

#define OUT4(x...) fprintf(stderr, x) 

#define USG(x...) fprintf(stderr, x)
#define ERR(x...) OUT("ERR", DBG_ERR_LVL, x)
#define LOG(x...) OUT("LOG", DBG_LOG_LVL, x)
#define DBG(x...) OUT("DBG", DBG_DBG_LVL, x)
#define DMP(x...) OUT("DMP", DBG_DMP_LVL, x)



#endif
