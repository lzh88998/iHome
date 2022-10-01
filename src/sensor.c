/*
 * Copyright lzh88998 and distributed under Apache 2.0 license
 * 
 * sensor is a micro service receive sensor data from LCD I2C
 * bus and publish to redis
 * 
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <hiredis.h>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <fcntl.h>

#include <errno.h>

#include "log.h"
#include "to_socket.h"

#define REDIS_IP                "127.0.0.1"
#define REDIS_PORT              6379

#define FLAG_KEY                "sensor"
#define SENSOR_KEY              "sensor_"

#define SENSOR_COUNT            4

static to_socket_ctx gs_socket = -1;
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
    printf("%s controller_ip controller_port <log_level> <redis_ip> <redis_port>\n", argv[0]);
    printf("E.g.:\n");
    printf("%s 192.168.100.100 5000\n\n", argv[0]);
    printf("%s 192.168.100.100 5000 debug\n\n", argv[0]);
    printf("%s 192.168.100.100 5000 127.0.0.1 6379\n\n", argv[0]);
    printf("%s 192.168.100.100 5000 debug 127.0.0.1 6379\n\n", argv[0]);
}

/*
 * Main entry of the service. It will first connect
 * to LCD sensor port, and then connect to redis
 * through a sync connection. When received message
 * from LCD sensor the data will be sent to redis
 * through the sync connectoin
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
    
    struct timeval timeout = { 0, 100000};
    
    unsigned char temp = 0;
    unsigned char buffer[4];
    int buffer_state = 0;
    
    const char* serv_ip;
    const char* redis_ip;
    
    redisReply* topics[SENSOR_COUNT];
    
    int serv_port, redis_port;
    
    int topic_index = 0;
    
    memset(buffer, 0, 4);
    
    for(int i = 0; i < SENSOR_COUNT; i++) {
        topics[i] = NULL;
    }
    
    LOG_INFO("=================== Service start! ===================");
    LOG_INFO("Parsing parameters!");
    switch(argc) {
        case 6:
            if(0 > log_set_level(argv[3])) {
                print_usage(argc, argv);
                return -2;
            }
            redis_port = atoi(argv[5]);
            redis_ip = argv[4];
            serv_port = atoi(argv[2]);
            serv_ip = argv[1];
            break;
        case 5:
            redis_port = atoi(argv[4]);
            redis_ip = argv[3];
            serv_port = atoi(argv[2]);
            serv_ip = argv[1];
            break;
        case 4:
            if(0 > log_set_level(argv[3])) {
                print_usage(argc, argv);
                return -3;
            }
            serv_ip = argv[1];
            serv_port = atoi(argv[2]);
            redis_ip = REDIS_IP;
            redis_port = REDIS_PORT;
            break;
        case 3:
            serv_ip = argv[1];
            serv_port = atoi(argv[2]);
            redis_ip = REDIS_IP;
            redis_port = REDIS_PORT;
            break;
        default:
            print_usage(argc, argv);
            return -1;
    }
    
l_start:
    LOG_INFO("Connecting to sensor!");
    gs_socket = to_connect(serv_ip, serv_port);
    if(-1 == gs_socket) {
        LOG_ERROR("Error connecting to sensor!\n");
        goto l_exit;
    }
    
    if(0 > to_recv(gs_socket, &temp, 1, 0)) {
        LOG_ERROR("Error receiving initial data! %s\n", strerror(errno));
        goto l_socket_cleanup;
    }
    
    LOG_INFO("Connected to sensor, remote socket: %d\n", temp);

    LOG_INFO("Connecting to Redis...");
    gs_sync_context = redisConnectWithTimeout(redis_ip, redis_port, timeout);
    if(NULL == gs_sync_context) {
        LOG_ERROR("Connection error: can't allocate redis context\n");
        goto l_socket_cleanup;
    }
    
    if(gs_sync_context->err) {
        LOG_ERROR("Connection error: %s\n", gs_sync_context->errstr);
        goto l_free_redis;
    }

    redisReply* reply = redisCommand(gs_sync_context,"PING");
    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s\n", gs_sync_context->errstr);
        goto l_free_redis;
    }
    LOG_DEBUG("PING: %s\n", reply->str);
    freeReplyObject(reply);
    
    LOG_INFO("Connected to Redis!");

    LOG_INFO("Loading config!");
    topic_index = 0;
    while(topic_index < SENSOR_COUNT) {
        topics[topic_index] = redisCommand(gs_sync_context,"HGET %s/%s/%d %s%d", FLAG_KEY, serv_ip, serv_port, SENSOR_KEY, topic_index);
        if(NULL == topics[topic_index]) {
            topic_index++;
            LOG_ERROR("Failed to sync query redis %s\n", gs_sync_context->errstr);
            goto l_free_topics;
        }
        LOG_DETAILS("Topic %d: %s", topic_index, topics[topic_index]->str);
        topic_index++;
    }
    
    LOG_INFO("Switch to DB 1!");
    reply = redisCommand(gs_sync_context,"SELECT 1");
    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s\n", gs_sync_context->errstr);
        goto l_free_redis;
    }
    freeReplyObject(reply);
    
    while(!gs_exit) {
        if(-1 == to_recv(gs_socket, &temp, 1, 0)) {
            LOG_DEBUG("Receive returned -1, %s", strerror(errno));
            if(EAGAIN == errno || EWOULDBLOCK == errno) {
                // Check log level
                LOG_DEBUG("Check log level")
                reply = redisCommand(gs_sync_context,"GET %s/%s/%d/%s", FLAG_KEY, serv_ip, serv_port, LOG_LEVEL_FLAG_VALUE);
                if(NULL == reply) {
                    LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
                    goto l_free_topics;
                }
                LOG_DETAILS("Get log level returned")
                if(NULL != reply->str) {
                    LOG_DETAILS("%s", reply->str);
                    if(0 > log_set_level(reply->str)) {
                        LOG_ERROR("Invalid log option: %s", reply->str);
                    } else {
                        freeReplyObject(reply);

                        // delete the flag ensure not find it next start
                        reply = redisCommand(gs_sync_context,"DEL %s/%s/%d/%s", FLAG_KEY, serv_ip, serv_port, LOG_LEVEL_FLAG_VALUE);
                        if(NULL == reply) {
                            LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
                            goto l_free_topics;
                        }
                        if(NULL != reply->str) {
                            LOG_DETAILS("%s", reply->str);
                        }
                    }
                }
                
                freeReplyObject(reply);

                // Check exit flag
                LOG_DEBUG("Check exit flag")
                reply = redisCommand(gs_sync_context,"GET %s/%s/%d/%s", FLAG_KEY, serv_ip, serv_port, EXIT_FLAG_VALUE);
                if(NULL == reply) {
                    LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
                    goto l_free_topics;
                }
                if(NULL != reply->str) {
                    LOG_DETAILS("Exit received %s", reply->str);
                    if(0 == strcmp(EXIT_FLAG_VALUE, reply->str)) {
                        gs_exit = 1;
                    }

                    freeReplyObject(reply);
                    
                    // delete the flag ensure not find it next start
                    reply = redisCommand(gs_sync_context,"DEL %s/%s/%d/%s", FLAG_KEY, serv_ip, serv_port, EXIT_FLAG_VALUE);
                    if(NULL == reply) {
                        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
                        goto l_free_topics;
                    }
                    if(NULL != reply->str) {
                        LOG_DETAILS("Del exit reply%s", reply->str);
                    }
                }
                
                freeReplyObject(reply);

                // Check reset flag
                LOG_DEBUG("Check reset flag")
                reply = redisCommand(gs_sync_context,"GET %s/%s/%d/%s", FLAG_KEY, serv_ip, serv_port, RESET_FLAG_VALUE);
                if(NULL == reply) {
                    LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
                    goto l_free_topics;
                }
                
                if(NULL != reply->str) {
                    LOG_DETAILS("Reset received %s", reply->str);
                    if(0 == strcmp(RESET_FLAG_VALUE, reply->str)) {
                        freeReplyObject(reply);
                        
                        // delete the flag ensure not find it next start
                        reply = redisCommand(gs_sync_context,"DEL %s/%s/%d/%s", FLAG_KEY, serv_ip, serv_port, RESET_FLAG_VALUE);
                        if(NULL == reply) {
                            LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
                            goto l_free_topics;
                        }
                        if(NULL != reply->str) {
                            LOG_DETAILS("Del reset reply %s", reply->str);
                        }

                        freeReplyObject(reply);
                        goto l_free_topics;
                    }

                    freeReplyObject(reply);
                    
                    // delete the flag ensure not find it next start
                    reply = redisCommand(gs_sync_context,"DEL %s/%s/%d/%s", FLAG_KEY, serv_ip, serv_port, RESET_FLAG_VALUE);
                    if(NULL == reply) {
                        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
                        goto l_free_topics;
                    }
                    if(NULL != reply->str) {
                        LOG_DETAILS("Del reset reply %s", reply->str);
                    }
                }
                
                freeReplyObject(reply);
            } else {
                goto l_free_topics;
            }
        } else { //
            LOG_DETAILS("Received 0x%x\n", temp);
            switch(buffer_state) {
                case 0: // first 0xAA
                    if(0xAA == temp) {
                        buffer_state++;
                    } // skip other value items
                    break;
                case SENSOR_COUNT + 1: // last 0xFF
                    if(0xFF == temp) {
                        for(int i = 0; i < SENSOR_COUNT; i++) {
                            if(NULL != topics[i]->str) {
                                redisReply* reply = redisCommand(gs_sync_context,"PUBLISH %s %d", topics[i]->str, buffer[i]);
                                if(NULL == reply) {
                                    LOG_ERROR("Failed to sync query redis %s\n", gs_sync_context->errstr);
                                    goto l_free_topics;
                                }
                                freeReplyObject(reply);
                            }
                        }
                    } else { // out of sync
                        redisCommand(gs_sync_context,"PUBLISH %s/%s/%d/sync %s", FLAG_KEY, serv_ip, serv_port, "out of sync");
                    }
                    buffer_state = 0;
                    break;
                default: // 1-4
                    // Temperature
                    // Move 1
                    // Move 2
                    // Light
                    buffer[buffer_state -1] = temp;
                    buffer_state++;
                    break;
            }
        }
    }

l_free_topics:
    while(--topic_index >= 0) {
        freeReplyObject(topics[topic_index]);
        topics[topic_index] = NULL;
    }
    
l_free_redis:
    redisFree(gs_sync_context);
    
l_socket_cleanup:
    close(gs_socket);
    gs_socket = -1;

l_exit:
    if(!gs_exit) {    
        printf("Monitor execution failed retry!\n");
        sleep(1);
        goto l_start;
    }
    
    printf("exit!\n");
    return 0;
}
