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

#define FLAG_KEY                "lcd"
#define EXIT_FLAG_VALUE         "exit"
#define LOG_LEVEL_FLAG_KEY      "log_level"
#define BRIGHTNESS_TOPIC        "brightness"
#define SWITCH_TOPIC            "switch"
#define TARGET_TEMP_TOPIC       "target_temp"

#define RECEIVE_STATE_CMD       0
#define RECEIVE_STATE_X_FIRST   1
#define RECEIVE_STATE_X_SECOND  2
#define RECEIVE_STATE_Y_FIRST   3
#define RECEIVE_STATE_Y_SECOND  4
#define RECEIVE_STATE_FINISHED  5

#define PROCESS_CLICK_OK        0
#define PROCESS_CLICK_FAILED    -1

#define TOUCH_MAX_SW_CNT        6

#define LCD_ACTIVE_BACKLIGHT    0
#define LCD_IDLE_BACKLIGHT      255

static int gs_socket = -1;
static redisContext *gs_sync_context = NULL;
static char* serv_ip;

static int gs_exit = 0;

static char gs_sw_status[TOUCH_MAX_SW_CNT];
static unsigned char target_temp;
static redisReply *gs_sw_config = NULL;
static redisReply *gs_target_temp_topic = NULL;

#define EXEC_REDIS_CMD(reply, goto_label, cmd, ...)		LOG_DEBUG(cmd, ##__VA_ARGS__);\
                                                        reply = redisCommand(gs_sync_context, cmd, ##__VA_ARGS__);\
                                                        if(NULL == reply) {\
                                                            LOG_ERROR("Failed to sync query redis %s", gs_sync_context->errstr);\
                                                            LOG_ERROR(cmd, ##__VA_ARGS__);\
                                                            goto goto_label;\
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
    printf("%s 192.168.100.100 5001\n\n", argv[0]);
    printf("%s 192.168.100.100 5001 debug\n\n", argv[0]);
    printf("%s 192.168.100.100 5001 127.0.0.1 6379\n\n", argv[0]);
    printf("%s 192.168.100.100 5001 debug 127.0.0.1 6379\n\n", argv[0]);
}

/*
 * When click on the touch screen is received, the coordinates of the 
 * clicked pixel is passed to process_click function through x and y. 
 * The process_click function will publish event according to given
 * input
 * 
 * Parameters:
 * unsigned int x           The x coordinate of clicked position
 * unsigned int y           The y coordinate of clicked position
 * 
 * Return value:
 * 0                        Execution Successful
 * Others                   Failed
 */
int process_click(unsigned int x, unsigned int y) {
    redisReply* reply = NULL;
    if(160 > x) {
        if(180 < y) {
            unsigned char bDirty = 0;
            if(target_temp > 150) {
                LOG_WARNING("Target temperature reached low limit");
                target_temp = 150;
                bDirty = 1;
            }
            
            if(target_temp < 100) {
                LOG_WARNING("Target temperature reached high limit");
                target_temp = 100;
                bDirty = 1;
            }
                
            if(x >= 80 && target_temp > 100) {
                LOG_DETAILS("Increasing target temperature");
                target_temp--;
                bDirty = 1;
            }
            
            if(x < 80 && target_temp < 150) {
                LOG_DETAILS("Decreasing target temperature");
                target_temp++;
                bDirty = 1;
            }

            if(bDirty) {
                EXEC_REDIS_CMD(reply, l_process_click_failed, "PUBLISH %s %d", gs_target_temp_topic->str, target_temp);
                freeReplyObject(reply);
                reply = NULL;
            }

            freeReplyObject(reply);
        } else {
            // clicked on info area, do nothing
        }
    } else {
        int clicked_idx = -1;
        switch(gs_sw_config->elements) {
            case 1:
                // publish clicked event to only topic
                clicked_idx = 0;
                break;
            case 2:
                if(y < 120) {
                    clicked_idx = 0;
                } else {
                    clicked_idx = 1;
                }
                break;
            case 3:
                if(y < 80) {
                    clicked_idx = 0;
                } else if(y < 160) {
                    clicked_idx = 1;
                } else {
                    clicked_idx = 2;
                }
                break;
            case 4:
                if(y < 120) {
                    if(x < 240) {
                        clicked_idx = 0;
                    } else {
                        clicked_idx = 1;
                    }
                } else {
                    if(x < 240) {
                        clicked_idx = 2;
                    } else {
                        clicked_idx = 3;
                    }
                }
                break;
            case 5:
                if(y < 80) {
                    if(x < 240) {
                        clicked_idx = 0;
                    } else {
                        clicked_idx = 1;
                    }
                } else if(y < 160) {
                    if(x < 240) {
                        clicked_idx = 2;
                    } else {
                        clicked_idx = 3;
                    }
                } else {
                    if(x < 240) {
                        clicked_idx = 4;
                    } else {
                    }
                }
                break;
            case 6:
                if(y < 80) {
                    if(x < 240) {
                        clicked_idx = 0;
                    } else {
                        clicked_idx = 1;
                    }
                } else if(y < 160) {
                    if(x < 240) {
                        clicked_idx = 2;
                    } else {
                        clicked_idx = 3;
                    }
                } else {
                    if(x < 240) {
                        clicked_idx = 4;
                    } else {
                        clicked_idx = 5;
                    }
                }
                break;
            default:
                // do nothing if the config is invalid
                break;
        }

        if(clicked_idx >= 0){
            // get sw key
            redisReply *reply2 = NULL;
            EXEC_REDIS_CMD(reply, l_process_click_failed, "HGET %s/%s/%s %s", FLAG_KEY, serv_ip, SWITCH_TOPIC, gs_sw_config->element[clicked_idx]->str);
            gs_sw_status[clicked_idx] = !gs_sw_status[clicked_idx];
            EXEC_REDIS_CMD(reply, l_process_click_failed, "PUBLISH %s %d", reply->str, gs_sw_status[clicked_idx]);
            freeReplyObject(reply2);
            reply2 = NULL;
            freeReplyObject(reply);
            reply = NULL;
        }
    }
    
    return PROCESS_CLICK_OK;

l_process_click_failed:
    if(reply) {
        freeReplyObject(reply);
        reply = NULL;
    }
        
    return PROCESS_CLICK_FAILED;
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
    
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    unsigned char temp, cur_state = 0;
    unsigned char cmd = 0, active = 0, inactive_cnt = 0;
    unsigned int  x = 0, y = 0;
    
    int serv_port = 0;
    const char* redis_ip;
    int redis_port = REDIS_PORT;
    
    struct timeval timeout = { 0, 100000 }; 

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
    cur_state = 0;
    x = 0;
    y = 0;
    cmd = 0;
    active = 0;
    inactive_cnt = 0;
    
    LOG_DETAILS("Initializing switch status!");
    for(int i = 0; i < TOUCH_MAX_SW_CNT; i++) {
        gs_sw_status[i] = 0;
    }

    LOG_DETAILS("Connecting to touch controller %s %d!", serv_ip, serv_port);
    gs_socket = to_connect(serv_ip, serv_port);
    if(0 > gs_socket) {
        LOG_ERROR("Error creating socket!");
        goto l_socket_cleanup;
    }
    
    // Receive initial bytes to activate W5500 keep alive
    if(0 > to_recv(gs_socket, &temp, 1, 0)) {
        LOG_ERROR("Error receiving initial data! %s", strerror(errno));
        goto l_socket_cleanup;
    }

    LOG_DETAILS("Connected to touch controller, remote socket: %d", temp);

    LOG_DETAILS("Connecting to redis %s %d!", redis_ip, redis_port);
    gs_sync_context = redisConnectWithTimeout(redis_ip, redis_port, timeout);
    if(NULL == gs_sync_context) {
        LOG_ERROR("Connection error: can't allocate redis context");
        goto l_socket_cleanup;
    }
    
    if(gs_sync_context->err) {
        LOG_ERROR("Connection error: %s", gs_sync_context->errstr);
        goto l_free_redis;
    }

    LOG_DETAILS("Testing redis connection!");

    // Test redis connection
    redisReply* reply = NULL;
    
    EXEC_REDIS_CMD(reply, l_free_redis, "PING");
    LOG_DEBUG("PING: %s", reply->str);
    freeReplyObject(reply);
    reply = NULL;
    
    LOG_DETAILS("Getting switch config!");
    EXEC_REDIS_CMD(gs_sw_config, l_free_redis, "HKEYS %s/%s/%s", FLAG_KEY, serv_ip, SWITCH_TOPIC);
    
    LOG_DETAILS("Loading switch status!");
    redisReply *reply2 = NULL;
    for(size_t i = 0; i < gs_sw_config->elements; i++) {
        EXEC_REDIS_CMD(reply, l_free_redis_reply, "HGET %s/%s/%s %s", FLAG_KEY, serv_ip, SWITCH_TOPIC, gs_sw_config->element[i]->str);
        EXEC_REDIS_CMD(reply2, l_free_redis_reply, "GET %s", reply->str);
        if(reply2->str && 0 == strcmp("1", reply2->str)) {
            gs_sw_status[i] = 1;
        }
        freeReplyObject(reply2);
        reply2 = NULL;
        freeReplyObject(reply);
        reply = NULL;
    }
    
    LOG_DETAILS("Loading target temperature!");
    EXEC_REDIS_CMD(gs_target_temp_topic, l_free_redis_reply, "GET %s/%s/%s", FLAG_KEY, serv_ip, TARGET_TEMP_TOPIC);
    
    EXEC_REDIS_CMD(reply, l_free_redis_reply, "GET %s", gs_target_temp_topic->str);
    
    if(reply->str) {
        int t = atoi(reply->str);
        if(t >= 150) {
            target_temp = 150;
        } else if(t <= 100) {
            target_temp = 100;
        } else {
            target_temp = t;
        }
    } else {
        target_temp = 127;
    }
    
    freeReplyObject(reply);
    reply = NULL;
    
    LOG_DETAILS("Publish current target temperture!");
    EXEC_REDIS_CMD(reply, l_free_redis_reply, "PUBLISH %s %d", gs_target_temp_topic->str, target_temp);
    freeReplyObject(reply);
    reply = NULL;
    
    LOG_DETAILS("Set LCD backlight to max!");
    EXEC_REDIS_CMD(reply, l_free_redis_reply, "PUBLISH %s/%s/%s %d", FLAG_KEY, serv_ip, BRIGHTNESS_TOPIC, LCD_ACTIVE_BACKLIGHT);
    freeReplyObject(reply);
    reply = NULL;
    
    active = 1;

    LOG_DETAILS("Start loop!");
    
    // main loop
    while(!gs_exit) {
        LOG_DETAILS("Start Receiving");
        if(0 > to_recv(gs_socket, &temp, 1, 0)) {
            if(EAGAIN != errno && EWOULDBLOCK != errno) {
                // recv error
                LOG_ERROR("Error receive bytes %s", strerror(errno));
                goto l_free_redis_reply;
            } 
            
            LOG_DETAILS("Receive timeout!");
            LOG_DETAILS("Check log level!");
            EXEC_REDIS_CMD(reply, l_free_redis_reply, "GET %s/%s/%s", FLAG_KEY, serv_ip, LOG_LEVEL_FLAG_KEY);
            if(NULL != reply->str) {
                if(LOG_SET_LEVEL_OK != log_set_level(reply->str)) {
                    LOG_WARNING("Invalid log option: %s", reply->str);
                }

                freeReplyObject(reply);
                reply = NULL;
                
                EXEC_REDIS_CMD(reply, l_free_redis_reply, "DEL %s/%s/%s", FLAG_KEY, serv_ip, LOG_LEVEL_FLAG_KEY);
            }

            freeReplyObject(reply);
            reply = NULL;

            LOG_DETAILS("Check exit flags!");
            EXEC_REDIS_CMD(reply, l_free_redis_reply, "GET %s/%s/%s", FLAG_KEY, serv_ip, EXIT_FLAG_VALUE);
            if(NULL != reply->str) {
                if(0 == strcmp(EXIT_FLAG_VALUE, reply->str)) {
                    gs_exit = 1;
                }

                freeReplyObject(reply);
                reply = NULL;
                
                EXEC_REDIS_CMD(reply, l_free_redis_reply, "DEL %s/%s/%s", FLAG_KEY, serv_ip, EXIT_FLAG_VALUE);
            }

            freeReplyObject(reply);
            reply = NULL;
            
            LOG_DETAILS("Check touch controller idle time!");
            if(active) {
                if(200 == inactive_cnt) {
                    LOG_DETAILS("Touch idle timeout reduce backlight of LCD!");
                    active = 0;
                    EXEC_REDIS_CMD(reply, l_free_redis_reply, "PUBLISH %s/%s/%s %d", FLAG_KEY, serv_ip, BRIGHTNESS_TOPIC, LCD_IDLE_BACKLIGHT);
                    freeReplyObject(reply);
                    reply = NULL;
                } else {
                    inactive_cnt++;
                }
            }
        } else {
            LOG_DETAILS("Receiving data!");
            switch(cur_state) {
                case RECEIVE_STATE_CMD:
                    cmd = temp;
                    break;
                case RECEIVE_STATE_X_FIRST:
                    x = temp;
                    break;
                case RECEIVE_STATE_X_SECOND:
                    x *= 256;
                    x += temp;
                    break;
                case RECEIVE_STATE_Y_FIRST:
                    y = temp;
                    break;
                case RECEIVE_STATE_Y_SECOND:
                    y *= 256;
                    y += temp;
                    break;
            }
            
            cur_state++;
            if(RECEIVE_STATE_FINISHED == cur_state) {
                cur_state = RECEIVE_STATE_CMD;
                
                // May need to change to publish switch or increase/decrease target temperature
                LOG_DETAILS("Input %#X %d %d", cmd, x, y);
                if(0xB1 == cmd) {
                    inactive_cnt = 0;
                    if(active) {
                        LOG_DETAILS("Processing %#X %d %d", cmd, x, y);
                        process_click(x, y);
                    } else {
                        LOG_DETAILS("LCD backlight is off, update backlight!");
                        EXEC_REDIS_CMD(reply, l_free_redis_reply, "PUBLISH %s/%s/%s %d", FLAG_KEY, serv_ip, BRIGHTNESS_TOPIC, LCD_ACTIVE_BACKLIGHT);
                        freeReplyObject(reply);
                        reply = NULL;
                        active = 1;
                    }
                }
            }
        }
    }
    
l_free_redis_reply:
    if(reply) {
        freeReplyObject(reply);
        reply = NULL;
    }
    
    if(reply2) {
        freeReplyObject(reply2);
        reply2 = NULL;
    }

    if(gs_sw_config) {
        freeReplyObject(gs_sw_config);
        gs_sw_config = NULL;
    }
    
    if(gs_target_temp_topic) {
        freeReplyObject(gs_target_temp_topic);
        gs_target_temp_topic = NULL;
    }

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
