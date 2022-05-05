#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <hiredis.h>
#include <async.h>
#include <adapters/libevent.h>

#define REDIS_IP                "127.0.0.1"
#define REDIS_PORT              6379

#define REDIS_MESSAGE_TYPE      "message"

#define EXIT_FLAG_KEY           "godown_keeper"
#define EXIT_FLAG_VALUE         "exit"

#define SND_RCV_OK              0

static redisContext *gs_sync_context = NULL;
static redisAsyncContext *gs_async_context = NULL;

static int gs_exit = 0;

static char* enable_topic = NULL;

static char* touch_topic = NULL;
static char* sw_topic = NULL;

static int x_start = -1;
static int x_end = -1;
static int y_start = -1;
static int y_end = -1;

void subscribeCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    if (reply == NULL) {
        if (c->errstr) {
            printf("errstr: %s\n", c->errstr);
        }
        return;
    }
    
    printf("sub reply type: %d\n", reply->type);
    printf("sub reply element count: %d\n", reply->elements);
    switch(reply->type) {
        case REDIS_REPLY_ARRAY: 
            for(int i = 0; i < reply->elements; i++) {
                printf("sub Array element %d: %s\n", i, reply->element[i]->str);
            }
            
            if(3 == reply->elements) { // psubscribe element 0 is "message", element 1 is key pattern element 2 is key element 3 is value string
                if(NULL != reply->element[0]->str && 0 == strcmp(REDIS_MESSAGE_TYPE, reply->element[0]->str)) {
                    if(NULL != reply->element[2]) {
                        if(0 == strcmp(EXIT_FLAG_KEY, reply->element[2]->str)) {
                            if(NULL != reply->element[2] && 0 == strcmp(EXIT_FLAG_VALUE, reply->element[2]->str)) {
                                redisAsyncDisconnect(c);
                            }
                        } else if(NULL != reply->element[2]) {
                            unsigned char cmd = 0x00;
                            unsigned int x = 0;
                            unsigned int y = 0;
                            
                            char* ch = reply->element[2]->str;
                            while('\0' != *ch && '/' != *ch) {
                                if('0' <= *ch && *ch <= '9') {
                                    cmd *= 10;
                                    cmd += (*ch - '0');
                                } else {
                                    printf("Error invalid input 1 %d!\n", *ch);
                                    return;
                                }
                                ch++;
                            }
                            
                            if('\0' != *ch)
                                ch++;
                                
                            while('\0' != *ch && '/' != *ch) {
                                if('0' <= *ch && *ch <= '9') {
                                    x *= 10;
                                    x += (*ch - '0');
                                } else {
                                    printf("Error invalid input 2 %d!\n", *ch);
                                    return;
                                }
                                ch++;
                            }

                            if('\0' != *ch)
                                ch++;
                                
                            while('\0' != *ch && '/' != *ch) {
                                if('0' <= *ch && *ch <= '9') {
                                    y *= 10;
                                    y += (*ch - '0');
                                } else {
                                    printf("Error invalid input 3 %d!\n", *ch);
                                    return;
                                }
                                ch++;
                            }
                            
                            printf("Cmd: 0x%x, x: %d, y: %d\n", cmd, x, y);
                            
                            if(0xB3 != cmd) {
                                printf("Skip command 0x%x\n", cmd);
                                return;
                            }
                            
                            printf("X_Start: %d, Y_Start: %d,X_End %d, Y_End %d\n", x_start, y_start, x_end, y_end);
                            
                            if(x_start <= x && x <= x_end && y_start <= y && y <= y_end) {
                                redisReply* reply = redisCommand(gs_sync_context,"GET %s", sw_topic);
                                unsigned char result = 0;
                                if(NULL == reply) {
                                    printf("Failed to sync query redis %s\n", gs_sync_context->errstr);
                                    redisAsyncDisconnect(c);
                                }
                                printf("PING: %s\n", reply->str);
                                if(NULL != reply->str) {
                                    result = 0 == strcmp("0", reply->str);
                                }
                                
                                printf("Cur State in redis: %s\n", reply->str);
                                freeReplyObject(reply);
                                
                                reply = redisCommand(gs_sync_context, "PUBLISH %s %d", sw_topic, result);
                                if(NULL == reply) {
                                    printf("Failed to sync query redis %s\n", gs_sync_context->errstr);
                                    redisAsyncDisconnect(c);
                                }
                                printf("Set result: %s\n", reply->str);
                                freeReplyObject(reply);
                            }
                        }
                    }
                }
            }
        break;
        case REDIS_REPLY_ERROR:
        case REDIS_REPLY_VERB:
        case REDIS_REPLY_STRING:
        case REDIS_REPLY_BIGNUM:
            printf("sub argv[%s]: %s\n", (char*)privdata, reply->str);
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
        printf("Error: %s\n", c->errstr);
        return;
    }
    printf("Disconnected...\n");
}

