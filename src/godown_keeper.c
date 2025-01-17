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
#define EXIT_FLAG_VALUE         "exit"
#define LOG_LEVEL_FLAG_KEY      "godown_keeper/log_level"

static redisContext *gs_sync_context = NULL;
static redisAsyncContext *gs_async_context = NULL;

static int gs_exit = 0;

/*
 * After start phase, the godown_keeper will subscribe all 
 * message channels to keep the data stored in redis kv 
 * store. This will ensure that offline micro services
 * will get latest information when back online.
 * 
 * This callback function will be called when there is new
 * messages published to redis.
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
 *                          Because when the service is started, the 
 *                          service use psubscribe to subscribe chagnes
 *                          from redis, so the reply item usually has
 *                          4 elements 1 is "pmessage", second is 
 *                          subscribe pattern, third is actual channel, 
 *                          fourth will contains the value.
 * void *privdata           not used
 * 
 * Return value:
 * There is no return value
 * 
 * Note: When send/receive error occures, this service will terminate
 * itself and restart, so when send/recv error occur, it will disconnect
 * from redis and main function will restart trying to reconnect.
 * 
 */
void subscribeCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    if (reply == NULL) {
        if (c->errstr) {
            LOG_ERROR("errstr: %s", c->errstr);
            redisAsyncDisconnect(c);
        }
        return;
    }
    
    LOG_DETAILS("sub reply type: %d", reply->type);
    LOG_DETAILS("sub reply element count: %ld", reply->elements);
    switch(reply->type) {
        case REDIS_REPLY_ARRAY: 
            for(size_t i = 0; i < reply->elements; i++) {
                LOG_DETAILS("sub Array element %ld: %s", i, reply->element[i]->str);
            }
            
            if(4 == reply->elements) { // psubscribe element 0 is "pmessage", element 1 is key pattern element 2 is key element 3 is value string
                if(NULL != reply->element[0]->str && 0 == strcmp(REDIS_MESSAGE_TYPE, reply->element[0]->str)) {
                    if(NULL != reply->element[2] && NULL != reply->element[2]->str) {
                        if(0 == strcmp(EXIT_FLAG_KEY, reply->element[2]->str)) {
                            if(NULL != reply->element[3] && 0 == strcmp(EXIT_FLAG_VALUE, reply->element[3]->str)) {
                                gs_exit = 1;
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
                                LOG_ERROR("Failed to set item in redis %s", gs_sync_context->errstr);
                                redisAsyncDisconnect(c);
                            } else {
                                LOG_DETAILS("Set item in redis %s", sync_reply->str);
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
            LOG_DETAILS("sub argv[%s]: %s", (char*)privdata, reply->str);
        break;
        case REDIS_REPLY_DOUBLE:
            LOG_DETAILS("sub Double %lf", reply->dval);
        break;
        case REDIS_REPLY_INTEGER:
            LOG_DETAILS("sub Integer %lld", reply->integer);
        break;
        }

    LOG_DEBUG("subscribe finished!");
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
        LOG_ERROR("Error: %s", c->errstr);
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
    printf("%s <redis_ip> <redis_port>\n", argv[0]);
    printf("E.g.:\n");
    printf("%s 127.0.0.1 6379\n\n", argv[0]);
}

/*
 * Main entry of the service. It will first connect
 * to redis use a sync connection, and then use 
 * another async connection as main thread to
 * subscribe all channels.
 * 
 * When new messages are published to any channel
 * the subscribeCallback is called. And it will
 * first check whether this message is related
 * with godown_keeper exit or log level config
 * if neither of above items, then use the sync
 * connection to store the value to kv store of
 * redis.
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
    // 100ms
    struct timeval timeout = { 0, 100000 }; 
    struct event_base *base = NULL;

    const char* redis_ip;
    int redis_port;
    
    LOG_INFO("=================== Service start! ===================");
    LOG_INFO("Parsing parameters!");

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

    LOG_INFO("Connecting to Redis in sync mode!");
    gs_sync_context = redisConnectWithTimeout(redis_ip, redis_port, timeout);
    if(NULL == gs_sync_context) {
        LOG_ERROR("Connection error: can't allocate redis context");
        goto l_exit;
    }
    
    if(gs_sync_context->err) {
        LOG_ERROR("Connection error: %s", gs_sync_context->errstr);
        goto l_free_sync_redis;
    }

    redisReply* reply = redisCommand(gs_sync_context,"PING");
    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        goto l_free_sync_redis;
    }
    LOG_DEBUG("PING: %s", reply->str);
    freeReplyObject(reply);
    
    LOG_INFO("Connected to Redis in sync mode");
    
    LOG_INFO("Connecting to Redis in async mode!");
    base = event_base_new();
    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, redis_ip, redis_port);
    options.connect_timeout = &timeout;

    gs_async_context = redisAsyncConnectWithOptions(&options);
    if (gs_async_context->err) {
        LOG_ERROR("Error: %s", gs_async_context->errstr);
        goto l_free_async_redis;
    }

    if(REDIS_OK != redisLibeventAttach(gs_async_context,base)) {
        LOG_ERROR("Error: error redis libevent attach!");
        goto l_free_async_redis;
    }

    LOG_INFO("Connected to redis in async mode");

    redisAsyncSetConnectCallback(gs_async_context,connectCallback);
    redisAsyncSetDisconnectCallback(gs_async_context,disconnectCallback);

    redisAsyncCommand(gs_async_context, subscribeCallback, NULL, "PSUBSCRIBE *");
    
    LOG_INFO("Switch to Redis DB 1 to load status");
    LOG_DETAILS("SELECT 1");
    redisReply* tempReply = redisCommand(gs_sync_context,"SELECT 1");

    if(NULL == tempReply) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        goto l_free_async_redis;
    }

    freeReplyObject(tempReply);
    tempReply = NULL;

    LOG_INFO("Loading log_level configuration!");
    LOG_DETAILS("GET %s", LOG_LEVEL_FLAG_VALUE);
    tempReply = redisCommand(gs_sync_context,"GET %s", LOG_LEVEL_FLAG_VALUE);

    if(NULL == tempReply) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        goto l_free_async_redis;
    }
    
    if(NULL != tempReply->str) {
        if(LOG_SET_LEVEL_OK != log_set_level(tempReply->str)) {
            LOG_WARNING("Failed to set log level %s", tempReply->str);
        }
    }

    freeReplyObject(tempReply);
    tempReply = NULL;

    event_base_dispatch(base);
    
l_free_async_redis:
    redisAsyncFree(gs_async_context);
    event_base_free(base);
 
l_free_sync_redis:
    redisFree(gs_sync_context);
    
l_exit:
    if(!gs_exit) {    
        LOG_ERROR("Godown_keeper execution failed retry!");
        sleep(1);
        goto l_start;
    }

    LOG_INFO("exit!");
    return 0;
}
