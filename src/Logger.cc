#include "Logger.h"
#include "Timestamp.h"

// fetch the only instance object of the log singleton
Logger &Logger::instance()
{
    static Logger logger;
    return logger;
}

// set log level
void Logger::setLogLevel(int level)
{
    logLevel_ = level;
}

// write log
void Logger::log(std::string msg)
{
    std::string pre = "";
    switch (logLevel_)
    {
    case INFO:
        pre = "[INFO] ";
        break;
    case ERROR:
        pre = "[ERROR] ";
        break;
    case FATAL:
        pre = "[FATAL] ";
        break;
    case DEBUG:
        pre = "[DEBUG] ";
        break;
    default:
        break;
    }
    std::cout << pre + Timestamp::now().toString() + " : " + msg << std::endl;
}