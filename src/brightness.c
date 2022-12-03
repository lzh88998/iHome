/*
 * Copyright lzh88998 and distributed under Apache 2.0 license
 * 
 * Brightness is used to control the brightness of LCD touch screen
 * The brightness sensor's value can be passed to the LCD touch screen
 * directly to control the brightness
 * 
 * The update interval is suggested to be around 1-5 seconds
 */

#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "hiredis.h"
#include "async.h"
#include "adapters/libevent.h"

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
#define FLAG_KEY                "brightness"

/*
 * Interval in seconds to update the brightness of LCD
 */
#define BRIGHTNESS_UPDATE_INTERVAL  5

/*
 * Context for redis connection 
 */
static redisAsyncContext *gs_async_context = NULL;
static redisContext *gs_sync_context = NULL;

/*
 * Flag for micro srevice exit event, when set 
 * to 1 the micro service will not restart
 * automatically, but exit
 */
static int gs_exit = 0;

/*
 * Time interval recorder for items
 */
static struct timeval* gs_interval = NULL;

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

    for(size_t i = 0; i < reply->elements; i++) {
        LOG_DETAILS("sub reply element %d: %s", i, reply->element[i]->str);
    }

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
    
    for(size_t i = 0; i < reply->elements; i++) {
        LOG_DETAILS("sub reply element %d: %s", i, reply->element[i]->str);
    }

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

    for(size_t i = 0; i < reply->elements; i++) {
        LOG_DETAILS("sub reply element %d: %s", i, reply->element[i]->str);
    }

    if(3 == reply->elements && reply->element[1] && reply->element[1]->str && reply->element[2] && reply->element[2]->str) { 
        if(LOG_SET_LEVEL_OK != log_set_level(reply->element[2]->str)) {
            LOG_WARNING("Invalid log option: %s", reply->element[2]->str);
        } 
    }

    LOG_DEBUG("Set log level finished!\n");
}

