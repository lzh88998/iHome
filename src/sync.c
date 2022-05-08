/*
 * Copyright lzh88998 and distributed under Apache 2.0 license
 * 
 * touch is a micro service that receive the touch information
 * from touch panel and perform cache and translate the received
 * information then publish to redis channel.
 * 
 * Due to the touch pad can return data points in a very fast
 * manner (several data points per second) and may cause some
 * trouble for controller, so touch will cache and aggregate
 * the message for a period of MSG_INTERVAL_MS.
 * 
 * The translation rule is as following:
 * If last message have been sent out for at least 
 * MSG_INTERVAL_MS, then current message will be sent out
 * immediately. Otherwise, modify the behavior according to
 * previous and current behavior. for example if last one
 * is click and current is move, then the whole message 
 * will be translate to click and update to latest position.
 * if last message is move and current is click up, then
 * update the behavior as click up. if last message is click
 * up and current is click down, then update to move.
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <hiredis.h>
#include <async.h>
#include <adapters/libevent.h>

#include <errno.h>

#include "log.h"

#define REDIS_IP                "127.0.0.1"
#define REDIS_PORT              6379

#define FLAG_KEY                "sync"
#define EXIT_FLAG_VALUE         "exit"

#define MSG_INTERVAL_MS         300

static redisContext *gs_sync_context = NULL;
static redisAsyncContext *gs_async_context = NULL;

static int gs_exit = 0;

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
    printf("%s controller_ip controller_port <log level> <redis_ip> <redis_port>\n", argv[0]);
    printf("E.g.:\n");
    printf("%s 192.168.100.100 5000\n\n", argv[0]);
    printf("%s 192.168.100.100 5000 debug\n\n", argv[0]);
    printf("%s 192.168.100.100 5000 127.0.0.1 6379\n\n", argv[0]);
    printf("%s 192.168.100.100 5000 debug 127.0.0.1 6379\n\n", argv[0]);
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
    if(NULL != privdata) {
        LOG_ERROR("Invalid priv data! %s", (char*)privdata);
        redisAsyncDisconnect(c);
        return;
    }
    
    if (reply == NULL) {
        if (c->errstr) {
            LOG_ERROR("errstr: %n", c->errstr);
            redisAsyncDisconnect(c);
        }
        return;
    }
    
    LOG_DETAILS("sub reply type: %d", reply->type);
    LOG_DETAILS("sub reply element count: %d", reply->elements);

    if(3 == reply->elements && reply->element[1] && reply->element[1]->str && reply->element[2] && reply->element[2]->str) { 
        // 1 is message, 2 is key, 3 is value
        LOG_DETAILS("HGET %s", FLAG_KEY);
        redisReply *sync_reply = redisCommand(gs_sync_context,"HGET %s %s", FLAG_KEY, reply->element[1]->str);
        if(NULL == sync_reply) {
            LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
            redisAsyncDisconnect(c);
            return;
        }

        LOG_DETAILS("PUBLISH %s %s", FLAG_KEY, sync_reply->str, reply->element[2]->str);
        redisReply *sync_reply2 = redisCommand(gs_sync_context,"PUBLISH %s %s", sync_reply->str, reply->element[2]->str);
        if(NULL == sync_reply2) {
            LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
            freeReplyObject(sync_reply);   
            redisAsyncDisconnect(c);
            return;
        }

        freeReplyObject(sync_reply2);   
        freeReplyObject(sync_reply);   
    }

    LOG_DEBUG("subscribe finished!\n");
}

/*
 * Main entry of the service. It will first connect
 * to touch controller, and then connect to redis
 * through a sync connection. When received message
 * from touch controller, the data will be sent to
 * redis through the sync connectoin
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

    const char* redis_ip = REDIS_IP;
    int redis_port = REDIS_PORT;
    
    struct timeval timeout = { 0, 100000 }; 
    struct event_base *base = event_base_new();

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

    redisAsyncSetConnectCallback(gs_async_context,connectCallback);
    redisAsyncSetDisconnectCallback(gs_async_context,disconnectCallback);

    LOG_DETAILS("HGETALL %s", FLAG_KEY);
    reply = redisCommand(gs_sync_context,"HGETALL %s", FLAG_KEY);
    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        goto l_free_sync_redis;
    }
    
    LOG_DETAILS("%d", reply->type);
    LOG_DETAILS("%s", reply->str);
    
    for(size_t i = 0; i < reply->elements; i++) {
        if(NULL != reply->element[i] && NULL != reply->element[i]->str) {
            LOG_DETAILS("Item %ld: %s", i, reply->element[i]->str);
            if(0 == (i % 2)) {
                // key
                redisAsyncCommand(gs_async_context, subscribeCallback, NULL, "SUBSCRIBE %s", reply->element[i]->str);
            } else {
                // value
            }
        }
    }
    freeReplyObject(reply);   

    event_base_dispatch(base);
    
l_free_sync_redis:
    redisFree(gs_sync_context);
    
l_free_async_redis:
    redisAsyncFree(gs_async_context);
    event_base_free(base);
 
l_exit:
    if(!gs_exit) {    
        LOG_ERROR("Godown_keeper execution failed retry!");
        goto l_start;
    }

    LOG_INFO("exit!");
}
