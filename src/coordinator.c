#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <sys/wait.h>

#include <hiredis.h>
#include "log.h"

#define REDIS_IP                        "127.0.0.1"
#define REDIS_PORT_STR                  "6379"
#define REDIS_PORT                        atoi(REDIS_PORT_STR)

#define MAX_IP_ADDR_LENGTH                16            // including NULL terminator
#define MAX_PORT_LENGTH                    6            // including NULL terminator

/*
 * Services that do not need to query redis
 */
#define GODOWN_KEEPER_START_CMD            "./godown_keeper"
#define CELSIUS_CONVERTER_START_CMD        "./celsius_converter"
#define TOUCH_PROCESSOR_START_CMD        "./touch_processor"

/*
 * Services that need to get configuration
 * from redis
 */
#define CARGADOR_START_CMD                "./cargador"
#define LCD_START_CMD                    "./lcd"
#define SENSOR_START_CMD                "./sensor"
#define TOUCH_START_CMD                    "./touch"

#define GODOWN_KEEPER_FLAG                "godown_keeper"
#define CELSIUS_CONVERTER_FLAG            "celsius_converter"
#define TOUCH_PROCESSOR_FLAG            "touch_processor"

#define LCD_FLAG                        "lcd"
#define TOUCH_FLAG                        "touch"
#define CARGADOR_FLAG                    "cargador"
#define SENSOR_FLAG                        "sensor"

void print_usage(int argc, char **argv) {
    if(0 > argc) {
        return;
    }
    
    printf("Invalid input parameters!\n");
    printf("Usage: (<optional parameters>)\n");
    printf("%s <log_level> <redis_ip> <redis_port>\n", argv[0]);
    printf("E.g.:\n");
    printf("%s\n\n", argv[0]);
    printf("%s debug\n\n", argv[0]);
    printf("%s debug 127.0.0.1 6379\n\n", argv[0]);
};

typedef struct {
    char ip_addr[MAX_IP_ADDR_LENGTH];
    char port[MAX_PORT_LENGTH];
} Remote_Items;

/*
 * Main entry of the service. It will first connect
 * to redis, and get configuration for cardinator,
 * lcd, touch, sensor. Then use fork to create
 * sub processes run and monitor the status of the 
 * started service, restart when needed.
 * 
 * Parameters:
 * int argc                 Number of input parameters, same function 
 *                          with argc of main.
 * char **argv              Actual input parameters, same function with
 *                          argv of main.
 * 
 * Return value:
 * 0 when run successfully
 * -1 when cannot connect to redis
 * -2 when failed to fork sub processes
 * -3 when failed to execute command for sub process
 * 
 * Note: in this function we use printf not using log
 * as it is necessary to ensure the hint is always 
 * printed out without the loglevel configuration.
 * 
 */
