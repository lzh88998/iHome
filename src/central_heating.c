/*
 * Copyright lzh88998 and distributed under Apache 2.0 license
 * 
 * Central_heating is a micro service that monitoring the water
 * temperature from heating plant, and the room temperature.
 * When the water temperature from heating plant is higher than
 * room temperature with a configurable threshold, then enable
 * the heating feature.
 * 
 * When the heating feature is enabled, the central heating 
 * will monitor the room temperature and compare it with a 
 * target temperature. If the room temperature is lower than 
 * target temerature for 1 minute then start heating switch
 * if the room temperature is higher than the target 
 * temperature for 1 minute then stop heating switch
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

#define EXIT_FLAG_KEY           "exit"
#define EXIT_FLAG_VALUE         "exit"
#define LOG_LEVEL_FLAG_KEY      "central_heating/log_level"

#define FLAG_KEY                "central_heating"

#define ROOM_TEMP_NAME          "room_temp"
#define WATER_TEMP_NAME         "water_temp"
#define HASH_LIST_NAME          "room_list"
#define TEMP_THRESHOLD_NAME     "threshold"
#define TEMP_POSTFIX            "temp_postfix"
#define TARGET_POSTFIX          "target_postfix"

#define MESSAGE_ID              "message"

#define TEMP_THRESHOLD_INTERVAL 60

static redisContext *gs_sync_context = NULL;
static redisAsyncContext *gs_async_context = NULL;

static int gs_exit = 0;

int gs_room_temp = 127;
int gs_water_temp = 127;
int gs_threshold = 20;

/*
 * Use this linked list format to describe PINs that depends on 
 * same redis topic
 */                                                        
typedef struct linked_list_node {
    int cur_temp;
    int target_temp;
    int last_status;
    redisReply* sw_topic;
    struct timeval last_check;
    struct linked_list_node* pNext;
} list_node;

static list_node* room_list = NULL;

void thresholdChangeCallback(redisAsyncContext *c, void *r, void *privdata) {
    UNUSED(privdata);

    redisReply *reply = r;
    
    LOG_DEBUG("Subscribe threshold callback!");
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
    
    if(NULL == reply->element[0] || NULL == reply->element[0]->str) {
        LOG_WARNING("Error: Unkonwn message type!");
        return;
    }
    
    if(strcmp(MESSAGE_ID, reply->element[0]->str)) {
        LOG_WARNING("Error: not publish message!");
        return;
    }
    
    // index 2 is the value
    if(NULL == reply->element[2] || NULL == reply->element[2]->str) {
        LOG_WARNING("Error: Empty content!");
        return;
    }
    
    int temp = atoi(reply->element[2]->str);
    if(temp) {
        gs_threshold = temp;
    }
    
    LOG_DEBUG("Subscribe finished!");
}

void roomTempChangeCallback(redisAsyncContext *c, void *r, void *privdata) {
    UNUSED(privdata);

    redisReply *reply = r;
    
    LOG_DEBUG("Subscribe room temperature change callback!");
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
    
    if(NULL == reply->element[0] || NULL == reply->element[0]->str) {
        LOG_WARNING("Error: Unkonwn message type!");
        return;
    }
    
    if(strcmp(MESSAGE_ID, reply->element[0]->str)) {
        LOG_WARNING("Error: not publish message!");
        return;
    }
    
    // index 2 is the value
    if(NULL == reply->element[2] || NULL == reply->element[2]->str) {
        LOG_WARNING("Error: Empty content!");
        return;
    }
    
    gs_room_temp = atoi(reply->element[2]->str);
    
    LOG_DEBUG("Subscribe finished!");}

void waterTempChangeCallback(redisAsyncContext *c, void *r, void *privdata) {
    UNUSED(privdata);

    redisReply *reply = r;
    
    LOG_DEBUG("Subscribe water temperature change callback!");
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
    
    if(NULL == reply->element[0] || NULL == reply->element[0]->str) {
        LOG_WARNING("Error: Unkonwn message type!");
        return;
    }
    
    if(strcmp(MESSAGE_ID, reply->element[0]->str)) {
        LOG_WARNING("Error: not publish message!");
        return;
    }
    
    // index 2 is the value
    if(NULL == reply->element[2] || NULL == reply->element[2]->str) {
        LOG_WARNING("Error: Empty content!");
        return;
    }
    
    gs_water_temp = atoi(reply->element[2]->str);
    
    LOG_DEBUG("Subscribe finished!");
}

void curTempChangeCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    list_node* node = (list_node*)privdata;
    
    LOG_DEBUG("Subscribe current temperature change callback!");
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
    
    if(NULL == reply->element[0] || NULL == reply->element[0]->str) {
        LOG_WARNING("Error: Unkonwn message type!");
        return;
    }
    
    if(strcmp(MESSAGE_ID, reply->element[0]->str)) {
        LOG_WARNING("Error: not publish message!");
        return;
    }
    
    // index 2 is the value
    if(NULL == reply->element[2] || NULL == reply->element[2]->str) {
        LOG_WARNING("Error: Empty content!");
        return;
    }
    
    int temp = atoi(reply->element[2]->str);
    if(node->cur_temp > node->target_temp && temp > node->target_temp) {
        // too cold
        struct timeval t;
        gettimeofday(&t, NULL);
        LOG_DETAILS("Cold!");
        LOG_DETAILS("Seconds established! %d", t.tv_sec - node->last_check.tv_sec);
        LOG_DETAILS("Last status! %d", node->last_status);
        LOG_DETAILS("Water temperature! %d", gs_water_temp);
        LOG_DETAILS("Room temperature! %d", gs_room_temp);
        LOG_DETAILS("Threshold! %d", TEMP_THRESHOLD_INTERVAL);
        LOG_DETAILS("Temperature! %d", temp);
        LOG_DETAILS("Target temperature! %d", node->target_temp);
        if(t.tv_sec - node->last_check.tv_sec > TEMP_THRESHOLD_INTERVAL) {
            if(gs_room_temp - gs_water_temp > gs_threshold) {
                // heating water temperature is higher than threshold
                if(!node->last_status) {
                    // publish 1 to enable heating
                    LOG_INFO("Publish 1 to enable heating");
                    LOG_DETAILS("PUBLISH %s 1", node->sw_topic->str);
                    reply = redisCommand(gs_sync_context, "PUBLISH %s 1", node->sw_topic->str);

                    if(NULL == reply) {
                        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
                        redisAsyncDisconnect(c);
                        return;
                    }

                    freeReplyObject(reply);
                    node->last_status = 1;
                }
            } else {
                // heating water termperature is not high enough
                if(node->last_status) {
                    // publish 0 to disable heating
                    LOG_INFO("Publish 0 to disable heating");
                    LOG_DETAILS("PUBLISH %s 0", node->sw_topic->str);
                    reply = redisCommand(gs_sync_context,"PUBLISH %s 0", node->sw_topic->str);

                    if(NULL == reply) {
                        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
                        redisAsyncDisconnect(c);
                        return;
                    }

                    freeReplyObject(reply);
                    node->last_status = 0;
                }
            }
            gettimeofday(&node->last_check, NULL);
        }
    } else if(node->cur_temp < node->target_temp && temp < node->target_temp) {
        // too hot
        struct timeval t;
        gettimeofday(&t, NULL);
        LOG_DETAILS("Hot!");
        LOG_DETAILS("Seconds established! %d", t.tv_sec - node->last_check.tv_sec);
        LOG_DETAILS("Last status! %d", node->last_status);
        if(t.tv_sec - node->last_check.tv_sec > TEMP_THRESHOLD_INTERVAL) {
            if(node->last_status) {
                // publish 0 to disable heating
                LOG_INFO("Publish 0 to disable heating");
                LOG_DETAILS("PUBLISH %s 0", node->sw_topic->str);
                reply = redisCommand(gs_sync_context,"PUBLISH %s 0", node->sw_topic->str);

                if(NULL == reply) {
                    LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
                    redisAsyncDisconnect(c);
                    return;
                }

                freeReplyObject(reply);
                node->last_status = 0;
                gettimeofday(&node->last_check, NULL);
            }
        }
    } else {
        gettimeofday(&node->last_check, NULL);
    }
    
    node->cur_temp = temp;
    LOG_DEBUG("Subscribe finished!");
}

void targetTempChangeCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    list_node* node = (list_node*)privdata;
    
    LOG_DEBUG("Subscribe target temperature change callback!");
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
    
    if(NULL == reply->element[0] || NULL == reply->element[0]->str) {
        LOG_WARNING("Error: Unkonwn message type!");
        return;
    }
    
    if(strcmp(MESSAGE_ID, reply->element[0]->str)) {
        LOG_WARNING("Error: not publish message!");
        return;
    }
    
    // index 2 is the value
    if(NULL == reply->element[2] || NULL == reply->element[2]->str) {
        LOG_WARNING("Error: Empty content!");
        return;
    }
    
    node->target_temp = atoi(reply->element[2]->str);
    gettimeofday(&node->last_check, NULL);
    
    LOG_DEBUG("Subscribe finished!");
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

    redisReply* temp_postfix_reply = NULL;
    redisReply* target_postfix_reply = NULL;
    redisReply* room_temp_reply = NULL;
    redisReply* water_temp_reply = NULL;
    
    redisReply* list = NULL;
    redisReply* tempReply = NULL;

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

    // set async status processing
    redisAsyncSetConnectCallback(gs_async_context,connectCallback);
    redisAsyncSetDisconnectCallback(gs_async_context,disconnectCallback);

    // set exit/reset/loglevel processing
    redisAsyncCommand(gs_async_context, exitCallback, NULL, "SUBSCRIBE %s/%s", FLAG_KEY, EXIT_FLAG_VALUE);
    redisAsyncCommand(gs_async_context, resetCallback, NULL, "SUBSCRIBE %s/%s", FLAG_KEY, RESET_FLAG_VALUE);
    redisAsyncCommand(gs_async_context, setLogLevelCallback, NULL, "SUBSCRIBE %s/%s", FLAG_KEY, LOG_LEVEL_FLAG_VALUE);

    // load configuration here
    
    // load temp threshold for enable heating
    redisAsyncCommand(gs_async_context, thresholdChangeCallback, NULL, "SUBSCRIBE %s/%s", FLAG_KEY, TEMP_THRESHOLD_NAME);

    // load room temperature topic
    room_temp_reply = redisCommand(gs_sync_context,"HGET %s %s", FLAG_KEY, ROOM_TEMP_NAME);
    if(NULL == room_temp_reply) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        goto l_free_async_redis;
    }
    
    redisAsyncCommand(gs_async_context, roomTempChangeCallback, NULL, "SUBSCRIBE %s", room_temp_reply->str);

    // load heating water temperature topic
    water_temp_reply = redisCommand(gs_sync_context,"HGET %s %s", FLAG_KEY, WATER_TEMP_NAME);
    if(NULL == water_temp_reply) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        goto l_free_reply;
    }
    
    redisAsyncCommand(gs_async_context, waterTempChangeCallback, NULL, "SUBSCRIBE %s", water_temp_reply->str);
    
    // load temperature postfix string
    temp_postfix_reply = redisCommand(gs_sync_context,"HGET %s %s", FLAG_KEY, TEMP_POSTFIX);
    if(NULL == temp_postfix_reply) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        goto l_free_reply;
    }

    // load target temperature postfix string
    target_postfix_reply = redisCommand(gs_sync_context,"HGET %s %s", FLAG_KEY, TARGET_POSTFIX);
    if(NULL == target_postfix_reply) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        goto l_free_reply;
    }
    
    // load all rooms and switches
    tempReply = redisCommand(gs_sync_context,"HGET %s %s", FLAG_KEY, HASH_LIST_NAME);
    if(NULL == tempReply) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        goto l_free_reply;
    }
    
    list = redisCommand(gs_sync_context,"HKEYS %s", tempReply->str);
    
    if(NULL == list) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        goto l_free_reply;
    }
    
    list_node* cur_node = room_list; // room list should be NULL now
    // transverse all rooms and allocate necessary storage spaces
    for(size_t i = 0; i < list->elements; i++) {
        // key is target temperature topic, value is switch topic
        if(!cur_node) {
            room_list = (list_node*)malloc(sizeof(list_node));
            cur_node = room_list;
        } else {
            cur_node->pNext = (list_node*)malloc(sizeof(list_node));
            cur_node = cur_node->pNext;
        }
        
        cur_node->pNext = NULL;
        
        // get switch topic
        LOG_INFO("Get switch topic");
        LOG_INFO("HGET %s %s", tempReply->str, list->element[i]->str);
        cur_node->sw_topic = redisCommand(gs_sync_context,"HGET %s %s", tempReply->str, list->element[i]->str);
        if(NULL == cur_node->sw_topic) {
            LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
            goto l_free_reply;
        }
        
        // switch to DB 1
        LOG_INFO("Switch to Redis DB 1 to load status");
        LOG_INFO("SELECT 1");
        reply = redisCommand(gs_sync_context,"SELECT 1");

        if(NULL == reply) {
            LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
            goto l_free_reply;
        }
        
        LOG_INFO(reply->str);

        freeReplyObject(reply);

        // get current switch status
        LOG_INFO("Get switch status");
        LOG_INFO("GET %s", cur_node->sw_topic->str);
        reply = redisCommand(gs_sync_context,"GET %s", cur_node->sw_topic->str);

        if(NULL == reply) {
            LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
            goto l_free_reply;
        }
        
        LOG_INFO(reply->str);

        if(reply->str) {
            cur_node->last_status = atoi(reply->str);
        }

        freeReplyObject(reply);
        
        // get current temperature
        LOG_INFO("Get current temperature");
        LOG_INFO("GET %s/%s", list->element[i]->str, temp_postfix_reply->str);
        reply = redisCommand(gs_sync_context,"GET %s/%s", list->element[i]->str, temp_postfix_reply->str);

        if(NULL == reply) {
            LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
            goto l_free_reply;
        }
        
        LOG_INFO(reply->str);

        if(reply->str) {
            cur_node->cur_temp = atoi(reply->str);
        }

        freeReplyObject(reply);
        
        gettimeofday(&cur_node->last_check, NULL);
        
        // get target temperature
        LOG_INFO("Get target temperature");
        LOG_INFO("GET %s/%s", list->element[i]->str, target_postfix_reply->str);
        reply = redisCommand(gs_sync_context,"GET %s/%s", list->element[i]->str, target_postfix_reply->str);

        if(NULL == reply) {
            LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
            goto l_free_reply;
        }
        
        LOG_INFO(reply->str);

        if(reply->str) {
            cur_node->target_temp = atoi(reply->str);
        }

        freeReplyObject(reply);
        
        // switch to DB 0
        LOG_INFO("Switch to Redis DB 0 to load status");
        LOG_INFO("SELECT 0");
        reply = redisCommand(gs_sync_context,"SELECT 0");

        if(NULL == reply) {
            LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
            goto l_free_reply;
        }

        LOG_INFO(reply->str);

        freeReplyObject(reply);

        LOG_INFO("SUBSCRIBE %s/%s", list->element[i]->str, temp_postfix_reply->str);
        redisAsyncCommand(gs_async_context, curTempChangeCallback, cur_node, "SUBSCRIBE %s/%s", list->element[i]->str, temp_postfix_reply->str);
        LOG_INFO("SUBSCRIBE %s/%s", list->element[i]->str, target_postfix_reply->str);
        redisAsyncCommand(gs_async_context, targetTempChangeCallback, cur_node, "SUBSCRIBE %s/%s", list->element[i]->str, target_postfix_reply->str);
    }
    freeReplyObject(list);
    freeReplyObject(tempReply);
    tempReply = NULL;

    // switch to db1 to load status
    LOG_INFO("Switch to Redis DB 1 to load status");
    LOG_DETAILS("SELECT 1");
    reply = redisCommand(gs_sync_context,"SELECT 1");

    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        goto l_free_reply;
    }

    freeReplyObject(reply);
    
    // load central heating threshold
    LOG_INFO("Load central heating threshold");
    LOG_DETAILS("GET %s/%s", FLAG_KEY, TEMP_THRESHOLD_NAME);
    reply = redisCommand(gs_sync_context, "GET %s/%s", FLAG_KEY, TEMP_THRESHOLD_NAME);

    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        goto l_free_reply;
    }
    
    if(reply->str) {
        if(atoi(reply->str)) {
            gs_threshold = atoi(reply->str);
        }
    }

    freeReplyObject(reply);

    // load room temperature
    LOG_INFO("Load room temperature");
    LOG_DETAILS("GET %s", room_temp_reply);
    reply = redisCommand(gs_sync_context, "GET %s", room_temp_reply);

    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        goto l_free_reply;
    }
    
    if(reply->str) {
        gs_room_temp = atoi(reply->str);
    }

    freeReplyObject(reply);

    // load water temperature
    LOG_INFO("Load water temperature");
    LOG_DETAILS("GET %s", water_temp_reply);
    reply = redisCommand(gs_sync_context,"GET %s", water_temp_reply);

    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        goto l_free_reply;
    }

    if(reply->str) {
        gs_water_temp = atoi(reply->str);
    }

    freeReplyObject(reply);

    LOG_INFO("Loading log_level configuration!");
    LOG_DETAILS("GET %s", LOG_LEVEL_FLAG_VALUE);
    reply = redisCommand(gs_sync_context,"GET %s", LOG_LEVEL_FLAG_VALUE);

    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        goto l_free_reply;
    }
    
    if(NULL != reply->str) {
        if(LOG_SET_LEVEL_OK != log_set_level(reply->str)) {
            LOG_WARNING("Failed to set log level %s", reply->str);
        }
    }

    freeReplyObject(reply);

    // start loop
    event_base_dispatch(base);
l_free_reply:
    if(tempReply) {
        freeReplyObject(tempReply);
    }
    
    while(room_list) {
        list_node* temp = room_list;
        room_list = room_list->pNext;
        freeReplyObject(temp->sw_topic);
        free(temp);
    }

    if(target_postfix_reply) {
        freeReplyObject(target_postfix_reply);
    }
    
    if(temp_postfix_reply) {
        freeReplyObject(temp_postfix_reply);
    }
    
    if(list) {
        freeReplyObject(list);
    }
    
    if(water_temp_reply) {
        freeReplyObject(water_temp_reply);
    }
    
    if(room_temp_reply) {
        freeReplyObject(room_temp_reply);
    }
    
l_free_async_redis:
    redisAsyncFree(gs_async_context);
    event_base_free(base);
 
l_free_sync_redis:
    redisFree(gs_sync_context);
    
l_exit:
    if(!gs_exit) {    
        LOG_ERROR("central heating_keeper execution failed retry!");
        sleep(1);
        goto l_start;
    }

    LOG_INFO("exit!");
    return 0;
}
