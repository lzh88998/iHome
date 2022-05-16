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
#define BACKGROUND_COLOR            COLOR_BLUE

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
#define SENSOR_VALUE_INTERVAL_MS    5000

/*
 * Identifier used in redis keys for this
 * micro service
 */
#define FLAG_KEY                    "lcd"

/*
 * Configuration items in redis hash
 */
#define SENSOR_NAME_TOPIC           "sensor_name_"
#define SENSOR_TOPIC                "sensor_"
#define BRIGHTNESS_TOPIC            "brightness"
#define SW_NAME_TOPIC               "switch_name_"
#define SW_TOPIC                    "switch_"

/*
 * Store last update time information
 * for sensor values
 */
static struct timeval gs_sensor[4];

/*
 * Context for redis connection and controller
 * network connection
 */
static to_socket_ctx gs_socket = -1;
static redisAsyncContext *gs_async_context = NULL;

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
 * Used to pass parameters to subscribeCallback
 */
static char gs_idx_name[6] = {'0', '1', '2', '3', '4', '5'};

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
    LOG_DETAILS("draw_string");
    ret = vsprintf(input_buffer, fmt, ap);
    va_end(ap);
    
    LOG_DETAILS("Convert %s", input_buffer);
    converted_cnt = convert_encoding();
    
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
    if(0 > ret) {
        LOG_ERROR("Drawstring send error: %s", strerror(errno));
        return ret;
    }
    
    LOG_DETAILS("Send result: %d", ret);

    ret = to_recv(gs_socket, input_buffer, 1, 0);
    LOG_DETAILS("Received: %d 0x%x", ret, input_buffer[0]);
    
    if(0 > ret) {
        LOG_ERROR("Drawstring receive error: %s", strerror(errno));
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
    unsigned char retry = 0;
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
    if(0 > ret) {
        LOG_ERROR("Draw rectangle failed send!");
        return ret;
    }
    
    do
    {
        retry++;
        ret = to_recv(gs_socket, bytes, 1, 0);
    }
    while(0 > ret && (EAGAIN == errno || EWOULDBLOCK == errno) && retry < 5);
    LOG_DETAILS("Draw rectangle received %d 0x%x!", ret, bytes[0]);
    
    if(0 > ret) {
        LOG_ERROR("Draw rectangle failed receive! %s", strerror(errno));
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
    if(0 > ret) {
        LOG_ERROR("Draw line failed send! %s", strerror(errno));
        return ret;
    }
    
    ret = to_recv(gs_socket, bytes, 1, 0);
    
    if(0 > ret) {
        LOG_ERROR("Draw line failed receive! %s", strerror(errno));
    }

    return ret;
}

/*
 * draw_sensor_name is used to draw the name of
 * sensor index in corresponding area. It is 
 * called internally from drawSensorNameCallback
 * and main function.
 * 
 * Parameters:
 * unsigned char index      index of the sensor
 * char* value              sensor name string
 * 
 * Return value:
 * Equal or greater than 0 means successful
 * Less than 0 means failed
 */
int draw_sensor_name(unsigned char index, char* value) {
    int ret;
    unsigned int x_start, y_start, x_end, y_end;
    unsigned char font_size = 24;

    x_start = 2;
    x_end = 82;
    y_start = index * 60 + 6;
    y_end = index * 60 + 31;

    LOG_DETAILS("Sensor name location: %d %d %d %d", x_start, y_start, x_end, y_end);

    // erase target area
    ret = draw_rectangle(x_start, y_start, x_end, y_end, BACKGROUND_COLOR);
    if(0 > ret) {
        LOG_ERROR("Error drawSensor1Callback drawing rectangle!");
        return ret;
    }
    
    ret = draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, font_size, value);
    if(0 > ret) {
        LOG_ERROR("Error drawSensor1Callback drawing string!");
    }
    
    return ret;
}

/*
 * drawSensor1NameCallback is used to draw the name of
 * sensor 1 in corresponding area. The design here is 
 * to put the name string in redis, and hard code the
 * location of each item in program. So user can change
 * the text at any time, but the overall layout will
 * keep the same.
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 * void *privdata           Not used
 * 
 * Return value:
 * There is no return value
 */
void drawSensorNameCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    
    LOG_DETAILS("Enter drawSensorNameCallback");
    
    char* priv_ch = (char*)privdata;
    if(*priv_ch < '0' || *priv_ch > '3') {
        LOG_WARNING("Warning: invalid priv data %c!", priv_ch);
        return;
    }

    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }
    
    LOG_DETAILS("Priv data: %c", *priv_ch);
    
    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        if(0 > draw_sensor_name(*priv_ch - '0', reply->element[2]->str)) {
            LOG_ERROR("Error drawSensorNameCallback!");
            redisAsyncDisconnect(c);
        }
    } else {
        LOG_WARNING("Invalid drawSensorNameCallback input format!");
    }
    
    LOG_DETAILS("drawSensorNameCallback finished!");
}