int main(int argc, char **argv) {
    int status = 0, ret = 0;
    int godown_keeper_pid = -1, celsius_converter_pid = -1, touch_processor_pid = -1;

    const char* redis_ip = REDIS_IP;
    int redis_port = REDIS_PORT;

    redisContext *sync_context = NULL;
    
    size_t lcd_count = 0;
    int* lcd_pids = NULL;
    Remote_Items* lcd_items = NULL;
    
    size_t touch_count = 0;
    int* touch_pids = NULL;
    Remote_Items* touch_items = NULL;
    
    size_t sensor_count = 0;
    int* sensor_pids = NULL;
    Remote_Items* sensor_items = NULL;
    
    size_t cargador_count = 0;
    int* cargador_pids = NULL;
    Remote_Items* cargador_items = NULL;
    
    LOG_INFO("[%d]Parsing parameters", getpid());
    switch(argc) {
        case 1:
            break;
        case 2:
            if(0 > log_set_level(argv[1])) {
                print_usage(argc, argv);
                return -2;
            }
            break;
        case 3:
            redis_ip = argv[1];
            redis_port = atoi(argv[2]);
            break;
        case 4:
            if(0 > log_set_level(argv[3])) {
                print_usage(argc, argv);
                return -3;
            }
            redis_ip = argv[1];
            redis_port = atoi(argv[2]);
            break;
        default:
            print_usage(argc, argv);
            return -1;
    }

    LOG_DETAILS("[%d]Connecting to redis", getpid());
    struct timeval timeout = { 0, 500000 }; // 0.5 seconds
    sync_context = redisConnectWithTimeout(redis_ip, redis_port, timeout);
    if(NULL == sync_context) {
        LOG_ERROR("[%d]Connection error: can't allocate redis context\n", getpid());
        goto l_exit;
    }
    
    if(sync_context->err) {
        printf("[%d]Connection error: %s\n", getpid(), sync_context->errstr);
        goto l_free_sync_redis;
    }

    redisReply* reply = redisCommand(sync_context,"PING");
    if(NULL == reply) {
        LOG_ERROR("[%d]Failed to sync query redis %s\n", getpid(), sync_context->errstr);
        goto l_free_sync_redis;
    }
    printf("[%d]PING: %s\n", getpid(), reply->str);
    freeReplyObject(reply);
    
// No need to query redis
// start godown_keeper
    godown_keeper_pid = fork();
    if(0 > godown_keeper_pid) {
        // failed
        LOG_ERROR("[%d]Failed to fork process! Returned: %d", getpid(), godown_keeper_pid);
        LOG_ERROR("[%d]Fail reason: %s", getpid(), strerror(errno));
        goto l_free_sync_redis;
    } else if(0 == godown_keeper_pid) {
        ret = execl(GODOWN_KEEPER_START_CMD, GODOWN_KEEPER_START_CMD, NULL);
        if(0 > ret) {
            LOG_ERROR("[%d]Failed to execute process! Returned %d", getpid(), ret);
            LOG_ERROR("[%d]Fail reason: %s", getpid(), strerror(errno));
        }
        LOG_INFO("[%d]execel finished!", getpid());
        
        return ret;
    } 
    
    // start celsius_converter
    celsius_converter_pid = fork();
    if(0 > celsius_converter_pid) {
        // failed
        LOG_ERROR("[%d]Failed to fork process! Returned: %d", getpid(), celsius_converter_pid);
        LOG_ERROR("[%d]Fail reason: %s", getpid(), strerror(errno));
        goto l_free_godown_keeper;
    } else if(0 == celsius_converter_pid) {
        ret = execl(CELSIUS_CONVERTER_START_CMD, CELSIUS_CONVERTER_START_CMD, NULL);
        if(0 > ret) {
            LOG_ERROR("[%d]Failed to execute process! Returned %d", getpid(), ret);
            LOG_ERROR("[%d]Fail reason: %s", getpid(), strerror(errno));
        }
        LOG_INFO("[%d]execel finished!", getpid());
        
        return ret;
    } 
    
    // start touch_processor
    touch_processor_pid = fork();
    if(0 > touch_processor_pid) {
        // failed
        LOG_ERROR("[%d]Failed to fork process! Returned: %d", getpid(), touch_processor_pid);
        LOG_ERROR("[%d]Fail reason: %s", getpid(), strerror(errno));
        goto l_free_celsius_converter;
    } else if(0 == touch_processor_pid) {
        ret = execl(TOUCH_PROCESSOR_START_CMD, TOUCH_PROCESSOR_START_CMD, NULL);
        if(0 > ret) {
            LOG_ERROR("[%d]Failed to execute process! Returned %d", getpid(), ret);
            LOG_ERROR("[%d]Fail reason: %s", getpid(), strerror(errno));
        }
        LOG_INFO("[%d]execel finished!", getpid());
        
        return ret;
    } 
    
    // start lcd
    reply = redisCommand(sync_context,"SMEMBERS %s", LCD_FLAG);
    if(NULL == reply) {
        LOG_ERROR("[%d]Failed to sync query redis %s\n", getpid(), sync_context->errstr);
        goto l_free_touch_processor;
    }
    LOG_DETAILS("[%d]LCD count: %d", getpid(), reply->elements);
    if(reply->elements > 0) {
        lcd_items = malloc(sizeof(Remote_Items) * reply->elements);
        if(NULL == lcd_items) {
            LOG_ERROR("[%d]Allocate memory for lcd items failed!", getpid());
            goto l_free_touch_processor;
        }
        
        memset(lcd_items, 0, sizeof(Remote_Items) * reply->elements);
        
        lcd_pids = malloc(sizeof(int) * reply->elements);
        if(NULL == lcd_pids) {
            LOG_ERROR("[%d]Allocate memory for lcd pids failed!", getpid());
            free(lcd_items);
            goto l_free_touch_processor;
        }
        
        size_t temp_index = 0;
        while(temp_index < reply->elements) {
            LOG_DETAILS("[%d]Item %d: %s", getpid(), temp_index, reply->element[temp_index]->str);
            char* start_ch = reply->element[temp_index]->str;
            char* seperator = strchr(start_ch, '/');
            if(NULL == seperator || seperator - start_ch > MAX_IP_ADDR_LENGTH || strlen(seperator + 1) > MAX_PORT_LENGTH) {
                LOG_ERROR("[%d]invalid input format of item %d!", getpid(), temp_index);
                temp_index++;
                continue;
            }
            
            memcpy(lcd_items[lcd_count].ip_addr, reply->element[temp_index]->str, seperator - start_ch);
            memcpy(lcd_items[lcd_count].port, seperator + 1, strlen(seperator + 1));
            
            LOG_DETAILS("[%d]Saved item %d: %s %s", getpid(), lcd_count, lcd_items[lcd_count].ip_addr, lcd_items[lcd_count].port);
            
            // fork process
            lcd_pids[lcd_count] = fork();
            if(0 > lcd_pids[lcd_count]) {
                // failed
                LOG_ERROR("[%d]Failed to fork process! Returned: %d", getpid(), lcd_pids[lcd_count]);
                LOG_ERROR("[%d]Fail reason: %s", getpid(), strerror(errno));
                goto l_free_lcd;
            } else if(0 == lcd_pids[lcd_count]) {
                ret = execl(LCD_START_CMD, LCD_START_CMD, lcd_items[lcd_count].ip_addr, lcd_items[lcd_count].port, NULL);
                if(0 > ret) {
                    LOG_ERROR("[%d]Failed to execute process! Returned %d", getpid(), ret);
                    LOG_ERROR("[%d]Fail reason: %s", getpid(), strerror(errno));
                }
                LOG_INFO("[%d]execel finished!", getpid());
                
                return ret;
            } 
            
            LOG_INFO("[%d]Started lcd pid %d", lcd_pids[lcd_count]);

            lcd_count++; // last position is not pointing to actual item
            temp_index++;
        }
    }

    freeReplyObject(reply);
    
    // start touch
    reply = redisCommand(sync_context,"SMEMBERS %s", TOUCH_FLAG);
    if(NULL == reply) {
        LOG_ERROR("[%d]Failed to sync query redis %s\n", getpid(), sync_context->errstr);
        goto l_free_lcd;
    }
    LOG_DETAILS("[%d]Touch count: %d", getpid(), reply->elements);
    if(reply->elements > 0) {
        touch_items = malloc(sizeof(Remote_Items) * reply->elements);
        if(NULL == touch_items) {
            LOG_ERROR("[%d]Allocate memory for touch items failed!", getpid());
            goto l_free_lcd;
        }
        
        memset(touch_items, 0, sizeof(Remote_Items) * reply->elements);
        
        touch_pids = malloc(sizeof(int) * reply->elements);
        if(NULL == touch_pids) {
            LOG_ERROR("[%d]Allocate memory for touch pids failed!", getpid());
            free(touch_items);
            goto l_free_lcd;
        }
        
        size_t temp_index = 0;
        while(temp_index < reply->elements) {
            LOG_DETAILS("[%d]Item %d: %s", getpid(), temp_index, reply->element[temp_index]->str);
            char* start_ch = reply->element[temp_index]->str;
            char* seperator = strchr(start_ch, '/');
            if(NULL == seperator || seperator - start_ch > MAX_IP_ADDR_LENGTH || strlen(seperator + 1) > MAX_PORT_LENGTH) {
                LOG_ERROR("[%d]invalid input format of item %d!", getpid(), temp_index);
                temp_index++;
                continue;
            }
            
            memcpy(touch_items[touch_count].ip_addr, reply->element[temp_index]->str, seperator - start_ch);
            memcpy(touch_items[touch_count].port, seperator + 1, strlen(seperator + 1));
            
            LOG_DETAILS("[%d]Saved item %d: %s %s", getpid(), touch_count, touch_items[touch_count].ip_addr, touch_items[touch_count].port);

            // fork process
            touch_pids[touch_count] = fork();
            if(0 > touch_pids[touch_count]) {
                // failed
                LOG_ERROR("[%d]Failed to fork process! Returned: %d", getpid(), touch_pids[touch_count]);
                LOG_ERROR("[%d]Fail reason: %s", getpid(), strerror(errno));
                goto l_free_touch;
            } else if(0 == touch_pids[touch_count]) {
                ret = execl(TOUCH_START_CMD, TOUCH_START_CMD, touch_items[touch_count].ip_addr, touch_items[touch_count].port, NULL);
                if(0 > ret) {
                    LOG_ERROR("[%d]Failed to execute process! Returned %d", getpid(), ret);
                    LOG_ERROR("[%d]Fail reason: %s", getpid(), strerror(errno));
                }
                LOG_INFO("[%d]execel finished!", getpid());
                
                return ret;
            } 
            
            LOG_INFO("[%d]Started touch pid %d", touch_pids[touch_count]);
            
            touch_count++; // last position is not pointing to actual item
            temp_index++;
        }
    }

    freeReplyObject(reply);
     
    // start cargador
    reply = redisCommand(sync_context,"SMEMBERS %s", CARGADOR_FLAG);
    if(NULL == reply) {
        LOG_ERROR("[%d]Failed to sync query redis %s\n", getpid(), sync_context->errstr);
        goto l_free_touch;
    }
    LOG_DETAILS("[%d]Cargador count: %d", getpid(), reply->elements);
    if(reply->elements > 0) {
        cargador_items = malloc(sizeof(Remote_Items) * reply->elements);
        if(NULL == cargador_items) {
            LOG_ERROR("[%d]Allocate memory for cargador items failed!", getpid());
            goto l_free_touch;
        }
        
        memset(cargador_items, 0, sizeof(Remote_Items) * reply->elements);
        
        cargador_pids = malloc(sizeof(int) * reply->elements);
        if(NULL == cargador_pids) {
            LOG_ERROR("[%d]Allocate memory for cargador pids failed!", getpid());
            free(cargador_items);
            goto l_free_touch;
        }
        
        size_t temp_index = 0;
        while(temp_index < reply->elements) {
            LOG_DETAILS("[%d]Item %d: %s", getpid(), temp_index, reply->element[temp_index]->str);
            char* start_ch = reply->element[temp_index]->str;
            char* seperator = strchr(start_ch, '/');
            if(NULL == seperator || seperator - start_ch > MAX_IP_ADDR_LENGTH || strlen(seperator + 1) > MAX_PORT_LENGTH) {
                LOG_ERROR("[%d]invalid input format of item %d!", getpid(), temp_index);
                temp_index++;
                continue;
            }
            
            memcpy(cargador_items[cargador_count].ip_addr, reply->element[temp_index]->str, seperator - start_ch);
            memcpy(cargador_items[cargador_count].port, seperator + 1, strlen(seperator + 1));
            
            LOG_DETAILS("[%d]Saved item %d: %s %s", getpid(), cargador_count, cargador_items[cargador_count].ip_addr, cargador_items[cargador_count].port);

            // fork process
            cargador_pids[cargador_count] = fork();
            if(0 > cargador_pids[cargador_count]) {
                // failed
                LOG_ERROR("[%d]Failed to fork process! Returned: %d", getpid(), cargador_pids[cargador_count]);
                LOG_ERROR("[%d]Fail reason: %s", getpid(), strerror(errno));
                goto l_free_cargador;
            } else if(0 == cargador_pids[cargador_count]) {
                ret = execl(CARGADOR_START_CMD, CARGADOR_START_CMD, cargador_items[cargador_count].ip_addr, cargador_items[cargador_count].port, NULL);
                if(0 > ret) {
                    LOG_ERROR("[%d]Failed to execute process! Returned %d", getpid(), ret);
                    LOG_ERROR("[%d]Fail reason: %s", getpid(), strerror(errno));
                }
                LOG_INFO("[%d]execel finished!", getpid());
                
                return ret;
            } 
            
            LOG_INFO("[%d]Started touch pid %d", cargador_pids[cargador_count]);
            
            cargador_count++; // last position is not pointing to actual item
            temp_index++;
        }
    }

    freeReplyObject(reply);
     
    // start sensor
    reply = redisCommand(sync_context,"SMEMBERS %s", SENSOR_FLAG);
    if(NULL == reply) {
        LOG_ERROR("[%d]Failed to sync query redis %s\n", getpid(), sync_context->errstr);
        goto l_free_cargador;
    }
    LOG_DETAILS("[%d]Sensor count: %d", getpid(), reply->elements);
    if(reply->elements > 0) {
        sensor_items = malloc(sizeof(Remote_Items) * reply->elements);
        if(NULL == sensor_items) {
            LOG_ERROR("[%d]Allocate memory for sensor_items items failed!", getpid());
            goto l_free_cargador;
        }
        
        memset(sensor_items, 0, sizeof(Remote_Items) * reply->elements);
        
        sensor_pids = malloc(sizeof(int) * reply->elements);
        if(NULL == sensor_pids) {
            LOG_ERROR("[%d]Allocate memory for sensor pids failed!", getpid());
            free(sensor_items);
            goto l_free_cargador;
        }
        
        size_t temp_index = 0;
        while(temp_index < reply->elements) {
            LOG_DETAILS("[%d]Item %d: %s", getpid(), temp_index, reply->element[temp_index]->str);
            char* start_ch = reply->element[temp_index]->str;
            char* seperator = strchr(start_ch, '/');
            if(NULL == seperator || seperator - start_ch > MAX_IP_ADDR_LENGTH || strlen(seperator + 1) > MAX_PORT_LENGTH) {
                LOG_ERROR("[%d]invalid input format of item %d!", getpid(), temp_index);
                temp_index++;
                continue;
            }
            
            memcpy(sensor_items[sensor_count].ip_addr, reply->element[temp_index]->str, seperator - start_ch);
            memcpy(sensor_items[sensor_count].port, seperator + 1, strlen(seperator + 1));
            
            LOG_DETAILS("[%d]Saved item %d: %s %s", getpid(), sensor_count, sensor_items[sensor_count].ip_addr, sensor_items[sensor_count].port);
            
            // fork process
            sensor_pids[sensor_count] = fork();
            if(0 > sensor_pids[sensor_count]) {
                // failed
                LOG_ERROR("[%d]Failed to fork process! Returned: %d", getpid(), sensor_pids[sensor_count]);
                LOG_ERROR("[%d]Fail reason: %s", getpid(), strerror(errno));
                goto l_free_sensor;
            } else if(0 == sensor_pids[sensor_count]) {
                ret = execl(SENSOR_START_CMD, SENSOR_START_CMD, sensor_items[sensor_count].ip_addr, sensor_items[sensor_count].port, NULL);
                if(0 > ret) {
                    LOG_ERROR("[%d]Failed to execute process! Returned %d", getpid(), ret);
                    LOG_ERROR("[%d]Fail reason: %s", getpid(), strerror(errno));
                }
                LOG_INFO("[%d]execel finished!", getpid());
                
                return ret;
            } 
            
            LOG_INFO("[%d]Started sensor pid %d", sensor_items[sensor_count]);

            sensor_count++; // last position is not pointing to actual item
            temp_index++;
        }
    }

    freeReplyObject(reply);
 
    // may need to add guard process here
    sleep(5);
    
l_free_sensor:
    LOG_DEBUG("[%d]Exiting %d cargadors...", getpid(), cargador_count);
    for(size_t i = 0; i < cargador_count; i++) {
        reply = redisCommand(sync_context,"PUBLISH %s/%s %s", CARGADOR_FLAG, cargador_items[i].ip_addr, EXIT_FLAG_VALUE, EXIT_FLAG_VALUE);
        if(NULL == reply) {
            LOG_ERROR("Failed to sync query redis %s\n", sync_context->errstr);
            // kill godown_keeper
            if(0 > kill(cargador_pids[i], 0)) {
                LOG_ERROR("Failed to kill touch_processor");
            }
            continue;
        }
        
        LOG_DEBUG("[%d]Stop cargadors %d result: %s\n", getpid(), i, reply->str);
        freeReplyObject(reply);

        waitpid(cargador_pids[i], &status, WUNTRACED);
    }

    LOG_DEBUG("[%d]Exited cargadors", getpid());


l_free_cargador:
    LOG_DEBUG("[%d]Exiting %d cargadors...", getpid(), cargador_count);
    for(size_t i = 0; i < cargador_count; i++) {
        reply = redisCommand(sync_context,"PUBLISH %s/%s %s", CARGADOR_FLAG, cargador_items[i].ip_addr, EXIT_FLAG_VALUE, EXIT_FLAG_VALUE);
        if(NULL == reply) {
            LOG_ERROR("Failed to sync query redis %s\n", sync_context->errstr);
            // kill godown_keeper
            if(0 > kill(cargador_pids[i], 0)) {
                LOG_ERROR("Failed to kill touch_processor");
            }
            continue;
        }
        
        LOG_DEBUG("[%d]Stop cargadors %d result: %s\n", getpid(), i, reply->str);
        freeReplyObject(reply);

        waitpid(cargador_pids[i], &status, WUNTRACED);
    }

    LOG_DEBUG("[%d]Exited cargadors", getpid());

l_free_touch:
    LOG_DEBUG("[%d]Exiting %d touches...", getpid(), touch_count);
    for(size_t i = 0; i < touch_count; i++) {
        reply = redisCommand(sync_context,"PUBLISH %s/%s %s", TOUCH_FLAG, touch_items[i].ip_addr, EXIT_FLAG_VALUE, EXIT_FLAG_VALUE);
        if(NULL == reply) {
            LOG_ERROR("Failed to sync query redis %s\n", sync_context->errstr);
            // kill godown_keeper
            if(0 > kill(touch_pids[i], 0)) {
                LOG_ERROR("Failed to kill touch_processor");
            }
            continue;
        }
        
        LOG_DEBUG("[%d]Stop touch %d result: %s\n", getpid(), i, reply->str);
        freeReplyObject(reply);

        waitpid(touch_pids[i], &status, WUNTRACED);
    }

    LOG_DEBUG("[%d]Exited touches", getpid());

l_free_lcd:
    LOG_DEBUG("[%d]Exiting %d lcds...", getpid(), lcd_count);
    for(size_t i = 0; i < lcd_count; i++) {
        reply = redisCommand(sync_context,"PUBLISH %s/%s %s", LCD_FLAG, lcd_items[i].ip_addr, EXIT_FLAG_VALUE, EXIT_FLAG_VALUE);
        if(NULL == reply) {
            LOG_ERROR("Failed to sync query redis %s\n", sync_context->errstr);
            // kill godown_keeper
            if(0 > kill(lcd_pids[i], 0)) {
                LOG_ERROR("Failed to kill touch_processor");
            }
            continue;
        }
        
        LOG_DEBUG("[%d]Stop lcd %d result: %s\n", getpid(), i, reply->str);
        freeReplyObject(reply);

        waitpid(lcd_pids[i], &status, WUNTRACED);
    }

    LOG_DEBUG("[%d]Exited lcds", getpid());

l_free_touch_processor:

//    LOG_DEBUG("Exiting touch_processor...");
    reply = redisCommand(sync_context,"PUBLISH %s/%s %s", TOUCH_PROCESSOR_FLAG, EXIT_FLAG_VALUE, EXIT_FLAG_VALUE);
    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s\n", sync_context->errstr);
        // kill godown_keeper
        if(0 > kill(touch_processor_pid, 0)) {
            LOG_ERROR("Failed to kill touch_processor");
        }
        goto l_free_godown_keeper;
    }
    LOG_DEBUG("Stop godown_keeper result: %s\n", reply->str);
    freeReplyObject(reply);

    waitpid(touch_processor_pid, &status, WUNTRACED);
    LOG_DEBUG("Exited touch_processor");
    

