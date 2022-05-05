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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <hiredis.h>
#include <async.h>
#include <adapters/libevent.h>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <fcntl.h>

#include "log.h"

#define REDIS_IP                "127.0.0.1"
#define REDIS_PORT              6379

#define REDIS_MESSAGE_TYPE      "pmessage"
#define FLAG_KEY                "cargador"
#define EXIT_FLAG_VALUE         "exit"

static redisAsyncContext *gs_async_context = NULL;
static int gs_socket = -1;
static int gs_exit = 0;

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
    char temp;
    LOG_DEBUG("Get channel %ld callback!", (long)privdata);
    if (reply == NULL) {
        if (c->errstr) {
            LOG_ERROR("Get errstr: %s", c->errstr);
        }
        return;
    }
    
    LOG_DETAILS("Get type: %d", reply->type);
    LOG_DETAILS("Get result: %s", reply->str);
    LOG_DETAILS("Get elements: %zd", reply->elements);
    LOG_DETAILS("Get param: %ld", (long)privdata);
    
    if(NULL != reply->str) {
        temp = (0 != strcmp("0", reply->str) ? 0x00 : 0x20) | ((long)privdata & 31);
        if(-1 == send(gs_socket, &temp, 1, 0)) {
            redisAsyncDisconnect(c);
            return;
        }
        
        if(-1 == recv(gs_socket, &temp, 1, 0)) {
            redisAsyncDisconnect(c);
        }
        else {
            LOG_DETAILS("Get received: 0x%x", temp);
        }
    }
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
 *                          Because when the service is started, the 
 *                          service use psubscribe to subscribe chagnes
 *                          from redis, so the reply item usually has
 *                          4 elements 1 is "pmessage", second is 
 *                          subscribe pattern, third is actual topic, 
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
    if (reply == NULL) {
        if (c->errstr) {
            LOG_ERROR("Subscribe errstr: %s", c->errstr);
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
                // subscribe element 0 is "message", element 1 is key pattern element 2 is key element 3 is value string
                if(NULL != reply->element[0] && NULL != reply->element[0]->str && 0 == strcmp(REDIS_MESSAGE_TYPE, reply->element[0]->str)) {
                    if(NULL != reply->element[2] && NULL != reply->element[3]) {
                        unsigned char status;
                        char *idx_start = strrchr(reply->element[2]->str);
                        if(NULL != idx_start) {
                            ++idx_start; // move to next pos;
                            if(0 == strcmp(EXIT_FLAG_VALUE, idx_start)) { // exit
                                LOG_DETAILS("Exit flag found!");
                                redisAsyncDisconnect(c);
                                gs_exit = 1;
                            } else {
                                int idx = atoi(++idx_start);
                                status = (0 != strcmp("0", reply->element[3]->str) ? 0x00 : 0x20);
                                
                                printf("Index: %d Value: 0x%x", idx, status);
                                status |= (idx & 31);
                                
                                if(-1 == send(gs_socket, &status, 1, 0)) {
                                    redisAsyncDisconnect(c);
                                } else if(-1 == recv(gs_socket, &status, 1, 0)) {
                                    redisAsyncDisconnect(c);
                                }
                                else {
                                    printf("Subscribe received: 0x%x", status);
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
            LOG_DETAILS("sub argv[%s]: %s", (char*)privdata, reply->str);
        break;
        case REDIS_REPLY_DOUBLE:
            LOG_DETAILS("sub Double %lf", reply->dval);
        break;
        case REDIS_REPLY_INTEGER:
            LOG_DETAILS("sub Integer %lld", reply->integer);
        break;
        }

    printf("subscribe finished!");
}

void connectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        printf("Error: %s", c->errstr);
        return;
    }
    printf("Connected...");
}

void disconnectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        printf("Error: %s", c->errstr);
        return;
    }
    printf("Disconnected...");
}

void print_usage(int argc, char **argv) {
    if(0 == argc) {
        return;
    }
    
    printf("Invalid input parameters!");
    printf("Usage: (<optional parameters>)");
    printf("%s id controller_ip controller_port <redis_ip> <redis_port>", argv[0]);
    printf("E.g.:");
    printf("%s 1 192.168.100.100 5000 127.0.0.1 6379", argv[0]);
}

int main (int argc, char **argv) {
    
l_start:
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    struct event_base *base;
    struct sockaddr_in serv_addr;
    
    struct timeval timeout = { 0, 100000};
    
    int ret;
    unsigned char temp = 0;
    
    fd_set myset;
    int valopt; 
    socklen_t lon; 

    const char* serv_ip;
    const char* redis_ip;
    int serv_port, redis_port;
    
    if(argc < 4) {
        print_usage(argc, argv);
        return -1;
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
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(serv_port);
    ret = inet_pton(AF_INET, serv_ip, &serv_addr.sin_addr.s_addr);
    if(-1 == ret) {
        printf("Error assigning address!");
        goto l_start;
    }
    
    gs_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(-1 == gs_socket) {
        printf("Error connecting to controller!");
        goto l_start;
    }
        
    printf("Socket: %d!", gs_socket);
    printf("Socket Family: %d", serv_addr.sin_family);
    printf("Socket Port: %d", serv_addr.sin_port);
    
    setsockopt(gs_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(struct timeval));
    setsockopt(gs_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(struct timeval));
    
    ret = 1;
    setsockopt(gs_socket, SOL_SOCKET, SO_KEEPALIVE, &ret, sizeof(ret));
    
    // Set non-blocking 
    if( (ret = fcntl(gs_socket, F_GETFL, NULL)) < 0) { 
        printf("Error fcntl(..., F_GETFL) (%s)", strerror(errno)); 
        goto l_socket_cleanup;
    } 
    ret |= O_NONBLOCK; 
    if( fcntl(gs_socket, F_SETFL, ret) < 0) { 
        printf("Error fcntl(..., F_SETFL) (%s)", strerror(errno)); 
        goto l_socket_cleanup;
    } 
    // Trying to connect with timeout 
    ret = connect(gs_socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)); 
    if (ret < 0) { 
        if (EINPROGRESS == errno) { 
            printf("EINPROGRESS in connect() - selecting"); 
            do { 
                FD_ZERO(&myset); 
                FD_SET(gs_socket, &myset); 
                ret = select(gs_socket+1, NULL, &myset, NULL, &timeout); 
                if (ret < 0 && EINTR != errno) { 
                    printf("Error connecting %d - %s", errno, strerror(errno)); 
                    exit(0); 
                } 
                else if (ret > 0) { 
                    // Socket selected for write 
                    lon = sizeof(int); 
                    if (getsockopt(gs_socket, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &lon) < 0) { 
                        printf("Error in getsockopt() %d - %s", errno, strerror(errno)); 
                        goto l_socket_cleanup;
                    } 
                    // Check the value returned... 
                    if (valopt) { 
                        printf("Error in delayed connection() %d - %s", valopt, strerror(valopt)); 
                        goto l_socket_cleanup;
                    } 
                    break; 
                } 
                else { 
                    printf("Timeout in select() - Cancelling!"); 
                    goto l_socket_cleanup;
                } 
            } while (1); 
        } 
        else { 
            printf("Error connecting %d - %s", errno, strerror(errno)); 
            goto l_socket_cleanup;
        } 
    } 
    // Set to blocking mode again... 
    if( (ret = fcntl(gs_socket, F_GETFL, NULL)) < 0) { 
        printf("Error fcntl(..., F_GETFL) (%s)", strerror(errno)); 
        goto l_socket_cleanup;
    } 
    ret &= (~O_NONBLOCK); 
    if( fcntl(gs_socket, F_SETFL, ret) < 0) { 
        printf("Error fcntl(..., F_SETFL) (%s)", strerror(errno)); 
        goto l_socket_cleanup;
    } 
  
    ret = recv(gs_socket, &temp, 1, 0);
    if(-1 == ret) {
        printf("Error receiving initial data! %d", ret);
        goto l_socket_cleanup;
    }
    
    printf("Connected to controller, remote socket: %d", temp);
    
    base = event_base_new();
    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, redis_ip, redis_port);
    options.connect_timeout = &timeout;

    gs_async_context = redisAsyncConnectWithOptions(&options);
    if (gs_async_context->err) {
        printf("Error: %s", gs_async_context->errstr);
        goto l_free_async_redis;
    }

    if(REDIS_OK != redisLibeventAttach(gs_async_context,base)) {
        printf("Error: error redis libevent attach!");
        goto l_free_async_redis;
    }

    redisAsyncSetConnectCallback(gs_async_context,connectCallback);
    redisAsyncSetDisconnectCallback(gs_async_context,disconnectCallback);

    for(long i = 0; i < 32; i++) { // apply current status to controller
        redisAsyncCommand(gs_async_context, getCallback, (void*)i, "GET %s/%s/%d", FLAG_KEY, argv[1], i);
    }

    redisAsyncCommand(gs_async_context, subscribeCallback, argv[1], "PSUBSCRIBE %s/%s/*", FLAG_KEY, argv[1]);
    event_base_dispatch(base);
    
l_free_async_redis:
    redisAsyncFree(gs_async_context);
    event_base_free(base);
    
l_socket_cleanup:
    close(gs_socket);
    gs_socket = -1;

    if(!gs_exit) {    
        printf("Subscriber execution failed retry!");
        goto l_start;
    }
    
    printf("exit!");
    return 0;
}
