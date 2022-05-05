#ifndef __LOG_H__
#define __LOG_H__

#include <stdio.h>

#define LOG_LEVEL_ERROR		0
#define LOG_LEVEL_WARNING	1
#define LOG_LEVEL_INFO		2
#define LOG_LEVEL_DEBUG		3
#define LOG_LEVEL_DETAILES	4

void log_set_level(int level);
void log_with_level(int level, const char* fmt, ...);

#define LOG_ERROR(fmt, ...)		log_with_level(LOG_LEVEL_ERROR, fmt, __VA_ARGS__);
#define LOG_WARNING(fmt, ...)	log_with_level(LOG_LEVEL_ERROR, fmt, __VA_ARGS__);
#define LOG_INFO(fmt, ...)		log_with_level(LOG_LEVEL_ERROR, fmt, __VA_ARGS__);
#define LOG_DEBUG(fmt, ...)		log_with_level(LOG_LEVEL_ERROR, fmt, __VA_ARGS__);
#define LOG_DETAILS(fmt, ...)	log_with_level(LOG_LEVEL_ERROR, fmt, __VA_ARGS__);

#endif