l_free_celsius_converter:
//    LOG_DEBUG("Exiting celsius_converter...");
    reply = redisCommand(sync_context,"PUBLISH %s/%s %s", CELSIUS_CONVERTER_FLAG, EXIT_FLAG_VALUE, EXIT_FLAG_VALUE);
    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s\n", sync_context->errstr);
        // kill godown_keeper
        if(0 > kill(celsius_converter_pid, 0)) {
            LOG_ERROR("Failed to kill celsius_converter");
        }
        goto l_free_godown_keeper;
    }
    LOG_DEBUG("Stop godown_keeper result: %s\n", reply->str);
    freeReplyObject(reply);

    waitpid(celsius_converter_pid, &status, WUNTRACED);
    LOG_DEBUG("Exited celsius_converter");
    
l_free_godown_keeper:
//    LOG_DEBUG("Exiting god_own_keeper...");
    reply = redisCommand(sync_context,"PUBLISH %s/%s %s", GODOWN_KEEPER_FLAG, EXIT_FLAG_VALUE, EXIT_FLAG_VALUE);
    if(NULL == reply) {
        LOG_ERROR("Failed to sync query redis %s\n", sync_context->errstr);
        if(0 > kill(godown_keeper_pid, 0)) {
            LOG_ERROR("Failed to kill godown_keeper_pid");
        }
        goto l_free_sync_redis;
    }
    LOG_DEBUG("Stop godown_keeper result: %s\n", reply->str);
    freeReplyObject(reply);

    waitpid(godown_keeper_pid, &status, WUNTRACED);
    LOG_DEBUG("Exited god_own_keeper");
    
l_free_sync_redis:
    redisFree(sync_context);
    
l_exit:    
    return ret;
}
