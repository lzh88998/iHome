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

#define SERV_IP                 "192.168.100.100"
#define SERV_PORT               5000

#define REDIS_IP                "127.0.0.1"
#define REDIS_PORT              6379

#define REDIS_MESSAGE_TYPE      "pmessage"

#define FLAG_KEY                "cargador"
#define EXIT_FLAG_VALUE         "exit"

static int gs_socket = -1;

static redisAsyncContext *gs_async_context = NULL;

static int gs_exit = 0;

void getCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    char temp;
    if (reply == NULL) {
        if (c->errstr) {
            printf("get errstr: %s\n", c->errstr);
        }
        return;
    }
    
    printf("get type: %d\n", reply->type);
    printf("get result: %s\n", reply->str);
    printf("get elements: %zd\n", reply->elements);
    printf("get param: %ld\n", (long)privdata);
    
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
            printf("get received: 0x%x\n", temp);
        }
    }
}

void subscribeCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    if (reply == NULL) {
        if (c->errstr) {
            printf("subscribe errstr: %s\n", c->errstr);
        }
        return;
    }
    
    printf("sub reply type: %d\n", reply->type);
    printf("sub reply elements: %zd\n", reply->elements);
    switch(reply->type) {
        case REDIS_REPLY_ARRAY: 
            for(size_t i = 0; i < reply->elements; i++) {
                printf("sub Array element %zd: %s\n", i, reply->element[i]->str);
            }
            
            if(4 == reply->elements) { // subscribe element 0 is "message", element 1 is key pattern element 2 is key element 3 is value string
                if(NULL != reply->element[0] && NULL != reply->element[0]->str && 0 == strcmp(REDIS_MESSAGE_TYPE, reply->element[0]->str)) {
                    if(NULL != reply->element[2] && NULL != reply->element[3]) {
                        unsigned char status;
                        char *idx_start = NULL;
                        char *ch = reply->element[2]->str;
                        while(*ch) {
                            if('/' == *ch) {
                                idx_start = ch;
                            }
                            ch++;
                        }
                        
                        ++idx_start; // move to next pos;
                        if(0 == strcmp(EXIT_FLAG_VALUE, idx_start)) { // exit
                            printf("Exit flag found!\n");
                            redisAsyncDisconnect(c);
                            gs_exit = 1;
                        } else {
                            int idx = atoi(++idx_start);
                            status = (0 != strcmp("0", reply->element[3]->str) ? 0x00 : 0x20);
                            
                            printf("Index: %d Value: 0x%x\n", idx, status);
                            status |= (idx & 31);
                            
                            if(-1 == send(gs_socket, &status, 1, 0)) {
                                redisAsyncDisconnect(c);
                            } else if(-1 == recv(gs_socket, &status, 1, 0)) {
                                redisAsyncDisconnect(c);
                            }
                            else {
                                printf("Subscribe received: 0x%x\n", status);
                            }
                        }
                    }
                }
            }
        break;
        case REDIS_REPLY_ERROR:
        case REDIS_REPLY_VERB:
        case REDIS_REPLY_STRING:
        case REDIS_REPLY_BIGNUM:
            printf("sub argv[%s]: %s\n", (char*)privdata, reply->str);
        break;
        case REDIS_REPLY_DOUBLE:
            printf("sub Double %lf\n", reply->dval);
        break;
        case REDIS_REPLY_INTEGER:
            printf("sub Integer %lld\n", reply->integer);
        break;
        }

    printf("subscribe finished!\n");
}

void connectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        printf("Error: %s\n", c->errstr);
        return;
    }
    printf("Connected...\n");
}

void disconnectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        printf("Error: %s\n", c->errstr);
        return;
    }
    printf("Disconnected...\n");
}

