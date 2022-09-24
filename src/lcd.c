/*
 * Copyright lzh88998 and distributed under Apache 2.0 license
 * 
 * LCD is a micro service that control display content on LCD
 * LCD have tight relationship with touch service as display
 * content will determine the touch behavior
 * 
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <hiredis.h>
#include <async.h>
#include <adapters/libevent.h>

#include <iconv.h>

#include "log.h"
#include "to_socket.h"

/*
 * Default address and port for redis 
 * connection
 */
#define REDIS_IP                    "127.0.0.1"
#define REDIS_PORT                  6379

/*
 * LCD coordinates
 */
#define LCD_MIN_X                   0
#define LCD_MIN_Y                   0
#define LCD_MAX_X                   319
#define LCD_MAX_Y                   239

/*
 * Color constant used to draw things
 * on lcd
 */
#define COLOR_RED                   0xF800
#define COLOR_GREEN                 0x07E0
#define COLOR_BLUE                  0x001F
#define COLOR_WHITE                 0xFFFF
#define COLOR_BLACK                 0x0000

/*
 * Here define the background color of
 * lcd this makes change the background
 * easier
 */
#define BG_COLOR                    COLOR_BLACK

/*
 * Here define the foreground color of
 * lcd this makes change the background
 * easier
 */
#define FG_COLOR                    COLOR_WHITE

/*
 * Return value for sending command to lcd
 */
#define SND_RCV_OK                  0;

/*
 * Due to the limit of lcd refresh rate,
 * sensor values can not be updated in 
 * very short time, update happens only
 * when last update happens greater than 
 * this interval value
 */
#define SENSOR_VALUE_INTERVAL_MS    3000

/*
 * Identifier used in redis keys for this
 * micro service
 */
#define FLAG_KEY                    "lcd"

/*
 * Configuration items in redis hash
 */
#define SWITCH_TOPIC                "switch"
#define SENSOR_TOPIC                "sensor"
#define BRIGHTNESS_TOPIC            "brightness"
#define AREA1_TOPIC                 "area1"
#define AREA2_TOPIC                 "area2"
#define NAME_TOPIC                  "name"
#define TEMP_TOPIC                  "temp"
#define TARGET_TEMP_TOPIC           "target_temp"

/*
 * Line seperate SW and display info
 */
#define LCD_FUNCTION_SEP_LINE_X_START       160
#define LCD_FUNCTION_SEP_LINE_X_END         160
#define LCD_FUNCTION_SEP_LINE_Y_START       0
#define LCD_FUNCTION_SEP_LINE_Y_END         239

/*
 * Time display area
 */
#define TIME_X_START                        0
#define TIME_X_END                          159
#define TIME_Y_START                        0
#define TIME_Y_END                          59

/*
 * Time font_size
 */
#define TIME_FONT_SIZE                      48

/*
 * Line seperate time and weather forcast
 */
#define TIME_WEATHER_SEP_LINE_X_START       0
#define TIME_WEATHER_SEP_LINE_X_END         160
#define TIME_WEATHER_SEP_LINE_Y_START       60
#define TIME_WEATHER_SEP_LINE_Y_END         60

/*
 * Forcast display area
 */
#define FORCAST_X_START                     0
#define FORCAST_X_END                       79
#define FORCAST_Y_START                     61
#define FORCAST_Y_END                       139

/*
 * Time font_size
 */
#define FORCAST_FONT_SIZE                   32

/*
 * Weather forcast and detail data seperate line
 */
#define FORCAST_DATA_SEP_LINE_X_START       80
#define FORCAST_DATA_SEP_LINE_X_END         80
#define FORCAST_DATA_SEP_LINE_Y_START       60
#define FORCAST_DATA_SEP_LINE_Y_END         140

/*
 * Forcast temperature display area
 */
#define FORCAST_TEMP_X_START                81
#define FORCAST_TEMP_X_END                  159
#define FORCAST_TEMP_Y_START                61
#define FORCAST_TEMP_Y_END                  79

/*
 * Forcast temperature font_size
 */
#define FORCAST_TEMP_FONT_SIZE              16

/*
 * Weather forcast detail temperature humidity seperate line
 */
#define FORCAST_TEMP_SEP_LINE_X_START       80
#define FORCAST_TEMP_SEP_LINE_X_END         160
#define FORCAST_TEMP_SEP_LINE_Y_START       80
#define FORCAST_TEMP_SEP_LINE_Y_END         80

/*
 * Forcast humidity display area
 */
#define FORCAST_HUMIDITY_X_START            81
#define FORCAST_HUMIDITY_X_END              159
#define FORCAST_HUMIDITY_Y_START            81
#define FORCAST_HUMIDITY_Y_END              99

/*
 * Forcast humidity font_size
 */
#define FORCAST_HUMIDITY_FONT_SIZE          16

/*
 * Weather forcast detail humidity wind seperate line
 */
#define FORCAST_HUMIDITY_SEP_LINE_X_START   80
#define FORCAST_HUMIDITY_SEP_LINE_X_END     160
#define FORCAST_HUMIDITY_SEP_LINE_Y_START   100
#define FORCAST_HUMIDITY_SEP_LINE_Y_END     100

/*
 * Forcast wind display area
 */
#define FORCAST_WIND_X_START                81
#define FORCAST_WIND_X_END                  159
#define FORCAST_WIND_Y_START                101
#define FORCAST_WIND_Y_END                  119

/*
 * Forcast wind font_size
 */
#define FORCAST_WIND_FONT_SIZE              16

/*
 * Weather forcast detail wind AQI seperate line
 */
#define FORCAST_WIND_SEP_LINE_X_START       80
#define FORCAST_WIND_SEP_LINE_X_END         160
#define FORCAST_WIND_SEP_LINE_Y_START       120
#define FORCAST_WIND_SEP_LINE_Y_END         120

/*
 * Forcast AQI display area
 */
#define FORCAST_AQI_X_START                 81
#define FORCAST_AQI_X_END                   159
#define FORCAST_AQI_Y_START                 121
#define FORCAST_AQI_Y_END                   139

/*
 * Forcast AQI font_size
 */
#define FORCAST_AQI_FONT_SIZE               16

/*
 * Weather forcast and area 1 seperate line
 */
#define FORCAST_AREA1_SEP_LINE_X_START      0
#define FORCAST_AREA1_SEP_LINE_X_END        160
#define FORCAST_AREA1_SEP_LINE_Y_START      140
#define FORCAST_AREA1_SEP_LINE_Y_END        140

/*
 * Area 1 name display area
 */
#define AREA1_NAME_X_START                 0
#define AREA1_NAME_X_END                   49
#define AREA1_NAME_Y_START                 141
#define AREA1_NAME_Y_END                   159

/*
 * Area 1 name font_size
 */
#define AREA1_NAME_FONT_SIZE               16

/*
 * Area 1 temperature display area
 */
#define AREA1_TEMP_X_START                 55
#define AREA1_TEMP_X_END                   120
#define AREA1_TEMP_Y_START                 141
#define AREA1_TEMP_Y_END                   159

/*
 * Area 1 temperature font_size
 */
#define AREA1_TEMP_FONT_SIZE               16

/*
 * Area 1 brightness display area
 */
#define AREA1_BRIGHTNESS_X_START           125
#define AREA1_BRIGHTNESS_X_END             159
#define AREA1_BRIGHTNESS_Y_START           141
#define AREA1_BRIGHTNESS_Y_END             159

/*
 * Area 1 brightness font_size
 */
#define AREA1_BRIGHTNESS_FONT_SIZE         16

/*
 * Area 1 and area 2 seperate line
 */
#define AREA1_AREA2_SEP_LINE_X_START        0
#define AREA1_AREA2_SEP_LINE_X_END          160
#define AREA1_AREA2_SEP_LINE_Y_START        160
#define AREA1_AREA2_SEP_LINE_Y_END          160

/*
 * Area 2 name display area
 */
#define AREA2_NAME_X_START                 0
#define AREA2_NAME_X_END                   49
#define AREA2_NAME_Y_START                 161
#define AREA2_NAME_Y_END                   179

/*
 * Area 2 name font_size
 */
#define AREA2_NAME_FONT_SIZE               16

/*
 * Area 2 temperature display area
 */
#define AREA2_TEMP_X_START                 55
#define AREA2_TEMP_X_END                   120
#define AREA2_TEMP_Y_START                 161
#define AREA2_TEMP_Y_END                   179

/*
 * Area 2 temperature font_size
 */
#define AREA2_TEMP_FONT_SIZE               16

/*
 * Area 2 brightness display area
 */
#define AREA2_BRIGHTNESS_X_START           125
#define AREA2_BRIGHTNESS_X_END             159
#define AREA2_BRIGHTNESS_Y_START           161
#define AREA2_BRIGHTNESS_Y_END             179

/*
 * Area 2 brightness font_size
 */
#define AREA2_BRIGHTNESS_FONT_SIZE         16

