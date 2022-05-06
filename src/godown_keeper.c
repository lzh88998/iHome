/*
 * Copyright lzh88998 and distributed under Apache 2.0 license
 * 
 * Godown_keeper is a micro service that save latest published
 * messages to redis kv stores. Due to sometimes the messages
 * are published while the receiver micro service might
 * not online. Save the message to redis kv will ensure when
 * the receiver micro service back online, it still can 
 * receive the message.
 * 
 */

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

#define REDIS_MESSAGE_TYPE      "pmessage"

#define EXIT_FLAG_KEY           "godown_keeper/exit"
#define EXIT_FLAG_VALUE         "1"
#define LOG_LEVEL_FLAG_KEY      "godown_keeper/log_level"

static redisContext *gs_sync_context = NULL;
static redisAsyncContext *gs_async_context = NULL;

static int gs_exit = 0;

void subscribeCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    if (reply == NULL) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s\n", c->errstr);
            redisAsyncDisconnect(c);
        }
        return;
    }
    
    LOG_DETAILS("sub reply type: %d\n", reply->type);
    LOG_DETAILS("sub reply element count: %ld\n", reply->elements);
    switch(reply->type) {
        case REDIS_REPLY_ARRAY: 
            for(size_t i = 0; i < reply->elements; i++) {
                LOG_DETAILS("sub Array element %ld: %s\n", i, reply->element[i]->str);
            }
            
            if(4 == reply->elements) { // psubscribe element 0 is "pmessage", element 1 is key pattern element 2 is key element 3 is value string
                if(NULL != reply->element[0]->str && 0 == strcmp(REDIS_MESSAGE_TYPE, reply->element[0]->str)) {
                    if(NULL != reply->element[2] && NULL != reply->element[2]->str) {
                        if(0 == strcmp(EXIT_FLAG_KEY, reply->element[2]->str)) {
                            if(NULL != reply->element[3] && 0 == strcmp(EXIT_FLAG_VALUE, reply->element[3]->str)) {
                                redisAsyncDisconnect(c);
                            }
                        } else if(0 == strcmp(LOG_LEVEL_FLAG_KEY, reply->element[2]->str)) {
                            LOG_DETAILS("Subscribe log level flag found!");
                            if(0 > log_set_level(reply->element[3]->str)) {
                                LOG_ERROR("Invalid log option: %s", reply->element[3]->str);
                            }
                        }
                        
                        if(NULL != reply->element[3] && NULL != reply->element[3]->str) {
                            redisReply* sync_reply = redisCommand(gs_sync_context,"SET %s %s", reply->element[2]->str, reply->element[3]->str);
                            if(NULL == sync_reply) {
                                LOG_ERROR("Failed to set item in redis %s\n", gs_sync_context->errstr);
                                redisAsyncDisconnect(c);
                            } else {
                                LOG_DETAILS("Set item in redis %s\n", sync_reply->str);
                                freeReplyObject(sync_reply);
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
            LOG_DETAILS("sub argv[%s]: %s\n", (char*)privdata, reply->str);
        break;
        case REDIS_REPLY_DOUBLE:
            LOG_DETAILS("sub Double %lf\n", reply->dval);
        break;
        case REDIS_REPLY_INTEGER:
            LOG_DETAILS("sub Integer %lld\n", reply->integer);
        break;
        }

    LOG_DEBUG("subscribe finished!\n");
}

void connectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        LOG_ERROR("Error: %s\n", c->errstr);
        return;
    }
    LOG_INFO("Connected...\n");
}

void disconnectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        LOG_ERROR("Error: %s\n", c->errstr);
        return;
    }
    LOG_INFO("Disconnected...\n");
}

void print_usage(int argc, char **argv) {
    if(0 >= argc) {
        return;
    }
    printf("Invalid input parameters!\n");
    printf("Usage: (<optional parameters>)\n");
    printf("%s <redis_ip> <redis_port>\n", argv[0]);
    printf("E.g.:\n");
    printf("%s 127.0.0.1 6379\n\n", argv[0]);
}

int main (int argc, char **argv) {
    
l_start:
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    // 100ms
    struct timeval timeout = { 0, 100000 }; 
    struct event_base *base = event_base_new();

    const char* redis_ip;
    int redis_port;
    
    if(argc >= 2) {
        if(0 == strcmp("?", argv[1])) {
            print_usage(argc, argv);
            return -1;
        } else {
            redis_ip = argv[1];
        }
    } else {
        redis_ip = REDIS_IP;
    }
    
    if(argc >=3) {
        redis_port = atoi(argv[2]);
    } else {
        redis_port = REDIS_PORT;
    }

    gs_sync_context = redisConnectWithTimeout(redis_ip, redis_port, timeout);
    if(NULL == gs_sync_context) {
        LOG_ERROR("Connection error: can't allocate redis context\n");
        goto l_exit;
    }
    
    if(gs_sync_context->err) {
        LOG_ERROR("Connection error: %s\n", gs_sync_context->errstr);
        goto l_free_sync_redis;
    }

    redisReply* reply = redisCommand(gs_sync_context,"PING");
    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s\n", gs_sync_context->errstr);
        goto l_free_sync_redis;
    }
    LOG_DEBUG("PING: %s\n", reply->str);
    freeReplyObject(reply);
    
    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, redis_ip, redis_port);
    options.connect_timeout = &timeout;

    gs_async_context = redisAsyncConnectWithOptions(&options);
    if (gs_async_context->err) {
        LOG_ERROR("Error: %s\n", gs_async_context->errstr);
        goto l_free_async_redis;
    }

    if(REDIS_OK != redisLibeventAttach(gs_async_context,base)) {
        LOG_ERROR("Error: error redis libevent attach!\n");
        goto l_free_async_redis;
    }

    redisAsyncSetConnectCallback(gs_async_context,connectCallback);
    redisAsyncSetDisconnectCallback(gs_async_context,disconnectCallback);

    redisAsyncCommand(gs_async_context, subscribeCallback, argv[1], "PSUBSCRIBE *");
    event_base_dispatch(base);
    
l_free_sync_redis:
    redisFree(gs_sync_context);
    
l_free_async_redis:
    redisAsyncFree(gs_async_context);
    event_base_free(base);
 
l_exit:
    if(!gs_exit) {    
        LOG_ERROR("Godown_keeper execution failed retry!\n");
        goto l_start;
    }

    LOG_INFO("exit!\n");
    return 0;
}