void print_usage(int argc, char **argv) {
    if(0 == argc) {
        return;
    }
    
    printf("Invalid input parameters!\n");
    printf("Usage: (<optional parameters>)\n");
    printf("%s id controller_ip controller_port <redis_ip> <redis_port>\n", argv[0]);
    printf("E.g.:\n");
    printf("%s 1 192.168.100.100 5000 127.0.0.1 6379\n\n", argv[0]);
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
        printf("Error assigning address!\n");
        goto l_start;
    }
    
    gs_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(-1 == gs_socket) {
        printf("Error connecting to controller!\n");
        goto l_start;
    }
        
    printf("Socket: %d!\n", gs_socket);
    printf("Socket Family: %d\n", serv_addr.sin_family);
    printf("Socket Port: %d\n", serv_addr.sin_port);
    
    setsockopt(gs_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(struct timeval));
    setsockopt(gs_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(struct timeval));
    
    ret = 1;
    setsockopt(gs_socket, SOL_SOCKET, SO_KEEPALIVE, &ret, sizeof(ret));
    
    // Set non-blocking 
    if( (ret = fcntl(gs_socket, F_GETFL, NULL)) < 0) { 
        printf("Error fcntl(..., F_GETFL) (%s)\n", strerror(errno)); 
        goto l_socket_cleanup;
    } 
    ret |= O_NONBLOCK; 
    if( fcntl(gs_socket, F_SETFL, ret) < 0) { 
        printf("Error fcntl(..., F_SETFL) (%s)\n", strerror(errno)); 
        goto l_socket_cleanup;
    } 
    // Trying to connect with timeout 
    ret = connect(gs_socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)); 
    if (ret < 0) { 
        if (EINPROGRESS == errno) { 
            printf("EINPROGRESS in connect() - selecting\n"); 
            do { 
                FD_ZERO(&myset); 
                FD_SET(gs_socket, &myset); 
                ret = select(gs_socket+1, NULL, &myset, NULL, &timeout); 
                if (ret < 0 && EINTR != errno) { 
                    printf("Error connecting %d - %s\n", errno, strerror(errno)); 
                    exit(0); 
                } 
                else if (ret > 0) { 
                    // Socket selected for write 
                    lon = sizeof(int); 
                    if (getsockopt(gs_socket, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &lon) < 0) { 
                        printf("Error in getsockopt() %d - %s\n", errno, strerror(errno)); 
                        goto l_socket_cleanup;
                    } 
                    // Check the value returned... 
                    if (valopt) { 
                        printf("Error in delayed connection() %d - %s\n", valopt, strerror(valopt)); 
                        goto l_socket_cleanup;
                    } 
                    break; 
                } 
                else { 
                    printf("Timeout in select() - Cancelling!\n"); 
                    goto l_socket_cleanup;
                } 
            } while (1); 
        } 
        else { 
            printf("Error connecting %d - %s\n", errno, strerror(errno)); 
            goto l_socket_cleanup;
        } 
    } 
    // Set to blocking mode again... 
    if( (ret = fcntl(gs_socket, F_GETFL, NULL)) < 0) { 
        printf("Error fcntl(..., F_GETFL) (%s)\n", strerror(errno)); 
        goto l_socket_cleanup;
    } 
    ret &= (~O_NONBLOCK); 
    if( fcntl(gs_socket, F_SETFL, ret) < 0) { 
        printf("Error fcntl(..., F_SETFL) (%s)\n", strerror(errno)); 
        goto l_socket_cleanup;
    } 
  
    ret = recv(gs_socket, &temp, 1, 0);
    if(-1 == ret) {
        printf("Error receiving initial data! %d\n", ret);
        goto l_socket_cleanup;
    }
    
    printf("Connected to controller, remote socket: %d\n", temp);
    
    base = event_base_new();
    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, redis_ip, redis_port);
    options.connect_timeout = &timeout;

    gs_async_context = redisAsyncConnectWithOptions(&options);
    if (gs_async_context->err) {
        printf("Error: %s\n", gs_async_context->errstr);
        goto l_free_async_redis;
    }

    if(REDIS_OK != redisLibeventAttach(gs_async_context,base)) {
        printf("Error: error redis libevent attach!\n");
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
        printf("Subscriber execution failed retry!\n");
        goto l_start;
    }
    
    printf("exit!\n");
    return 0;
}
