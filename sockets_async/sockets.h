#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string>

// Sockets OS
#ifdef _WIN32

	#include <winsock.h>

	#define socklen_t int

	#define SOCK_LAST_ERROR	WSAGetLastError()
	#define SOCK_WOULDBLOCK	WSAEWOULDBLOCK
	#define SOCK_INPROGRESS	WSAEINPROGRESS

#else //linux

	#include <unistd.h>
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <netdb.h>
	#include <arpa/inet.h>

	#define closesocket(s) close(s)

	#ifndef SOCKET
	#define SOCKET int
	#endif

	#ifndef SOCKET_ERROR
	#define SOCKET_ERROR -1
	#endif


	#define SOCK_LAST_ERROR	errno
	#define SOCK_WOULDBLOCK	EAGAIN
	#define SOCK_INPROGRESS	EINPROGRESS


#endif

// Module
#include "sdk/amxxmodule.h"
#include "sdk/CVector.h"
#include "sdk/sh_stack.h"

#define SOCK_TYPE_UDP	0
#define SOCK_TYPE_TCP	1
#define SOCK_TYPE_CHILD	2
#define MAX_LISTEN		10

#define VALID_SOCKET(id) s = get_socket_byid(id); \
	if(!s || !s->sock) { \
	MF_LogError(amx, AMX_ERR_NATIVE, "Invalid Socket: SocketID (%d)", id); \
	return 0; }