/*
 * Area 2 and target temperature seperate line
 */
#define AREA2_TARGET_SEP_LINE_X_START       0
#define AREA2_TARGET_SEP_LINE_X_END         160
#define AREA2_TARGET_SEP_LINE_Y_START       180
#define AREA2_TARGET_SEP_LINE_Y_END         180

/*
 * Target temperature minus seperate line
 */
#define TARGET_MINUS_SEP_LINE_X_START       30
#define TARGET_MINUS_SEP_LINE_X_END         30
#define TARGET_MINUS_SEP_LINE_Y_START       180
#define TARGET_MINUS_SEP_LINE_Y_END         239

/*
 * Target temperature minus display area
 */
#define TARGET_MINUS_X_START                0
#define TARGET_MINUS_X_END                  29
#define TARGET_MINUS_Y_START                181
#define TARGET_MINUS_Y_END                  239

/*
 * Target temperature minus font_size
 */
#define TARGET_MINUS_FONT_SIZE              48

/*
 * Target temperature plus seperate line
 */
#define TARGET_PLUS_SEP_LINE_X_START        130
#define TARGET_PLUS_SEP_LINE_X_END          130
#define TARGET_PLUS_SEP_LINE_Y_START        180
#define TARGET_PLUS_SEP_LINE_Y_END          239

/*
 * Target temperature plus display area
 */
#define TARGET_PLUS_X_START                 131
#define TARGET_PLUS_X_END                   159
#define TARGET_PLUS_Y_START                 181
#define TARGET_PLUS_Y_END                   239

/*
 * Target temperature plus font_size
 */
#define TARGET_PLUS_FONT_SIZE               48

/*
 * Target temperature plus display area
 */
#define TARGET_TEMP_X_START                 31
#define TARGET_TEMP_X_END                   129
#define TARGET_TEMP_Y_START                 181
#define TARGET_TEMP_Y_END                   239

/*
 * Target temperature plus font_size
 */
#define TARGET_TEMP_FONT_SIZE               24

/*
 * Switch area X start location
 */
#define SWITCH_X_START                      160

/*
 * Switch area X half location
 */
#define SWITCH_X_HALF                       240

/*
 * Switch area X end location
 */
#define SWITCH_X_END                        319

/*
 * Switch area Y start location
 */
#define SWITCH_Y_START                      0

/*
 * Switch area Y 1/3 location
 */
#define SWITCH_Y_ONE_THIRD                  80

/*
 * Switch area Y half location
 */
#define SWITCH_Y_HALF                       120

/*
 * Switch area Y 2/3 location
 */
#define SWITCH_Y_TWO_THIRD                  160

/*
 * Switch area Y end location
 */
#define SWITCH_Y_END                        239

/*
 * Switch font_size
 */
#define SWITCH_FONT_SIZE                    32

/*
 * Macro for query redis and log info
 */
#define EXEC_REDIS_CMD(reply, goto_label, cmd, ...)		LOG_DEBUG(cmd, ##__VA_ARGS__);\
                                                        reply = redisCommand(gs_sync_context, cmd, ##__VA_ARGS__);\
                                                        if(NULL == reply) {\
                                                            LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);\
                                                            LOG_ERROR(cmd, ##__VA_ARGS__);\
                                                            goto goto_label;\
                                                        }

/*
 * Macro for subscribe redis and log info
 */
#define ASYNC_REDIS_CMD(callback, parameter, cmd, ...)  LOG_DEBUG(cmd, ##__VA_ARGS__);\
                                                        redisAsyncCommand(gs_async_context, callback, parameter, cmd, ##__VA_ARGS__);

/*
 * Store last update time information
 * for sensor values
 */
static struct timeval gs_sensor[4];

/*
 * Char format switch index used to pass 
 * parameters to call back function
 */
static char sw_idx[6] = {'0', '1', '2', '3', '4', '5'};

/*
 * Context for redis connection and controller
 * network connection
 */
static to_socket_ctx gs_socket = -1;
static redisAsyncContext *gs_async_context = NULL;
static redisContext *gs_sync_context = NULL;

/*
 * Stores config for switch items
 */
redisReply *gs_sw_config = NULL;

/*
 * Buffer use to convert and send strings
 */
static char input_buffer[1024];
static char output_buffer[1024];

/*
 * convert object used when converting encoding
 * from utf-8 to gb2312
 */
static iconv_t conv = NULL;

/*
 * Flag for micro srevice exit event, when set 
 * to 1 the micro service will not restart
 * automatically, but exit
 */
static int gs_exit = 0;

/*
 * convert_encoding is used to convert default utf-8
 * encoding to a LCD accept GB2312 encoding when
 * drawing string.
 * 
 * covnert_endoding is only called in draw_string.
 * 
 * convert_encoding has an external dependency iconv.
 * 
 * Note: this conversion happens between input and
 * output buffer. the input buffer need to be filled
 * with utf-8 string first and then the output buffer
 * will contains the converted bytes
 * 
 * Parameters:
 * There is no input parameter
 * 
 * Return value:
 * -1 when error occured and bytes that converted to
 * GB2312 including the NULL terminator.
 * 
 * Note: the input and output buffer is 1024 bytes at
 * max so need to ensure that the string will not 
 * exceed this value
 * 
 */
int convert_encoding(void) {
    LOG_DEBUG("Start convert");
    if(NULL == conv) {
        conv = iconv_open ("gb2312", "utf-8");
    }

    if((iconv_t)-1 == conv) {
        LOG_ERROR("Failed to allocate iconv context %s!", strerror(errno))
        return -1;
    }
    
    char* ci = input_buffer;
    char* co = output_buffer;
    size_t in_size = strlen(ci);
    size_t out_size = 1024;
    
    LOG_DETAILS("Input: %s", ci);
    
    size_t r = iconv (conv, &ci, &in_size, &co, &out_size);
    
    LOG_DETAILS("iConv ret: %d", r);
    LOG_DETAILS("iConv out_size: %d", out_size);
    return 1024 - out_size + 1;
}

/*
 * Send the string to LCD. input string is in UTF-8 encoding
 * and draw_string will convert it to GB2312 then send to 
 * LCD.
 * 
 * Parameters:
 * unsigned int x_start             Location on LCD to draw
 * unsigned int y_start             the string. The limits
 * unsigned int x_end               of these parameters are
 * unsigned int y_end               related with LCD. Common
 *                                  320*240 LCD supports x 
 *                                  from 0 to 319 and y from 
 *                                  0 to 239
 * 
 * unsigned int color               font color, common values
 *                                  are defined in the beginning
 *                                  of this file
 * d
 * unsigned char font_size          font size in pixel, 
 *                                  common font sizes are 24 
 *                                  32 etc.
 * 
 * const char* fmt                  printf style format string
 * 
 * ...                              variable parameters
 *                                  should follow the printf
 *                                  style
 * 
 * Return value:
 * less than 0 if failed, greater than or equal to 0
 * when successful
 * 
 */
int draw_string(unsigned int x_start, unsigned int y_start, unsigned int x_end, unsigned int y_end, unsigned int color, unsigned char font_size, const char* fmt, ...) {
    int ret;
    int converted_cnt = 0;
    va_list ap;
    va_start(ap, fmt);
    LOG_DETAILS("draw_string %d %d %d %d", x_start, y_start, x_end, y_end);
    ret = vsprintf(input_buffer, fmt, ap);
    va_end(ap);
    
    LOG_DETAILS("Convert %s", input_buffer);
    converted_cnt = convert_encoding();

    y_start += (y_end - y_start - font_size)/2;
    y_end = y_start + font_size;

    int cur_idx = 0;
    unsigned int used_len = 0;
    while(cur_idx < converted_cnt - 1) {
        if(0x20 <= output_buffer[cur_idx] && output_buffer[cur_idx] <= 0x7E) {
            used_len += 8;
            cur_idx += 1;
        } else if(0xAA == output_buffer[cur_idx] || 0xAB == output_buffer[cur_idx]) {
            used_len += 8;
            cur_idx += 2;
        } else {
            used_len += 16;
            cur_idx += 2;
        }
    }
    
    LOG_DETAILS("used_len: %d font size: %d x_start: %d x_end: %d", used_len, font_size, x_start, x_end);
    if(used_len * font_size / 16 < x_end - x_start + 1) {
        x_start += (x_end - x_start + 1 - used_len * font_size / 16)/2;
    }
    
    input_buffer[0] = 0x63; //cmd
    input_buffer[1] = ((x_start >> 8) & 0xFF);
    input_buffer[2] = (x_start & 0xFF);
    input_buffer[3] = ((y_start >> 8) & 0xFF);
    input_buffer[4] = (y_start & 0xFF);
    input_buffer[5] = ((x_end >> 8) & 0xFF);
    input_buffer[6] = (x_end & 0xFF);
    input_buffer[7] = ((y_end >> 8) & 0xFF);
    input_buffer[8] = (y_end & 0xFF);
    input_buffer[9] = ((color >> 8) & 0xFF);
    input_buffer[10] = (color & 0xFF);
    input_buffer[11] = font_size;
    
    memcpy(input_buffer+12, output_buffer, converted_cnt);

    input_buffer[converted_cnt + 11] = '\0';
    LOG_DETAILS("send %d bytes: %s", converted_cnt, input_buffer+12);
    
    ret = to_send(gs_socket, input_buffer, converted_cnt + 12, 0);
    LOG_DETAILS("Send result: %d", ret);

    if(0 > ret) {
        LOG_ERROR("Drawstring send error: %s", strerror(errno));
    }
    
    return ret;
}

/*
 * Draw a rectangle on LCD with given color
 * 
 * Parameters:
 * unsigned int x_start             Location on LCD to draw
 * unsigned int y_start             rectangle. The limits of
 * unsigned int x_end               these parameters are
 * unsigned int y_end               related with LCD. Common
 *                                  320*240 LCD supports x 
 *                                  from 0 to 319 and y from 
 *                                  0 to 239
 * 
 * unsigned int color               The color used to fill 
 *                                  the rectangle. Common 
 *                                  values are defined in 
 *                                  the beginning of this 
 *                                  file
 * 
 * Return value:
 * less than 0 if failed, greater than or equal to 0
 * when successful
 * 
 */
 int draw_rectangle(unsigned int x_start, unsigned int y_start, unsigned int x_end, unsigned int y_end, unsigned int color) {
    int ret;
    unsigned char bytes[11];
    
    bytes[0] = 0x62; //cmd
    bytes[1] = ((x_start >> 8) & 0xFF);
    bytes[2] = (x_start & 0xFF);
    bytes[3] = ((y_start >> 8) & 0xFF);
    bytes[4] = (y_start & 0xFF);
    bytes[5] = ((x_end >> 8) & 0xFF);
    bytes[6] = (x_end & 0xFF);
    bytes[7] = ((y_end >> 8) & 0xFF);
    bytes[8] = (y_end & 0xFF);
    bytes[9] = ((color >> 8) & 0xFF);
    bytes[10] = (color & 0xFF);
    
    LOG_DEBUG("Draw rectangle send!");
    ret = to_send(gs_socket, bytes, 11, 0);
    LOG_DETAILS("Send result: %d", ret);
    if(0 > ret) {
        LOG_ERROR("Draw rectangle failed send!");
    }
    
    return ret;
}

/*
 * Draw a line on LCD with given color
 * 
 * Parameters:
 * unsigned int x_start             Start and end points on 
 * unsigned int y_start             LCD. The limits of these
 * unsigned int x_end               parameters are related 
 * unsigned int y_end               with LCD. Common 320*240
 *                                  LCD supports x from 0 to
 *                                  319 and y from 0 to 239
 * 
 * unsigned int color               The color used to fill 
 *                                  the rectangle. Common 
 *                                  values are defined in 
 *                                  the beginning of this 
 *                                  file
 * 
 * Return value:
 * less than 0 if failed, greater than or equal to 0
 * when successful
 * 
 */
int draw_line(unsigned int x_start, unsigned int y_start, unsigned int x_end, unsigned int y_end, unsigned int color) {
    int ret;
    unsigned char bytes[11];
    
    bytes[0] = 0x61; //cmd
    bytes[1] = ((x_start >> 8) & 0xFF);
    bytes[2] = (x_start & 0xFF);
    bytes[3] = ((y_start >> 8) & 0xFF);
    bytes[4] = (y_start & 0xFF);
    bytes[5] = ((x_end >> 8) & 0xFF);
    bytes[6] = (x_end & 0xFF);
    bytes[7] = ((y_end >> 8) & 0xFF);
    bytes[8] = (y_end & 0xFF);
    bytes[9] = ((color >> 8) & 0xFF);
    bytes[10] = (color & 0xFF);
    
    LOG_DEBUG("Draw line send!");
    ret = to_send(gs_socket, bytes, 11, 0);
    LOG_DETAILS("Send result: %d", ret);
    if(0 > ret) {
        LOG_ERROR("Draw line failed send! %s", strerror(errno));
    }

    return ret;
}

/*
 * draw_time is used to draw time area
 * of LCD display. This is called from 
 * drawTimeCallback.
 * 
 * Parameters:
 * char* time   The time string
 * 
 * Return value:
 * Equal or greater than 0 means successful
 * Less than 0 means failed
 */
 int draw_time(const char* time) {
    int ret = draw_rectangle(TIME_X_START, TIME_Y_START, TIME_X_END, TIME_Y_END, BG_COLOR);
    if(0 > ret)
        return ret;
        
    return draw_string(TIME_X_START, TIME_Y_START, TIME_X_END, TIME_Y_END, FG_COLOR, TIME_FONT_SIZE, time);
}

/*
 * drawTimeCallback is used to draw time area
 * of LCD display. 
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 * void *privdata           Not used
 * 
 * Return value:
 * There is no return value
 */
void drawTimeCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    
    UNUSED(privdata);
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }
    
    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        if(0 > draw_time(reply->element[2]->str)) {
            LOG_ERROR("Draw time failed!");
            redisAsyncDisconnect(c);
        }
    }
    
    LOG_DETAILS("drawTimeCallback finished!");
}

