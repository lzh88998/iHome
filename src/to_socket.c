#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <errno.h>

#include <fcntl.h>
#include "log.h"
#include "to_socket.h"

to_socket_ctx to_connect(const char* addr, int port) {
    // for 100ms
    struct timeval timeout = { 0, 100000}; 
    struct sockaddr_in target_addr;
    to_socket_ctx ret, socket_ctx;
    
    fd_set myset;
    int valopt; 
    socklen_t lon; 
    
	// initialize address struct
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(port);
    
    ret = inet_pton(AF_INET, addr, &target_addr.sin_addr.s_addr);
    if(-1 == ret) {
        LOG_ERROR("Error assigning address!");
        return TO_SOCKET_ERROR_ADDRESS;
    }
    
    socket_ctx = socket(AF_INET, SOCK_STREAM, 0);
    if(-1 == socket_ctx) {
        LOG_ERROR("Error connecting to controller!");
        return TO_SOCKET_ERROR_PROTOCOL;
    }
        
    LOG_DETAILS("Socket: %d!", socket_ctx);
    LOG_DETAILS("Socket Family: %d", target_addr.sin_family);
    LOG_DETAILS("Socket Port: %d", target_addr.sin_port);
    
    // set send & recv timeout
    setsockopt(socket_ctx, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(struct timeval));
    setsockopt(socket_ctx, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(struct timeval));

	// enable keep alive
    setsockopt(socket_ctx, SOL_SOCKET, SO_KEEPALIVE, &ret, sizeof(ret));
    
    // Set non-blocking 
    if( (ret = fcntl(socket_ctx, F_GETFL, NULL)) < 0) { 
        LOG_ERROR("Error fcntl(..., F_GETFL) (%s)", strerror(errno)); 
        ret = TO_SOCKET_ERROR_GET_OPTIONS;
        goto l_socket_cleanup;
    } 
    ret |= O_NONBLOCK; 
    if( fcntl(socket_ctx, F_SETFL, ret) < 0) { 
        LOG_ERROR("Error fcntl(..., F_SETFL) (%s)", strerror(errno)); 
        ret = TO_SOCKET_ERROR_SET_NONBLOCK;
        goto l_socket_cleanup;
    } 
    // Trying to connect with timeout 
    ret = connect(socket_ctx, (struct sockaddr *)&target_addr, sizeof(target_addr)); 
    if (0 > ret) { 
        if (EINPROGRESS == errno) { 
            LOG_DEBUG("EINPROGRESS in connect() - selecting"); 
			FD_ZERO(&myset); 
			FD_SET(socket_ctx, &myset); 
			ret = select(socket_ctx+1, NULL, &myset, NULL, &timeout); 
			if (ret < 0 && EINTR != errno) { 
				LOG_ERROR("Error connecting %d - %s", errno, strerror(errno)); 
				ret = TO_SOCKET_ERROR_CONNECT;
				goto l_socket_cleanup;
			} 
			else if (ret > 0) { 
				// Socket selected for write 
				lon = sizeof(int); 
				if (getsockopt(socket_ctx, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &lon) < 0) { 
					LOG_ERROR("Error in getsockopt() %d - %s", errno, strerror(errno)); 
					ret = TO_SOCKET_ERROR_GET_SOCKET_ERROR;
					goto l_socket_cleanup;
				} 
				// Check the value returned... 
				if (valopt) { 
					LOG_ERROR("Error in delayed connection() %d - %s", valopt, strerror(valopt)); 
					ret = TO_SOCKET_ERROR_SOCKET_ERROR;
					goto l_socket_cleanup;
				} 
			} 
			else { // ret <= 0 && EINTR == errno
				LOG_ERROR("Timeout in select() - Cancelling!"); 
				ret = TO_SOCKET_ERROR_CONNECT_TIMEOUT;
				goto l_socket_cleanup;
			} 
        } 
        else { 
            LOG_ERROR("Error connecting %d - %s", errno, strerror(errno)); 
			ret = TO_SOCKET_ERROR_CONNECT;
            goto l_socket_cleanup;
        } 
    } // otherwise means connect successful
    
    // Set to blocking mode again... 
    if( (ret = fcntl(socket_ctx, F_GETFL, NULL)) < 0) { 
        LOG_ERROR("Error fcntl(..., F_GETFL) (%s)", strerror(errno)); 
        goto l_socket_disconnect;
    } 
    ret &= (~O_NONBLOCK); 
    if( fcntl(socket_ctx, F_SETFL, ret) < 0) { 
        LOG_ERROR("Error fcntl(..., F_SETFL) (%s)", strerror(errno)); 
        goto l_socket_disconnect;
    } 
    
    return socket_ctx;

l_socket_disconnect:
	if(0 > shutdown(socket_ctx, SHUT_RDWR)) {
		LOG_ERROR("Error shutdown socket! Error no: %s", strerror(errno));
	}
  
l_socket_cleanup:
	if(0 > close(socket_ctx)) {
		LOG_ERROR("Error closing socket! Error no: %s", strerror(errno));
	}
	
	return ret;
}
