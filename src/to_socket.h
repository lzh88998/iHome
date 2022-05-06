/*
 * Copyright lzh88998 and distributed under Apache 2.0 license
 * 
 * to_socket is an extension to standard socket application.
 * Added timeout control to connect, send and recv. 
 * 
 * Current default timeout is 100ms which is suitable for 
 * intranet use especially when all devices are in the same
 * subnet.
 * 
 */
 
#ifndef __TO_SOCKET_H__
#define __TO_SOCKET_H__

#include <sys/socket.h>

/*
 * Define return values of to_socket functions
 * in a more readable manner
 */
#define TO_SOCKET_OK						0
#define TO_SOCKET_ERROR_ADDRESS				-1
#define TO_SOCKET_ERROR_PROTOCOL			-2
#define TO_SOCKET_ERROR_GET_OPTIONS			-3
#define TO_SOCKET_ERROR_SET_NONBLOCK		-4
#define TO_SOCKET_ERROR_CONNECT				-5
#define TO_SOCKET_ERROR_GET_SOCKET_ERROR	-6
#define TO_SOCKET_ERROR_SOCKET_ERROR		-7
#define TO_SOCKET_ERROR_CONNECT_TIMEOUT		-8

typedef int to_socket_ctx;

/*
 * Initiatelize a socket descriptor with timeout configuration
 * specified for local network response time
 * 
 * Parameters:
 * const char* addr		IP address in character format. E.g.: 
 * 						"192.168.100.100"
 * int port				Port number for target address
 * 
 * Return Value:		Socket descriptor if successful which
 * 						will be greater than 0. Otherwiese 
 * 						return error code defined in above 
 * 						section.
 * 
 * Note current timeout is set to 100ms.
 */
to_socket_ctx to_connect(const char* addr, int port);

#define to_send(socket, buf, len, flags)	send(socket, buf, len, flags)
#define to_recv(socket, buf, len, flags)	recv(socket, buf, len, flags)
#define to_shutdown(socket, how)			send(socket, how)
#define to_close(socket)					close(socket)

#endif