/*
 * draw_weather is used to draw weather area
 * of LCD display. This is called from 
 * drawWeatherCallback and main function
 * 
 * Parameters:
 * char* weather   The weather string
 * 
 * Return value:
 * Equal or greater than 0 means successful
 * Less than 0 means failed
 */
int draw_weather(const char* weather) {
    int ret = draw_rectangle(FORCAST_X_START, FORCAST_Y_START, FORCAST_X_END, FORCAST_Y_END, BG_COLOR);
    if(0 > ret)
        return ret;
        
    return draw_string(FORCAST_X_START, FORCAST_Y_START, FORCAST_X_END, FORCAST_Y_END, FG_COLOR, FORCAST_FONT_SIZE, weather);
}

/*
 * drawWeatherCallback is used to draw weather area
 * of LCD display. 
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 * void *privdata           Not used
 * 
 * Return value:
 * There is no return value
 */
void drawWeatherCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    
    UNUSED(privdata);
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }
    
    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        if(0 > draw_weather(reply->element[2]->str)) {
            LOG_ERROR("Draw weather failed!");
            redisAsyncDisconnect(c);
        }
    }
    
    LOG_DETAILS("drawWeatherCallback finished!");
}

/*
 * draw_temp is used to draw temperature area
 * of LCD display. This is called from 
 * drawTempCallback and main function
 * 
 * Parameters:
 * char* temperature   The temperature string
 * 
 * Return value:
 * Equal or greater than 0 means successful
 * Less than 0 means failed
 */
int draw_temp(const char* temperature) {
    int ret = draw_rectangle(FORCAST_TEMP_X_START, FORCAST_TEMP_Y_START, FORCAST_TEMP_X_END, FORCAST_TEMP_Y_END, BG_COLOR);
    if(0 > ret)
        return ret;
        
    return draw_string(FORCAST_TEMP_X_START, FORCAST_TEMP_Y_START, FORCAST_TEMP_X_END, FORCAST_TEMP_Y_END, FG_COLOR, FORCAST_TEMP_FONT_SIZE, temperature);;
}

/*
 * drawTempCallback is used to draw temperature area
 * of LCD display. 
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 * void *privdata           Not used
 * 
 * Return value:
 * There is no return value
 */
void drawTempCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    
    UNUSED(privdata);
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }
    
    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        if(0 > draw_temp(reply->element[2]->str)) {
            LOG_ERROR("Draw forcast temperature failed!");
            redisAsyncDisconnect(c);
        }
    }
    
    LOG_DETAILS("drawTempCallback finished!");
}

/*
 * draw_humidity is used to draw humidity area
 * of LCD display. This is called from 
 * drawHumidityCallback and main function
 * 
 * Parameters:
 * char* humidity   The humidity string
 * 
 * Return value:
 * Equal or greater than 0 means successful
 * Less than 0 means failed
 */
int draw_humidity(const char* humidity) {
    int ret = draw_rectangle(FORCAST_HUMIDITY_X_START, FORCAST_HUMIDITY_Y_START, FORCAST_HUMIDITY_X_END, FORCAST_HUMIDITY_Y_END, BG_COLOR);
    if(0 > ret)
        return ret;
        
    return draw_string(FORCAST_HUMIDITY_X_START, FORCAST_HUMIDITY_Y_START, FORCAST_HUMIDITY_X_END, FORCAST_HUMIDITY_Y_END, FG_COLOR, FORCAST_HUMIDITY_FONT_SIZE, humidity);;
}

/*
 * drawHumidityCallback is used to draw humidity area
 * of LCD display. 
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 * void *privdata           Not used
 * 
 * Return value:
 * There is no return value
 */
void drawHumidityCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    
    UNUSED(privdata);
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }
    
    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        if(0 > draw_humidity(reply->element[2]->str)) {
            LOG_ERROR("Draw forcast humidity failed!");
            redisAsyncDisconnect(c);
        }
    }
    
    LOG_DETAILS("drawHumidityCallback finished!");
}

/*
 * draw_wind is used to draw wind area
 * of LCD display. This is called from 
 * drawWindCallback and main function
 * 
 * Parameters:
 * char* wind   The wind string
 * 
 * Return value:
 * Equal or greater than 0 means successful
 * Less than 0 means failed
 */
int draw_wind(const char* wind) {
    int ret = draw_rectangle(FORCAST_WIND_X_START, FORCAST_WIND_Y_START, FORCAST_WIND_X_END, FORCAST_WIND_Y_END, BG_COLOR);
    if(0 > ret)
        return ret;
        
    return draw_string(FORCAST_WIND_X_START, FORCAST_WIND_Y_START, FORCAST_WIND_X_END, FORCAST_WIND_Y_END, FG_COLOR, FORCAST_WIND_FONT_SIZE, wind);;
}

/*
 * drawWindCallback is used to draw wind area
 * of LCD display. 
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 * void *privdata           Not used
 * 
 * Return value:
 * There is no return value
 */
void drawWindCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    
    UNUSED(privdata);
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }
    
    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        if(0 > draw_wind(reply->element[2]->str)) {
            LOG_ERROR("Draw forcast wind failed!");
            redisAsyncDisconnect(c);
        }
    }
    
    LOG_DETAILS("drawWindCallback finished!");
}

/*
 * draw_aqi is used to draw aqi area
 * of LCD display. This is called from 
 * drawAqiCallback and main function
 * 
 * Parameters:
 * char* aqi   The aqi string
 * 
 * Return value:
 * Equal or greater than 0 means successful
 * Less than 0 means failed
 */
int draw_aqi(const char* aqi) {
    int ret = draw_rectangle(FORCAST_AQI_X_START, FORCAST_AQI_Y_START, FORCAST_AQI_X_END, FORCAST_AQI_Y_END, BG_COLOR);
    if(0 > ret)
        return ret;
        
    return draw_string(FORCAST_AQI_X_START, FORCAST_AQI_Y_START, FORCAST_AQI_X_END, FORCAST_AQI_Y_END, FG_COLOR, FORCAST_AQI_FONT_SIZE, aqi);;
}

/*
 * drawAqiCallback is used to draw aqi area
 * of LCD display. 
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 * void *privdata           Not used
 * 
 * Return value:
 * There is no return value
 */
void drawAqiCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    
    UNUSED(privdata);
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }
    
    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        if(0 > draw_aqi(reply->element[2]->str)) {
            LOG_ERROR("Draw forcast aqi failed!");
            redisAsyncDisconnect(c);
        }
    }
    
    LOG_DETAILS("drawAqiCallback finished!");
}

/*
 * draw_area1_name is used to draw area1 name
 * of LCD display. This is called from 
 * drawArea1NameCallback and main function
 * 
 * Parameters:
 * char* name   The name string
 * 
 * Return value:
 * Equal or greater than 0 means successful
 * Less than 0 means failed
 */
int draw_area1_name(const char* name) {
    int ret = draw_rectangle(AREA1_NAME_X_START, AREA1_NAME_Y_START, AREA1_NAME_X_END, AREA1_NAME_Y_END, BG_COLOR);
    if(0 > ret)
        return ret;
        
    return draw_string(AREA1_NAME_X_START, AREA1_NAME_Y_START, AREA1_NAME_X_END, AREA1_NAME_Y_END, FG_COLOR, AREA1_NAME_FONT_SIZE, name);;
}

/*
 * drawAqiCallback is used to draw area 1 name area
 * of LCD display. 
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 * void *privdata           Not used
 * 
 * Return value:
 * There is no return value
 */
void drawArea1NameCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    
    UNUSED(privdata);
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }
    
    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        if(0 > draw_area1_name(reply->element[2]->str)) {
            LOG_ERROR("Draw area 1 name failed!");
            redisAsyncDisconnect(c);
        }
    }
    
    LOG_DETAILS("drawArea1NameCallback finished!");
}

/*
 * draw_area1_temp is used to draw area1 temperature
 * of LCD display. This is called from 
 * drawArea1TempCallback and main function
 * 
 * Parameters:
 * char* temp   The temperature string
 * 
 * Return value:
 * Equal or greater than 0 means successful
 * Less than 0 means failed
 */
int draw_area1_temp(const char* temp) {
    int ret = draw_rectangle(AREA1_TEMP_X_START, AREA1_TEMP_Y_START, AREA1_TEMP_X_END, AREA1_TEMP_Y_END, BG_COLOR);
    if(0 > ret)
        return ret;
        
    return draw_string(AREA1_TEMP_X_START, AREA1_TEMP_Y_START, AREA1_TEMP_X_END, AREA1_TEMP_Y_END, FG_COLOR, AREA1_TEMP_FONT_SIZE, temp);;
}

/*
 * drawArea1TempCallback is used to draw area 1 temperature area
 * of LCD display. 
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 * void *privdata           Not used
 * 
 * Return value:
 * There is no return value
 */
void drawArea1TempCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    
    UNUSED(privdata);
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }
    
    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        if(0 > draw_area1_temp(reply->element[2]->str)) {
            LOG_ERROR("Draw area 1 temperature failed!");
            redisAsyncDisconnect(c);
        }
    }
    
    LOG_DETAILS("drawArea1TempCallback finished!");
}

/*
 * draw_area1_brightness is used to draw area1 brightness
 * of LCD display. This is called from 
 * drawArea1BrightnessCallback and main function
 * 
 * Parameters:
 * char* brightness   The brightness string
 * 
 * Return value:
 * Equal or greater than 0 means successful
 * Less than 0 means failed
 */
int draw_area1_brightness(const char* brightness) {
    int ret = draw_rectangle(AREA1_BRIGHTNESS_X_START, AREA1_BRIGHTNESS_Y_START, AREA1_BRIGHTNESS_X_END, AREA1_BRIGHTNESS_Y_END, BG_COLOR);
    if(0 > ret)
        return ret;
        
    return draw_string(AREA1_BRIGHTNESS_X_START, AREA1_BRIGHTNESS_Y_START, AREA1_BRIGHTNESS_X_END, AREA1_BRIGHTNESS_Y_END, FG_COLOR, AREA1_BRIGHTNESS_FONT_SIZE, brightness);
}

/*
 * drawArea1BrightnessCallback is used to draw area 1 temperature area
 * of LCD display. 
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 * void *privdata           Not used
 * 
 * Return value:
 * There is no return value
 */
void drawArea1BrightnessCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    
    UNUSED(privdata);
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }
    
    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        if(0 > draw_area1_brightness(reply->element[2]->str)) {
            LOG_ERROR("Draw area 1 brightness failed!");
            redisAsyncDisconnect(c);
        }
    }
    
    LOG_DETAILS("drawArea1BrightnessCallback finished!");
}

/*
 * draw_area2_name is used to draw area2 name
 * of LCD display. This is called from 
 * drawArea2NameCallback and main function
 * 
 * Parameters:
 * char* name   The name string
 * 
 * Return value:
 * Equal or greater than 0 means successful
 * Less than 0 means failed
 */
int draw_area2_name(const char* name) {
    int ret = draw_rectangle(AREA2_NAME_X_START, AREA2_NAME_Y_START, AREA2_NAME_X_END, AREA2_NAME_Y_END, BG_COLOR);
    if(0 > ret)
        return ret;
        
    return draw_string(AREA2_NAME_X_START, AREA2_NAME_Y_START, AREA2_NAME_X_END, AREA2_NAME_Y_END, FG_COLOR, AREA2_NAME_FONT_SIZE, name);;
}

/*
 * drawAqiCallback is used to draw area 2 name area
 * of LCD display. 
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 * void *privdata           Not used
 * 
 * Return value:
 * There is no return value
 */
void drawArea2NameCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    
    UNUSED(privdata);
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }
    
    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        if(0 > draw_area2_name(reply->element[2]->str)) {
            LOG_ERROR("Draw area2 name failed!");
            redisAsyncDisconnect(c);
        }
    }
    
    LOG_DETAILS("drawArea2NameCallback finished!");
}

/*
 * draw_area2_temp is used to draw area1 temperature
 * of LCD display. This is called from 
 * drawArea1TempCallback and main function
 * 
 * Parameters:
 * char* temp   The temperature string
 * 
 * Return value:
 * Equal or greater than 0 means successful
 * Less than 0 means failed
 */
