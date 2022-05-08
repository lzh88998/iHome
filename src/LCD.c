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

#define REDIS_IP        "127.0.0.1"
#define REDIS_PORT      6379

#define COLOR_RED       0xF800
#define COLOR_GREEN     0x07E0
#define COLOR_BLUE      0x001F
#define COLOR_WHITE     0xFFFF
#define COLOR_BLACK     0x0000

#define SND_RCV_OK      0;

#define FLAG_KEY                "lcd"
#define EXIT_FLAG_VALUE         "exit"

#define SENSOR_1_NAME_TOPIC     "sensor1name"
#define SENSOR_2_NAME_TOPIC     "sensor2name"
#define SENSOR_3_NAME_TOPIC     "sensor3name"
#define SENSOR_4_NAME_TOPIC     "sensor4name"

#define SENSOR_1_TOPIC          "sensor1"
#define SENSOR_2_TOPIC          "sensor2"
#define SENSOR_3_TOPIC          "sensor3"
#define SENSOR_4_TOPIC          "sensor4"

#define BRIGHTNESS_TOPIC        "brightness"

#define SW_1_NAME_TOPIC         "sw1name"
#define SW_2_NAME_TOPIC         "sw2name"
#define SW_3_NAME_TOPIC         "sw3name"
#define SW_4_NAME_TOPIC         "sw4name"
#define SW_5_NAME_TOPIC         "sw5name"
#define SW_6_NAME_TOPIC         "sw6name"

#define SW_1_TOPIC              "sw1"
#define SW_2_TOPIC              "sw2"
#define SW_3_TOPIC              "sw3"
#define SW_4_TOPIC              "sw4"
#define SW_5_TOPIC              "sw5"
#define SW_6_TOPIC              "sw6"

static to_socket_ctx gs_socket = -1;

static redisAsyncContext *gs_async_context = NULL;

static char input_buffer[1024];
static char output_buffer[1024];

static iconv_t conv = NULL;

static int gs_exit = 0;

/*
 * convert_temp is used to convert sampled AD value
 * to corresponding environment temperature through
 * a formular. The formular is hardware related
 * in this example it use a 10K NTC with B value
 * 3950
 * 
 * Parameters:
 * unbsigned char v         sampled value from 
 *                          sensor, valid range is 
 *                          0-255
 * 
 * Return value:
 * the converted float value of temperature
 * 
 */
/*float convert_temp(unsigned char v) {
    return 298.15 * 3950 / (log((float)v/(255-v)) * 298.15 + 3950) - 273.15;
}*/

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
    int ret = SND_RCV_OK;
    int converted_cnt = 0;
    va_list ap;
    va_start(ap, fmt);
    LOG_DETAILS("Print");
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
    int ret = SND_RCV_OK;
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
    
    LOG_DEBUG("Draw Rectangle send!");
    ret = to_send(gs_socket, bytes, 11, 0);
    if(0 > ret) {
        return ret;
    }
    
    do
    {
        retry++;
        ret = to_recv(gs_socket, bytes, 1, 0);
    }
    while(0 > ret && (EAGAIN == errno || EWOULDBLOCK == errno) && retry < 5);
    LOG_DETAILS("Draw Rectangle received %d 0x%x!", ret, bytes[0]);
    
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
    int ret = SND_RCV_OK;
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
    
    ret = to_send(gs_socket, bytes, 11, 0);
    if(0 > ret) {
        return ret;
    }
    
    ret = to_recv(gs_socket, bytes, 1, 0);
    
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
void drawSensor1NameCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;
    unsigned char font_size;

    x_start = 2;
    y_start = 6;
    x_end = 82;
    y_end = 59;
    font_size = 24;
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        return;
    }
    
    if(0 > draw_rectangle(x_start, y_start, x_end, y_end, COLOR_BLUE)) {
        LOG_ERROR("Error drawSensor1Callback drawing rectangle!");
        redisAsyncDisconnect(c);
        return;
    }
    
    if(0 > draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, font_size, ch)) {
        LOG_ERROR("Error drawSensor1Callback drawing string!");
        redisAsyncDisconnect(c);
    }
}

