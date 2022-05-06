#ifndef __LOG_H__
#define __LOG_H__

#include <stdio.h>

/*
 * Log level flag is used in subscribtion for changing log
 * levels. And to ensure consistent experience of different
 * micro services, this value is defined in log.h but it is
 * not used in log.c
 */
#define LOG_LEVEL_FLAG_VALUE    	"log_level"

/*
 * Valid log level names. These values are used in the
 * log_set_level function and it will be converted into a 
 * numberical values.
 * 
 * Here use string is for easier parsing from command line.
 */
#define LOG_LEVEL_ERROR_NAME		"error"
#define LOG_LEVEL_ERROR_WARNING		"warning"
#define LOG_LEVEL_ERROR_INFO		"info"
#define LOG_LEVEL_ERROR_DEBUG		"debug"
#define LOG_LEVEL_ERROR_DETAILS		"details"

/*
 * Numberical values for different log values. lower value
 * means higher priority.
 */
#define LOG_LEVEL_INFO				0
#define LOG_LEVEL_ERROR				1
#define LOG_LEVEL_WARNING			2
#define LOG_LEVEL_DEBUG				3
#define LOG_LEVEL_DETAILES			4

/*
 * Parse the input string and convert it to an internal
 * numberical value and store it as current log level
 */
int log_set_level(const char* level);

/*
 * This function will print the valid log info levels
 * and corresponding string values for reference.
 * Usually used in response of invalid input to 
 * log_set_level.
 */
void log_print_level_info(void);

/*
 * This is used in below macros to print messages.
 * User are not encouraged to use this directly, but
 * it can be called from end user.
 */
void log_with_level(const int level, const char* fmt, ...);

/*
 * Below is used in app code to provider better readability
 * The usage is similiar with printf except it automatically
 * filter messages according to log level
 */
#define LOG_ERROR(fmt, ...)		log_with_level(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__);
#define LOG_WARNING(fmt, ...)	log_with_level(LOG_LEVEL_WARNING, fmt, ##__VA_ARGS__);
#define LOG_INFO(fmt, ...)		log_with_level(LOG_LEVEL_INFO, fmt, ##__VA_ARGS__);
#define LOG_DEBUG(fmt, ...)		log_with_level(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__);
#define LOG_DETAILS(fmt, ...)	log_with_level(LOG_LEVEL_DETAILES, fmt, ##__VA_ARGS__);

#endif