void print_usage(int argc, char **argv) {
    printf("Invalid input parameters!\n");
    printf("Usage: (<optional parameters>)\n");
    printf("%s redis_ip redis_port enable_topic touch_topic sw_topic x_start y_start x_end y_end\n");
    printf("E.g.:\n");
    printf("%s 127.0.0.1 6379 touch/1/enabled touch/1 controller/1/1 84 0 163 119\n\n");
}

int main (int argc, char **argv) {
    
l_start:
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    struct event_base *base;
    
    int ret;

    char* redis_ip;
    int redis_port;
    
    if(10 != argc) {
        print_usage(argc, argv);
        return -1;
    }
    
    redis_ip = argv[1];
    redis_port = atoi(argv[2]);
    
    enable_topic = argv[3];
    touch_topic = argv[4];
    sw_topic = argv[5];
    
    x_start = atoi(argv[6]);
    y_start = atoi(argv[7]);
    x_end = atoi(argv[8]);
    y_end = atoi(argv[9]);
    
    if(x_start > x_end) { // swap x
        ret = x_start;
        x_start = x_end;
        x_end = ret;
    }
    
    if(y_start >= y_end) { // swap y
        ret = y_start;
        y_start = y_end;
        y_end = ret;
    }

    struct timeval timeout = { 0, 500000 }; // 1.5 seconds
    gs_sync_context = redisConnectWithTimeout(redis_ip, redis_port, timeout);
    if(NULL == gs_sync_context) {
        printf("Connection error: can't allocate redis context\n");
        goto l_exit;
    }
    
    if(gs_sync_context->err) {
        printf("Connection error: %s\n", gs_sync_context->errstr);
        goto l_free_sync_redis;
    }

    redisReply* reply = redisCommand(gs_sync_context,"PING");
    if(NULL == reply) {
        printf("Failed to sync query redis %s\n", gs_sync_context->errstr);
        goto l_free_sync_redis;
    }
    printf("PING: %s\n", reply->str);
    freeReplyObject(reply);
    
    base = event_base_new();
    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, redis_ip, redis_port);
    options.connect_timeout = &timeout;

    gs_async_context = redisAsyncConnectWithOptions(&options);
    if (gs_async_context->err) {
        printf("Error: %s\n", gs_async_context->errstr);
        goto l_free_async_redis;
    }

    if(REDIS_OK != redisLibeventAttach(gs_async_context,base)) {
        printf("Error: error redis libevent attach!\n");
        goto l_free_async_redis;
    }

    redisAsyncSetConnectCallback(gs_async_context,connectCallback);
    redisAsyncSetDisconnectCallback(gs_async_context,disconnectCallback);

    redisAsyncCommand(gs_async_context, subscribeCallback, NULL, "SUBSCRIBE %s", touch_topic);
    event_base_dispatch(base);
    
l_free_sync_redis:
    redisFree(gs_sync_context);
    
l_free_async_redis:
    redisAsyncFree(gs_async_context);
    event_base_free(base);
 
l_exit:
    if(!gs_exit) {    
        printf("Godown_keeper execution failed retry!\n");
        goto l_start;
    }

    printf("exit!\n");
    return 0;
}
