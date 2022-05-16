/*
 * Copyright lzh88998 and distributed under Apache 2.0 license
 * 
 * celsius_converter is a micro service that receive sensor
 * value from configured channel and convert the received
 * value to a celsius temperature value and publish to 
 * a new channel which has a name of original channel with
 * postfix of VALUE_KEY
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

#include <math.h>

#include "log.h"

/*
 * Default address and port for redis 
 * connection
 */
#define REDIS_IP                "127.0.0.1"
#define REDIS_PORT              6379

/*
 * Identifier used in redis keys for this
 * micro service
 */
#define FLAG_KEY                "celsius_converter"

/*
 * Postfix used for storing converted
 * celsius temperature
 */
#define VALUE_KEY               "\xE6\x91\x84\xE6\xB0\x8F\xE5\xBA\xA6"

/*
 * Celsius temperature sign used as
 * postfix of the value
 */
#define UNIT_KEY                "\xE2\x84\x83"

/*
 * Context for redis connection 
 */
static redisContext *gs_sync_context = NULL;
static redisAsyncContext *gs_async_context = NULL;

/*
 * Flag for micro srevice exit event, when set 
 * to 1 the micro service will not restart
 * automatically, but exit
 */
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
float convert_temp(unsigned char v) {
    return 298.15 * 3950 / (log((float)v/(255-v)) * 298.15 + 3950) - 273.15;
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
    printf("%s controller_ip controller_port <log level> <redis_ip> <redis_port>\n", argv[0]);
    printf("E.g.:\n");
    printf("%s debug\n", argv[0]);
    printf("%s 192.168.100.100 5000\n", argv[0]);
    printf("%s 192.168.100.100 5000 debug\n", argv[0]);
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
 * exitCallback is used to process exit notification
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
    UNUSED(privdata);

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

    if(3 == reply->elements && reply->element[1] && reply->element[1]->str && reply->element[2] && reply->element[2]->str) {
        if(0 == strcmp(EXIT_FLAG_VALUE, reply->element[2]->str)) { 
            gs_exit = 1;
            redisAsyncDisconnect(c);
        }
    }

    LOG_DEBUG("Exit finished!\n");
}

/*
 * resetCallback is used to process reset notification
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
    UNUSED(privdata);

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

    if(3 == reply->elements && reply->element[1] && reply->element[1]->str && reply->element[2] && reply->element[2]->str) {
        if(0 == strcmp(RESET_FLAG_VALUE, reply->element[2]->str)) { 
            redisAsyncDisconnect(c);
        }
    }
    
    LOG_DEBUG("Reset finished!\n");
}

/*
 * setLogLevelCallback is used to dynamically update
 * logging levels of standard output. This is helpful
 * when debugging the application.
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
    UNUSED(privdata);

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

    if(3 == reply->elements && reply->element[1] && reply->element[1]->str && reply->element[2] && reply->element[2]->str) { 
        if(LOG_SET_LEVEL_OK != log_set_level(reply->element[2]->str)) {
            LOG_WARNING("Invalid log option: %s", reply->element[2]->str);
        } 
    }

    LOG_DEBUG("Set log level finished!\n");
}

/*
 * subscribeCallback is used to temperature sensor
 * value change event
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
    UNUSED(privdata);

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

    if(3 != reply->elements) {
        LOG_ERROR("Error: Unexpected format!");
        redisAsyncDisconnect(c);
        return;
    }
    
    if(NULL == reply->element[2] || NULL == reply->element[2]->str) {
        LOG_WARNING("Error: Empty content!");
        return;
    }

    float value = convert_temp(atoi(reply->element[2]->str) & 0xFF);

    LOG_DETAILS("PUBLISH %s/%s %.1f%s", reply->element[1]->str, VALUE_KEY, value, UNIT_KEY);

    redisReply *sync_reply = redisCommand(gs_sync_context,"PUBLISH %s/%s %.1f%s", reply->element[1]->str, VALUE_KEY, value, UNIT_KEY);
    if(NULL == sync_reply) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        redisAsyncDisconnect(c);
    } else {
        freeReplyObject(sync_reply); 
    }  

    LOG_DEBUG("Subscribe finished!\n");
}

/*
 * Main entry of the service. It will first connect
 * to redis through a sync connection. And then 
 * connect to redis through a async connection
 * and subscribe channels that configured in redis
 * Sets named of FLAG_KEY.
 * 
 * When received message from redis channel, these
 * data will be processed by subscribeCallback
 * funcation. And converted data will be send back
 * to redis through sync connection
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
            redis_ip = argv[1];
            redis_port = atoi(argv[2]);
            break;
        default:
            print_usage(argc, argv);
            return -1;
    }

    LOG_DETAILS("Connecting to Redis!");
    gs_sync_context = redisConnectWithTimeout(redis_ip, redis_port, timeout);
    if(NULL == gs_sync_context) {
        LOG_ERROR("Connection error: can't allocate redis context");
        goto l_exit;
    }
    
    if(gs_sync_context->err) {
        LOG_ERROR("Connection error: %s", gs_sync_context->errstr);
        goto l_free_sync_redis;
    }

    LOG_DETAILS("Connected to Redis!");

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

    LOG_DETAILS("GET %s/%s", FLAG_KEY, LOG_LEVEL_FLAG_VALUE);
    reply = redisCommand(gs_sync_context,"GET %s/%s", FLAG_KEY, LOG_LEVEL_FLAG_VALUE);

    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        goto l_free_async_redis;
    }
    
    if(NULL != reply->str) {
        if(0 > log_set_level(reply->str)) {
            LOG_WARNING("Failed to set log level %s", reply->str);
        }
    }

    freeReplyObject(reply);

    LOG_DETAILS("HGETALL %s", FLAG_KEY);
    reply = redisCommand(gs_sync_context,"SMEMBERS %s", FLAG_KEY);
    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        goto l_free_sync_redis;
    }
    
    LOG_DETAILS("%d", reply->type);
    LOG_DETAILS("%s", reply->str);
    
    for(size_t i = 0; i < reply->elements; i++) {
        LOG_DETAILS("Item %ld: %s", i, reply->element[i]);
        if(NULL != reply->element[i] && NULL != reply->element[i]->str) {
            LOG_DETAILS("Item %ld content: %s", i, reply->element[i]->str);
            redisAsyncCommand(gs_async_context, subscribeCallback, NULL, "SUBSCRIBE %s", reply->element[i]->str);
        }
    }
    freeReplyObject(reply);   

    redisAsyncCommand(gs_async_context, resetCallback, NULL, "SUBSCRIBE %s/%s", FLAG_KEY, RESET_FLAG_VALUE);
    redisAsyncCommand(gs_async_context, exitCallback, NULL, "SUBSCRIBE %s/%s", FLAG_KEY, EXIT_FLAG_VALUE);
    redisAsyncCommand(gs_async_context, setLogLevelCallback, NULL, "SUBSCRIBE %s/%s", FLAG_KEY, LOG_LEVEL_FLAG_VALUE);
    
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
}
