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

#define REDIS_IP                "127.0.0.1"
#define REDIS_PORT              6379

#define REDIS_MESSAGE_TYPE      "pmessage"
#define FLAG_KEY                "cargador"
#define EXIT_FLAG_VALUE         "exit"

#define MAX_OUTPUT_PIN_COUNT    32

#define TRUE                    1
#define FALSE                   0

static redisAsyncContext *gs_async_context = NULL;
static to_socket_ctx gs_socket = -1;
static int gs_exit = 0;

#define CARGADOR_SND_RCV_ERROR      -1
#define CARGADOR_SND_RCV_OK         0

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
    
    if(0 > send(gs_socket, &status, 1, 0)) {
        LOG_ERROR("Send receive send command to controller failed! Error code: %s", strerror(errno));
        return CARGADOR_SND_RCV_ERROR;
    } else if(0 > recv(gs_socket, &status, 1, 0)) {
        LOG_ERROR("Send receive receive result from controller failed! Error code: %s", strerror(errno));
        return CARGADOR_SND_RCV_ERROR;
    }
    else {
        LOG_DETAILS("Send receive received: 0x%x", status);
    }
    
    return CARGADOR_SND_RCV_OK;
}

/*
 * During start phase, the cargador will get expected
 * status from redis and set the controller PINs to 
 * exptcted status
 * 
 * This callback function is used to do this task.
 * 
 * Parameters:
 * redisAsyncContext *c     Connection context to redis
 * void *r                  Response struct for redis returned values
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
void getCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    LOG_DEBUG("Get channel %ld callback!", (long)privdata);
    if (reply == NULL) {
        if (c->errstr) {
            redisAsyncDisconnect(c);
            LOG_ERROR("Get errstr: %s", c->errstr);
        }
        return;
    }
    
    LOG_DETAILS("Get type: %d", reply->type);
    LOG_DETAILS("Get result: %s", reply->str);
    LOG_DETAILS("Get elements: %zd", reply->elements);
    LOG_DETAILS("Get param: %ld", (long)privdata);
    
    if(NULL != reply->str) {
        if(CARGADOR_SND_RCV_OK != sendRecvCommand((long)privdata, reply->str)) {
            redisAsyncDisconnect(c);
            return;
        }
    }
    LOG_DEBUG("Get channel %ld callback finished!", (long)privdata);
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
    LOG_DEBUG("Subscribe callback!");
    if (reply == NULL) {
        if (c->errstr) {
            LOG_ERROR("Subscribe errstr: %s", c->errstr);
            redisAsyncDisconnect(c);
        }
        return;
    }
    
    LOG_DETAILS("Subscribe reply type: %d", reply->type);
    LOG_DETAILS("Subscribe reply elements: %zd", reply->elements);
    switch(reply->type) {
        case REDIS_REPLY_ARRAY: 
            for(size_t i = 0; i < reply->elements; i++) {
                LOG_DETAILS("Subscribe Array element %zd: %s", i, reply->element[i]->str);
            }
            
            if(4 == reply->elements) { 
                // subscribe element 0 is "pmessage", element 1 is key pattern element 2 is key element 3 is value string
                if(NULL != reply->element[0] && NULL != reply->element[0]->str && 0 == strcmp(REDIS_MESSAGE_TYPE, reply->element[0]->str)) {
                    if(NULL != reply->element[2] && NULL != reply->element[2]->str && NULL != reply->element[3] && NULL != reply->element[3]->str) {
                        char *idx_start = strrchr(reply->element[2]->str, '/');
                        if(NULL != idx_start) {
                            ++idx_start; // move to next pos;
                            if(0 == strcmp(EXIT_FLAG_VALUE, idx_start)) { // exit
                                if(0 != strcmp("0", reply->element[3]->str)) {
                                    LOG_DETAILS("Subscribe exit flag found!");
                                    gs_exit = 1;
                                    redisAsyncDisconnect(c);
                                } else {
                                    LOG_WARNING("Subscribe warning: Exit flag received iwht value %s!", reply->element[3]->str);
                                }
                            } else if(0 == strcmp(LOG_LEVEL_FLAG_VALUE, idx_start)) {
                                LOG_DETAILS("Subscribe log level flag found!");
                                if(0 > log_set_level(reply->element[3]->str)) {
                                    LOG_ERROR("Invalid log option: %s", reply->element[3]->str);
                                }
                            } else {
                                int idx = atoi(idx_start);
                                if(idx >= 0 && idx < MAX_OUTPUT_PIN_COUNT) {
                                    if(CARGADOR_SND_RCV_OK != sendRecvCommand(idx, reply->element[3]->str)) {
                                        redisAsyncDisconnect(c);
                                    }
                                } else {
                                    LOG_WARNING("Subscribe warning: Invalid input index %d!", idx);
                                }
                            }
                        } else {
                            LOG_ERROR("Subscribe error: Cannot find '/' in returned string!");
                        }
                    }
                }
            }
        break;
        case REDIS_REPLY_ERROR:
        case REDIS_REPLY_VERB:
        case REDIS_REPLY_STRING:
        case REDIS_REPLY_BIGNUM:
            LOG_DETAILS("Subscribe argv[%s]: %s", (char*)privdata, reply->str);
        break;
        case REDIS_REPLY_DOUBLE:
            LOG_DETAILS("Subscribe Double %lf", reply->dval);
        break;
        case REDIS_REPLY_INTEGER:
            LOG_DETAILS("Subscribe Integer %lld", reply->integer);
        break;
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
    printf("%s log_level controller_ip controller_port <redis_ip> <redis_port>\r\n", argv[0]);
    log_print_level_info();
    printf("E.g.:\r\n");
    printf("%s debug 192.168.100.100 5000 127.0.0.1 6379\r\n", argv[0]);
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
    struct timeval timeout = { 0, 100000}; 

    int ret;
    unsigned char temp = 0;
    
    const char* serv_ip;
    const char* redis_ip;
    int serv_port, redis_port;
    
    LOG_INFO("===================Service start!===================");
    LOG_INFO("Parsing parameters!");
    
    if(argc < 4) {
        print_usage(argc, argv);
        return -1;
    }
    
    if(0 > log_set_level(argv[1])) {
        print_usage(argc, argv);
        return -2;
    }
    
    serv_ip = argv[2];
    if(argc >= 5)
        redis_ip = argv[4];
    else
        redis_ip = REDIS_IP;
        
    serv_port = atoi(argv[3]);
    if(argc >= 6)
        redis_port = atoi(argv[5]);
    else
        redis_port = REDIS_PORT;
    
    gs_socket = to_connect(serv_ip, serv_port);
  
    ret = to_recv(gs_socket, &temp, 1, 0);
    if(0 > ret) {
        LOG_ERROR("Error receiving initial data! Error code: %s", strerror(errno));
        goto l_socket_cleanup;
    }
    
    LOG_INFO("Connected to controller, remote socket: %d", temp);
    
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

    redisAsyncSetConnectCallback(gs_async_context,connectCallback);
    redisAsyncSetDisconnectCallback(gs_async_context,disconnectCallback);

    for(long i = 0; i < 32; i++) { // apply current status to controller
        redisAsyncCommand(gs_async_context, getCallback, (void*)i, "GET %s/%s/%d", FLAG_KEY, argv[2], i);
    }

    redisAsyncCommand(gs_async_context, subscribeCallback, argv[1], "PSUBSCRIBE %s/%s/*", FLAG_KEY, argv[2]);
    event_base_dispatch(base);
    
l_free_async_redis:
    redisAsyncFree(gs_async_context);
    event_base_free(base);
    
l_socket_cleanup:
    close(gs_socket);
    gs_socket = -1;

    if(!gs_exit) {    
        LOG_ERROR("Execution failed retry!");
        goto l_start;
    }
    
    LOG_INFO("exit!");
    return 0;
}
