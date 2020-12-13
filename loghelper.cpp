#include "loghelper.h"
#include <stdio.h>
#include <stdarg.h>



void LogHelper::log(LogType type, const char * format, ...) {
#ifdef LOG_TRACE
    if(type <= Trace)
#elif defined LOG_DEBUG
    if(type <= Debug)
#elif defined LOG_INFO
    if(type <= Info)
#elif defined LOG_WARN
    if(type <= Warn)
#endif
    {
        va_list args;
        if (type == Error) {
            fprintf(stderr, "\033[40;31m");
        }
        else if (type == Warn) {
            fprintf(stderr, "\033[40;33m");
        }
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
        if (type == Error || type == Warn) {
            fprintf(stderr, "\033[0m");
        }
        fprintf(stderr, "\n");
    }
}