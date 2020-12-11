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
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
        fprintf(stderr, "\n");
    }
}