int draw_area2_temp(const char* temp) {
    int ret = draw_rectangle(AREA2_TEMP_X_START, AREA2_TEMP_Y_START, AREA2_TEMP_X_END, AREA2_TEMP_Y_END, BG_COLOR);
    if(0 > ret)
        return ret;
        
    return draw_string(AREA2_TEMP_X_START, AREA2_TEMP_Y_START, AREA2_TEMP_X_END, AREA2_TEMP_Y_END, FG_COLOR, AREA2_TEMP_FONT_SIZE, temp);
}

/*
 * drawArea2TempCallback is used to draw area 2 temperature area
 * of LCD display. 
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 * void *privdata           Not used
 * 
 * Return value:
 * There is no return value
 */
void drawArea2TempCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    
    UNUSED(privdata);
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }
    
    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        if(0 > draw_area2_temp(reply->element[2]->str)) {
            LOG_ERROR("Draw area 2 temperature failed!");
            redisAsyncDisconnect(c);
        }
    }
    
    LOG_DETAILS("drawArea2TempCallback finished!");
}

/*
 * draw_area2_brightness is used to draw area1 brightness
 * of LCD display. This is called from 
 * drawArea2BrightnessCallback and main function
 * 
 * Parameters:
 * char* brightness   The temperature string
 * 
 * Return value:
 * Equal or greater than 0 means successful
 * Less than 0 means failed
 */
int draw_area2_brightness(const char* brightness) {
    int ret = draw_rectangle(AREA2_BRIGHTNESS_X_START, AREA2_BRIGHTNESS_Y_START, AREA2_BRIGHTNESS_X_END, AREA2_BRIGHTNESS_Y_END, BG_COLOR);
    if(0 > ret)
        return ret;
        
    return draw_string(AREA2_BRIGHTNESS_X_START, AREA2_BRIGHTNESS_Y_START, AREA2_BRIGHTNESS_X_END, AREA2_BRIGHTNESS_Y_END, FG_COLOR, AREA2_BRIGHTNESS_FONT_SIZE, brightness);
}

/*
 * drawArea2BrightnessCallback is used to draw area 2 brightness area
 * of LCD display. 
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 * void *privdata           Not used
 * 
 * Return value:
 * There is no return value
 */
void drawArea2BrightnessCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    
    UNUSED(privdata);
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }
    
    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        if(0 > draw_area2_brightness(reply->element[2]->str)) {
            LOG_ERROR("Draw area 2 brightness failed!");
            redisAsyncDisconnect(c);
        }
    }
    
    LOG_DETAILS("drawArea2BrightnessCallback finished!");
}

/*
 * draw_target_temp is used to draw target temperature
 * of LCD display. This is called from 
 * drawArea2BrightnessCallback and main function
 * 
 * Parameters:
 * char* temp   The temperature string
 * 
 * Return value:
 * Equal or greater than 0 means successful
 * Less than 0 means failed
 */
int draw_target_temp(const char* temp) {
    int ret = draw_rectangle(TARGET_TEMP_X_START, TARGET_TEMP_Y_START, TARGET_TEMP_X_END, TARGET_TEMP_Y_END, BG_COLOR);
    if(0 > ret)
        return ret;
    return draw_string(TARGET_TEMP_X_START, TARGET_TEMP_Y_START, TARGET_TEMP_X_END, TARGET_TEMP_Y_END, FG_COLOR, TARGET_TEMP_FONT_SIZE, temp);
}

/*
 * drawTargetTempCallback is used to draw target temperature area
 * of LCD display. 
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 * void *privdata           Not used
 * 
 * Return value:
 * There is no return value
 */
void drawTargetTempCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    
    UNUSED(privdata);
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }
    
    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        if(0 > draw_target_temp(reply->element[2]->str)) {
            LOG_ERROR("Draw target temperature failed!");
            redisAsyncDisconnect(c);
        }
    }
    
    LOG_DETAILS("drawTargetTempCallback finished!");
}

/*
 * set_brightness is used to set brightness
 * of LCD display. This is called from 
 * setBrightnessCallback and main function
 * 
 * Parameters:
 * unsigned char brightness brightness value
 * 
 * Return value:
 * Equal or greater than 0 means successful
 * Less than 0 means failed
 */
int set_brightness(unsigned char brightness) {
    int ret;
    unsigned char bytes[2];

    bytes[0] = 0x65;
    bytes[1] = brightness;

    ret = to_send(gs_socket, bytes, 2, 0);
    LOG_DETAILS("Send result: %d", ret);
    if(0 > ret) {
        LOG_ERROR("Error setBrightnessCallback send failed! %s", strerror(errno));
    }
    
    return ret;
}

/*
 * setBrightnessCallback is used to set the brightness
 * of LCD display. valid data range is 0-255
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 * void *privdata           Not used
 * 
 * Return value:
 * There is no return value
 */
void setBrightnessCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    
    UNUSED(privdata);
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }
    
    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        if(0 > set_brightness(atoi(reply->element[2]->str) & 0xFF)) {
            LOG_ERROR("Set brightness failed!");
            redisAsyncDisconnect(c);
        }
    }
    
    LOG_DETAILS("set_brightness finished!");
}

/*
 * exitCallback is used to check whether
 * the micro service needs to exit. When
 * "exit" is received then exit the 
 * micro service
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 * void *privdata           Not used
 * 
 * Return value:
 * There is no return value
 */
void exitCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    
    UNUSED(privdata);
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        if(0 == strcmp(EXIT_FLAG_VALUE, reply->element[2]->str)) {
            gs_exit = 1;
            redisAsyncDisconnect(c);
        }
    }

    LOG_DEBUG("Exit finished!\n");
}

/*
 * resetCallback is used to check whether
 * the micro service needs to reset. When
 * "exit" is received then exit the micro
 * service
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 * void *privdata           Not used
 * 
 * Return value:
 * There is no return value
 */
void resetCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    
    UNUSED(privdata);
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        if(0 == strcmp(RESET_FLAG_VALUE, reply->element[2]->str)) {
            redisAsyncDisconnect(c);
        }
    }

    LOG_DEBUG("Reset finished!\n");
}

/*
 * setLogLevelCallback is used to update
 * log level for current micro service.
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 * void *privdata           Not used
 * 
 * Return value:
 * There is no return value
 */
void setLogLevelCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(NULL != privdata) {
        LOG_WARNING("Warning: setLogLevelCallback invalid priv data!");
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: setLogLevelCallback does not have valid value!");
        if(0 == reply->elements) {
            LOG_DETAILS("Sting: %s", reply->str);
        } else {
            for(size_t i = 0; i < reply->elements; i++) {
                LOG_DETAILS("Element %d: %s", i, reply->element[i]->str);
            }
        }
        return;
    }
    
    if(0 > log_set_level(ch)) {
        LOG_WARNING("Warning: invalid log level %s!", ch);
    }

    LOG_DETAILS("setLogLevelCallback finished!");
}

/*
 * When successfully connected to redis or failed to connect to redis,
 * this function will be called to update the status
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * int status               Status code for redis connection, REDIS_OK
 *                          means successfully connected to redis server.
 *                          Other code means failure;
 * 
 * Return value:
 * There is no return value
 * 
 */
void connectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        LOG_ERROR("Error: %s", c->errstr);
        return;
    }
    LOG_INFO("Connected...");
}

/*
 * When exiting the program or there are anything goes wrong and 
 * disconnect is called, this function is called and provide the
 * disconnect result
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * int status               Status code for disconnect from redis, 
 *                          REDIS_OK means successfully disconnected 
 *                          from to redis server Other code means 
 *                          failure;
 * 
 * Return value:
 * There is no return value
 * 
 */
void disconnectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        LOG_ERROR("Error disconnect: %s", c->errstr);
        return;
    }
    LOG_INFO("Disconnected...");
}

/*
 * When user input incorrect data, this service will
 * exit immediately. And with this function, it can
 * provide user a friendly hint for usage.
 * 
 * Parameters:
 * int argc                 Number of input parameters, same function 
 *                          with argc of main.
 * char **argv              Actual input parameters, same function with
 *                          argv of main.
 * 
 * Return value:
 * There is no return value
 * 
 * Note: in this function we use printf not using log
 * as it is necessary to ensure the hint is always 
 * printed out without the loglevel configuration.
 * 
 */
void print_usage(int argc, char **argv) {
    if(0 >= argc) {
        return;
    }
    
    printf("Invalid input parameters!\n");
    printf("Usage: (<optional parameters>)\n");
    printf("%s lcd_ip lcd_port <log_level> <redis_ip> <redis_port>\n", argv[0]);
    printf("E.g.:\n");
    printf("%s 192.168.100.50 5000\n\n", argv[0]);
    printf("%s 192.168.100.50 5000 debug\n\n", argv[0]);
    printf("%s 192.168.100.50 5000 127.0.0.1 6379\n\n", argv[0]);
    printf("%s 192.168.100.50 5000 debug 127.0.0.1 6379\n\n", argv[0]);
}

/*
 * Draw lines according to activated switch count each area will be
 * considered as a switch
 */
