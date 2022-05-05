#include <stdarg.h>
#include <string.h>
#include "log.h"

#define LOG_LEVEL_ERROR_NAME		"error"
#define LOG_LEVEL_ERROR_WARNING		"warning"
#define LOG_LEVEL_ERROR_INFO		"info"
#define LOG_LEVEL_ERROR_DEBUG		"debug"
#define LOG_LEVEL_ERROR_DETAILS		"details"

#define LOG_LEVEL_ERROR				0
#define LOG_LEVEL_WARNING			1
#define LOG_LEVEL_INFO				2
#define LOG_LEVEL_DEBUG				3
#define LOG_LEVEL_DETAILES			4

static int gs_log_level = LOG_LEVEL_ERROR;
int log_set_level(char* level) {
    if(0 == strcmp(LOG_LEVEL_ERROR_NAME, level))
	gs_log_level = LOG_LEVEL_ERROR;
    else if(0 == strcmp(LOG_LEVEL_ERROR_WARNING, level))
	gs_log_level = LOG_LEVEL_WARNING;
    else if(0 == strcmp(LOG_LEVEL_ERROR_INFO, level))
	gs_log_level = LOG_LEVEL_INFO;
    else if(0 == strcmp(LOG_LEVEL_ERROR_DEBUG, level))
	gs_log_level = LOG_LEVEL_DEBUG;
    else if(0 == strcmp(LOG_LEVEL_ERROR_DETAILS, level))
	gs_log_level = LOG_LEVEL_DETAILES;
    else
	return -1;
    return 0;
}

void log_with_level(int level, const char* fmt, ...) {
	if(level <= gs_log_level) {
		va_list ap;
		va_start(ap, fmt);
		printf(fmt, ap);
		printf("\r\n");
		va_end(ap);
	}
}

void log_print_level_info(void) {
    printf("Valid log leves are: \r\n");
    printf("%s\tOnly show error messages. \r\n", LOG_LEVEL_ERROR_NAME);
    printf("%s\tShow warning and error messages. \r\n", LOG_LEVEL_ERROR_WARNING);
    printf("%s\tShow information, warning and error. \r\n", LOG_LEVEL_ERROR_INFO);
    printf("%s\tShow debug, information, warning and error. \r\n", LOG_LEVEL_ERROR_DEBUG);
    printf("%s\tShow all messages. \r\n", LOG_LEVEL_ERROR_DETAILS);
}
