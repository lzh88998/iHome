#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <hiredis.h>
#include <async.h>
#include <adapters/libevent.h>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <fcntl.h>

#include <iconv.h>

#include <math.h>

#define SERV_IP         "192.168.100.100"
#define SERV_PORT       5000

#define REDIS_IP        "127.0.0.1"
#define REDIS_PORT      6379

#define COLOR_RED       0xF800
#define COLOR_GREEN     0x07E0
#define COLOR_BLUE      0x001F
#define COLOR_WHITE     0xFFFF
#define COLOR_BLACK     0x0000

#define SND_RCV_OK      0;

#define EXIT_FLAG_VALUE         "exit"

static int gs_socket = -1;

static redisAsyncContext *gs_async_context = NULL;

static char* brightness_name = NULL;

static char* sw1_name = NULL;
static char* sw1_topic = NULL;
static int sw1_state = 0;

static char* sw2_name = NULL;
static char* sw2_topic = NULL;
static int sw2_state = 0;

static char* sw3_name = NULL;
static char* sw3_topic = NULL;
static int sw3_state = 0;

static char* sw4_name = NULL;
static char* sw4_topic = NULL;
static int sw4_state = 0;

static char* sw5_name = NULL;
static char* sw5_topic = NULL;
static int sw5_state = 0;

static char* sw6_name = NULL;
static char* sw6_topic = NULL;
static int sw6_state = 0;

static char* temp_1_name = NULL;
static unsigned char temp_1 = 0;
static char* temp_2_name = NULL;
static unsigned char temp_2 = 0;
static char* light_1_name = NULL;
static unsigned char lignth_1 = 0;
static char* light_2_name = NULL;
static unsigned char lignth_2 = 0;

static char input_buffer[1024];
static char output_buffer[1024];

static iconv_t conv = NULL;

static int gs_exit = 0;
// static redisReply* gs_reply = NULL;

float convert_temp(unsigned char v) {
    return 298.15 * 3950 / (log((float)v/(255-v)) * 298.15 + 3950) - 273.15;
}

int convert_encoding() {
    printf("Start convert\n");
    if(NULL == conv) {
        conv = iconv_open ("gb2312", "utf-8");
    }
    

    if(0 >= conv)
        return -1;
    
    char* ci = input_buffer;
    char* co = output_buffer;
    size_t in_size = strlen(ci);
    size_t out_size = 1024;
    
    printf("Input: %s\n", ci);
    
    size_t r = iconv (conv, &ci, &in_size, &co, &out_size);
    
    printf("iConv ret: %d\n", r);
    printf("iConv out_size: %d\n", out_size);
    return 1024 - out_size + 1;
}

int draw_string(unsigned int x_start, unsigned int y_start, unsigned int x_end, unsigned int y_end, unsigned int color, unsigned char font_size, const char* fmt, ...) {
    int ret = SND_RCV_OK;
    int converted_cnt = 0;
    va_list ap;
    va_start(ap, fmt);
    printf("Print\n");
    ret = vsprintf(input_buffer, fmt, ap);
    va_end(ap);
    
    printf("Convert %s\n", input_buffer);
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
    printf("send %d bytes: %s\n", converted_cnt, input_buffer+12);
    
    ret = send(gs_socket, input_buffer, converted_cnt + 12, 0);
    if(0 > ret) {
        return ret;
    }
    
    printf("Send result: %d\n", ret);

    ret = recv(gs_socket, input_buffer, 1, 0);
    printf("Received: %d 0x%x\n", ret, input_buffer[0]);
    
    if(0 > ret) {
        printf("Errorno: %d\n", errno);
    }
    
    return ret;
}

int draw_rectangle(unsigned int x_start, unsigned int y_start, unsigned int x_end, unsigned int y_end, unsigned int color) {
    int ret = SND_RCV_OK;
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
    
    printf("Draw Rectangle send!\n");
    ret = send(gs_socket, bytes, 11, 0);
    if(0 > ret) {
        return ret;
    }
    
    ret = recv(gs_socket, bytes, 1, 0);
    printf("Draw Rectangle received %d 0x%x!\n", ret, bytes[0]);
    
    return ret;
}

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
    
    ret = send(gs_socket, bytes, 11, 0);
    if(0 > ret) {
        return ret;
    }
    
    ret = recv(gs_socket, bytes, 1, 0);
    
    return ret;
}

