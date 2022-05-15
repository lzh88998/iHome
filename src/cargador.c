/*
 * Copyright lzh88998 and distributed under Apache 2.0 license
 * 
 * Cargador is a micro service that will connect to a custom build
 * controller device through ethernet and sync the status specified
 * in redis memroy store to the controller's output pin.
 * 
 * The data format is as following:
 * 
 * 1 byte per message
 * 
 * 0x80: 
 * System configuration flag, will set this bit when set system 
 * configuration like IP address or query current status. When 
 * the controller received any value between 0x80 - 0x9F, it will 
 * transit into receiving IP address configuration mode. another 
 * 18 bytes transfered will be stored as IP address information.
 * first 4 bytes are Gateway, followed by 4 bytes network mask,
 * followed by 6 bytes MAC address, followed by 4 bytes IP address
 * 
 * 0x40: 
 * Query status flag. When this bit is set to 1 then the controller
 * will return the current status for PIN number givened in 0x1F. 
 * The input 0x20 flag will be ignored when 0x40 flag is set to 1.
 * The current status will be returned using 0x20 flag. When 0x20 
 * flag is set to 1 means the output pin is in off state ("0" in 
 * redis) and when0x20 flag is set to 0 means the output pin is in 
 * on state. ("1" in redis)
 * 
 * Note: in some old version of controller firmware, is bit may
 * either used combine with 0x80 or without 0x80 to ensure correct
 * behavior. May fix this issue in 2022
 * 
 * 0x20: 
 * When 0x80 and 0x40 flags are not set, this flag is used to set
 * new status for controller's output pin or return current status
 * of controller's output pin. When 0x20 flag is set to 1 then the 
 * IO output pin is in off state ("0" in redis), and when 0x20 flag
 * is set to 0 then the IO output pin is in on state ("1" in redis).
 * 
 * 0x1F: 
 * This part contains the output PIN number which is from 0 to 31.
 * The controller set the corresponding output PIN status according  
 * to the status given in 0x20 flag.
 *  
 * data gram examples:
 * 
 * Set pin 7 on: 0x07
 * 
 * Set pin 7 off: 0x27
 * 
 * Set ip address: 
 * 0x80                             Set controller in configuration 
 *                                  mode     
 * 0xC0 0xA8 0x64 0xFE              Send gateway
 * 0xFF 0xFF 0xFF 0x00              Send network mask
 * 0x0C 0x29 0xAB 0x7D 0x01 0xA0    Send MAC address
 * 0xC0 0xA8 0x64 0x64              Send IP address
 * 
 * Note:
 * When connect to the controller, it will send back the connected
 * socket index number on the controller. The controller may have
 * up to 8 sockets so the number range is from 0-7.
 * Also sending back the number will enable the keep alive feature
 * on the controller.
 * 
 */

#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "hiredis.h"
#include "async.h"
#include "adapters/libevent.h"

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <fcntl.h>

#include "log.h"
#include "to_socket.h"

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
#define FLAG_KEY                "cargador"

/*
 * Available port count of controller
 */
#define MAX_OUTPUT_PIN_COUNT    32

/*
 * Context for redis connection and controller
 * network connection
 */
static redisAsyncContext *gs_async_context = NULL;
static to_socket_ctx gs_socket = -1;

/*
 * Flag for micro srevice exit event, when set 
 * to 1 the micro service will not restart
 * automatically, but exit
 */
static int gs_exit = 0;

/*
 * Return value for sending command to controller
 */
#define CARGADOR_SND_RCV_ERROR      -1
#define CARGADOR_SND_RCV_OK         0

/*
 * Used to pass parameters to subscribeCallback
 */
