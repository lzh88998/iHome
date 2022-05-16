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
#include <errno.h>

#include <sys/time.h>

#include "log.h"
#include "to_socket.h"

#define REDIS_IP                "127.0.0.1"
#define REDIS_PORT              6379

#define FLAG_KEY                "touch"
#define EXIT_FLAG_VALUE         "exit"

#define MSG_INTERVAL_MS         300

static int gs_socket = -1;
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
    printf("%s controller_ip controller_port <log level> <redis_ip> <redis_port>\n", argv[0]);
    printf("E.g.:\n");
    printf("%s 192.168.100.100 5000\n\n", argv[0]);
    printf("%s 192.168.100.100 5000 debug\n\n", argv[0]);
    printf("%s 192.168.100.100 5000 127.0.0.1 6379\n\n", argv[0]);
    printf("%s 192.168.100.100 5000 debug 127.0.0.1 6379\n\n", argv[0]);
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

    unsigned char temp = 0;
    unsigned char cmd = 0;
    unsigned int  x = 0, y = 0;
    int buffer_state = 0;
    
    const char* serv_ip;
    int serv_port = 0;
    const char* redis_ip;
    int redis_port = REDIS_PORT;
    
    struct timeval timeout = { 0, 100000 }; 
    struct timeval last_time, cur_time;
    gettimeofday(&last_time, NULL);
    gettimeofday(&cur_time, NULL);

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
    if(0 > gs_socket) {
        LOG_ERROR("Error creating socket!");
        goto l_socket_cleanup;
    }
    