int set_brightness(unsigned char v) {
    char bytes[2];
    int ret = SND_RCV_OK;
    
    bytes[0] = 0x65;
    bytes[1] = v;

    ret = send(gs_socket, bytes, 2, 0);
    if(0 > ret) {
        return ret;
    }
    
    ret = recv(gs_socket, bytes, 1, 0);
    
    return ret;
}

int draw_info(char* topic, char* v) {
    char* fmt = NULL;
    float f = 0;
    int ret = 0;
    unsigned int x_start, y_start, x_end, y_end;
    unsigned int old_x_start, old_y_start, old_x_end, old_y_end;
    unsigned char font_size = 16;

    printf("Draw Info\n");
    printf("Draw info: %s %s\n", topic, v);
    if(NULL != v) {
        if(0 == strcmp(topic, temp_1_name)) {
            fmt = "%.1f\xE2\x84\x83";
            x_start = 2;
            y_start = 34;
            x_end = 82;
            y_end = 59;
            old_x_start = 2;
            old_y_start = 30;
            old_x_end = 82;
            old_y_end = 59;
            font_size = 24;
            f = convert_temp(atoi(v));
        } else if(0 == strcmp(topic, temp_2_name)) {
            fmt = "%.1f ℃";
            x_start = 2;
            y_start = 94;
            x_end = 82;
            y_end = 119;
            old_x_start = 2;
            old_y_start = 90;
            old_x_end = 82;
            old_y_end = 119;
            font_size = 24;
            f = convert_temp(atoi(v));
        } else if(0 == strcmp(topic, light_1_name)) {
            fmt = "%.0f";
            x_start = 2;
            y_start = 154;
            x_end = 82;
            y_end = 179;
            old_x_start = 2;
            old_y_start = 150;
            old_x_end = 82;
            old_y_end = 179;
            font_size = 24;
            f = atoi(v);
        } else if(0 == strcmp(topic, light_2_name)) {
            fmt = "%.0f";
            x_start = 2;
            y_start = 214;
            x_end = 82;
            y_end = 239;
            old_x_start = 2;
            old_y_start = 210;
            old_x_end = 82;
            old_y_end = 239;
            font_size = 24;
            f = atoi(v);
        } else if(0 == strcmp(topic, sw1_topic)) {
            if(0 == strcmp("0", v)) {
                fmt = "关";
                x_start = 107;
                y_start = 69;
                x_end = 124;
                y_end = 86;
                old_x_start = 123;
                old_y_start = 69;
                old_x_end = 140;
                old_y_end = 86;
            } else {
                fmt = "开";
                x_start = 123;
                y_start = 69;
                x_end = 140;
                y_end = 86;
                old_x_start = 107;
                old_y_start = 69;
                old_x_end = 124;
                old_y_end = 86;
            }
            font_size = 16;
        } else if(0 == strcmp(topic, sw2_topic)) {
            if(0 == strcmp("0", v)) {
                fmt = "关";
                x_start = 187;
                y_start = 69;
                x_end = 204;
                y_end = 86;
                old_x_start = 203;
                old_y_start = 69;
                old_x_end = 220;
                old_y_end = 86;
            } else {
                fmt = "开";
                x_start = 203;
                y_start = 69;
                x_end = 220;
                y_end = 86;
                old_x_start = 187;
                old_y_start = 69;
                old_x_end = 204;
                old_y_end = 86;
            }
            font_size = 16;
        } else if(0 == strcmp(topic, sw3_topic)) {
            if(0 == strcmp("0", v)) {
                fmt = "关";
                x_start = 265;
                y_start = 69;
                x_end = 282;
                y_end = 86;
                old_x_start = 281;
                old_y_start = 69;
                old_x_end = 298;
                old_y_end = 86;
            } else {
                fmt = "开";
                x_start = 281;
                y_start = 69;
                x_end = 298;
                y_end = 86;
                old_x_start = 265;
                old_y_start = 69;
                old_x_end = 282;
                old_y_end = 86;
            }
            font_size = 16;
        } else if(0 == strcmp(topic, sw4_topic)) {
            if(0 == strcmp("0", v)) {
                fmt = "关";
                x_start = 107;
                y_start = 190;
                x_end = 124;
                y_end = 207;
                old_x_start = 123;
                old_y_start = 190;
                old_x_end = 140;
                old_y_end = 207;
            } else {
                fmt = "开";
                x_start = 123;
                y_start = 190;
                x_end = 140;
                y_end = 207;
                old_x_start = 107;
                old_y_start = 190;
                old_x_end = 124;
                old_y_end = 207;
            }
            font_size = 16;
        } else if(0 == strcmp(topic, sw5_topic)) {
            if(0 == strcmp("0", v)) {
                fmt = "关";
                x_start = 187;
                y_start = 190;
                x_end = 204;
                y_end = 207;
                old_x_start = 203;
                old_y_start = 190;
                old_x_end = 220;
                old_y_end = 207;
            } else {
                fmt = "开";
                x_start = 203;
                y_start = 190;
                x_end = 220;
                y_end = 207;
                old_x_start = 187;
                old_y_start = 190;
                old_x_end = 204;
                old_y_end = 207;
            }
            font_size = 16;
        } else if(0 == strcmp(topic, sw6_topic)) {
            if(0 == strcmp("0", v)) {
                fmt = "关";
                x_start = 265;
                y_start = 190;
                x_end = 282;
                y_end = 207;
                old_x_start = 281;
                old_y_start = 190;
                old_x_end = 298;
                old_y_end = 207;
            } else {
                fmt = "开";
                x_start = 281;
                y_start = 190;
                x_end = 298;
                y_end = 207;
                old_x_start = 265;
                old_y_start = 190;
                old_x_end = 282;
                old_y_end = 207;
            }
            font_size = 16;
        } else if(0 == strcmp(topic, sw6_topic)) {
            if(atoi(v) <= 255) {
                printf("Set brightness to %d!\n", atoi(v));
                ret = set_brightness(atoi(v) & 0xFF);
                return ret;
            }
        }
    }
    
    if(NULL != fmt) {
        printf("Draw info rectangle\n");
        ret = draw_rectangle(old_x_start, old_y_start, old_x_end, old_y_end, COLOR_BLUE);
        if(0 > ret) {
            return ret;
        } 
        
        printf("Draw info string %s\n", fmt);
        ret = draw_string(x_start, y_start, x_end, y_end, COLOR_WHITE, font_size, fmt, f);
    }
    
    return ret;
}

void getCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    
    if (NULL == reply) {
        if (c->errstr) {
            printf("errstr: %s\n", c->errstr);
        }
        return;
    }
    
    printf("get type: %d\n", reply->type);
    printf("get elements: %d\n", reply->elements);
    printf("get param: %s\n", (long)privdata);
    
    if(0 > draw_info((char*)privdata, reply->str)) {
        redisAsyncDisconnect(c);
    }
}

void subscribeCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    if (reply == NULL) {
        if (c->errstr) {
            printf("errstr: %s\n", c->errstr);
        }
        return;
    }
    
    printf("sub reply type: %d\n", reply->type);
    printf("sub reply elements: %d\n", reply->elements);
    switch(reply->type) {
        case REDIS_REPLY_ERROR:
        case REDIS_REPLY_VERB:
        case REDIS_REPLY_STRING:
        case REDIS_REPLY_BIGNUM:
            printf("sub argv[%s]: %s\n", (char*)privdata, reply->str);
        break;
        case REDIS_REPLY_ARRAY: 
            for(int i = 0; i < reply->elements; i++) {
                printf("sub Array element %d: %s\n", i, reply->element[i]->str);
            }
            
            if(3 == reply->elements) { // subscribe element 0 is "message", element 1 is key element 2 is value string
                if(NULL != reply->element[0]->str && 0 == strcmp("message", reply->element[0]->str)) {
                    printf("Processing sub message!\n");
                    if(NULL != reply->element[1] && NULL != reply->element[2]) {
                        unsigned char status;
                        char *idx_start = NULL;
                        char *ch = reply->element[1]->str;
                        printf("Seek for last '/'!\n");
                        while(*ch) {
                            if('/' == *ch) {
                                idx_start = ch;
                            }
                            ch++;
                        }
                        
                        printf("Find finished %c!\n", *idx_start);
                        ++idx_start; // move to next pos;
                        if(0 == strcmp(EXIT_FLAG_VALUE, idx_start)) { // exit
                            printf("Exit flag found!\n");
                            redisAsyncDisconnect(c);
                            gs_exit = 1;
                        } else {
                            printf("Start drawing info!\n");
                            if(0 > draw_info((char*)privdata, reply->element[2]->str)) {
                                printf("Error drawing info!\n");
                                redisAsyncDisconnect(c);
                            }
                        }
                    }
                }
            }
        break;
        case REDIS_REPLY_DOUBLE:
            printf("sub Double %lf\n", reply->dval);
        break;
        case REDIS_REPLY_INTEGER:
            printf("sub Integer %ld\n", reply->integer);
        break;
        }

    printf("subscribe finished!\n");
}

void connectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        printf("Error: %s\n", c->errstr);
        return;
    }
    printf("Connected...\n");
}

void disconnectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        printf("Error disconnect: %s\n", c->errstr);
        return;
    }
    printf("Disconnected...\n");
}

void print_usage(int argc, char **argv) {
    printf("Invalid input parameters!\n");
    printf("Usage: (<optional parameters>)\n");
    printf("%s lcd_ip lcd_port redis_ip redis_port temp1_topic temp2_topic light1_topic light2_topic <sw1_name> <sw1_topic> <sw2_name> <sw2_topic> ...... <sw6_name> <sw6_topic>\n");
    printf("E.g.:\n");
    printf("%s 192.168.100.100 5000 127.0.0.1 6379 monitor/1/temp1 monitor/1/temp2 monitor/1/light1 monitor/1/light2 Switch1 controller/1/1\n\n");
}

int main (int argc, char **argv) {
    
l_start:
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    struct event_base *base;
    struct sockaddr_in serv_addr;
    struct timeval timeout = { 0, 500000};

    fd_set myset;
    int valopt; 
    socklen_t lon; 
    
    int ret;
    unsigned char temp = 0;

    char* serv_ip;
    char* redis_ip;
    int serv_port, redis_port;
    
    if(argc < 9 || (argc%2) != 0) {
        print_usage(argc, argv);
        return -1;
    }
    
    serv_ip = argv[1];
    serv_port = atoi(argv[2]);
    redis_ip = argv[3];
    redis_port = atoi(argv[4]);
    
    brightness_name = argv[5];
    
    temp_1_name = argv[6];
    temp_2_name = argv[7];
    light_1_name = argv[8];
    light_2_name = argv[9];
    
    if(argc >= 11)
        sw1_name = argv[10];
        
    if(argc >= 12)
        sw1_topic = argv[11];

    if(argc >= 13)
        sw2_name = argv[12];
        
    if(argc >= 14)
        sw2_topic = argv[13];

    if(argc >= 15)
        sw3_name = argv[14];
        
    if(argc >= 16)
        sw3_topic = argv[15];

    if(argc >= 17)
        sw4_name = argv[16];
        
    if(argc >= 18)
        sw4_topic = argv[17];

    if(argc >= 19)
        sw5_name = argv[18];
        
    if(argc >= 20)
        sw5_topic = argv[19];

    if(argc >= 21)
        sw6_name = argv[20];
        
    if(argc >= 22)
        sw6_topic = argv[21];
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(serv_port);
    ret = inet_pton(AF_INET, serv_ip, &serv_addr.sin_addr.s_addr);
    if(-1 == ret) {
        printf("Error assigning address!\n");
        goto l_start;
    }
    
    gs_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(-1 == gs_socket) {
        printf("Error connecting to LCD screen!\n");
        goto l_start;
    }
        
    printf("Socket: %d!\n", gs_socket);
    printf("Socket Family: %d\n", serv_addr.sin_family);
    printf("Socket Port: %d\n", serv_addr.sin_port);
    
    setsockopt(gs_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(struct timeval));
    setsockopt(gs_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(struct timeval));
    
    ret = 1;
    setsockopt(gs_socket, SOL_SOCKET, SO_KEEPALIVE, &ret, sizeof(ret));
    
    // Set non-blocking 
    if( (ret = fcntl(gs_socket, F_GETFL, NULL)) < 0) { 
        printf("Error fcntl(..., F_GETFL) (%s)\n", strerror(errno)); 
        goto l_socket_cleanup;
    } 
    ret |= O_NONBLOCK; 
    if( fcntl(gs_socket, F_SETFL, ret) < 0) { 
        printf("Error fcntl(..., F_SETFL) (%s)\n", strerror(errno)); 
        goto l_socket_cleanup;
    } 
    // Trying to connect with timeout 
    ret = connect(gs_socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)); 
    if (ret < 0) { 
        if (EINPROGRESS == errno) { 
            printf("EINPROGRESS in connect() - selecting\n"); 
            do { 
                FD_ZERO(&myset); 
                FD_SET(gs_socket, &myset); 
                ret = select(gs_socket+1, NULL, &myset, NULL, &timeout); 
                if (ret < 0 && EINTR != errno) { 
                    printf("Error connecting %d - %s\n", errno, strerror(errno)); 
                    exit(0); 
                } 
                else if (ret > 0) { 
                    // Socket selected for write 
                    lon = sizeof(int); 
                    if (getsockopt(gs_socket, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &lon) < 0) { 
                        printf("Error in getsockopt() %d - %s\n", errno, strerror(errno)); 
                        goto l_socket_cleanup;
                    } 
                    // Check the value returned... 
                    if (valopt) { 
                        printf("Error in delayed connection() %d - %s\n", valopt, strerror(valopt)); 
                        goto l_socket_cleanup;
                    } 
                    break; 
                } 
                else { 
                    printf("Timeout in select() - Cancelling!\n"); 
                    goto l_socket_cleanup;
                } 
            } while (1); 
        } 
        else { 
            printf("Error connecting %d - %s\n", errno, strerror(errno)); 
            goto l_socket_cleanup;
        } 
    } 

    // Set to blocking mode again... 
    if( (ret = fcntl(gs_socket, F_GETFL, NULL)) < 0) { 
        printf("Error fcntl(..., F_GETFL) (%s)\n", strerror(errno)); 
        goto l_socket_cleanup;
    } 
    ret &= (~O_NONBLOCK); 
    if( fcntl(gs_socket, F_SETFL, ret) < 0) { 
        printf("Error fcntl(..., F_SETFL) (%s)\n", strerror(errno)); 
        goto l_socket_cleanup;
    } 