static const char* gs_idx_name[MAX_OUTPUT_PIN_COUNT] =  {
                                                            "0", "1", "2", "3",
                                                            "4", "5", "6", "7",
                                                            "8", "9", "10", "11",
                                                            "12", "13", "14", "15",
                                                            "16", "17", "18", "19",
                                                            "20", "21", "22", "23",
                                                            "24", "25", "26", "27",
                                                            "28", "29", "30", "31",
                                                        };

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
 * Send the command to controller and receive feedback
 * internal check the idx range to ensure no out of
 * bound message is sent to controller
 * 
 * Parameters:
 * long idx                 index of output PIN
 * char* v                  "0" means off other values
 *                          means on
 * 
 * Return value:            -1 means failed to send or
 *                          receive. 0 means OK
 * 
 * Note: when -1 is returned, outer program code need
 * to deal with socket close and reinitialization as
 * this usually caused by socket failure.
 * 
 */
int sendRecvCommand(long idx, const char* v) {
    unsigned char status;
    if(idx >= MAX_OUTPUT_PIN_COUNT) {
        LOG_WARNING("Send receive warning, index %ld out of bound!", idx);
        return CARGADOR_SND_RCV_OK;
    }
        
    status = (0 != strcmp("0", v) ? 0x00 : 0x20);
    
    LOG_DETAILS("Send receive index: %d Value: 0x%x", idx, status);
    status |= (idx & 31);
    
    if(0 > to_send(gs_socket, &status, 1, 0)) {
        LOG_ERROR("Send receive send command to controller failed! Error code: %s", strerror(errno));
        return CARGADOR_SND_RCV_ERROR;
    } else if(0 > to_recv(gs_socket, &status, 1, 0)) {
        LOG_ERROR("Send receive receive result from controller failed! Error code: %s", strerror(errno));
        return CARGADOR_SND_RCV_ERROR;
    }
    else {
        LOG_DETAILS("Send receive received: 0x%x", status);
    }
    
    return CARGADOR_SND_RCV_OK;
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
    char* ch = (char*)privdata;
    
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
    LOG_DETAILS("Subscribe parameters: %s", ch);

    if(3 != reply->elements) {
        LOG_ERROR("Error: Unexpected format!");
        redisAsyncDisconnect(c);
        return;
    }
    
    if(NULL == reply->element[2] || NULL == reply->element[2]->str) {
        LOG_WARNING("Error: Empty content!");
        return;
    }
    
    if(CARGADOR_SND_RCV_OK != sendRecvCommand(atoi(ch), reply->element[2]->str)) {
        LOG_ERROR("Error: failed to update status for PIN %d, %s", atoi(ch), reply->element[2]->str);
        redisAsyncDisconnect(c);
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
    printf("%s controller_ip controller_port <log_level> <redis_ip> <redis_port>\r\n", argv[0]);
    log_print_level_info();
    printf("E.g.:\r\n");
    printf("%s 192.168.100.100 5000\r\n", argv[0]);
    printf("%s 192.168.100.100 5000 debug\r\n", argv[0]);
    printf("%s 192.168.100.100 5000 127.0.0.1 6379\r\n", argv[0]);
    printf("%s 192.168.100.100 5000 debug 127.0.0.1 6379\r\n", argv[0]);
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
    
l_start:
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    struct event_base *base;
    struct timeval timeout = { 0, 100000 }; 

    int ret;
    unsigned char temp = 0;
    
    const char* serv_ip;
    const char* redis_ip;
    int serv_port, redis_port;
    
    redisContext *sync_context = NULL;
    
    LOG_INFO("===================Service start!===================");
    LOG_INFO("Parsing parameters!");
        
    redis_ip = REDIS_IP;
    redis_port = REDIS_PORT;
    
    switch(argc) {
        case 6:
            if(LOG_SET_LEVEL_OK != log_set_level(argv[3])) {
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
            if(LOG_SET_LEVEL_OK != log_set_level(argv[3])) {
                print_usage(argc, argv);
                return -3;
            }
            serv_port = atoi(argv[2]);
            serv_ip = argv[1];
            break;
        case 3:
            serv_port = atoi(argv[2]);
            serv_ip = argv[1];
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
  
    ret = to_recv(gs_socket, &temp, 1, 0);
    if(0 > ret) {
        LOG_ERROR("Error receiving initial data! Error code: %s", strerror(errno));
        goto l_socket_cleanup;
    }
    
    LOG_INFO("Connected to controller, remote socket: %d", temp);
    
    sync_context = redisConnectWithTimeout(redis_ip, redis_port, timeout);
    if(NULL == sync_context) {
        LOG_ERROR("Connection error: can't allocate redis context");
        goto l_exit;
    }
    
    if(sync_context->err) {
        LOG_ERROR("Connection error: %s", sync_context->errstr);
        goto l_free_sync_redis;
    }

    redisReply* reply = redisCommand(sync_context,"PING");
    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s", sync_context->errstr);
        goto l_free_sync_redis;
    }
    LOG_DEBUG("PING: %s", reply->str);
    freeReplyObject(reply);

    LOG_INFO("Connected to redis in sync mode");
    
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
    
    LOG_DETAILS("GET %s/%s/%s", FLAG_KEY, serv_ip, LOG_LEVEL_FLAG_VALUE);
    reply = redisCommand(sync_context,"GET %s/%s/%s", FLAG_KEY, serv_ip, LOG_LEVEL_FLAG_VALUE);

    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s", sync_context->errstr);
        goto l_free_async_redis;
    }
    
    if(LOG_SET_LEVEL_OK != log_set_level(reply->str)) {
        LOG_WARNING("Failed to set log level %s", reply->str);
    }

    freeReplyObject(reply);

    for(int i = 0; i < MAX_OUTPUT_PIN_COUNT; i++ ) {
        LOG_DETAILS("HGET %s/%s %d", FLAG_KEY, serv_ip, i);
        reply = redisCommand(sync_context,"HGET %s/%s %d", FLAG_KEY, serv_ip, i);
        if(NULL == reply) {
            LOG_ERROR("Failed to sync query redis %s", sync_context->errstr);
            goto l_free_async_redis;
        }
        
        LOG_DETAILS("HGET Result: %s", reply->str);
        
        if(NULL != reply->str) {
            LOG_DETAILS("GET %s", reply->str);
            redisReply* reply2 = redisCommand(sync_context,"GET %s", reply->str);
            
            if(NULL == reply2) {
                LOG_ERROR("Failed to sync query redis %s", sync_context->errstr);
                freeReplyObject(reply);
                goto l_free_async_redis;
            }
            
            LOG_DETAILS("GET %s", reply2->str);
            
            if(CARGADOR_SND_RCV_OK != sendRecvCommand(i, reply2->str)) {
                LOG_ERROR("Error updating controller status");
                freeReplyObject(reply);
                freeReplyObject(reply2);
                goto l_free_async_redis;
            }
            
            LOG_DETAILS("SUBSCRIBE %s", reply->str);
            redisAsyncCommand(gs_async_context, subscribeCallback, (void*)gs_idx_name[i], "SUBSCRIBE %s", reply->str);
            freeReplyObject(reply2);
        }
        
        freeReplyObject(reply);
    }

    redisFree(sync_context);
    sync_context = NULL;
    
    redisAsyncCommand(gs_async_context, exitCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, EXIT_FLAG_VALUE);
    redisAsyncCommand(gs_async_context, resetCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, RESET_FLAG_VALUE);
    redisAsyncCommand(gs_async_context, setLogLevelCallback, NULL, "SUBSCRIBE %s/%s/%s", FLAG_KEY, serv_ip, LOG_LEVEL_FLAG_VALUE);
    
    event_base_dispatch(base);
    
l_free_async_redis:
    redisAsyncFree(gs_async_context);
    event_base_free(base);
    
l_free_sync_redis:
    if(NULL != sync_context) {
        redisFree(sync_context);
        sync_context = NULL;
    }
    
l_socket_cleanup:
    to_close(gs_socket);
    gs_socket = -1;

l_exit:
    if(!gs_exit) {    
        LOG_ERROR("Execution failed retry!");
        goto l_start;
    }
    
    LOG_INFO("exit!");
    return 0;
}
