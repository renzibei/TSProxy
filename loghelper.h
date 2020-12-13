#ifndef LOGHELPER_H
#define LOGHELPER_H

#define LOG_INFO

enum  LogType{Error, Warn, Info, Debug, Trace};

class LogHelper {

public:
    static void log(LogType type, const char * format, ...);
};

#endif