int draw_sw_lines(void) {
    int ret = -1;
    switch(gs_sw_config->elements) {
        case 1:
            // only 1 switch do nothing
            return 0;
        case 2:
            // draw line in the middle of area
            return draw_line(SWITCH_X_START, SWITCH_Y_HALF, SWITCH_X_END, SWITCH_Y_HALF, FG_COLOR);
        case 3:
        case 4:
            ret = draw_line(SWITCH_X_START, SWITCH_Y_HALF, SWITCH_X_END, SWITCH_Y_HALF, FG_COLOR);
            if(0 > ret)
                return ret;
            
            return draw_line(SWITCH_X_HALF, SWITCH_Y_START, SWITCH_X_HALF, SWITCH_Y_END, FG_COLOR);
        case 5:
        case 6:
            ret = draw_line(SWITCH_X_START, SWITCH_Y_ONE_THIRD, SWITCH_X_END, SWITCH_Y_ONE_THIRD, FG_COLOR);
            if(0 > ret)
                return ret;
            
            ret = draw_line(SWITCH_X_START, SWITCH_Y_TWO_THIRD, SWITCH_X_END, SWITCH_Y_TWO_THIRD, FG_COLOR);
            if(0 > ret)
                return ret;
                
            return draw_line(SWITCH_X_HALF, SWITCH_Y_START, SWITCH_X_HALF, SWITCH_Y_END, FG_COLOR);
    }
    return ret;
}

#define DRAW_SW(xstart, ystart, xend, yend)     if(NULL != value && 0 == strcmp("1", value)) {\
                                                    ret = draw_rectangle(xstart + 1, ystart + 1, xend - 1, yend - 1, FG_COLOR);\
                                                    if(0 > ret) {\
                                                        return ret;\
                                                    }\
                                                    ret = draw_string(xstart + 1, ystart + 1, xend - 1, yend - 1, BG_COLOR, SWITCH_FONT_SIZE, gs_sw_config->element[idx]->str);\
                                                } else {\
                                                    ret = draw_rectangle(xstart + 1, ystart + 1, xend - 1, yend - 1, BG_COLOR);\
                                                    if(0 > ret) {\
                                                        return ret;\
                                                    }\
                                                    ret = draw_string(xstart + 1, ystart + 1, xend - 1, yend - 1, FG_COLOR, SWITCH_FONT_SIZE, gs_sw_config->element[idx]->str);\
                                                }

/*
 * Draw switch name and status
 */
int draw_sw(unsigned char idx, char* value) {
    int ret = -1;
    switch(gs_sw_config->elements) {
        case 1:
            if(0 != idx)
                return ret;
                
            DRAW_SW(SWITCH_X_START, SWITCH_Y_START, SWITCH_X_END, SWITCH_Y_END);
            return ret;
        case 2:
            switch(idx) {
                case 0:
                    DRAW_SW(SWITCH_X_START, SWITCH_Y_START, SWITCH_X_END, SWITCH_Y_HALF);
                    break;
                case 1:
                    DRAW_SW(SWITCH_X_START, SWITCH_Y_HALF, SWITCH_X_END, SWITCH_Y_END);
                    break;
                default:
                    break;
            }
            return ret;
        case 3:
            switch(idx) {
                case 0:
                    DRAW_SW(SWITCH_X_START, SWITCH_Y_START, SWITCH_X_HALF, SWITCH_Y_HALF);
                    break;
                case 1:
                    DRAW_SW(SWITCH_X_HALF, SWITCH_Y_START, SWITCH_X_END, SWITCH_Y_HALF);
                    break;
                case 2:
                    DRAW_SW(SWITCH_X_START, SWITCH_Y_HALF, SWITCH_X_HALF, SWITCH_Y_END);
                    break;
                default:
                    break;
            }
            return ret;
        case 4:
            switch(idx) {
                case 0:
                    DRAW_SW(SWITCH_X_START, SWITCH_Y_START, SWITCH_X_HALF, SWITCH_Y_HALF);
                    break;
                case 1:
                    DRAW_SW(SWITCH_X_HALF, SWITCH_Y_START, SWITCH_X_END, SWITCH_Y_HALF);
                    break;
                case 2:
                    DRAW_SW(SWITCH_X_START, SWITCH_Y_HALF, SWITCH_X_HALF, SWITCH_Y_END);
                    break;
                case 3:
                    DRAW_SW(SWITCH_X_HALF, SWITCH_Y_HALF, SWITCH_X_END, SWITCH_Y_END);
                    break;
                default:
                    break;
            }
            return ret;
        case 5:
            switch(idx) {
                case 0:
                    DRAW_SW(SWITCH_X_START, SWITCH_Y_START, SWITCH_X_HALF, SWITCH_Y_ONE_THIRD);
                    break;
                case 1:
                    DRAW_SW(SWITCH_X_HALF, SWITCH_Y_START, SWITCH_X_END, SWITCH_Y_ONE_THIRD);
                    break;
                case 2:
                    DRAW_SW(SWITCH_X_START, SWITCH_Y_ONE_THIRD, SWITCH_X_HALF, SWITCH_Y_TWO_THIRD);
                    break;
                case 3:
                    DRAW_SW(SWITCH_X_HALF, SWITCH_Y_ONE_THIRD, SWITCH_X_END, SWITCH_Y_TWO_THIRD);
                    break;
                case 4:
                    DRAW_SW(SWITCH_X_START, SWITCH_Y_TWO_THIRD, SWITCH_X_HALF, SWITCH_Y_END);
                    break;
                default:
                    break;
            }
            return ret;
        case 6:
            switch(idx) {
                case 0:
                    DRAW_SW(SWITCH_X_START, SWITCH_Y_START, SWITCH_X_HALF, SWITCH_Y_ONE_THIRD);
                    break;
                case 1:
                    DRAW_SW(SWITCH_X_HALF, SWITCH_Y_START, SWITCH_X_END, SWITCH_Y_ONE_THIRD);
                    break;
                case 2:
                    DRAW_SW(SWITCH_X_START, SWITCH_Y_ONE_THIRD, SWITCH_X_HALF, SWITCH_Y_TWO_THIRD);
                    break;
                case 3:
                    DRAW_SW(SWITCH_X_HALF, SWITCH_Y_ONE_THIRD, SWITCH_X_END, SWITCH_Y_TWO_THIRD);
                    break;
                case 4:
                    DRAW_SW(SWITCH_X_START, SWITCH_Y_TWO_THIRD, SWITCH_X_HALF, SWITCH_Y_END);
                    break;
                case 5:
                    DRAW_SW(SWITCH_X_HALF, SWITCH_Y_TWO_THIRD, SWITCH_X_END, SWITCH_Y_END);
                    break;
                default:
                    break;
            }
            return ret;
    }
    
    return ret;
}


/*
 * drawSWCallback is used to draw switch area
 * of LCD display. 
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 * void *privdata           char format switch index
 * 
 * Return value:
 * There is no return value
 */
void drawSWCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    
    char* ch = (char*)privdata;
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }
    
    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        if(0 > draw_sw(*ch - '0', reply->element[2]->str)) {
            LOG_ERROR("Draw area 2 brightness failed!");
            redisAsyncDisconnect(c);
        }
    }
    
    LOG_DETAILS("drawArea2BrightnessCallback finished!");
}

/*
 * Main entry of the service. It will first connect
 * to the lcd, and setup send/recv timeout to 
 * ensure the network traffic is not blocking and
 * can fail fast.
 * 
 * Then it will connect to redis and get necessary
 * data to display the names of sensors and 
 * switches. Then it will subscribe corresponding
 * channels to keep the data up to date
 * 
 * Parameters:
 * int argc                 Number of input parameters, same function 
 *                          with argc of main.
 * char **argv              Actual input parameters, same function with
 *                          argv of main.
 * 
 * Return value:
 * There is no return value
 * 
 * Note: in this function we use printf not using log
 * as it is necessary to ensure the hint is always 
 * printed out without the loglevel configuration.
 * 
 */
