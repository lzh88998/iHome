#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <hiredis.h>
#include <async.h>
#include <adapters/libevent.h>

#include "log.h"

#define REDIS_IP                "127.0.0.1"
#define REDIS_PORT              6379

#define TOUCH_TOPIC             "touch/*"

#define REDIS_MESSAGE_TYPE      "pmessage"

#define FLAG_KEY               "touch_processor"
#define LCD_FLAG_KEY            "lcd"
#define EXIT_FLAG_VALUE         "exit"

#define SW_TOPIC              "sw"

#define SND_RCV_OK              0

static redisContext *gs_sync_context = NULL;
static redisAsyncContext *gs_async_context = NULL;

static int gs_exit = 0;

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
            redisAsyncDisconnect(c);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: exitCallback does not have valid value!");
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
            redisAsyncDisconnect(c);
        }
        return;
    }

    if(3 == reply->elements && reply->element[2] && reply->element[2]->str) {
        ch = reply->element[2]->str;
    } else if(NULL != reply->str) {
        ch = reply->str;
    } else {
        LOG_WARNING("Warning: setLogLevelCallback does not have valid value!");
        return;
    }
    
    if(0 > log_set_level(ch)) {
        LOG_WARNING("Warning: invalid log level %s!", ch);
    }
}

/*
 * subscribeCallback is used to process
 * click event
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 * void *privdata           Not used
 * 
 * Return value:
 * There is no return value
 */
void subscribeCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    if (reply == NULL) {
        if (c->errstr) {
            LOG_ERROR("errstr: %n", c->errstr);
            redisAsyncDisconnect(c);
        }
        return;
    }
    
    LOG_DETAILS("sub reply type: %d", reply->type);
    LOG_DETAILS("sub reply element count: %d", reply->elements);
    
    if(REDIS_REPLY_ARRAY == reply->type && 4 == reply->elements) {
        if(NULL != reply->element[2] && NULL != reply->element[2]->str) {
            char* addr = strchr(reply->element[2]->str, '/');
            if(NULL != addr) {
                addr++;
                if(NULL != reply->element[3] && NULL != reply->element[3]->str) {
                    char* ch = reply->element[3]->str;
                    unsigned char cmd = atoi(ch) & 0xFF;
                    if(0xB3 == cmd) {
                        unsigned int x = 0, y = 0;
                        ch = strchr(ch, '/');
                        if(NULL != ch) {
                            ch++;
                            x = atoi(ch);
                            ch = strchr(ch, '/');
                            if(NULL != ch) {
                                ch++;
                                y = atoi(ch);
                                if(84 <=x && x < 163) {
                                    cmd = 1;
                                } else if (163 <=x && x < 241 ) {
                                    cmd = 2;
                                } else if (241 <=x && x < 320) {
                                    cmd = 3;
                                }
                                
                                if(120 < y) {
                                    cmd += 3;
                                }
                                
                                redisReply* reply2 = redisCommand(gs_sync_context,"GET %s/%s/%s%d", LCD_FLAG_KEY, addr, SW_TOPIC, cmd);
                                unsigned char result = 0;
                                if(NULL == reply2) {
                                    printf("Failed to sync query redis %s\n", gs_sync_context->errstr);
                                    redisAsyncDisconnect(c);
                                }
                                printf("Get reply: %s\n", reply2->str);
                                if(NULL != reply2->str) {
                                    result = 0 == strcmp("0", reply2->str);

                                    freeReplyObject(reply2);
                                    reply2 = redisCommand(gs_sync_context, "PUBLISH %s/%s/%s%d %d", LCD_FLAG_KEY, addr, SW_TOPIC, cmd, result);
                                    if(NULL == reply2) {
                                        printf("Failed to sync query redis %s\n", gs_sync_context->errstr);
                                        redisAsyncDisconnect(c);
                                    }
                                    printf("Set result: %s\n", reply2->str);
                                }
                                freeReplyObject(reply2);
                            }
                        }
                    }
                }
            }
        }
    } else {
        LOG_ERROR("Warning: invalid data type %d %d received!", reply->type, reply->elements);
        for(size_t i = 0; i < reply->elements; i++) {
            LOG_DEBUG("Item %d: %s", i, reply->element[i]->str);
        }
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
    if(0 > argc) {
        return;
    }
    
    printf("Invalid input parameters!\n");
    printf("Usage: (<optional parameters>)\n");
    printf("%s <log_level> <redis_ip> <redis_port>\n", argv[0]);
    printf("E.g.:\n");
    printf("%s\n\n", argv[0]);
    printf("%s debug\n\n", argv[0]);
    printf("%s debug 127.0.0.1 6379\n\n", argv[0]);
}

int main (int argc, char **argv) {
    
l_start:
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    printf("================= Start =================");
    const char* redis_ip = REDIS_IP;
    int redis_port = REDIS_PORT;
    struct event_base *base = event_base_new();
    
    printf("Parsing parameters");
    switch(argc) {
        case 1:
            break;
        case 2:
            if(0 > log_set_level(argv[1])) {
                print_usage(argc, argv);
                return -2;
            }
            break;
        case 3:
            redis_ip = argv[1];
            redis_port = atoi(argv[2]);
            break;
        case 4:
            if(0 > log_set_level(argv[3])) {
                print_usage(argc, argv);
                return -3;
            }
            redis_ip = REDIS_IP;
            redis_port = REDIS_PORT;
            break;
        default:
            print_usage(argc, argv);
            return -1;
    }
    
    LOG_DETAILS("Connecting to redis");
    struct timeval timeout = { 0, 500000 }; // 0.5 seconds
    gs_sync_context = redisConnectWithTimeout(redis_ip, redis_port, timeout);
    if(NULL == gs_sync_context) {
        printf("Connection error: can't allocate redis context\n");
        goto l_exit;
    }
    
    printf("async");
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

    redisAsyncCommand(gs_async_context, subscribeCallback, NULL, "PSUBSCRIBE %s", TOUCH_TOPIC);
    redisAsyncCommand(gs_async_context, exitCallback, NULL, "SUBSCRIBE %s", FLAG_KEY);
    redisAsyncCommand(gs_async_context, setLogLevelCallback, NULL, "SUBSCRIBE %s/%s", FLAG_KEY, LOG_LEVEL_FLAG_VALUE);
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