/*
 * After start phase, the cargador will subscribe expected 
 * message channels to keep the controller's output PIN in
 * the expected status with value specified in redis.
 * 
 * This callback function will be called when there is new
 * expected value.
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
 * void *privdata           The PIN index is passed in as a long integer
 *                          although this seems to be a pointer but will
 *                          force convert it to a long integer
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
    redisReply *list = (redisReply*)privdata;
    
    LOG_DEBUG("Subscribe callback!");
    if (reply == NULL) {
        LOG_ERROR("Error empty subscribed response!");
        if (c->errstr) {
            LOG_ERROR("Subscribe errstr: %s", c->errstr);
        }
        redisAsyncDisconnect(c);
        return;
    }
    
    LOG_DETAILS("Subscribe reply type: %d", reply->type);
    LOG_DETAILS("Subscribe reply elements: %zd", reply->elements);

    for(size_t i = 0; i < reply->elements; i++) {
        LOG_DETAILS("sub reply element %d: %s", i, reply->element[i]->str);
    }

    if(3 != reply->elements) {
        LOG_ERROR("Error: Unexpected format!");
        redisAsyncDisconnect(c);
        return;
    }
    
    struct timeval t;
    gettimeofday(&t, 0);
    
    // reply->element[1] is topic and reply->element[2] is new value
    for(size_t i = 0; i < list->elements; i+=2) {
        if(0 == strcmp(reply->element[1]->str, list->element[i+1]->str) 
           && t.tv_sec - gs_interval[i/2].tv_sec > BRIGHTNESS_UPDATE_INTERVAL) {
            gettimeofday(&gs_interval[i/2], 0);
            LOG_DETAILS("PUBLISH %s %s", list->element[i]->str, reply->element[2]->str);
            redisReply* sync_reply = redisCommand(gs_sync_context, "PUBLISH %s %s", list->element[i]->str, reply->element[2]->str);
            if(NULL == sync_reply) {
                LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
                redisAsyncDisconnect(c);
            }
            LOG_DETAILS("PUBLISH %s %s result: %s", list->element[i]->str, reply->element[2]->str, sync_reply->str);
            freeReplyObject(sync_reply);
        }
    }
    
    LOG_DEBUG("Subscribe finished!");
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
    LOG_INFO("Connected to redis...");
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
    LOG_INFO("Disconnected from redis...");
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
    if(0 == argc) {
        return;
    }
    
    printf("Invalid input parameters!\r\n");
    printf("Usage: (<optional parameters>)\r\n");
    printf("%s <log_level> <redis_ip> <redis_port>\r\n", argv[0]);
    log_print_level_info();
    printf("E.g.:\r\n");
    printf("%s \r\n", argv[0]);
    printf("%s 127.0.0.1\r\n", argv[0]);
    printf("%s 127.0.0.1 6379\r\n", argv[0]);
}

/*
 * Main entry of the service. It will first connect
 * to the controller, and setup send/recv timeout
 * to ensure the network traffic is not blocking
 * and can fail fast.
 * 
 * Then it will connect to redis and get expected
 * status for each controller's output PIN and 
 * send the command to controller through 
 * getCallback.
 * 
 * And then the redis connection will use psubscribe
 * to subscribe the expected status and if there
 * is any update of expected status, the 
 * subscribeCallback will process the update message
 * and send command to controller
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

    const char* redis_ip;
    int redis_port;
    
    redisReply* reply = NULL;

    struct event_base *base = NULL;
    struct timeval timeout = { 0, 100000 }; 
//    list_node*  nodes[MAX_OUTPUT_PIN_COUNT];
    
l_start:
    LOG_INFO("=================== Service start! ===================");
    LOG_INFO("Parsing parameters!");
        
    redis_ip = REDIS_IP;
    redis_port = REDIS_PORT;
    
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
        LOG_ERROR("Connection error: can't allocate sync redis context");
        goto l_exit;
    }
    
    if(gs_sync_context->err) {
        LOG_ERROR("Connection error: %s", gs_sync_context->errstr);
        goto l_free_sync_redis;
    }

    LOG_INFO("PING");
    reply = redisCommand(gs_sync_context,"PING");
    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        goto l_free_sync_redis;
    }
    LOG_DEBUG("PING: %s", reply->str);
    freeReplyObject(reply);
    reply = NULL;

    LOG_INFO("Connected to Redis in sync mode");
    
    LOG_INFO("Connecting to Redis in async mode!");
    base = event_base_new();
    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, redis_ip, redis_port);
    options.connect_timeout = &timeout;

    gs_async_context = redisAsyncConnectWithOptions(&options);
    if(NULL == gs_async_context) {
        LOG_ERROR("Connection error: can't allocate async redis context");
        goto l_free_sync_redis;
    }

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

    LOG_INFO("Subscribe exit, reset, log_level configuration changes!")
    redisAsyncCommand(gs_async_context, exitCallback, NULL, "SUBSCRIBE %s/%s", FLAG_KEY, EXIT_FLAG_VALUE);
    redisAsyncCommand(gs_async_context, resetCallback, NULL, "SUBSCRIBE %s/%s", FLAG_KEY, RESET_FLAG_VALUE);
    redisAsyncCommand(gs_async_context, setLogLevelCallback, NULL, "SUBSCRIBE %s/%s", FLAG_KEY, LOG_LEVEL_FLAG_VALUE);
    
    // load topics from redis hashset
    LOG_INFO("Loading topics!");
    redisReply* list = redisCommand(gs_sync_context, "HGETALL %s", FLAG_KEY);
    if(NULL == list) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        goto l_free_async_redis;
    }
    
    gs_interval = (struct timeval*)malloc(sizeof(struct timeval) * (list->elements / 2));
    if(NULL == gs_interval) {
        LOG_ERROR("Allocate memory failed!");
        goto l_free_list;
    }

    for(size_t i = 0; i < list->elements; i+=2) {
        LOG_INFO("SUBSCRIBE %s", list->element[i + 1]->str);
        redisAsyncCommand(gs_async_context, subscribeCallback, list, "SUBSCRIBE %s", list->element[i + 1]->str);
        gettimeofday(&gs_interval[i/2], 0);
    }
    
    // Switch to DB 1 for current status
    LOG_INFO("Switch to Redis DB 1 to load status");
    LOG_DETAILS("SELECT 1");
    reply = redisCommand(gs_sync_context,"SELECT 1");
    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        goto l_free_interval;
    }
    freeReplyObject(reply);

    // Load log_level config and set log_level
    LOG_INFO("Loading log_level configuration!");
    LOG_DETAILS("GET %s/%s", FLAG_KEY, LOG_LEVEL_FLAG_VALUE);
    reply = redisCommand(gs_sync_context,"GET %s/%s", FLAG_KEY, LOG_LEVEL_FLAG_VALUE);
    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        goto l_free_interval;
    }
    if(NULL != reply->str) {
        if(LOG_SET_LEVEL_OK != log_set_level(reply->str)) {
            LOG_WARNING("Failed to set log level %s", reply->str);
        }
    }
    freeReplyObject(reply);

    // start running
    event_base_dispatch(base);

l_free_interval:    
    free(gs_interval);
    
l_free_list:
    freeReplyObject(list);
    
l_free_async_redis:
    LOG_INFO("Free async Redis connection!");
    redisAsyncFree(gs_async_context);
    event_base_free(base);
    
l_free_sync_redis:
    LOG_INFO("Free sync Redis connection!");
    if(NULL != gs_sync_context) {
        redisFree(gs_sync_context);
        gs_sync_context = NULL;
    }
    
l_exit:
    if(!gs_exit) {    
        LOG_ERROR("Execution failed retry!");
        sleep(1);
        goto l_start;
    }
    
    LOG_INFO("exit!");
    return 0;
}
