//
// Created by 何文婷 on 18/3/21.
//

#ifndef RASTERDB_RT_CONTEXT_H
#define RASTERDB_RT_CONTEXT_H
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include "postgres.h"
#include "utils/builtins.h"

extern void rterror(const char *fmt, ...);
extern void rtinfo(const char *fmt, ...);
extern void rtwarn(const char *fmt, ...);
extern void rtdealloc(void * mem);
extern void *rtrealloc(void * mem, size_t size);
extern void *rtalloc(size_t size);

#endif //RASTERDB_RT_CONTEXT_H