/*  
    if(0 > to_recv(gs_socket, &temp, 1, 0)) {
        LOG_ERROR("Error receiving initial data! %d %s", ret, strerror(errno));
        goto l_socket_cleanup;
    }
    LOG_INFO("Connected to controller, remote socket: %d", temp);
*/    

    gs_sync_context = redisConnectWithTimeout(redis_ip, redis_port, timeout);
    if(NULL == gs_sync_context) {
        LOG_ERROR("Connection error: can't allocate redis context");
        goto l_socket_cleanup;
    }
    
    if(gs_sync_context->err) {
        LOG_ERROR("Connection error: %s", gs_sync_context->errstr);
        goto l_free_redis;
    }

    redisReply* reply = redisCommand(gs_sync_context,"PING");
    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
        goto l_free_redis;
    }
    LOG_DEBUG("PING: %s", reply->str);
    freeReplyObject(reply);
    
    LOG_INFO("Start loop!");

    do {
        LOG_DEBUG("Start Receiving");
        if(-1 == to_recv(gs_socket, &temp, 1, 0)) {
            LOG_DEBUG("Receive returned -1, %s", strerror(errno));
            if(EAGAIN == errno || EWOULDBLOCK == errno) {
                // receive timeout
                // Send out pending MSG
                if(0 != cmd) {
                    LOG_DETAILS("Get time info");
                    gettimeofday(&cur_time, NULL);
                    if(((cur_time.tv_sec - last_time.tv_sec) * 1000 + 
                        (cur_time.tv_usec - last_time.tv_usec) / 1000) > MSG_INTERVAL_MS) {
                        // only send out when there is enough interval
                        last_time.tv_sec = cur_time.tv_sec;
                        last_time.tv_usec = cur_time.tv_usec;
                        LOG_DETAILS("PUBLISH %s/%s 0x%x %d %d", FLAG_KEY, serv_ip, cmd, x, y);
                        reply = redisCommand(gs_sync_context,"PUBLISH %s/%s %d/%d/%d", FLAG_KEY, serv_ip, cmd, x, y);
                        if(NULL == reply) {
                            LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
                            goto l_free_redis;
                        }
                        if(NULL != reply->str) {
                            LOG_DETAILS("%s", reply->str);
                        }
                        freeReplyObject(reply);
                        
                        // clear cmd
                        cmd = 0;
                    }
                }
                
                // Check log level
                LOG_DEBUG("Check log level")
                reply = redisCommand(gs_sync_context,"GET %s/%s/%s", FLAG_KEY, serv_ip, LOG_LEVEL_FLAG_VALUE);
                if(NULL == reply) {
                    LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
                    goto l_free_redis;
                }
                
                LOG_DETAILS("Get log level returned")
                
                if(NULL != reply->str) {
                    LOG_DETAILS("%s", reply->str);
                    if(0 > log_set_level(reply->str)) {
                        LOG_WARNING("Invalid log option: %s", reply->str);
                    } else {
                        freeReplyObject(reply);

                        // delete the flag ensure not find it next start
                        reply = redisCommand(gs_sync_context,"DEL %s/%s/%s", FLAG_KEY, serv_ip, LOG_LEVEL_FLAG_VALUE);
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
                reply = redisCommand(gs_sync_context,"GET %s/%s/%s", FLAG_KEY, serv_ip, EXIT_FLAG_VALUE);
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
                        reply = redisCommand(gs_sync_context,"DEL %s/%s/%s", FLAG_KEY, serv_ip, EXIT_FLAG_VALUE);
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

                // Check reset flag
                LOG_DEBUG("Check reset flag")
                reply = redisCommand(gs_sync_context,"GET %s/%s/%s", FLAG_KEY, serv_ip, RESET_FLAG_VALUE);
                if(NULL == reply) {
                    LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
                    goto l_free_redis;
                }
                
                if(NULL != reply->str) {
                    LOG_DETAILS("%s", reply->str);
                    if(0 == strcmp(RESET_FLAG_VALUE, reply->str)) {
                        freeReplyObject(reply);
                        
                        // delete the flag ensure not find it next start
                        reply = redisCommand(gs_sync_context,"DEL %s/%s/%s", FLAG_KEY, serv_ip, RESET_FLAG_VALUE);
                        if(NULL == reply) {
                            LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
                            goto l_free_redis;
                        }
                        if(NULL != reply->str) {
                            LOG_DETAILS("%s", reply->str);
                        }

                        freeReplyObject(reply);
                        goto l_free_redis;
                    }
                }
                
                freeReplyObject(reply);
            } else {
                // other errors
                LOG_ERROR("Error receive bytes %s", strerror(errno));
                goto l_free_redis;
            }
        } else { //
            LOG_DETAILS("Received 0x%x %d", temp, buffer_state);
            switch(buffer_state) {
                case 0: // first 0xB1/B2/B3
                    if(0xB1 == temp) {
                        switch(cmd) {
                            case 0xB1:
                                // this shouldn't happen as B3 should always come before B1.
                                LOG_WARNING("Warning: invalid cmd state! 0x%x", cmd);
                                break;
                            case 0xB2: 
                                // this shouldn't happen as B3 should always come before B1.
                                LOG_WARNING("Warning: invalid cmd state! 0x%x", cmd);
                                break;
                            case 0xB3:
                                // last msg haven't been sent out convert to move
                                cmd = 0xB2;
                                break;
                            default: // last msg have been sent out, new click
                                cmd = 0xB1;
                                break;
                        }
                        buffer_state++;
                    } else if(0xB2 == temp) {
                        switch(cmd) {
                            case 0xB1: // last msg haven't been sent out, keep click event
                                break;
                            case 0xB2: 
                                // this shouldn't happen as B3 should always come before B1.
                                LOG_WARNING("Warning: invalid cmd state! 0x%x", cmd);
                                break;
                            case 0xB3:
                                // last msg haven't been sent out convert to move
                                cmd = 0xB2;
                                break;
                            default: // last msg have been sent out, new click
                                cmd = 0xB2;
                                break;
                        }
                        buffer_state++;
                    } else if(0xB3 == temp) {
                        gettimeofday(&cur_time, NULL);
                        if(((cur_time.tv_sec - last_time.tv_sec) * 1000 + 
                            (cur_time.tv_usec - last_time.tv_usec) / 1000) > MSG_INTERVAL_MS) {
                            // only send out when there is enough interval
                            last_time.tv_sec = cur_time.tv_sec;
                            last_time.tv_usec = cur_time.tv_usec;
                            cmd = temp;
                            LOG_DETAILS("PUBLISH %s/%s 0x%x %d %d", FLAG_KEY, serv_ip, cmd, x, y);
                            reply = redisCommand(gs_sync_context,"PUBLISH %s/%s %d/%d/%d", FLAG_KEY, serv_ip, cmd, x, y);
                            if(NULL == reply) {
                                LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
                                goto l_free_redis;
                            }
                            if(NULL != reply->str) {
                                LOG_DETAILS("%s", reply->str);
                            }
                            freeReplyObject(reply);
                            
                            // clear cmd
                            cmd = 0;
                        } else {
                            cmd = 0xB3;
                        }
                    }
                    break;
                case 1: // x1 
                    x = temp;
                    buffer_state++;
                    break;
                case 2: // x2
                    x *= 256;
                    x += temp;
                    buffer_state++;
                    break;
                case 3: // y1
                    y = temp;
                    buffer_state++;
                    break;
                case 4: // y2
                    y *= 256;
                    y += temp;
                    
                    // this can be sure that not b3
                    gettimeofday(&cur_time, NULL);
                    if(((cur_time.tv_sec - last_time.tv_sec) * 1000 + 
                        (cur_time.tv_usec - last_time.tv_usec) / 1000) > MSG_INTERVAL_MS) {
                        // only send out when there is enough interval
                        last_time.tv_sec = cur_time.tv_sec;
                        last_time.tv_usec = cur_time.tv_usec;
                        
                        LOG_DETAILS("PUBLISH %s/%s 0x%x %d %d", FLAG_KEY, serv_ip, cmd, x, y);
                        reply = redisCommand(gs_sync_context,"PUBLISH %s/%s %d/%d/%d", FLAG_KEY, serv_ip, cmd, x, y);
                        if(NULL == reply) {
                            LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);
                            goto l_free_redis;
                        }
                        if(NULL != reply->str) {
                            LOG_DETAILS("%s", reply->str);
                        }
                        freeReplyObject(reply);
                        
                        // clear cmd
                        cmd = 0;
                    }

                    buffer_state = 0;
                    break;
            }
        }
    } while(!gs_exit);
    
l_free_redis:
    redisFree(gs_sync_context);
    
l_socket_cleanup:
    to_close(gs_socket);
    gs_socket = -1;

    if(!gs_exit) {    
        LOG_ERROR("Monitor execution failed retry!");
        sleep(1);
        goto l_start;
    }
    
    LOG_INFO("exit!");
    return 0;
}
