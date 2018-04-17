//
// Created by 何文婷 on 18/3/21.
//

#include "rt_context.h"
void *
default_rt_allocator(size_t size)
{
    void *mem = malloc(size);
    return mem;
}

void *
default_rt_reallocator(void *mem, size_t size)
{
    void *ret = realloc(mem, size);
    return ret;
}

void
default_rt_deallocator(void *mem)
{
    free(mem);
}

void
default_rt_error_handler(const char *fmt, va_list ap) {

    static const char *label = "ERROR: ";
    char newfmt[1024] = {0};
    snprintf(newfmt, 1024, "%s%s\n", label, fmt);
    newfmt[1023] = '\0';

    vprintf(newfmt, ap);

    va_end(ap);
}

void
default_rt_warning_handler(const char *fmt, va_list ap) {

    static const char *label = "WARNING: ";
    char newfmt[1024] = {0};
    snprintf(newfmt, 1024, "%s%s\n", label, fmt);
    newfmt[1023] = '\0';

    vprintf(newfmt, ap);

    va_end(ap);
}

void
default_rt_info_handler(const char *fmt, va_list ap) {

    static const char *label = "INFO: ";
    char newfmt[1024] = {0};
    snprintf(newfmt, 1024, "%s%s\n", label, fmt);
    newfmt[1023] = '\0';

    vprintf(newfmt, ap);

    va_end(ap);
}

/**
* Global functions for memory/logging handlers.
*/
typedef void* (*rt_allocator)(size_t size);
typedef void* (*rt_reallocator)(void *mem, size_t size);
typedef void  (*rt_deallocator)(void *mem);
typedef void  (*rt_message_handler)(const char* string, va_list ap)
        __attribute__ (( format(printf,1,0) ));

/**
 * Struct definition here
 */
struct rt_context_t {
    rt_allocator alloc;
    rt_reallocator realloc;
    rt_deallocator dealloc;
    rt_message_handler err;
    rt_message_handler warn;
    rt_message_handler info;
};

/* Static variable, to be used for all rt_core functions */
static struct rt_context_t ctx_t = {
        .alloc = default_rt_allocator,
        .realloc = default_rt_reallocator,
        .dealloc = default_rt_deallocator,
        .err = default_rt_error_handler,
        .warn = default_rt_warning_handler,
        .info = default_rt_info_handler
};

void *
rtalloc(size_t size) {
    void * mem = ctx_t.alloc(size);
//    RASTER_DEBUGF(5, "rtalloc called: %d@%p", size, mem);
    return mem;
}


void *
rtrealloc(void * mem, size_t size) {
    void * result = ctx_t.realloc(mem, size);
//    RASTER_DEBUGF(5, "rtrealloc called: %d@%p", size, result);
    return result;
}

void
rtdealloc(void * mem) {
    ctx_t.dealloc(mem);
//    RASTER_DEBUG(5, "rtdealloc called");
}

void
rterror(const char *fmt, ...) {
	va_list ap;
	elog(ERROR,"rterror");

    va_start(ap, fmt);

    /* Call the supplied function */
    (*ctx_t.err)(fmt, ap);

    va_end(ap);
}

void
rtinfo(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);

    /* Call the supplied function */
    (*ctx_t.info)(fmt, ap);

    va_end(ap);
}


void
rtwarn(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);

    /* Call the supplied function */
    (*ctx_t.warn)(fmt, ap);

    va_end(ap);
}
