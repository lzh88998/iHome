#include <stdarg.h>
#include "log.h"

static int gs_log_level = LOG_LEVEL_ERROR;
void log_set_level(int level) {
	gs_log_level = level;
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
