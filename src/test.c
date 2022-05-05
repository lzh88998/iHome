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

#define SERV_IP         "192.168.100.100"
#define SERV_PORT       5000

#define REDIS_IP        "127.0.0.1"
#define REDIS_PORT      6379

static int gs_socket = -1;

static redisContext *gs_sync_context = NULL;
static redisAsyncContext *gs_async_context = NULL;

static redisReply* gs_reply = NULL;

void setCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    if (reply == NULL) {
        if (c->errstr) {
            printf("errstr: %s\n", c->errstr);
        }
        return;
    }

    printf("reply type: %d\n", reply->type);
    printf("reply elements: %d\n", reply->elements);
    switch(reply->type) {
        case REDIS_REPLY_ERROR:
        case REDIS_REPLY_VERB:
        case REDIS_REPLY_STRING:
        case REDIS_REPLY_BIGNUM:
            printf("argv[%s]: %s\n", (char*)privdata, reply->str);
        break;
        case REDIS_REPLY_ARRAY:
            for(int i = 0; i < reply->elements; i++) {
                printf("Array element %d: %s\n", i, reply->element[i]->str);
            }
                
            if(3 == reply->elements && NULL != reply->element[2]->str && 0 == strcmp("exit", reply->element[2]->str)) {
                redisAsyncDisconnect(c);
            }
        break;
        case REDIS_REPLY_DOUBLE:
            printf("Double %lf\n", reply->dval);
        break;
        case REDIS_REPLY_INTEGER:
            printf("Integer %ld\n", reply->integer);
        break;
        }
}

void getCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    char temp;
    if (reply == NULL) {
        if (c->errstr) {
            printf("errstr: %s\n", c->errstr);
        }
        return;
    }
    
    printf("get type: %d\n", reply->type);
    printf("get elements: %d\n", reply->elements);
    printf("get param: %d\n", (long)privdata);
    
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
            printf("errstr: %s\n", c->errstr);
        }
        return;
    }
    
    printf("sub reply type: %d\n", reply->type);
    printf("sub reply elements: %d\n", reply->elements);
    switch(reply->type) {
        case REDIS_REPLY_ERROR:
        case REDIS_REPLY_VERB:
        case REDIS_REPLY_STRING:
        case REDIS_REPLY_BIGNUM:
            printf("sub argv[%s]: %s\n", (char*)privdata, reply->str);
        break;
        case REDIS_REPLY_ARRAY: 
            for(int i = 0; i < reply->elements; i++) {
                printf("sub Array element %d: %s\n", i, reply->element[i]->str);
            }
            
            if(4 == reply->elements) { // subscribe element 0 is "message", element 1 is key pattern element 2 is key element 3 is value string
                if(NULL != reply->element[0]->str && 0 == strcmp("pmessage", reply->element[0]->str)) {
                    printf("GET %s\n", reply->element[2]->str);
                    
                    gs_reply = redisCommand(gs_sync_context,"GET %s", reply->element[2]->str);
                    if(NULL == gs_reply) {
                        printf("Failed to sync query redis %s\n", gs_sync_context->errstr);
                        redisAsyncDisconnect(c);
                    }
                    printf("RESULT: %s\n", gs_reply->str);
                    
                    if(NULL != gs_reply->str) {
                        unsigned char status = 0 == strcmp("0", gs_reply->str) ? 0x00 : 0x20;
                        char *idx_start = NULL;
                        char *ch = reply->element[2]->str;
                        while(*ch) {
                            if('/' == *ch) {
                                idx_start = ch;
                            }
                            ch++;
                        }
                        
                        int idx = atoi(++idx_start);
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
                    freeReplyObject(gs_reply);
                }
                printf("Validating exit!\n");
                if(NULL != reply->element[3]->str && 0 == strcmp("exit", reply->element[3]->str)) {
                    redisAsyncDisconnect(c);
                }
            }
        break;
        case REDIS_REPLY_DOUBLE:
            printf("sub Double %lf\n", reply->dval);
        break;
        case REDIS_REPLY_INTEGER:
            printf("sub Integer %ld\n", reply->integer);
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
    printf("Invalid input parameters!\n");
    printf("Usage: (<optional parameters>)\n");
    printf("%s redis_topic controller_ip controller_port <redis_ip> <redis_port>\n");
    printf("E.g.:\n");
    printf("%s controller/1 192.168.100.100 5000 127.0.0.1 6379\n\n");
}

int main (int argc, char **argv) {
    
l_start:
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    struct event_base *base;
    struct sockaddr_in serv_addr;
    
    int ret;
    unsigned char temp = 0;

    char* serv_ip;
    char* redis_ip;
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
    
    ret = connect(gs_socket, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    
    if(-1 == ret) {
        printf("Error connecting to controller! %d\n", ret);
        goto l_socket_cleanup;
    }
    
    ret = recv(gs_socket, &temp, 1, 0);
    if(-1 == ret) {
        printf("Error receiving initial data! %d\n", ret);
        goto l_socket_cleanup;
    }
    
    printf("Connected to controller, remote socket: %d\n", temp);
    
    struct timeval timeout = { 1, 500000 }; // 1.5 seconds
    gs_sync_context = redisConnectWithTimeout(redis_ip, redis_port, timeout);
    if(NULL == gs_sync_context) {
        printf("Connection error: can't allocate redis context\n");
        goto l_socket_cleanup;
    }
    
    if(gs_sync_context->err) {
        printf("Connection error: %s\n", gs_sync_context->errstr);
        goto l_free_sync_redis;
    }

    gs_reply = redisCommand(gs_sync_context,"PING");
    if(NULL == gs_reply) {
        printf("Failed to sync query redis %s\n", gs_sync_context->errstr);
        goto l_free_sync_redis;
    }
    printf("PING: %s\n", gs_reply->str);
    freeReplyObject(gs_reply);
    
    base = event_base_new();
    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, redis_ip, redis_port);
    struct timeval tv = {0};
    tv.tv_sec = 1;
    options.connect_timeout = &tv;

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
        redisAsyncCommand(gs_async_context, getCallback, (void*)i, "GET %s/%d", argv[1], i);
    }

    redisAsyncCommand(gs_async_context, subscribeCallback, argv[1], "PSUBSCRIBE %s/*", argv[1]);
    event_base_dispatch(base);
    
l_free_sync_redis:
    redisFree(gs_sync_context);
    
l_free_async_redis:
    redisAsyncFree(gs_async_context);
    event_base_free(base);
    printf("exit!\n");
    
l_socket_cleanup:
    close(gs_socket);
    gs_socket = -1;
    
    goto l_start;
    
    return 0;
}
