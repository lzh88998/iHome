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

#define REDIS_IP                    "127.0.0.1"
#define REDIS_PORT                  6379

#define COLOR_RED                   0xF800
#define COLOR_GREEN                 0x07E0
#define COLOR_BLUE                  0x001F
#define COLOR_WHITE                 0xFFFF
#define COLOR_BLACK                 0x0000

#define SND_RCV_OK                  0;

#define SENSOR_VALUE_INTERVAL_MS    1500

#define FLAG_KEY                    "lcd"
#define EXIT_FLAG_VALUE             "exit"

#define SENSOR_NAME_TOPIC           "sensorname"

#define SENSOR_TOPIC                "sensor"

#define BRIGHTNESS_TOPIC            "brightness"

#define SW_NAME_TOPIC               "swname"

#define SW_TOPIC                    "sw"

static struct timeval gs_sensor[4];

static to_socket_ctx gs_socket = -1;

static redisAsyncContext *gs_async_context = NULL;

static char input_buffer[1024];
static char output_buffer[1024];

static iconv_t conv = NULL;

static char gs_idx_name[6] = {'1', '2', '3', '4', '5', '6'};

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
void drawSensorNameCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;
    unsigned char font_size;
    
    LOG_DETAILS("Enter drawSensorNameCallback");
    
    char* priv_ch = (char*)privdata;
    if(*priv_ch < '1' || *priv_ch > '4') {
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

    x_start = 2;
    x_end = 82;
    y_start = (*priv_ch - '1') * 60 + 6;
    y_end = (*priv_ch - '1') * 60 + 31;
    font_size = 24;
    
    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        if(0 == reply->elements) {
            LOG_DETAILS("Sting: %s", reply->str);
        } else {
            for(size_t i = 0; i < reply->elements; i++) {
                LOG_DETAILS("Element %d: %s", i, reply->element[i]->str);
            }
        }
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
    LOG_DETAILS("Enter finished!");
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
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;
    unsigned char font_size;

    char* priv_ch = (char*)privdata;
    if(*priv_ch < '1' || *priv_ch > '4') {
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
    if(((newTime.tv_sec - gs_sensor[*priv_ch - '1'].tv_sec) * 1000 + 
        (newTime.tv_usec - gs_sensor[*priv_ch - '1'].tv_usec) / 1000) < SENSOR_VALUE_INTERVAL_MS) {
        LOG_DETAILS("Refresh frequency of channel %c is too high, skip", *priv_ch);
        return;
    }

    gettimeofday(&gs_sensor[*priv_ch - '1'], NULL);

    x_start = 2;
    x_end = 82;
    y_start = (*priv_ch - '1') * 60 + 34;
    y_end = (*priv_ch - '1') * 60 + 59;
    font_size = 24;
    
    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        if(0 == reply->elements) {
            LOG_DETAILS("Sting: %s", reply->str);
        } else {
            for(size_t i = 0; i < reply->elements; i++) {
                LOG_DETAILS("Element %d: %s", i, reply->element[i]->str);
            }
        }
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
void drawSWNameCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;
    unsigned char font_size;

    char* priv_ch = (char*)privdata;
    if(*priv_ch < '1' || *priv_ch > '6') {
        LOG_WARNING("Warning: invalid priv data %c!", priv_ch);
        return;
    }

    if (NULL == reply) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
        }
        return;
    }

    x_start = ((*priv_ch - '1') % 3) * 79 + 91;
    x_end = ((*priv_ch - '1') % 3) * 79 + 162;

    if(*priv_ch > '3') {
        y_start = 152;
        y_end = 185;
    } else {
        y_start = 31;
        y_end = 64;
    }
    font_size = 32;
    
    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        if(0 == reply->elements) {
            LOG_DETAILS("Sting: %s", reply->str);
        } else {
            for(size_t i = 0; i < reply->elements; i++) {
                LOG_DETAILS("Element %d: %s", i, reply->element[i]->str);
            }
        }
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
void drawSWStatusCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    const char* ch = NULL;
    unsigned int x_start, y_start, x_end, y_end;

    char* priv_ch = (char*)privdata;
    if(*priv_ch < '1' || *priv_ch > '6') {
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
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: drawSensor1Callback does not have valid value!");
        if(0 == reply->elements) {
            LOG_DETAILS("Sting: %s", reply->str);
        } else {
            for(size_t i = 0; i < reply->elements; i++) {
                LOG_DETAILS("Element %d: %s", i, reply->element[i]->str);
            }
        }
        return;
    }
    
    if(*priv_ch > '3') {
        y_start = 190;
        y_end = 207;
    } else {
        y_start = 69;
        y_end = 86;
    }

    if(0 == strcmp("0", ch)) {
        x_start = ((*priv_ch - '1') % 3) * 79 + 107;
        x_end = ((*priv_ch - '1') % 3) * 79 + 124;
        ch = "关";
    } else {
        x_start = ((*priv_ch - '1') % 3) * 79 + 123;
        x_end = ((*priv_ch - '1') % 3) * 79 + 140;
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
    
    if(NULL != privdata) {
        LOG_WARNING("Warning: setBrightnessCallback invalid priv data!");
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: setBrightnessCallback does not have valid value!");
        if(0 == reply->elements) {
            LOG_DETAILS("Sting: %s", reply->str);
        } else {
            for(size_t i = 0; i < reply->elements; i++) {
                LOG_DETAILS("Element %d: %s", i, reply->element[i]->str);
            }
        }
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

    if(NULL != privdata) {
        LOG_WARNING("Warning: exitCallback invalid priv data!");
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: exitCallback does not have valid value!");
        if(0 == reply->elements) {
            LOG_DETAILS("Sting: %s", reply->str);
        } else {
            for(size_t i = 0; i < reply->elements; i++) {
                LOG_DETAILS("Element %d: %s", i, reply->element[i]->str);
            }
        }
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
    
    for(int i = 0; i < 4; i++) {
        gettimeofday(&gs_sensor[i], NULL);
    }

    for(int i = 0; i < 4; i++) {
        LOG_DETAILS("GET %s/%s/%s%d", FLAG_KEY, serv_ip, SENSOR_NAME_TOPIC, i + 1);
        redisAsyncCommand(gs_async_context, drawSensorNameCallback, &gs_idx_name[i], "GET %s/%s/%s%d", FLAG_KEY, serv_ip, SENSOR_NAME_TOPIC, i + 1);
    }

    for(int i = 0; i < 4; i++) {
        LOG_DETAILS("GET %s/%s/%s%d", FLAG_KEY, serv_ip, SENSOR_TOPIC, i + 1);
        redisAsyncCommand(gs_async_context, drawSensorCallback, &gs_idx_name[i], "GET %s/%s/%s%d", FLAG_KEY, serv_ip, SENSOR_TOPIC, i + 1);
    }

    LOG_DETAILS("GET %s/%s/%s", FLAG_KEY, serv_ip, BRIGHTNESS_TOPIC);
    redisAsyncCommand(gs_async_context, setBrightnessCallback, NULL, "GET %s/%s/%s", FLAG_KEY, serv_ip, BRIGHTNESS_TOPIC);
    
    for(int i = 0; i < 6; i++) {
        LOG_DETAILS("SUBSCRIBE %s/%s/%s%d", FLAG_KEY, serv_ip, SW_NAME_TOPIC, i + 1);
        redisAsyncCommand(gs_async_context, drawSWNameCallback, &gs_idx_name[i], "SUBSCRIBE %s/%s/%s%d", FLAG_KEY, serv_ip, SW_NAME_TOPIC, i + 1);
    }

    for(int i = 0; i < 6; i++) {
        LOG_DETAILS("SUBSCRIBE %s/%s/%s%d", FLAG_KEY, serv_ip, SW_TOPIC, i + 1);
        redisAsyncCommand(gs_async_context, drawSWStatusCallback, &gs_idx_name[i], "SUBSCRIBE %s/%s/%s%d", FLAG_KEY, serv_ip, SW_TOPIC, i + 1);
    }
    
    LOG_DETAILS("SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, LOG_LEVEL_FLAG_VALUE);
    redisAsyncCommand(gs_async_context, setLogLevelCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, LOG_LEVEL_FLAG_VALUE);

    for(int i = 0; i < 4; i++) {
        LOG_DETAILS("SUBSCRIBE %s/%s/%s%d", FLAG_KEY, serv_ip, SENSOR_NAME_TOPIC, i + 1);
        redisAsyncCommand(gs_async_context, drawSensorNameCallback, &gs_idx_name[i], "SUBSCRIBE %s/%s/%s%d", FLAG_KEY, serv_ip, SENSOR_NAME_TOPIC, i + 1);
    }

    for(int i = 0; i < 4; i++) {
        LOG_DETAILS("SUBSCRIBE %s/%s/%s%d", FLAG_KEY, serv_ip, SENSOR_TOPIC, i + 1);
        redisAsyncCommand(gs_async_context, drawSensorCallback, &gs_idx_name[i], "SUBSCRIBE %s/%s/%s%d", FLAG_KEY, serv_ip, SENSOR_TOPIC, i + 1);
    }

    LOG_DETAILS("SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, BRIGHTNESS_TOPIC);
    redisAsyncCommand(gs_async_context, setBrightnessCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, BRIGHTNESS_TOPIC);
    
    for(int i = 0; i < 6; i++) {
        LOG_DETAILS("SUBSCRIBE %s/%s/%s%d", FLAG_KEY, serv_ip, SW_NAME_TOPIC, i + 1);
        redisAsyncCommand(gs_async_context, drawSWNameCallback, &gs_idx_name[i], "SUBSCRIBE %s/%s/%s%d", FLAG_KEY, serv_ip, SW_NAME_TOPIC, i + 1);
    }

    for(int i = 0; i < 6; i++) {
        LOG_DETAILS("SUBSCRIBE %s/%s/%s%d", FLAG_KEY, serv_ip, SW_TOPIC, i + 1);
        redisAsyncCommand(gs_async_context, drawSWStatusCallback, &gs_idx_name[i], "SUBSCRIBE %s/%s/%s%d", FLAG_KEY, serv_ip, SW_TOPIC, i + 1);
    }
    
    LOG_DETAILS("SUBSCRIBE %s/%s", FLAG_KEY, serv_ip);
    redisAsyncCommand(gs_async_context, exitCallback, NULL, "SUBSCRIBE %s/%s", FLAG_KEY, serv_ip);
    LOG_DETAILS("SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, LOG_LEVEL_FLAG_VALUE);
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
    if(0 > draw_line(242, 0, 242, 239, COLOR_WHITE)) {
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
