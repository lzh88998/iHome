#include <stdarg.h>
#include <string.h>
#include "log.h"

static int gs_log_level = LOG_LEVEL_ERROR;
int log_set_level(const char* level) {
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
    else {
	printf("Invalid log level %s\r\n", level);
	return -1;
    }
	
    printf("Log level set to %s value %d\r\n", level, gs_log_level);
    return 0;
}

void log_with_level(const int level, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
	    
    if(level <= gs_log_level) {
	vprintf(fmt, ap);
	printf("\r\n");
    }
    va_end(ap);
}

void log_print_level_info(void) {
    printf("Valid log leves are: \r\n");
    printf("%s\tOnly show error messages. \r\n", LOG_LEVEL_ERROR_NAME);
    printf("%s\tShow warning and error messages. \r\n", LOG_LEVEL_ERROR_WARNING);
    printf("%s\tShow information, warning and error. \r\n", LOG_LEVEL_ERROR_INFO);
    printf("%s\tShow debug, information, warning and error. \r\n", LOG_LEVEL_ERROR_DEBUG);
    printf("%s\tShow all messages. \r\n", LOG_LEVEL_ERROR_DETAILS);
}
