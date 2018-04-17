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

void rterror(const char *fmt, ...);
void rtinfo(const char *fmt, ...);
void rtwarn(const char *fmt, ...);
void rtdealloc(void * mem);
void *rtrealloc(void * mem, size_t size);
void *rtalloc(size_t size);

void * default_rt_allocator(size_t size);
void * default_rt_reallocator(void * mem, size_t size);
void default_rt_deallocator(void * mem);
void default_rt_error_handler(const char * fmt, va_list ap);
void default_rt_warning_handler(const char * fmt, va_list ap);
void default_rt_info_handler(const char * fmt, va_list ap);
#endif //RASTERDB_RT_CONTEXT_H