/*
    ret = recv(gs_socket, &temp, 1, 0);
    if(-1 == ret) {
        printf("Error receiving initial data! %d\n", ret);
        goto l_socket_cleanup;
    }
    printf("Connected to controller, remote socket: %d\n", temp);
*/    
    
    draw_rectangle(0, 0, 319, 239, COLOR_BLUE);
    draw_line(0, 60, 84, 60, COLOR_WHITE);
    draw_line(0, 120, 319, 120, COLOR_WHITE);
    draw_line(0, 180, 84, 180, COLOR_WHITE);
    draw_line(84, 0, 84, 239, COLOR_WHITE);
    draw_line(163, 0, 163, 239, COLOR_WHITE);
    draw_line(241, 0, 241, 239, COLOR_WHITE);

    draw_string(2, 6, 82, 59, COLOR_WHITE, 24, "温度１");
    draw_string(2, 66, 82, 119, COLOR_WHITE, 24, "温度２");
    draw_string(2, 126, 82, 179, COLOR_WHITE, 24, "光照１");
    draw_string(2, 186, 82, 239, COLOR_WHITE, 24, "光照２");

    if(NULL != sw1_name) {
        printf("SW1 name: %s\n", sw1_name);
        draw_string(91, 31, 163, 64, COLOR_WHITE, 32, sw1_name);
    }
    
    if(NULL != sw2_name) {
        printf("SW2 name: %s\n", sw2_name);
        draw_string(171, 31, 241, 64, COLOR_WHITE, 32, sw2_name);
    }

    if(NULL != sw3_name) {
        printf("SW3 name: %s\n", sw3_name);
        draw_string(249, 31, 319, 64, COLOR_WHITE, 32, sw3_name);
    }

    if(NULL != sw4_name) {
        printf("SW4 name: %s\n", sw4_name);
        draw_string(91, 152, 163, 185, COLOR_WHITE, 32, sw4_name);
    }

    if(NULL != sw5_name) {
        printf("SW5 name: %s\n", sw5_name);
        draw_string(171, 152, 241, 185, COLOR_WHITE, 32, sw5_name);
    }

    if(NULL != sw6_name) {
        printf("SW6 name: %s\n", sw6_name);
        draw_string(249, 152, 163, 185, COLOR_WHITE, 32, sw6_name);
    }

    base = event_base_new();
    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, redis_ip, redis_port);
    struct timeval tv = {0};
    tv.tv_sec = 1;
    options.connect_timeout = &tv;

    gs_async_context = redisAsyncConnectWithOptions(&options);
    if (gs_async_context->err) {
        printf("Error async connect: %s\n", gs_async_context->errstr);
        goto l_free_async_redis;
    }

    if(REDIS_OK != redisLibeventAttach(gs_async_context,base)) {
        printf("Error: error redis libevent attach!\n");
        goto l_free_async_redis;
    }

    redisAsyncSetConnectCallback(gs_async_context,connectCallback);
    redisAsyncSetDisconnectCallback(gs_async_context,disconnectCallback);

    redisAsyncCommand(gs_async_context, getCallback, temp_1_name, "GET %s", temp_1_name);
    redisAsyncCommand(gs_async_context, getCallback, temp_2_name, "GET %s", temp_2_name);
    redisAsyncCommand(gs_async_context, getCallback, light_1_name, "GET %s", light_1_name);
    redisAsyncCommand(gs_async_context, getCallback, light_2_name, "GET %s", light_2_name);
    redisAsyncCommand(gs_async_context, getCallback, brightness_name, "GET %s", brightness_name);
    
    if(sw1_topic)
        redisAsyncCommand(gs_async_context, getCallback, sw1_topic, "GET %s", sw1_topic);

    if(sw2_topic)
        redisAsyncCommand(gs_async_context, getCallback, sw2_topic, "GET %s", sw2_topic);

    if(sw3_topic)
        redisAsyncCommand(gs_async_context, getCallback, sw3_topic, "GET %s", sw3_topic);

    if(sw4_topic)
        redisAsyncCommand(gs_async_context, getCallback, sw4_topic, "GET %s", sw4_topic);

    if(sw5_topic)
        redisAsyncCommand(gs_async_context, getCallback, sw5_topic, "GET %s", sw5_topic);

    if(sw6_topic)
        redisAsyncCommand(gs_async_context, getCallback, sw6_topic, "GET %s", sw6_topic);

    redisAsyncCommand(gs_async_context, subscribeCallback, temp_1_name, "SUBSCRIBE %s", temp_1_name);
    redisAsyncCommand(gs_async_context, subscribeCallback, temp_2_name, "SUBSCRIBE %s", temp_2_name);
    redisAsyncCommand(gs_async_context, subscribeCallback, light_1_name, "SUBSCRIBE %s", light_1_name);
    redisAsyncCommand(gs_async_context, subscribeCallback, light_2_name, "SUBSCRIBE %s", light_2_name);
    redisAsyncCommand(gs_async_context, subscribeCallback, brightness_name, "SUBSCRIBE %s", brightness_name);

    if(sw1_topic)
        redisAsyncCommand(gs_async_context, subscribeCallback, sw1_topic, "SUBSCRIBE %s", sw1_topic);

    if(sw2_topic)
        redisAsyncCommand(gs_async_context, subscribeCallback, sw2_topic, "SUBSCRIBE %s", sw2_topic);

    if(sw3_topic)
        redisAsyncCommand(gs_async_context, subscribeCallback, sw3_topic, "SUBSCRIBE %s", sw3_topic);

    if(sw4_topic)
        redisAsyncCommand(gs_async_context, subscribeCallback, sw4_topic, "SUBSCRIBE %s", sw4_topic);

    if(sw5_topic)
        redisAsyncCommand(gs_async_context, subscribeCallback, sw5_topic, "SUBSCRIBE %s", sw5_topic);

    if(sw6_topic)
        redisAsyncCommand(gs_async_context, subscribeCallback, sw6_topic, "SUBSCRIBE %s", sw6_topic);

    event_base_dispatch(base);
        
l_free_async_redis:
    redisAsyncFree(gs_async_context);
    event_base_free(base);
    printf("exit!\n");
    
l_socket_cleanup:
    close(gs_socket);
    gs_socket = -1;
    
    goto l_start;
    
    return 0;
}
