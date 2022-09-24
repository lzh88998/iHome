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

#include <time.h>

#include "log.h"

#define REDIS_IP                "127.0.0.1"
#define REDIS_PORT              6379

#define FLAG_KEY                "time"
#define EXIT_FLAG_KEY           "exit"
#define EXIT_FLAG_VALUE         "exit"
#define LOG_LEVEL_FLAG_KEY      "log_level"

static redisContext *gs_sync_context = NULL;

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
    
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    // 100ms
    struct timeval timeout = { 0, 100000 }; 
    
    time_t now;
    struct tm* info;

    const char* redis_ip;
    int redis_port;
    
    int last_h = -1;
    int last_m = -1;
    
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

l_start:
    LOG_INFO("============================ Time Start ============================");

    LOG_DEBUG("Connect to Redis!");
    gs_sync_context = redisConnectWithTimeout(redis_ip, redis_port, timeout);
    if(NULL == gs_sync_context) {
        LOG_ERROR("Connection error: can't allocate redis context");
        goto l_exit;
    }
    
    if(gs_sync_context->err) {
        LOG_ERROR("Connection error: %s", gs_sync_context->errstr);
        goto l_free_sync_redis;
    }

    LOG_DEBUG("Test Redis Connection!");
    redisReply* reply = redisCommand(gs_sync_context,"PING");
    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        goto l_free_sync_redis;
    }
    LOG_DEBUG("PING: %s", reply->str);
    freeReplyObject(reply);
    
    LOG_DEBUG("Start main loop!");
    while(!gs_exit) {
        // publish time
        LOG_DETAILS("Get system time!");
        time(&now);
        info = localtime(&now);
        
        if(info->tm_hour != last_h || info->tm_min != last_m) {
            last_h = info->tm_hour;
            last_m = info->tm_min;
            LOG_DETAILS("PUBLISH %s %02d:%02d", FLAG_KEY, last_h, last_m);
            reply = redisCommand(gs_sync_context,"PUBLISH %s %02d:%02d", FLAG_KEY, last_h, last_m);
            if(NULL == reply) {
                LOG_ERROR("Failed to publish time to redis %s", gs_sync_context->errstr);
                goto l_free_sync_redis;
            }

            freeReplyObject(reply);
        }
        
        // check log level
        LOG_DETAILS("GET %s/%s", FLAG_KEY, LOG_LEVEL_FLAG_KEY);
        reply = redisCommand(gs_sync_context,"GET %s/%s", FLAG_KEY, LOG_LEVEL_FLAG_KEY);
        if(NULL == reply) {
            LOG_ERROR("Failed to check exit status %s", gs_sync_context->errstr);
            goto l_free_sync_redis;
        }
        
        if(NULL != reply->str) {
            if(LOG_SET_LEVEL_OK != log_set_level(reply->str)) {
                LOG_WARNING("Invalid log option: %s", reply->str);
            }

            freeReplyObject(reply);
            reply = redisCommand(gs_sync_context,"DEL %s/%s", FLAG_KEY, LOG_LEVEL_FLAG_KEY);
            if(NULL == reply) {
                LOG_ERROR("Failed to check exit status %s", gs_sync_context->errstr);
                goto l_free_sync_redis;
            }
        }

        freeReplyObject(reply);

        // check exit
        LOG_DETAILS("GET %s/%s", FLAG_KEY, EXIT_FLAG_KEY);
        reply = redisCommand(gs_sync_context,"GET %s/%s", FLAG_KEY, EXIT_FLAG_KEY);
        if(NULL == reply) {
            LOG_ERROR("Failed to check exit status %s", gs_sync_context->errstr);
            goto l_free_sync_redis;
        }
        
        if(NULL != reply->str) {
            if(0 == strcmp(EXIT_FLAG_VALUE, reply->str)) {
                gs_exit = 1;
            }

            freeReplyObject(reply);
            reply = redisCommand(gs_sync_context,"DEL %s/%s", FLAG_KEY, EXIT_FLAG_VALUE);
            if(NULL == reply) {
                LOG_ERROR("Failed to check exit status %s", gs_sync_context->errstr);
                goto l_free_sync_redis;
            }
        }

        freeReplyObject(reply);
        
        sleep(1);
    }
    
    LOG_DEBUG("Exit main loop!");
    
l_free_sync_redis:
    LOG_DEBUG("Free redis object!");
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
