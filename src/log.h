#ifndef __LOG_H__
#define __LOG_H__

#include <stdio.h>

#define LOG_LEVEL_ERROR_NAME		"error"
#define LOG_LEVEL_ERROR_WARNING		"warning"
#define LOG_LEVEL_ERROR_INFO		"info"
#define LOG_LEVEL_ERROR_DEBUG		"debug"
#define LOG_LEVEL_ERROR_DETAILS		"details"

#define LOG_LEVEL_INFO				0
#define LOG_LEVEL_ERROR				1
#define LOG_LEVEL_WARNING			2
#define LOG_LEVEL_DEBUG				3
#define LOG_LEVEL_DETAILES			4

int log_set_level(const char* level);
void log_print_level_info(void);
void log_with_level(const int level, const char* fmt, ...);

#define LOG_ERROR(fmt, ...)		log_with_level(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__);
#define LOG_WARNING(fmt, ...)	log_with_level(LOG_LEVEL_WARNING, fmt, ##__VA_ARGS__);
#define LOG_INFO(fmt, ...)		log_with_level(LOG_LEVEL_INFO, fmt, ##__VA_ARGS__);
#define LOG_DEBUG(fmt, ...)		log_with_level(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__);
#define LOG_DETAILS(fmt, ...)	log_with_level(LOG_LEVEL_DETAILES, fmt, ##__VA_ARGS__);

#endif
