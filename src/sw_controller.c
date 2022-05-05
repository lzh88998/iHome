#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <hiredis.h>
#include <async.h>
#include <adapters/libevent.h>

#define REDIS_IP                "127.0.0.1"
#define REDIS_PORT              6379

#define REDIS_MESSAGE_TYPE      "pmessage"

#define EXIT_FLAG_KEY           "godown_keeper"
#define EXIT_FLAG_VALUE         "exit"

#define SND_RCV_OK              0

static redisContext *gs_sync_context = NULL;
static redisAsyncContext *gs_async_context = NULL;

static int gs_exit = 0;

static char* enable_topic = NULL;

static char* subscribe_topic = NULL;
static char* sw_topic = NULL;

static char* on_threshold_topic = NULL;
static char* off_threshold_topic = NULL;

int update_sw(unsigned char v) {
    unsigned char enabled;
    unsigned char on_threshold;
    unsigned char off_threshold;

    redisReply* reply = redisCommand(gs_sync_context,"GET %s", enable_topic);

    if(NULL == reply) {
        printf("Failed to sync query redis %s\n", gs_sync_context->errstr);
        return -1;
    }
    printf("Enabled: %s\n", reply->str);
    enabled = (0 != strcmp("0", reply->str));
    freeReplyObject(reply);
    
    if(enabled) {
        reply = redisCommand(gs_sync_context,"GET %s", on_threshold_topic);

        if(NULL == reply) {
            printf("Failed to sync query redis %s\n", gs_sync_context->errstr);
            return -1;
        }
        
        printf("On threshold: %s\n", reply->str);
        on_threshold = (atoi(reply->str) & 0xFF);
        freeReplyObject(reply);

        reply = redisCommand(gs_sync_context,"GET %s", off_threshold_topic);

        if(NULL == reply) {
            printf("Failed to sync query redis %s\n", gs_sync_context->errstr);
            return -1;
        }
        
        printf("Off threshold: %s\n", reply->str);
        off_threshold = (atoi(reply->str) & 0xFF);
        freeReplyObject(reply);
        
        if(on_threshold < off_threshold) {
            if(v <= on_threshold) {
                reply = redisCommand(gs_sync_context,"SET %s %s", sw_topic, "1");

                if(NULL == reply) {
                    printf("Failed to sync query redis %s\n", gs_sync_context->errstr);
                    return -1;
                }
                
                printf("Set result: %s\n", reply->str);
                freeReplyObject(reply);
            } else if(v >= off_threshold) {
                reply = redisCommand(gs_sync_context,"SET %s %s", sw_topic, "0");

                if(NULL == reply) {
                    printf("Failed to sync query redis %s\n", gs_sync_context->errstr);
                    return -1;
                }
                
                printf("Set result: %s\n", reply->str);
                freeReplyObject(reply);
            }
        } else {
            if(v >= on_threshold) {
                reply = redisCommand(gs_sync_context,"SET %s %s", sw_topic, "1");

                if(NULL == reply) {
                    printf("Failed to sync query redis %s\n", gs_sync_context->errstr);
                    return -1;
                }
                
                printf("Set result: %s\n", reply->str);
                freeReplyObject(reply);
            } else if(v <= off_threshold) {
                reply = redisCommand(gs_sync_context,"SET %s %s", sw_topic, "0");

                if(NULL == reply) {
                    printf("Failed to sync query redis %s\n", gs_sync_context->errstr);
                    return -1;
                }
                
                printf("Set result: %s\n", reply->str);
                freeReplyObject(reply);
            }
        }
    }
    
    return SND_RCV_OK;
}

void getCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    
    if (NULL == reply) {
        if (c->errstr) {
            printf("errstr: %s\n", c->errstr);
        }
        return;
    }
    
    printf("get type: %d\n", reply->type);
    printf("get elements: %d\n", reply->elements);
    printf("get param: %s\n", (long)privdata);
    
    if(0 > update_sw(atoi(reply->str) & 0xFF)) {
        redisAsyncDisconnect(c);
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
    printf("sub reply element count: %d\n", reply->elements);
    switch(reply->type) {
        case REDIS_REPLY_ARRAY: 
            for(int i = 0; i < reply->elements; i++) {
                printf("sub Array element %d: %s\n", i, reply->element[i]->str);
            }
            
            if(4 == reply->elements) { // psubscribe element 0 is "message", element 1 is key pattern element 2 is key element 3 is value string
                if(NULL != reply->element[0]->str && 0 == strcmp(REDIS_MESSAGE_TYPE, reply->element[0]->str)) {
                    if(NULL != reply->element[2]) {
                        if(0 == strcmp(EXIT_FLAG_KEY, reply->element[2]->str)) {
                            if(NULL != reply->element[3] && 0 == strcmp(EXIT_FLAG_VALUE, reply->element[3]->str)) {
                                redisAsyncDisconnect(c);
                            }
                        } else if(NULL != reply->element[3]) {
                            if(0 > update_sw(atoi(reply->element[3]->str) & 0xFF)) {
                                redisAsyncDisconnect(c);
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
    printf("%s redis_ip redis_port subscribe_topic sw_topic on_threshold_topic off_threshold_topic\n");
    printf("E.g.:\n");
    printf("%s 127.0.0.1 6379 monitor/1/1 controller/1/0 living_room/heat/on living_room/heat/off\n\n");
}

int main (int argc, char **argv) {
    
l_start:
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    struct event_base *base;
    
    int ret;

    char* redis_ip;
    int redis_port;
    
    if(8 != argc) {
        print_usage(argc, argv);
        return -1;
    }
    
    redis_ip = argv[1];
    redis_port = atoi(argv[2]);
    
    enable_topic = argv[3];
    subscribe_topic = argv[4];
    sw_topic = argv[5];
    on_threshold_topic = argv[6];
    off_threshold_topic = argv[7];

    struct timeval timeout = { 0, 500000 }; // 1.5 seconds
    gs_sync_context = redisConnectWithTimeout(redis_ip, redis_port, timeout);
    if(NULL == gs_sync_context) {
        printf("Connection error: can't allocate redis context\n");
        goto l_exit;
    }
    
    if(gs_sync_context->err) {
        printf("Connection error: %s\n", gs_sync_context->errstr);
        goto l_free_sync_redis;
    }

    redisReply* reply = redisCommand(gs_sync_context,"PING");
    if(NULL == reply) {
        printf("Failed to sync query redis %s\n", gs_sync_context->errstr);
        goto l_free_sync_redis;
    }
    printf("PING: %s\n", reply->str);
    freeReplyObject(reply);
    
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
    
    redisAsyncCommand(gs_async_context, getCallback, NULL, "GET %s", subscribe_topic);

    redisAsyncCommand(gs_async_context, subscribeCallback, NULL, "SUBSCRIBE %s", subscribe_topic);
    event_base_dispatch(base);
    
l_free_sync_redis:
    redisFree(gs_sync_context);
    
l_free_async_redis:
    redisAsyncFree(gs_async_context);
    event_base_free(base);
 
l_exit:
    if(!gs_exit) {    
        printf("Godown_keeper execution failed retry!\n");
        goto l_start;
    }

    printf("exit!\n");
    return 0;
}
