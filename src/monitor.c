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
#define SERV_PORT               5002

#define REDIS_IP                "127.0.0.1"
#define REDIS_PORT              6379

#define REDIS_MESSAGE_TYPE      "pmessage"

#define FLAG_KEY                "monitor"
#define EXIT_FLAG_VALUE         "exit"

static int gs_socket = -1;
static redisContext *gs_sync_context = NULL;

static int gs_exit = 0;

void print_usage(int argc, char **argv) {
    printf("Invalid input parameters!\n");
    printf("Usage: (<optional parameters>)\n");
    printf("%s id controller_ip controller_port <redis_ip> <redis_port>\n");
    printf("E.g.:\n");
    printf("%s 1 192.168.100.100 5000 127.0.0.1 6379\n\n");
}

int main (int argc, char **argv) {
    
l_start:
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    struct sockaddr_in serv_addr;
    
    struct timeval timeout = { 0, 100000};
    
    int ret;
    unsigned char temp = 0;
    unsigned char buffer[4];
    int buffer_state = 0;
    
    fd_set myset;
    int valopt; 
    socklen_t lon; 

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
 /* 
    ret = recv(gs_socket, &temp, 1, 0);
    if(-1 == ret) {
        printf("Error receiving initial data! %d\n", ret);
        goto l_socket_cleanup;
    }
    printf("Connected to controller, remote socket: %d\n", temp);
*/    

    gs_sync_context = redisConnectWithTimeout(redis_ip, redis_port, timeout);
    if(NULL == gs_sync_context) {
        printf("Connection error: can't allocate redis context\n");
        goto l_socket_cleanup;
    }
    
    if(gs_sync_context->err) {
        printf("Connection error: %s\n", gs_sync_context->errstr);
        goto l_free_redis;
    }

    redisReply* reply = redisCommand(gs_sync_context,"PING");
    if(NULL == reply) {
        printf("Failed to sync query redis %s\n", gs_sync_context->errstr);
        goto l_free_redis;
    }
    printf("PING: %s\n", reply->str);
    freeReplyObject(reply);

    while(1) {
        if(-1 == recv(gs_socket, &temp, 1, 0)) {
            goto l_free_redis;
        } else { //
            printf("Received 0x%x\n", temp);
            switch(buffer_state) {
                case 0: // first 0xAA
                    if(0xAA == temp) {
                        buffer_state++;
                    }
                    break;
                case 1: // Temperature
                case 2: // Move 1
                case 3: // Move 2
                case 4: // Light
                    buffer[buffer_state -1] = temp;
                    buffer_state++;
                    break;
                case 5: // last 0xFF
                    if(0xFF == temp) {
                        redisReply* reply = redisCommand(gs_sync_context,"PUBLISH %s/%s/temp %d", FLAG_KEY, argv[1], buffer[0]);
                        if(NULL == reply) {
                            printf("Failed to sync query redis %s\n", gs_sync_context->errstr);
                            goto l_free_redis;
                        }
                        freeReplyObject(reply);
                        reply = redisCommand(gs_sync_context,"PUBLISH %s/%s/mov1 %d", FLAG_KEY, argv[1], buffer[1]);
                        if(NULL == reply) {
                            printf("Failed to sync query redis %s\n", gs_sync_context->errstr);
                            goto l_free_redis;
                        }
                        freeReplyObject(reply);
                        reply = redisCommand(gs_sync_context,"PUBLISH %s/%s/mov2 %d", FLAG_KEY, argv[1], buffer[2]);
                        if(NULL == reply) {
                            printf("Failed to sync query redis %s\n", gs_sync_context->errstr);
                            goto l_free_redis;
                        }
                        freeReplyObject(reply);
                        reply = redisCommand(gs_sync_context,"PUBLISH %s/%s/light %d", FLAG_KEY, argv[1], buffer[3]);
                        if(NULL == reply) {
                            printf("Failed to sync query redis %s\n", gs_sync_context->errstr);
                            goto l_free_redis;
                        }
                        freeReplyObject(reply);
                    } else { // out of sync
                        redisCommand(gs_sync_context,"PUBLISH %s/%s/sync %s", FLAG_KEY, argv[1], "out of sync");
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
