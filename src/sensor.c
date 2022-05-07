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

#define REDIS_MESSAGE_TYPE      "pmessage"

#define FLAG_KEY                "sensor"
#define EXIT_FLAG_VALUE         "exit"

static to_socket_ctx gs_socket = -1;
static redisContext *gs_sync_context = NULL;

static int gs_exit = 0;

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

int main (int argc, char **argv) {
    
l_start:
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    
    struct timeval timeout = { 0, 100000};
    
    unsigned char temp = 0;
    unsigned char buffer[4];
    int buffer_state = 0;
    
    const char* serv_ip;
    const char* redis_ip;
    int serv_port, redis_port;
    
    memset(buffer, 0, 4);
    
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
    
    gs_socket = to_connect(serv_ip, serv_port);
    if(-1 == gs_socket) {
        LOG_ERROR("Error connecting to controller!\n");
        goto l_start;
    }
    
 /* 
    if(0 > to_recv(gs_socket, &temp, 1, 0)) {
        LOG_ERROR("Error receiving initial data! %s\n", strerror(errno));
        goto l_socket_cleanup;
    }
    LOG_INFO("Connected to controller, remote socket: %d\n", temp);
*/    

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

    while(!gs_exit) {
        if(-1 == to_recv(gs_socket, &temp, 1, 0)) {
            LOG_DEBUG("Receive returned -1, %s", strerror(errno));
            if(EAGAIN == errno || EWOULDBLOCK == errno) {
                // Check log level
                LOG_DEBUG("Check log level")
                reply = redisCommand(gs_sync_context,"GET %s/%s/%d/%s", FLAG_KEY, serv_ip, serv_port, LOG_LEVEL_FLAG_VALUE);
                if(NULL == reply) {
                    LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
                    goto l_free_redis;
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
                            goto l_free_redis;
                        }
                        if(NULL != reply->str) {
                            LOG_DETAILS("%s", reply->str);
                        }
                    }
                }
                
                freeReplyObject(reply);

                // Check exit flag
                LOG_DEBUG("Check exit flag")
                reply = redisCommand(gs_sync_context,"GET %s/%s/%d", FLAG_KEY, serv_ip, serv_port);
                if(NULL == reply) {
                    LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
                    goto l_free_redis;
                }
                if(NULL != reply->str) {
                    LOG_DETAILS("%s", reply->str);
                    if(0 == strcmp(EXIT_FLAG_VALUE, reply->str)) {
                        gs_exit = 1;
                        
                        freeReplyObject(reply);
                        
                        // delete the flag ensure not find it next start
                        reply = redisCommand(gs_sync_context,"DEL %s/%s/%d", FLAG_KEY, serv_ip, serv_port);
                        if(NULL == reply) {
                            LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
                            goto l_free_redis;
                        }
                        if(NULL != reply->str) {
                            LOG_DETAILS("%s", reply->str);
                        }
                    }
                }
                
                freeReplyObject(reply);
            } else {
                goto l_free_redis;
            }
        } else { //
            LOG_DETAILS("Received 0x%x\n", temp);
            switch(buffer_state) {
                case 0: // first 0xAA
                    if(0xAA == temp) {
                        buffer_state++;
                    } // skip other value items
                    break;
                case 1: // Temperature
                    buffer[buffer_state -1] = temp;
                    buffer_state++;
                    break;
                case 2: // Move 1
                    buffer[buffer_state -1] = temp;
                    buffer_state++;
                    break;
                case 3: // Move 2
                    buffer[buffer_state -1] = temp;
                    buffer_state++;
                    break;
                case 4: // Light
                    buffer[buffer_state -1] = temp;
                    buffer_state++;
                    break;
                case 5: // last 0xFF
                    if(0xFF == temp) {
                        redisReply* reply = redisCommand(gs_sync_context,"PUBLISH %s/%s/%d/1 %d", FLAG_KEY, serv_ip, serv_port, buffer[0]); // temp
                        if(NULL == reply) {
                            LOG_ERROR("Failed to sync query redis %s\n", gs_sync_context->errstr);
                            goto l_free_redis;
                        }
                        freeReplyObject(reply);
                        reply = redisCommand(gs_sync_context,"PUBLISH %s/%s/%d/2 %d", FLAG_KEY, serv_ip, serv_port, buffer[1]); //mov1
                        if(NULL == reply) {
                            LOG_ERROR("Failed to sync query redis %s\n", gs_sync_context->errstr);
                            goto l_free_redis;
                        }
                        freeReplyObject(reply);
                        reply = redisCommand(gs_sync_context,"PUBLISH %s/%s/%d/3 %d", FLAG_KEY, serv_ip, serv_port, buffer[2]); //mov2
                        if(NULL == reply) {
                            LOG_ERROR("Failed to sync query redis %s\n", gs_sync_context->errstr);
                            goto l_free_redis;
                        }
                        freeReplyObject(reply);
                        reply = redisCommand(gs_sync_context,"PUBLISH %s/%s/%d/4 %d", FLAG_KEY, serv_ip, serv_port, buffer[3]); // light
                        if(NULL == reply) {
                            LOG_ERROR("Failed to sync query redis %s\n", gs_sync_context->errstr);
                            goto l_free_redis;
                        }
                        freeReplyObject(reply);
                    } else { // out of sync
                        redisCommand(gs_sync_context,"PUBLISH %s/%s/%d/sync %s", FLAG_KEY, serv_ip, serv_port, "out of sync");
                    }
                    buffer_state = 0;
                    break;
            }
        }
    }
    
l_free_redis:
    redisFree(gs_sync_context);
    
l_socket_cleanup:
    close(gs_socket);
    gs_socket = -1;

    if(!gs_exit) {    
        printf("Monitor execution failed retry!\n");
        goto l_start;
    }
    
    printf("exit!\n");
    return 0;
}