/*
 * draw_sensor is used to draw the value of
 * sensor index in corresponding area. It is 
 * called internally from drawSensorCallback
 * and main function.
 * 
 * Parameters:
 * unsigned char index      index of the sensor
 * char* value              sensor name string
 * 
 * Return value:
 * Equal or greater than 0 means successful
 * Less than 0 means failed
 */
int draw_sensor(unsigned char index, char* value) {
    int ret;
    unsigned int x_start, y_start, x_end, y_end;
    unsigned char font_size = 24;

    x_start = 2;
    x_end = 82;
    y_start = 60 * index + 34;
    y_end = 60 * index + 59;
    
    LOG_DETAILS("Sensor location: %d %d %d %d", x_start, y_start, x_end, y_end);

    // erase target area
    ret = draw_rectangle(x_start, y_start, x_end, y_end, BACKGROUND_COLOR);
    if(0 > ret) {
        LOG_ERROR("Error draw_sensor drawing rectangle!");
        return ret;
    }
    
    ret = draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, font_size, value);
    if(0 > ret) {
        LOG_ERROR("Error draw_sensor drawing string!");
    }
    
    return ret;
}

/*
 * drawSensor1Callback is used to draw the value of
 * sensor 1 in corresponding area. The design here is 
 * to put the value string in redis, and hard code the
 * location of each item in program. So user can change
 * the text at any time, but the overall layout will
 * keep the same.
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 * void *privdata           Not used
 * 
 * Return value:
 * There is no return value
 */
void drawSensorCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;

    char* priv_ch = (char*)privdata;
    if(*priv_ch < '0' || *priv_ch > '3') {
        LOG_WARNING("Warning: invalid priv data %c!", priv_ch);
        return;
    }

    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }
    
    struct timeval newTime;
    gettimeofday(&newTime, NULL);
    if(((newTime.tv_sec - gs_sensor[*priv_ch - '0'].tv_sec) * 1000 + 
        (newTime.tv_usec - gs_sensor[*priv_ch - '0'].tv_usec) / 1000) < SENSOR_VALUE_INTERVAL_MS) {
        LOG_DETAILS("Refresh frequency of channel %c is too high, skip", *priv_ch);
        return;
    }

    gettimeofday(&gs_sensor[*priv_ch - '0'], NULL);

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        if(0 > draw_sensor(*priv_ch - '0', reply->element[2]->str)) {
            LOG_ERROR("Error drawSensorCallback!");
            redisAsyncDisconnect(c);
        }
    } else {
        LOG_WARNING("Invalid drawSensorCallback input format!");
    }
    
    LOG_DETAILS("drawSensorCallback finished!");
}

/*
 * draw_sw_name is used to draw the name of
 * switch index in corresponding area. It is 
 * called internally from drawSWNameCallback
 * and main function.
 * 
 * Parameters:
 * unsigned char index      index of the sensor
 * char* value              sensor name string
 * 
 * Return value:
 * Equal or greater than 0 means successful
 * Less than 0 means failed
 */
int draw_sw_name(unsigned char index, char* value) {
    int ret = SND_RCV_OK;
    unsigned int x_start, y_start, x_end, y_end;
    unsigned char font_size = 32;

    x_start = 79 * (index % 3) + 91;
    x_end = 79 * (index % 3) + 162;

    if(index >= 3) {
        y_start = 152;
        y_end = 185;
    } else {
        y_start = 31;
        y_end = 64;
    }

    // erase target area
    ret = draw_rectangle(x_start, y_start, x_end, y_end, BACKGROUND_COLOR);
    if(0 > ret) {
        LOG_ERROR("Error drawSensor1Callback drawing rectangle!");
        return ret;
    }
    
    ret = draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, font_size, value);
    if(0 > ret) {
        LOG_ERROR("Error drawSensor1Callback drawing string!");
    }
    
    return ret;
}

/*
 * drawSensor1NameCallback is used to draw the name of
 * switch 1 in corresponding area. The design here is 
 * to put the name string in redis, and hard code the
 * location of each item in program. So user can change
 * the text at any time, but the overall layout will
 * keep the same.
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 * void *privdata           Not used
 * 
 * Return value:
 * There is no return value
 */
void drawSWNameCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;

    char* priv_ch = (char*)privdata;
    if(*priv_ch < '0' || *priv_ch > '5') {
        LOG_WARNING("Warning: invalid priv data %c!", priv_ch);
        return;
    }

    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        if(0 > draw_sw_name(*priv_ch - '0', reply->element[2]->str)) {
            LOG_ERROR("Error drawSWNameCallback!");
            redisAsyncDisconnect(c);
        }
    } else {
        LOG_WARNING("Invalid drawSensorCallback input format!");
    }
    
    LOG_DETAILS("drawSWNameCallback finished!");
}

/*
 * draw_sw_status is used to draw the status of
 * switch index in corresponding area. It is 
 * called internally from drawSWStatusCallback
 * and main function.
 * 
 * Parameters:
 * unsigned char index      index of the sensor
 * char* value              sensor name string
 * 
 * Return value:
 * Equal or greater than 0 means successful
 * Less than 0 means failed
 */
int draw_sw_status(unsigned char index, unsigned char status) {
    int ret;
    unsigned int x_start, y_start, x_end, y_end, x_min, x_max;
    unsigned char font_size = 16;
    const char* ch = NULL;

    if(index >= 3) {
        y_start = 190;
        y_end = 207;
    } else {
        y_start = 69;
        y_end = 86;
    }

    x_min = (index % 3) * 79 + 107;
    x_max = (index % 3) * 79 + 140;
    if(0 == status) {
        x_start = (index % 3) * 79 + 107;
        x_end = (index % 3) * 79 + 124;
        ch = "关";
    } else {
        x_start = (index % 3) * 79 + 123;
        x_end = (index % 3) * 79 + 140;
        ch = "开";
    }

    ret = draw_rectangle(x_min, y_start, x_max, y_end, BACKGROUND_COLOR);
    if(0 > ret) {
        LOG_ERROR("Error draw_sw_status drawing rectangle!");
        return ret;
    }
   
    ret = draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, font_size, ch);
    if(0 > ret) {
        LOG_ERROR("Error draw_sw_status drawing string!");
    }
        
    return ret;
}

/*
 * drawSW1StatusCallback is used to draw the value of
 * switch 1 in corresponding area. The design here is 
 * to put the value string in redis, and hard code the
 * location of each item in program. So user can change
 * the text at any time, but the overall layout will
 * keep the same.
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 * void *privdata           Not used
 * 
 * Return value:
 * There is no return value
 */
void drawSWStatusCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;

    char* priv_ch = (char*)privdata;
    if(*priv_ch < '0' || *priv_ch > '5') {
        LOG_WARNING("Warning: invalid priv data %c!", priv_ch);
        return;
    }

    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        if(0 > draw_sw_status(*priv_ch - '0', 0 != strcmp("0", reply->element[2]->str))) {
            LOG_ERROR("Error drawSWStatusCallback!");
            redisAsyncDisconnect(c);
        }
    } else {
        LOG_WARNING("Invalid drawSWStatusCallback input format!");
    }
    
    LOG_DETAILS("drawSWStatusCallback finished!");
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
    if(0 > ret) {
        LOG_ERROR("Error setBrightnessCallback send failed! %s", strerror(errno));
        return ret;
    }
    
    ret = to_recv(gs_socket, bytes, 1, 0);
    if(0 > ret){
        LOG_ERROR("Error setBrightnessCallback recv failed! %s", strerror(errno));
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
    
l_start:
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    struct event_base *base = event_base_new();;
    struct timeval timeout = { 0, 100000 }; 

    char* serv_ip;
    const char* redis_ip;
    int serv_port, redis_port;
    
    redisContext *sync_context = NULL;

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
        
    gs_socket = to_connect(serv_ip, serv_port);
    if(0 > gs_socket) {
        LOG_ERROR("Error creating socket!");
        goto l_socket_cleanup;
    }
    
/*  
 * Uncomment this part when LCD code upgraded
 */
/*
    if(0 > to_recv(gs_socket, &temp, 1, 0)) {
        LOG_ERROR("Error receiving initial data! %d %s", ret, strerror(errno));
        goto l_socket_cleanup;
    }
    LOG_INFO("Connected to controller, remote socket: %d", temp);
*/    
    
    sync_context = redisConnectWithTimeout(redis_ip, redis_port, timeout);
    if(NULL == sync_context) {
        LOG_ERROR("Connection error: can't allocate redis context");
        goto l_exit;
    }
    
    if(sync_context->err) {
        LOG_ERROR("Connection error: %s", sync_context->errstr);
        goto l_free_sync_redis;
    }

    redisReply* reply = redisCommand(sync_context,"PING");
    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s", sync_context->errstr);
        goto l_free_sync_redis;
    }
    LOG_DEBUG("PING: %s", reply->str);
    freeReplyObject(reply);

    LOG_INFO("Connected to redis in sync mode");
    
    // update log level to config in redis
    LOG_DETAILS("GET %s/%s/%s", FLAG_KEY, serv_ip, LOG_LEVEL_FLAG_VALUE);
    reply = redisCommand(sync_context,"GET %s/%s/%s", FLAG_KEY, serv_ip, LOG_LEVEL_FLAG_VALUE);

    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s", sync_context->errstr);
        goto l_free_async_redis;
    }
    
    if(NULL != reply->str) {
        if(LOG_SET_LEVEL_OK != log_set_level(reply->str)) {
            LOG_WARNING("Failed to set log level %s", reply->str);
        }
    }

    freeReplyObject(reply);
    
    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, redis_ip, redis_port);
    struct timeval tv = {0};
    tv.tv_sec = 1;
    options.connect_timeout = &tv;

    gs_async_context = redisAsyncConnectWithOptions(&options);
    if(NULL == gs_async_context) {
        LOG_ERROR("Error cannot allocate gs_async_context!");
        goto l_socket_cleanup;
    }
    
    if (gs_async_context->err) {
        LOG_ERROR("Error async connect: %s", gs_async_context->errstr);
        goto l_free_async_redis;
    }

    if(REDIS_OK != redisLibeventAttach(gs_async_context,base)) {
        LOG_ERROR("Error: error redis libevent attach!");
        goto l_free_async_redis;
    }

    redisAsyncSetConnectCallback(gs_async_context,connectCallback);
    redisAsyncSetDisconnectCallback(gs_async_context,disconnectCallback);
    
    for(int i = 0; i < 4; i++) {
        gettimeofday(&gs_sensor[i], NULL);
    }

    // draw grids
    if(0 > draw_rectangle(0, 0, 319, 239, COLOR_BLUE)) {
        goto l_free_async_redis;
    }
    if(0 > draw_line(0, 60, 84, 60, COLOR_WHITE)) {
        goto l_free_async_redis;
    }
    if(0 > draw_line(0, 120, 319, 120, COLOR_WHITE)) {
        goto l_free_async_redis;
    }
    if(0 > draw_line(0, 180, 84, 180, COLOR_WHITE)) {
        goto l_free_async_redis;
    }
    if(0 > draw_line(84, 0, 84, 239, COLOR_WHITE)) {
        goto l_free_async_redis;
    }
    if(0 > draw_line(163, 0, 163, 239, COLOR_WHITE)) {
        goto l_free_async_redis;
    }
    if(0 > draw_line(242, 0, 242, 239, COLOR_WHITE)) {
        goto l_free_async_redis;
    }

    // Initialize sensor
    for(int i = 0; i < 4; i++) {
        LOG_DETAILS("HGET %s/%s %s%d", FLAG_KEY, serv_ip, SENSOR_NAME_TOPIC, i);
        reply = redisCommand(sync_context,"HGET %s/%s %s%d", FLAG_KEY, serv_ip, SENSOR_NAME_TOPIC, i);

        if(NULL == reply) {
            LOG_ERROR("Failed to sync query redis sensor name config %s", sync_context->errstr);
            goto l_free_async_redis;
        }
        
        if(NULL != reply->str) {
            LOG_DETAILS("Get sensor name %s", reply->str);
            if(0 > draw_sensor_name(i, reply->str)) {
                LOG_ERROR("Failed to draw sensor name %d", i);
                goto l_free_async_redis;
            }

            LOG_DETAILS("SUBSCRIBE %s/%s %s%d", FLAG_KEY, serv_ip, SENSOR_NAME_TOPIC, i);
            redisAsyncCommand(gs_async_context, drawSensorNameCallback, &gs_idx_name[i], "SUBSCRIBE %s/%s %s%d", FLAG_KEY, serv_ip, SENSOR_NAME_TOPIC, i);
        }
        
        freeReplyObject(reply);

        LOG_DETAILS("HGET %s/%s %s%d", FLAG_KEY, serv_ip, SENSOR_TOPIC, i);
        reply = redisCommand(sync_context,"HGET %s/%s %s%d", FLAG_KEY, serv_ip, SENSOR_TOPIC, i);

        if(NULL == reply) {
            LOG_ERROR("Failed to sync query redis sensor value config %s", sync_context->errstr);
            goto l_free_async_redis;
        }
        
        if(NULL != reply->str) {
            redisReply* reply2 = redisCommand(sync_context,"GET %s", reply->str);
            
            if(NULL == reply2) {
                LOG_ERROR("Failed to sync query redis sensor value topic %s", sync_context->errstr);
                goto l_free_async_redis;
            }
            
            if(NULL != reply2->str) {
                if(0 > draw_sensor(i, reply2->str)) {
                    LOG_ERROR("Failed to draw sensor name %d", i);
                    freeReplyObject(reply2);
                    goto l_free_async_redis;
                }
            }
            
            freeReplyObject(reply2);

            redisAsyncCommand(gs_async_context, drawSensorCallback, &gs_idx_name[i], "SUBSCRIBE %s", reply->str);
        }
        
        freeReplyObject(reply);
    }

    // Initialize brightness
    LOG_DETAILS("GET %s/%s/%s", FLAG_KEY, serv_ip, BRIGHTNESS_TOPIC);
    reply = redisCommand(sync_context,"GET %s/%s/%s", FLAG_KEY, serv_ip, BRIGHTNESS_TOPIC);

    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis sensor topic %s", sync_context->errstr);
        goto l_free_async_redis;
    }
    
    if(NULL != reply->str) {
        if(0 > set_brightness(atoi(reply->str) & 0xFF)) {
            LOG_ERROR("Failed to set brightness");
            goto l_free_async_redis;
        }
    }
    
    freeReplyObject(reply);
    
    LOG_DETAILS("SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, BRIGHTNESS_TOPIC);
    redisAsyncCommand(gs_async_context, setBrightnessCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, BRIGHTNESS_TOPIC);
    
    // Initialize switch
    for(int i = 0; i < 6; i++) {
        LOG_DETAILS("HGET %s/%s %s%d", FLAG_KEY, serv_ip, SW_NAME_TOPIC, i);
        reply = redisCommand(sync_context,"HGET %s/%s %s%d", FLAG_KEY, serv_ip, SW_NAME_TOPIC, i);

        if(NULL == reply) {
            LOG_ERROR("Failed to sync query redis switch name config %s", sync_context->errstr);
            goto l_free_async_redis;
        }
        
        if(NULL != reply->str) {
            if(0 > draw_sw_name(i, reply->str)) {
                LOG_ERROR("Failed to draw sensor name %d", i);
                goto l_free_async_redis;
            }

            redisAsyncCommand(gs_async_context, drawSWNameCallback, &gs_idx_name[i], "SUBSCRIBE %s", reply->str);
        }
        
        freeReplyObject(reply);

        LOG_DETAILS("HGET %s/%s %s%d", FLAG_KEY, serv_ip, SW_TOPIC, i);
        reply = redisCommand(sync_context,"HGET %s/%s %s%d", FLAG_KEY, serv_ip, SW_TOPIC, i);

        if(NULL == reply) {
            LOG_ERROR("Failed to sync query redis switch status config %s", sync_context->errstr);
            goto l_free_async_redis;
        }
        
        if(NULL != reply->str) {
            redisReply* reply2 = redisCommand(sync_context,"GET %s", reply->str);
            
            if(NULL == reply2) {
                LOG_ERROR("Failed to sync query redis sensor status topic %s", sync_context->errstr);
                goto l_free_async_redis;
            }
            
            if(NULL != reply2->str) {
                if(0 > draw_sw_status(i, 0 != strcmp("0", reply2->str))) {
                    LOG_ERROR("Failed to draw sensor name %d", i);
                    freeReplyObject(reply2);
                    goto l_free_async_redis;
                }
            }
            
            freeReplyObject(reply2);

            redisAsyncCommand(gs_async_context, drawSWStatusCallback, &gs_idx_name[i], "SUBSCRIBE %s", reply->str);
        }
        
        freeReplyObject(reply);
    }
    
    // Subscribe changes for log_level, exit, reset
    LOG_DETAILS("SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, EXIT_FLAG_VALUE);
    redisAsyncCommand(gs_async_context, exitCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, EXIT_FLAG_VALUE);
    LOG_DETAILS("SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, RESET_FLAG_VALUE);
    redisAsyncCommand(gs_async_context, resetCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, RESET_FLAG_VALUE);
    LOG_DETAILS("SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, LOG_LEVEL_FLAG_VALUE);
    redisAsyncCommand(gs_async_context, setLogLevelCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, LOG_LEVEL_FLAG_VALUE);

    event_base_dispatch(base);
        
l_free_async_redis:
    redisAsyncFree(gs_async_context);
    event_base_free(base);
    LOG_INFO("exit!");
    
l_free_sync_redis:
    if(NULL != sync_context) {
        redisFree(sync_context);
        sync_context = NULL;
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
