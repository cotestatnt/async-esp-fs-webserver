#ifndef __SERIALLOG_H__
#define __SERIALLOG_H__

#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define __FILE_NAME__ (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)
#define _LOG_FORMAT(letter, format)  "\n["#letter"][%s:%u] %s():\t" format, __FILE_NAME__, __LINE__, __FUNCTION__

#if DEBUG_MODE
#define log_debug(format, ...) DBG_OUTPUT_PORT.printf(_LOG_FORMAT(D, format), ##__VA_ARGS__)
#define log_error(format, ...) DBG_OUTPUT_PORT.printf(_LOG_FORMAT(E, format), ##__VA_ARGS__)
#define log_info(format, ...) DBG_OUTPUT_PORT.printf(_LOG_FORMAT(I, format), ##__VA_ARGS__)
#else
#define log_debug(format, ...)
#define log_error(format, ...)
#define log_info(format, ...)
#endif


#ifdef __cplusplus
}
#endif

#endif