/*
 * drawSensor2NameCallback is used to draw the name of
 * sensor 2 in corresponding area. The design here is 
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
void drawSensor2NameCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;
    unsigned char font_size;

    x_start = 2;
    y_start = 66;
    x_end = 82;
    y_end = 119;
    font_size = 24;
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        return;
    }
    
    if(0 > draw_rectangle(x_start, y_start, x_end, y_end, COLOR_BLUE)) {
        LOG_ERROR("Error drawSensor1Callback drawing rectangle!");
        redisAsyncDisconnect(c);
        return;
    }
    
    if(0 > draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, font_size, ch)) {
        LOG_ERROR("Error drawSensor1Callback drawing string!");
        redisAsyncDisconnect(c);
    }
}

/*
 * drawSensor3NameCallback is used to draw the name of
 * sensor 3 in corresponding area. The design here is 
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
void drawSensor3NameCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;
    unsigned char font_size;

    x_start = 2;
    y_start = 126;
    x_end = 82;
    y_end = 179;
    font_size = 24;
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        return;
    }
    
    if(0 > draw_rectangle(x_start, y_start, x_end, y_end, COLOR_BLUE)) {
        LOG_ERROR("Error drawSensor1Callback drawing rectangle!");
        redisAsyncDisconnect(c);
        return;
    }
    
    if(0 > draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, font_size, ch)) {
        LOG_ERROR("Error drawSensor1Callback drawing string!");
        redisAsyncDisconnect(c);
    }
}

/*
 * drawSensor4NameCallback is used to draw the name of
 * sensor 4 in corresponding area. The design here is 
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
void drawSensor4NameCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;
    unsigned char font_size;

    x_start = 2;
    y_start = 186;
    x_end = 82;
    y_end = 239;
    font_size = 24;
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        return;
    }
    
    if(0 > draw_rectangle(x_start, y_start, x_end, y_end, COLOR_BLUE)) {
        LOG_ERROR("Error drawSensor1Callback drawing rectangle!");
        redisAsyncDisconnect(c);
        return;
    }
    
    if(0 > draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, font_size, ch)) {
        LOG_ERROR("Error drawSensor1Callback drawing string!");
        redisAsyncDisconnect(c);
    }
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
void drawSensor1Callback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;
    unsigned char font_size;

    x_start = 2;
    y_start = 34;
    x_end = 82;
    y_end = 59;
    font_size = 24;
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        return;
    }
    
    if(0 > draw_rectangle(x_start, y_start, x_end, y_end, COLOR_BLUE)) {
        LOG_ERROR("Error drawSensor1Callback drawing rectangle!");
        redisAsyncDisconnect(c);
        return;
    }
    
    if(0 > draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, font_size, ch)) {
        LOG_ERROR("Error drawSensor1Callback drawing string!");
        redisAsyncDisconnect(c);
    }
}

/*
 * drawSensor2Callback is used to draw the value of
 * sensor 2 in corresponding area. The design here is 
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
void drawSensor2Callback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;
    unsigned char font_size;

    x_start = 2;
    y_start = 94;
    x_end = 82;
    y_end = 119;
    font_size = 24;
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        return;
    }
    
    if(0 > draw_rectangle(x_start, y_start, x_end, y_end, COLOR_BLUE)) {
        LOG_ERROR("Error drawSensor1Callback drawing rectangle!");
        redisAsyncDisconnect(c);
        return;
    }
    
    if(0 > draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, font_size, ch)) {
        LOG_ERROR("Error drawSensor1Callback drawing string!");
        redisAsyncDisconnect(c);
    }
}

/*
 * drawSensor3Callback is used to draw the value of
 * sensor 3 in corresponding area. The design here is 
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
void drawSensor3Callback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;
    unsigned char font_size;

    x_start = 2;
    y_start = 154;
    x_end = 82;
    y_end = 179;
    font_size = 24;
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        return;
    }
    
    if(0 > draw_rectangle(x_start, y_start, x_end, y_end, COLOR_BLUE)) {
        LOG_ERROR("Error drawSensor1Callback drawing rectangle!");
        redisAsyncDisconnect(c);
        return;
    }
    
    if(0 > draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, font_size, ch)) {
        LOG_ERROR("Error drawSensor1Callback drawing string!");
        redisAsyncDisconnect(c);
    }
}

/*
 * drawSensor4Callback is used to draw the value of
 * sensor 4 in corresponding area. The design here is 
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
void drawSensor4Callback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;
    unsigned char font_size;

    x_start = 2;
    y_start = 214;
    x_end = 82;
    y_end = 239;
    font_size = 24;
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        return;
    }
    
    if(0 > draw_rectangle(x_start, y_start, x_end, y_end, COLOR_BLUE)) {
        LOG_ERROR("Error drawSensor1Callback drawing rectangle!");
        redisAsyncDisconnect(c);
        return;
    }
    
    if(0 > draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, font_size, ch)) {
        LOG_ERROR("Error drawSensor1Callback drawing string!");
        redisAsyncDisconnect(c);
    }
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
void drawSW1NameCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;
    unsigned char font_size;

    x_start = 91;
    y_start = 31;
    x_end = 162;
    y_end = 64;
    font_size = 32;
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        return;
    }
    
    if(0 > draw_rectangle(x_start, y_start, x_end, y_end, COLOR_BLUE)) {
        LOG_ERROR("Error drawSensor1Callback drawing rectangle!");
        redisAsyncDisconnect(c);
        return;
    }
    
    if(0 > draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, font_size, ch)) {
        LOG_ERROR("Error drawSensor1Callback drawing string!");
        redisAsyncDisconnect(c);
    }
}

/*
 * drawSensor2NameCallback is used to draw the name of
 * switch 2 in corresponding area. The design here is 
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
void drawSW2NameCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;
    unsigned char font_size;

    x_start = 171;
    y_start = 31;
    x_end = 240;
    y_end = 64;
    font_size = 32;
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        return;
    }
    
    if(0 > draw_rectangle(x_start, y_start, x_end, y_end, COLOR_BLUE)) {
        LOG_ERROR("Error drawSensor1Callback drawing rectangle!");
        redisAsyncDisconnect(c);
        return;
    }
    
    if(0 > draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, font_size, ch)) {
        LOG_ERROR("Error drawSensor1Callback drawing string!");
        redisAsyncDisconnect(c);
    }
}

/*
 * drawSensor3NameCallback is used to draw the name of
 * switch 3 in corresponding area. The design here is 
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
void drawSW3NameCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;
    unsigned char font_size;

    x_start = 249;
    y_start = 31;
    x_end = 319;
    y_end = 64;
    font_size = 32;
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        return;
    }
    
    if(0 > draw_rectangle(x_start, y_start, x_end, y_end, COLOR_BLUE)) {
        LOG_ERROR("Error drawSensor1Callback drawing rectangle!");
        redisAsyncDisconnect(c);
        return;
    }
    
    if(0 > draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, font_size, ch)) {
        LOG_ERROR("Error drawSensor1Callback drawing string!");
        redisAsyncDisconnect(c);
    }
}

/*
 * drawSensor4NameCallback is used to draw the name of
 * switch 4 in corresponding area. The design here is 
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
void drawSW4NameCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;
    unsigned char font_size;

    x_start = 91;
    y_start = 152;
    x_end = 162;
    y_end = 185;
    font_size = 32;
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        return;
    }
    
    if(0 > draw_rectangle(x_start, y_start, x_end, y_end, COLOR_BLUE)) {
        LOG_ERROR("Error drawSensor1Callback drawing rectangle!");
        redisAsyncDisconnect(c);
        return;
    }
    
    if(0 > draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, font_size, ch)) {
        LOG_ERROR("Error drawSensor1Callback drawing string!");
        redisAsyncDisconnect(c);
    }
}

/*
 * drawSensor5NameCallback is used to draw the name of
 * switch 5 in corresponding area. The design here is 
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
void drawSW5NameCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;
    unsigned char font_size;

    x_start = 171;
    y_start = 152;
    x_end = 240;
    y_end = 185;
    font_size = 32;
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        return;
    }
    
    if(0 > draw_rectangle(x_start, y_start, x_end, y_end, COLOR_BLUE)) {
        LOG_ERROR("Error drawSensor1Callback drawing rectangle!");
        redisAsyncDisconnect(c);
        return;
    }
    
    if(0 > draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, font_size, ch)) {
        LOG_ERROR("Error drawSensor1Callback drawing string!");
        redisAsyncDisconnect(c);
    }
}

/*
 * drawSensor6NameCallback is used to draw the name of
 * switch 6 in corresponding area. The design here is 
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
void drawSW6NameCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;
    unsigned char font_size;

    x_start = 249;
    y_start = 152;
    x_end = 319;
    y_end = 185;
    font_size = 32;
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        return;
    }
    
    if(0 > draw_rectangle(x_start, y_start, x_end, y_end, COLOR_BLUE)) {
        LOG_ERROR("Error drawSensor1Callback drawing rectangle!");
        redisAsyncDisconnect(c);
        return;
    }
    
    if(0 > draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, font_size, ch)) {
        LOG_ERROR("Error drawSensor1Callback drawing string!");
        redisAsyncDisconnect(c);
    }
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
void drawSW1StatusCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;

    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        return;
    }
    
    y_start = 69;
    y_end = 86;
    if(0 == strcmp("0", ch)) {
        x_start = 107;
        x_end = 124;
        ch = "关";
    } else {
        x_start = 123;
        x_end = 140;
        ch = "开";
    }
    
    if(0 > draw_rectangle(107, 69, 140, 86, COLOR_BLUE)) {
        LOG_ERROR("Error drawSensor1Callback drawing rectangle!");
        redisAsyncDisconnect(c);
        return;
    }
    
    if(0 > draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, 16, ch)) {
        LOG_ERROR("Error drawSensor1Callback drawing string!");
        redisAsyncDisconnect(c);
    }
 }

/*
 * drawSW2StatusCallback is used to draw the value of
 * switch 2 in corresponding area. The design here is 
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
void drawSW2StatusCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;

    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        return;
    }
    
    y_start = 69;
    y_end = 86;
    if(0 == strcmp("0", ch)) {
        x_start = 187;
        x_end = 204;
        ch = "关";
    } else {
        x_start = 203;
        x_end = 220;
        ch = "开";
    }
    
    if(0 > draw_rectangle(187, 69, 220, 86, COLOR_BLUE)) {
        LOG_ERROR("Error drawSensor1Callback drawing rectangle!");
        redisAsyncDisconnect(c);
        return;
    }
    
    if(0 > draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, 16, ch)) {
        LOG_ERROR("Error drawSensor1Callback drawing string!");
        redisAsyncDisconnect(c);
    }
}

/*
 * drawSW3StatusCallback is used to draw the value of
 * switch 3 in corresponding area. The design here is 
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
void drawSW3StatusCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;

    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        return;
    }
    
    y_start = 69;
    y_end = 86;
    if(0 == strcmp("0", ch)) {
        x_start = 265;
        x_end = 282;
        ch = "关";
    } else {
        x_start = 281;
        x_end = 298;
        ch = "开";
    }
    
    if(0 > draw_rectangle(265, 69, 298, 86, COLOR_BLUE)) {
        LOG_ERROR("Error drawSensor1Callback drawing rectangle!");
        redisAsyncDisconnect(c);
        return;
    }
    
    if(0 > draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, 16, ch)) {
        LOG_ERROR("Error drawSensor1Callback drawing string!");
        redisAsyncDisconnect(c);
    }
}

/*
 * drawSW4StatusCallback is used to draw the value of
 * switch 4 in corresponding area. The design here is 
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
void drawSW4StatusCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;

    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        return;
    }
    
    y_start = 190;
    y_end = 207;
    if(0 == strcmp("0", ch)) {
        x_start = 107;
        x_end = 124;
        ch = "关";
    } else {
        x_start = 123;
        x_end = 140;
        ch = "开";
    }
    
    if(0 > draw_rectangle(107, 190, 140, 207, COLOR_BLUE)) {
        LOG_ERROR("Error drawSensor1Callback drawing rectangle!");
        redisAsyncDisconnect(c);
        return;
    }
    
    if(0 > draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, 16, ch)) {
        LOG_ERROR("Error drawSensor1Callback drawing string!");
        redisAsyncDisconnect(c);
    }
}

/*
 * drawSW5StatusCallback is used to draw the value of
 * switch 5 in corresponding area. The design here is 
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
void drawSW5StatusCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;

    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        return;
    }
    
    y_start = 190;
    y_end = 207;
    if(0 == strcmp("0", ch)) {
        x_start = 187;
        x_end = 204;
        ch = "关";
    } else {
        x_start = 203;
        x_end = 220;
        ch = "开";
    }
    
    if(0 > draw_rectangle(187, 190, 220, 207, COLOR_BLUE)) {
        LOG_ERROR("Error drawSensor1Callback drawing rectangle!");
        redisAsyncDisconnect(c);
        return;
    }
    
    if(0 > draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, 16, ch)) {
        LOG_ERROR("Error drawSensor1Callback drawing string!");
        redisAsyncDisconnect(c);
    }
}

/*
 * drawSW6StatusCallback is used to draw the value of
 * switch 6 in corresponding area. The design here is 
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
void drawSW6StatusCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;

    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        return;
    }
    
    y_start = 190;
    y_end = 207;
    if(0 == strcmp("0", ch)) {
        x_start = 265;
        x_end = 282;
        ch = "关";
    } else {
        x_start = 281;
        x_end = 298;
        ch = "开";
    }
    
    if(0 > draw_rectangle(265, 190, 298, 207, COLOR_BLUE)) {
        LOG_ERROR("Error drawSensor1Callback drawing rectangle!");
        redisAsyncDisconnect(c);
        return;
    }
    
    if(0 > draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, 16, ch)) {
        LOG_ERROR("Error drawSensor1Callback drawing string!");
        redisAsyncDisconnect(c);
    }
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
    const char* ch = NULL;
    char bytes[2];
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        return;
    }
    
    bytes[0] = 0x65;
    bytes[1] = atoi(ch) & 0xFF;

    if(0 > to_send(gs_socket, bytes, 2, 0)) {
        LOG_ERROR("Error setBrightnessCallback send failed! %s", strerror(errno));
        redisAsyncDisconnect(c);
        return;
    }
    
    if(0 > to_recv(gs_socket, bytes, 1, 0)){
        LOG_ERROR("Error setBrightnessCallback recv failed! %s", strerror(errno));
        redisAsyncDisconnect(c);
    }
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
    const char* ch = NULL;
    
    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        return;
    }
    
    if(0 == strcmp(EXIT_FLAG_VALUE, ch)) {
        gs_exit = 1;
        redisAsyncDisconnect(c);
    }
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

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        return;
    }
    
    if(0 > log_set_level(ch)) {
        LOG_WARNING("Warning: invalid log level %s!", ch);
    }
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
    struct event_base *base;

    char* serv_ip;
    const char* redis_ip;
    int serv_port, redis_port;
    
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
            redis_ip = REDIS_IP;
            redis_port = REDIS_PORT;
            break;
        case 3:
            serv_ip = argv[1];
            serv_port = atoi(argv[2]);
            redis_ip = REDIS_IP;
            redis_port = REDIS_PORT;
            break;
        default:
            print_usage(argc, argv);
            return -1;
    }
        
    gs_socket = to_connect(serv_ip, serv_port);
    if(0 > gs_socket) {
        LOG_ERROR("Error creating socket!");
        goto l_start;
    }
    
/*  
    if(0 > to_recv(gs_socket, &temp, 1, 0)) {
        LOG_ERROR("Error receiving initial data! %d %s", ret, strerror(errno));
        goto l_socket_cleanup;
    }
    LOG_INFO("Connected to controller, remote socket: %d", temp);
*/    
    
    base = event_base_new();
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

    redisAsyncCommand(gs_async_context, drawSensor1NameCallback, NULL, "GET %s/%s/%s", FLAG_KEY, serv_ip, SENSOR_1_NAME_TOPIC);
    redisAsyncCommand(gs_async_context, drawSensor2NameCallback, NULL, "GET %s/%s/%s", FLAG_KEY, serv_ip, SENSOR_2_NAME_TOPIC);
    redisAsyncCommand(gs_async_context, drawSensor3NameCallback, NULL, "GET %s/%s/%s", FLAG_KEY, serv_ip, SENSOR_3_NAME_TOPIC);
    redisAsyncCommand(gs_async_context, drawSensor4NameCallback, NULL, "GET %s/%s/%s", FLAG_KEY, serv_ip, SENSOR_4_NAME_TOPIC);

    redisAsyncCommand(gs_async_context, drawSensor1Callback, NULL, "GET %s/%s/%s", FLAG_KEY, serv_ip, SENSOR_1_TOPIC);
    redisAsyncCommand(gs_async_context, drawSensor2Callback, NULL, "GET %s/%s/%s", FLAG_KEY, serv_ip, SENSOR_2_TOPIC);
    redisAsyncCommand(gs_async_context, drawSensor3Callback, NULL, "GET %s/%s/%s", FLAG_KEY, serv_ip, SENSOR_3_TOPIC);
    redisAsyncCommand(gs_async_context, drawSensor4Callback, NULL, "GET %s/%s/%s", FLAG_KEY, serv_ip, SENSOR_4_TOPIC);

    redisAsyncCommand(gs_async_context, setBrightnessCallback, NULL, "GET %s/%s/%s", FLAG_KEY, serv_ip, BRIGHTNESS_TOPIC);

    redisAsyncCommand(gs_async_context, drawSW1NameCallback, NULL, "GET %s/%s/%s", FLAG_KEY, serv_ip, SW_1_NAME_TOPIC);
    redisAsyncCommand(gs_async_context, drawSW2NameCallback, NULL, "GET %s/%s/%s", FLAG_KEY, serv_ip, SW_2_NAME_TOPIC);
    redisAsyncCommand(gs_async_context, drawSW3NameCallback, NULL, "GET %s/%s/%s", FLAG_KEY, serv_ip, SW_3_NAME_TOPIC);
    redisAsyncCommand(gs_async_context, drawSW4NameCallback, NULL, "GET %s/%s/%s", FLAG_KEY, serv_ip, SW_4_NAME_TOPIC);
    redisAsyncCommand(gs_async_context, drawSW5NameCallback, NULL, "GET %s/%s/%s", FLAG_KEY, serv_ip, SW_5_NAME_TOPIC);
    redisAsyncCommand(gs_async_context, drawSW6NameCallback, NULL, "GET %s/%s/%s", FLAG_KEY, serv_ip, SW_6_NAME_TOPIC);

    redisAsyncCommand(gs_async_context, drawSW1StatusCallback, NULL, "GET %s/%s/%s", FLAG_KEY, serv_ip, SW_1_TOPIC);
    redisAsyncCommand(gs_async_context, drawSW2StatusCallback, NULL, "GET %s/%s/%s", FLAG_KEY, serv_ip, SW_2_TOPIC);
    redisAsyncCommand(gs_async_context, drawSW3StatusCallback, NULL, "GET %s/%s/%s", FLAG_KEY, serv_ip, SW_3_TOPIC);
    redisAsyncCommand(gs_async_context, drawSW4StatusCallback, NULL, "GET %s/%s/%s", FLAG_KEY, serv_ip, SW_4_TOPIC);
    redisAsyncCommand(gs_async_context, drawSW5StatusCallback, NULL, "GET %s/%s/%s", FLAG_KEY, serv_ip, SW_5_TOPIC);
    redisAsyncCommand(gs_async_context, drawSW6StatusCallback, NULL, "GET %s/%s/%s", FLAG_KEY, serv_ip, SW_6_TOPIC);

    redisAsyncCommand(gs_async_context, setLogLevelCallback, NULL, "GET %s/%s/%s", FLAG_KEY, serv_ip, LOG_LEVEL_FLAG_VALUE);

    redisAsyncCommand(gs_async_context, drawSensor1NameCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, SENSOR_1_NAME_TOPIC);
    redisAsyncCommand(gs_async_context, drawSensor2NameCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, SENSOR_2_NAME_TOPIC);
    redisAsyncCommand(gs_async_context, drawSensor3NameCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, SENSOR_3_NAME_TOPIC);
    redisAsyncCommand(gs_async_context, drawSensor4NameCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, SENSOR_4_NAME_TOPIC);

    redisAsyncCommand(gs_async_context, drawSensor1Callback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, SENSOR_1_TOPIC);
    redisAsyncCommand(gs_async_context, drawSensor2Callback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, SENSOR_2_TOPIC);
    redisAsyncCommand(gs_async_context, drawSensor3Callback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, SENSOR_3_TOPIC);
    redisAsyncCommand(gs_async_context, drawSensor4Callback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, SENSOR_4_TOPIC);

    redisAsyncCommand(gs_async_context, setBrightnessCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, BRIGHTNESS_TOPIC);

    redisAsyncCommand(gs_async_context, drawSW1NameCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, SW_1_NAME_TOPIC);
    redisAsyncCommand(gs_async_context, drawSW2NameCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, SW_2_NAME_TOPIC);
    redisAsyncCommand(gs_async_context, drawSW3NameCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, SW_3_NAME_TOPIC);
    redisAsyncCommand(gs_async_context, drawSW4NameCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, SW_4_NAME_TOPIC);
    redisAsyncCommand(gs_async_context, drawSW5NameCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, SW_5_NAME_TOPIC);
    redisAsyncCommand(gs_async_context, drawSW6NameCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, SW_6_NAME_TOPIC);

    redisAsyncCommand(gs_async_context, drawSW1StatusCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, SW_1_TOPIC);
    redisAsyncCommand(gs_async_context, drawSW2StatusCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, SW_2_TOPIC);
    redisAsyncCommand(gs_async_context, drawSW3StatusCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, SW_3_TOPIC);
    redisAsyncCommand(gs_async_context, drawSW4StatusCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, SW_4_TOPIC);
    redisAsyncCommand(gs_async_context, drawSW5StatusCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, SW_5_TOPIC);
    redisAsyncCommand(gs_async_context, drawSW6StatusCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, SW_6_TOPIC);

    redisAsyncCommand(gs_async_context, exitCallback, NULL, "SUBSCRIBE %s/%s", FLAG_KEY, serv_ip);
    redisAsyncCommand(gs_async_context, setLogLevelCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, LOG_LEVEL_FLAG_VALUE);

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
    if(0 > draw_line(241, 0, 241, 239, COLOR_WHITE)) {
        goto l_free_async_redis;
    }

    event_base_dispatch(base);
        
l_free_async_redis:
    redisAsyncFree(gs_async_context);
    event_base_free(base);
    LOG_INFO("exit!");
    
l_socket_cleanup:
    to_close(gs_socket);
    gs_socket = -1;
    
    goto l_start;
    
    return 0;
}