int main (int argc, char **argv) {
    
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    struct event_base *base = NULL;
    struct timeval timeout = { 0, 100000 }; 

    char* serv_ip;
    const char* redis_ip;
    int serv_port, redis_port;
    
    unsigned char temp;
    
    redis_ip = REDIS_IP;
    redis_port = REDIS_PORT;
    
    switch(argc) {
        case 6:
            if(0 > log_set_level(argv[3])) {
                print_usage(argc, argv);
                return -2;
            }
            redis_port = atoi(argv[5]);
            redis_ip = argv[4];
            serv_port = atoi(argv[2]);
            serv_ip = argv[1];
            break;
        case 5:
            redis_port = atoi(argv[4]);
            redis_ip = argv[3];
            serv_port = atoi(argv[2]);
            serv_ip = argv[1];
            break;
        case 4:
            if(0 > log_set_level(argv[3])) {
                print_usage(argc, argv);
                return -3;
            }
            serv_ip = argv[1];
            serv_port = atoi(argv[2]);
            break;
        case 3:
            serv_ip = argv[1];
            serv_port = atoi(argv[2]);
            break;
        default:
            print_usage(argc, argv);
            return -1;
    }
        
l_start:
    // Connect to LCD controller
    gs_socket = to_connect(serv_ip, serv_port);
    if(0 > gs_socket) {
        LOG_ERROR("Error creating socket!");
        goto l_socket_cleanup;
    }
    
    // Receive initial byte to activate keep alive of remote device
    if(0 > to_recv(gs_socket, &temp, 1, 0)) {
        LOG_ERROR("Error receiving initial data! %s", strerror(errno));
        goto l_socket_cleanup;
    }
    
    LOG_INFO("Connected to LCD controller, remote socket: %d", temp);
    
    gs_sync_context = redisConnectWithTimeout(redis_ip, redis_port, timeout);
    if(NULL == gs_sync_context) {
        LOG_ERROR("Connection error: can't allocate redis context");
        goto l_exit;
    }
    
    if(gs_sync_context->err) {
        LOG_ERROR("Connection error: %s", gs_sync_context->errstr);
        goto l_free_sync_redis;
    }
    
    redisReply* reply  = NULL;
    redisReply* reply2  = NULL;
    EXEC_REDIS_CMD(reply, l_free_sync_redis, "PING");
    LOG_DEBUG("PING: %s", reply->str);
    freeReplyObject(reply);
    reply = NULL;

    LOG_INFO("Connected to redis in sync mode!");
    
    // update log level to config in redis
    EXEC_REDIS_CMD(reply, l_free_async_redis, "GET %s/%s/%s", FLAG_KEY, serv_ip, LOG_LEVEL_FLAG_VALUE);
    if(NULL != reply->str) {
        if(LOG_SET_LEVEL_OK != log_set_level(reply->str)) {
            LOG_WARNING("Failed to set log level %s", reply->str);
        }
    }
    freeReplyObject(reply);
    reply = NULL;
    
    // Connect to redis in async mode
    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, redis_ip, redis_port);
    struct timeval tv = {0};
    tv.tv_sec = 1;
    options.connect_timeout = &tv;

    LOG_INFO("Connected to redis in async mode!");
    gs_async_context = redisAsyncConnectWithOptions(&options);
    if(NULL == gs_async_context) {
        LOG_ERROR("Error cannot allocate gs_async_context!");
        goto l_socket_cleanup;
    }
    
    if (gs_async_context->err) {
        LOG_ERROR("Error async connect: %s", gs_async_context->errstr);
        goto l_free_async_redis;
    }

    // prepare async calls
    base = event_base_new();
    if(REDIS_OK != redisLibeventAttach(gs_async_context,base)) {
        LOG_ERROR("Error: error redis libevent attach!");
        goto l_free_async_redis;
    }

    LOG_INFO("Set async redis callbacks!");
    redisAsyncSetConnectCallback(gs_async_context,connectCallback);
    redisAsyncSetDisconnectCallback(gs_async_context,disconnectCallback);
    
    // Subscribe changes for log_level, exit, reset
    LOG_INFO("Subscribe exit, reset, log_level events!");
    ASYNC_REDIS_CMD(exitCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, EXIT_FLAG_VALUE);
    ASYNC_REDIS_CMD(resetCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, RESET_FLAG_VALUE);
    ASYNC_REDIS_CMD(setLogLevelCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, LOG_LEVEL_FLAG_VALUE);
    ASYNC_REDIS_CMD(setBrightnessCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, BRIGHTNESS_TOPIC);

    // initialize last update time for sensor values
    LOG_INFO("Reset sensor data timer!");
    for(int i = 0; i < 4; i++) {
        gettimeofday(&gs_sensor[i], NULL);
    }

    // draw grids
    LOG_INFO("Drawing grids!");
    if(0 > draw_rectangle(LCD_MIN_X, LCD_MIN_Y, LCD_MAX_X, LCD_MAX_Y, BG_COLOR)) {
        goto l_free_async_redis;
    }
    if(0 > draw_line(LCD_FUNCTION_SEP_LINE_X_START, LCD_FUNCTION_SEP_LINE_Y_START, LCD_FUNCTION_SEP_LINE_X_END, LCD_FUNCTION_SEP_LINE_Y_END, FG_COLOR)) {
        goto l_free_async_redis;
    }
    if(0 > draw_line(TIME_WEATHER_SEP_LINE_X_START, TIME_WEATHER_SEP_LINE_Y_START, TIME_WEATHER_SEP_LINE_X_END, TIME_WEATHER_SEP_LINE_Y_END, FG_COLOR)) {
        goto l_free_async_redis;
    }
    if(0 > draw_line(FORCAST_DATA_SEP_LINE_X_START, FORCAST_DATA_SEP_LINE_Y_START, FORCAST_DATA_SEP_LINE_X_END, FORCAST_DATA_SEP_LINE_Y_END, FG_COLOR)) {
        goto l_free_async_redis;
    }
    if(0 > draw_line(FORCAST_TEMP_SEP_LINE_X_START, FORCAST_TEMP_SEP_LINE_Y_START, FORCAST_TEMP_SEP_LINE_X_END, FORCAST_TEMP_SEP_LINE_Y_END, FG_COLOR)) {
        goto l_free_async_redis;
    }
    if(0 > draw_line(FORCAST_HUMIDITY_SEP_LINE_X_START, FORCAST_HUMIDITY_SEP_LINE_Y_START, FORCAST_HUMIDITY_SEP_LINE_X_END, FORCAST_HUMIDITY_SEP_LINE_Y_END, FG_COLOR)) {
        goto l_free_async_redis;
    }
    if(0 > draw_line(FORCAST_WIND_SEP_LINE_X_START, FORCAST_WIND_SEP_LINE_Y_START, FORCAST_WIND_SEP_LINE_X_END, FORCAST_WIND_SEP_LINE_Y_END, FG_COLOR)) {
        goto l_free_async_redis;
    }
    if(0 > draw_line(FORCAST_AREA1_SEP_LINE_X_START, FORCAST_AREA1_SEP_LINE_Y_START, FORCAST_AREA1_SEP_LINE_X_END, FORCAST_AREA1_SEP_LINE_Y_END, FG_COLOR)) {
        goto l_free_async_redis;
    }
    if(0 > draw_line(AREA1_AREA2_SEP_LINE_X_START, AREA1_AREA2_SEP_LINE_Y_START, AREA1_AREA2_SEP_LINE_X_END, AREA1_AREA2_SEP_LINE_Y_END, FG_COLOR)) {
        goto l_free_async_redis;
    }
    if(0 > draw_line(AREA2_TARGET_SEP_LINE_X_START, AREA2_TARGET_SEP_LINE_Y_START, AREA2_TARGET_SEP_LINE_X_END, AREA2_TARGET_SEP_LINE_Y_END, FG_COLOR)) {
        goto l_free_async_redis;
    }
    if(0 > draw_line(TARGET_MINUS_SEP_LINE_X_START, TARGET_MINUS_SEP_LINE_Y_START, TARGET_MINUS_SEP_LINE_X_END, TARGET_MINUS_SEP_LINE_Y_END, FG_COLOR)) {
        goto l_free_async_redis;
    }
    if(0 > draw_line(TARGET_PLUS_SEP_LINE_X_START, TARGET_PLUS_SEP_LINE_Y_START, TARGET_PLUS_SEP_LINE_X_END, TARGET_PLUS_SEP_LINE_Y_END, FG_COLOR)) {
        goto l_free_async_redis;
    }
    if(0 > draw_string(TARGET_MINUS_X_START, TARGET_MINUS_Y_START, TARGET_MINUS_X_END, TARGET_MINUS_Y_END, FG_COLOR, TARGET_MINUS_FONT_SIZE, "-")) {
        goto l_free_async_redis;
    }
    if(0 > draw_string(TARGET_PLUS_X_START, TARGET_PLUS_Y_START, TARGET_PLUS_X_END, TARGET_PLUS_Y_END, FG_COLOR, TARGET_PLUS_FONT_SIZE, "+")) {
        goto l_free_async_redis;
    }
    
    // Check enabled switch count
    LOG_INFO("Load switch config from redis!");
    EXEC_REDIS_CMD(gs_sw_config, l_free_async_redis, "HKEYS %s/%s/%s", FLAG_KEY, serv_ip, SWITCH_TOPIC);
    
    // Draw switch area
    if(0 > draw_sw_lines()) {
        LOG_ERROR("Failed to draw switch area lines!");
        goto l_free_sw_config;
    }
    
    ASYNC_REDIS_CMD(drawTimeCallback, NULL, "SUBSCRIBE time");

    EXEC_REDIS_CMD(reply, l_free_sw_config, "GET weather/forcast");
    if(0 > draw_weather(reply->str)) {
        LOG_ERROR("Failed to draw weather info!");
        goto l_free_redis_reply;
    }
    freeReplyObject(reply);
    reply = NULL;

    ASYNC_REDIS_CMD(drawWeatherCallback, NULL, "SUBSCRIBE weather/forcast");

    EXEC_REDIS_CMD(reply, l_free_sw_config, "GET weather/temperature");
    if(0 > draw_temp(reply->str)) {
        LOG_ERROR("Failed to draw temperature info!");
        goto l_free_redis_reply;
    }
    freeReplyObject(reply);
    reply = NULL;

    ASYNC_REDIS_CMD(drawTempCallback, NULL, "SUBSCRIBE weather/temperature");

    EXEC_REDIS_CMD(reply, l_free_sw_config, "GET weather/humidity");
    if(0 > draw_humidity(reply->str)) {
        LOG_ERROR("Failed to draw humidity info!");
        goto l_free_redis_reply;
    }
    freeReplyObject(reply);
    reply = NULL;

    ASYNC_REDIS_CMD(drawHumidityCallback, NULL, "SUBSCRIBE weather/humidity");

    EXEC_REDIS_CMD(reply, l_free_sw_config, "GET weather/wind");
    if(0 > draw_wind(reply->str)) {
        LOG_ERROR("Failed to draw wind info!");
        goto l_free_redis_reply;
    }
    freeReplyObject(reply);
    reply = NULL;

    ASYNC_REDIS_CMD(drawWindCallback, NULL, "SUBSCRIBE weather/wind");

    EXEC_REDIS_CMD(reply, l_free_sw_config, "GET weather/aqi");
    if(0 > draw_aqi(reply->str)) {
        LOG_ERROR("Failed to draw AQI info!");
        goto l_free_redis_reply;
    }
    freeReplyObject(reply);
    reply = NULL;
    
    ASYNC_REDIS_CMD(drawAqiCallback, NULL, "SUBSCRIBE weather/aqi");

    EXEC_REDIS_CMD(reply, l_free_sw_config, "HGET %s/%s/%s %s", FLAG_KEY, serv_ip, AREA1_TOPIC, NAME_TOPIC);
    if(NULL != reply->str) {
        ASYNC_REDIS_CMD(drawArea1NameCallback, NULL, "SUBSCRIBE %s", reply->str);
        EXEC_REDIS_CMD(reply2, l_free_redis_reply, "GET %s", reply->str);
        if(0 > draw_area1_name(reply2->str)) {
            LOG_ERROR("Failed to draw area 1 name info!");
            goto l_free_redis_reply;
        }
        freeReplyObject(reply2);
        reply2 = NULL;
    }
    freeReplyObject(reply);
    reply = NULL;
    
    EXEC_REDIS_CMD(reply, l_free_sw_config, "HGET %s/%s/%s %s", FLAG_KEY, serv_ip, AREA1_TOPIC, TEMP_TOPIC);
    if(NULL != reply->str) {
        ASYNC_REDIS_CMD(drawArea1TempCallback, NULL, "SUBSCRIBE %s", reply->str);
        EXEC_REDIS_CMD(reply2, l_free_redis_reply, "GET %s", reply->str);
        if(0 > draw_area1_temp(reply2->str)) {
            LOG_ERROR("Failed to draw area 1 temperature info!");
            goto l_free_redis_reply;
        }
        freeReplyObject(reply2);
        reply2 = NULL;
    }
    freeReplyObject(reply);
    reply = NULL;
    
    EXEC_REDIS_CMD(reply, l_free_sw_config, "HGET %s/%s/%s %s", FLAG_KEY, serv_ip, AREA1_TOPIC, BRIGHTNESS_TOPIC);
    if(NULL != reply->str) {
        ASYNC_REDIS_CMD(drawArea1BrightnessCallback, NULL, "SUBSCRIBE %s", reply->str);
        EXEC_REDIS_CMD(reply2, l_free_redis_reply, "GET %s", reply->str);
        if(0 > draw_area1_brightness(reply2->str)) {
            LOG_ERROR("Failed to draw area 1 brightness info!");
            goto l_free_redis_reply;
        }
        freeReplyObject(reply2);
        reply2 = NULL;
    }
    freeReplyObject(reply);
    reply = NULL;
    
    EXEC_REDIS_CMD(reply, l_free_sw_config, "HGET %s/%s/%s %s", FLAG_KEY, serv_ip, AREA2_TOPIC, NAME_TOPIC);
    if(NULL != reply->str) {
        ASYNC_REDIS_CMD(drawArea2NameCallback, NULL, "SUBSCRIBE %s", reply->str);
        EXEC_REDIS_CMD(reply2, l_free_redis_reply, "GET %s", reply->str);
        if(0 > draw_area1_name(reply2->str)) {
            LOG_ERROR("Failed to draw area 2 name info!");
            goto l_free_redis_reply;
        }
        freeReplyObject(reply2);
        reply2 = NULL;
    }
    freeReplyObject(reply);
    reply = NULL;
    EXEC_REDIS_CMD(reply, l_free_sw_config, "HGET %s/%s/%s %s", FLAG_KEY, serv_ip, AREA2_TOPIC, TEMP_TOPIC);
    if(NULL != reply->str) {
        ASYNC_REDIS_CMD(drawArea2TempCallback, NULL, "SUBSCRIBE %s", reply->str);
        EXEC_REDIS_CMD(reply2, l_free_redis_reply, "GET %s", reply->str);
        if(0 > draw_area1_temp(reply2->str)) {
            LOG_ERROR("Failed to draw area 2 temperature info!");
            goto l_free_redis_reply;
        }
        freeReplyObject(reply2);
        reply2 = NULL;
    }
    freeReplyObject(reply);
    reply = NULL;
    
    EXEC_REDIS_CMD(reply, l_free_sw_config, "HGET %s/%s/%s %s", FLAG_KEY, serv_ip, AREA2_TOPIC, BRIGHTNESS_TOPIC);
    if(NULL != reply->str) {
        ASYNC_REDIS_CMD(drawArea2BrightnessCallback, NULL, "SUBSCRIBE %s", reply->str);
        EXEC_REDIS_CMD(reply2, l_free_redis_reply, "GET %s", reply->str);
        if(0 > draw_area1_brightness(reply2->str)) {
            LOG_ERROR("Failed to draw area 2 brighness info!");
            goto l_free_redis_reply;
        }
        freeReplyObject(reply2);
        reply2 = NULL;
    }
    freeReplyObject(reply);
    reply = NULL;

    EXEC_REDIS_CMD(reply, l_free_async_redis, "GET %s/%s/%s", FLAG_KEY, serv_ip, TARGET_TEMP_TOPIC);
    if(NULL != reply->str) {
        ASYNC_REDIS_CMD(drawTargetTempCallback, NULL, "SUBSCRIBE %s", reply->str);
        EXEC_REDIS_CMD(reply2, l_free_redis_reply, "GET %s", reply->str);
        if(0 > draw_area1_brightness(reply2->str)) {
            LOG_ERROR("Failed to draw target temperature info!");
            goto l_free_redis_reply;
        }
        freeReplyObject(reply2);
        reply2 = NULL;
    }
    freeReplyObject(reply);
    reply = NULL;
    
    for(size_t i = 0; i < gs_sw_config->elements; i++) {
        EXEC_REDIS_CMD(reply, l_free_async_redis, "HGET %s/%s/%s %s", FLAG_KEY, serv_ip, SWITCH_TOPIC, gs_sw_config->element[i]->str);
        ASYNC_REDIS_CMD(drawSWCallback, &sw_idx[i], "SUBSCRIBE %s", reply->str);
        EXEC_REDIS_CMD(reply2, l_free_redis_reply, "GET %s", gs_sw_config->element[i]->str);
        if(0 >  draw_sw(i, reply2->str)) {
            LOG_ERROR("Failed to draw switch %d info!", i);
            goto l_free_redis_reply;
        }
        freeReplyObject(reply2);
        reply2 = NULL;
        freeReplyObject(reply);
        reply = NULL;

    }
    
    LOG_DETAILS("Started running!");
    event_base_dispatch(base);

l_free_redis_reply:
    if(NULL != reply2)
        freeReplyObject(reply2);

    if(NULL != reply)
        freeReplyObject(reply);

l_free_sw_config:
    freeReplyObject(gs_sw_config);
    gs_sw_config = NULL;
        
l_free_async_redis:
    redisAsyncFree(gs_async_context);
    event_base_free(base);
    base = NULL;
    LOG_INFO("exit!");
    
l_free_sync_redis:
    if(NULL != gs_sync_context) {
        redisFree(gs_sync_context);
        gs_sync_context = NULL;
    }
    
l_socket_cleanup:
    to_close(gs_socket);
    gs_socket = -1;
    
l_exit:
    if(!gs_exit) {    
        LOG_ERROR("Execution failed retry!");
        sleep(1);
        goto l_start;
    }
    
    return 0;
}
