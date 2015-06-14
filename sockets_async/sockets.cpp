#include "sockets.h"

enum {
	STATE_NEW=0,
	STATE_CONNECTING,
	STATE_CONNECTED,
	STATE_LISTEN,
	STATE_CLIENT
};

struct SocketAMXX
{
	byte state;
	int id;
	int customID;
	SOCKET sock;
	SOCKET parent;
	bool tcp;
};

CVector<SocketAMXX *> g_SockAMXX;
CStack<int> g_FreeSlots;

char g_Buff[4096];
int g_CountSockets=0;
float g_NextCheck=0.0;

int fwsockConnected, fwsockClosed, fwsockAccepted, fwsockReadable;

SocketAMXX *get_socket_byid(int id);
void DestroySockets();
void DestroyChildren(SOCKET sock);
int NewSocket(SOCKET sock, SOCKET parem, bool tcp, int customID);
bool FreeSocket(SocketAMXX *s);
int SocketSelect(SOCKET sock, bool &readable, bool &writable, bool &error);


/*=== New Natives ===================================================================*/
// native SOCKET:socket_create(type, customID);
static cell AMX_NATIVE_CALL socket_create(AMX *amx, cell *params)
{
	SOCKET sock=SOCKET_ERROR;
	bool TCP = (params[1] != SOCK_TYPE_UDP);

	sock = socket(AF_INET, TCP?SOCK_STREAM:SOCK_DGRAM, 0);
	if(sock < 0)
		return 0;

	if(TCP)
	{
		#ifdef WIN32

		unsigned long mode = 1;
		if(ioctlsocket(sock, FIONBIO, &mode) != 0)
		{
			closesocket(sock);
			return 0;
		}

		#else

		int flags = fcntl(sock, F_GETFL, 0);
		if(flags == -1)
			flags = 0;

		if(fcntl(sock, F_SETFL, (flags | O_NONBLOCK)) != 0)
		{
			closesocket(sock);
			return 0;
		}

		#endif
	}

	return NewSocket(sock, 0, TCP, params[2]);
}

// native socket_close(SOCKET:socket);
static cell AMX_NATIVE_CALL socket_close(AMX *amx, cell *params)
{
	SocketAMXX *s;
	VALID_SOCKET(params[1]);
	
	FreeSocket(s);
	return 1;
}

// native socket_bind(SOCKET:socket, const local_ip[], port);
static cell AMX_NATIVE_CALL socket_bind(AMX *amx, cell *params)
{
	SocketAMXX *s;
	VALID_SOCKET(params[1]);

	if(s->state != STATE_NEW)
	{
		MF_LogError(amx, AMX_ERR_NATIVE, "Invalid State: SocketID (%d)", params[1]);
		return 0;
	}


	struct sockaddr_in local;
	int len; char *IP;
	memset(&local, 0, sizeof(local));

	IP = MF_GetAmxString(amx, params[2], 0, &len);
	if(len)
		local.sin_addr.s_addr = inet_addr(IP);
	else
		local.sin_addr.s_addr = INADDR_ANY;

	local.sin_family = AF_INET;
	local.sin_port = htons(params[3]);

	if(bind(s->sock, (sockaddr *)&local, sizeof(local)) != 0)
		return 0;

	if(s->tcp)
	{
		listen(s->sock, MAX_LISTEN);

	}

	s->state = STATE_LISTEN;
	
	return 1;
}

// native socket_get_custom(SOCKET:socket)
static cell AMX_NATIVE_CALL socket_get_custom(AMX *amx, cell *params)
{
	SocketAMXX *s;
	VALID_SOCKET(params[1]);
	
	return s->customID;
}

// native socket_set_custom(SOCKET:socket, customID)
static cell AMX_NATIVE_CALL socket_set_custom(AMX *amx, cell *params)
{
	SocketAMXX *s;
	VALID_SOCKET(params[1]);
	
	s->customID = params[2];
	return 1;
}

// native socket_connect(SOCKET:socket, const ip[], port);
static cell AMX_NATIVE_CALL socket_connect(AMX *amx, cell *params)
{
	SocketAMXX *s;
	VALID_SOCKET(params[1]);

	if(!s->tcp || s->state != STATE_NEW)
	{
		MF_LogError(amx, AMX_ERR_NATIVE, "Invalid State or Type: SocketID (%d)", params[1]);
		return 0;
	}

	struct sockaddr_in remote;
	int len; char *hostname;
	unsigned long addr;

	hostname = MF_GetAmxString(amx, params[2], 0, &len);
	if(!len) return -1;

	if((addr = inet_addr(hostname)) != INADDR_NONE)
		remote.sin_addr.s_addr = addr;
	else
	{
		struct hostent *host_info;
		host_info = gethostbyname(hostname);
		if(host_info == NULL)
			return -1;

		memcpy((char *)&remote.sin_addr, host_info->h_addr, host_info->h_length);
	}

	remote.sin_family = AF_INET;
	remote.sin_port = htons(params[3]);

	len = connect(s->sock, (struct sockaddr*)&remote, sizeof(remote));
	if(len != 0 && SOCK_LAST_ERROR != SOCK_WOULDBLOCK)
	{
		MF_LogError(amx, AMX_ERR_NATIVE, "error al crear la conexion: (%d)", SOCK_LAST_ERROR);
		return 0;
	}

	s->state = STATE_CONNECTING;

	return 1;
}

// native socket_send(SOCKET:socket, const data[], sendsize=0);
static cell AMX_NATIVE_CALL socket_send(AMX *amx, cell *params)
{
	SocketAMXX *s;
	VALID_SOCKET(params[1]);

	if(!s->tcp || s->state == STATE_NEW)
	{
		MF_LogError(amx, AMX_ERR_NATIVE, "Invalid State or Type: SocketID (%d)", params[1]);
		return 0;
	}

	int sendsize = params[3];
	cell *pData = MF_GetAmxAddr(amx, params[2]);
	char *pBuff = g_Buff;

	if(!sendsize)
	{
		while((*pBuff++=(char)*pData++))
			sendsize++;
	}
	else {
		while(sendsize--)
			*pBuff++ = (char)*pData++;

		sendsize = params[3];
	}

	return send(s->sock, g_Buff, sendsize, 0);
}

// native socket_recv(SOCKET:socket, data[], maxlen)
static cell AMX_NATIVE_CALL socket_recv(AMX *amx, cell *params)
{
	SocketAMXX *s;
	VALID_SOCKET(params[1]);

	if(!s->tcp || s->state == STATE_NEW)
	{
		MF_LogError(amx, AMX_ERR_NATIVE, "Invalid State or Type: SocketID (%d)", params[1]);
		return 0;
	}

	int maxlen = params[3];
	int received = -1;

	memset(g_Buff, 0, maxlen);
	received = recv(s->sock, g_Buff, maxlen, 0);
	if(received == -1)
		return -1;

	g_Buff[received] = 0;
	int nlen = received;
	int max = maxlen;

	const char* src = g_Buff;
	cell* dest = MF_GetAmxAddr(amx, params[2]);

	while(max-- && nlen--)
		*dest++ = (cell)*src++;
	*dest = 0;

	return received;
}

// native socket_sendto(SOCKET:socket, const ip[], port, const data[], sendsize=0)
static cell AMX_NATIVE_CALL socket_sendto(AMX *amx, cell *params)
{
	SocketAMXX *s;
	VALID_SOCKET(params[1]);

	if(s->tcp)
	{
		MF_LogError(amx, AMX_ERR_NATIVE, "Invalid Type: SocketID (%d)", params[1]);
		return 0;
	}

	struct sockaddr_in remote;
	char *IP; unsigned int port; int len;
	unsigned long addr;

	port = params[3];
	IP = MF_GetAmxString(amx, params[2], 0, &len);

	if((addr = inet_addr(IP)) == INADDR_NONE)
		return -1;

	remote.sin_family = AF_INET;
	remote.sin_port = htons(port);
	remote.sin_addr.s_addr = addr;

	int sendsize = params[5];
	cell *pData = MF_GetAmxAddr(amx, params[4]);
	char *pBuff = g_Buff;

	if(!sendsize)
	{
		while((*pBuff++=(char)*pData++))
			sendsize++;
	}
	else {
		while(sendsize--)
			*pBuff++ = (char)*pData++;

		sendsize = params[5];
	}

	return sendto(s->sock, g_Buff, sendsize, 0, (struct sockaddr *)&remote, sizeof(remote));
}

// socket_recvfrom(SOCKET:socket, data[], maxlen, ip[], len, &port)
static cell AMX_NATIVE_CALL socket_recvfrom(AMX *amx, cell *params)
{
	SocketAMXX *s;
	VALID_SOCKET(params[1]);

	if(s->tcp)
	{
		MF_LogError(amx, AMX_ERR_NATIVE, "Invalid Type: SocketID (%d)", params[1]);
		return 0;
	}

	int maxlen = params[3];
	int received = -1;
	struct sockaddr_in cliaddr;
	socklen_t clilen = sizeof cliaddr;

	memset(g_Buff, 0, maxlen);

	received = recvfrom(s->sock, g_Buff, maxlen, 0, (struct sockaddr *)&cliaddr, &clilen);
	if(received == -1)
		return -1;

	g_Buff[received] = '\0';
	int nlen = received;
	int max = maxlen;

	const char* src = g_Buff;
	cell* dest = MF_GetAmxAddr(amx, params[2]);

	while(max-- && nlen--)
		*dest++ = (cell)*src++;
	*dest = 0;

	MF_SetAmxString(amx, params[4], inet_ntoa(cliaddr.sin_addr), params[5]);
	params[6] = (cell)ntohs(cliaddr.sin_port);

	return received;
}

AMX_NATIVE_INFO sockets_natives[] = {
	{"socket_create", socket_create},
	{"socket_close", socket_close},
	{"socket_bind", socket_bind},
	{"socket_get_custom", socket_get_custom},
	{"socket_set_custom", socket_set_custom},
	{"socket_connect", socket_connect},
	{"socket_send", socket_send},
	{"socket_recv", socket_recv},
	{"socket_sendto", socket_sendto},
	{"socket_recvfrom", socket_recvfrom},
	{NULL, NULL}
};

SocketAMXX *get_socket_byid(int id)
{
	id -= 1;
	if(id < 0 || size_t(id) >= g_SockAMXX.size())
		return NULL;

	return g_SockAMXX[id];
}

void DestroySockets()
{
	for(size_t slot = 0; slot < g_SockAMXX.size(); slot++)
	{
		if(g_SockAMXX[slot] && g_SockAMXX[slot]->sock)
			closesocket(g_SockAMXX[slot]->sock);

		delete g_SockAMXX[slot];
	}
	g_SockAMXX.clear();

	while(!g_FreeSlots.empty())
	{
		g_FreeSlots.pop();
	}

	g_CountSockets = 0;
}

void DestroyChildren(SOCKET sock)
{
	for(size_t slot = 0; slot < g_SockAMXX.size(); slot++)
	{
		if(g_SockAMXX[slot] && g_SockAMXX[slot]->parent == sock)
			FreeSocket(g_SockAMXX[slot]);
	}
}

int NewSocket(SOCKET sock, SOCKET parem, bool tcp, int customID)
{
	SocketAMXX *s = new SocketAMXX;

	s->state	= STATE_NEW;
	s->sock		= sock;
	s->parent	= parem;
	s->tcp		= tcp;
	s->customID	= customID;

	g_CountSockets++;

	if(g_FreeSlots.empty())
	{
		g_SockAMXX.push_back(s);
		s->id = (int)g_SockAMXX.size() - 1;
	}
	else
	{
		int slot = g_FreeSlots.front();
		g_FreeSlots.pop();

		s->id = slot;
		g_SockAMXX[slot] = s;
	}

	return s->id+1;
}

bool FreeSocket(SocketAMXX *s)
{
	if(!s) return false;

	if(s->sock)
	{
		if(s->state == STATE_LISTEN)
			DestroyChildren(s->sock);

		char error; socklen_t len=sizeof error;
		if(getsockopt(s->sock, SOL_SOCKET, SO_ERROR, &error, &len) != 0)
			error = -111;

		closesocket(s->sock);
		g_CountSockets--;
		s->sock = 0;

		MF_ExecuteForward(fwsockClosed, s->id+1, s->customID, SOCK_LAST_ERROR);
	}

	g_SockAMXX[s->id] = NULL;
	g_FreeSlots.push(s->id);

	delete s;

	return true;
}

/*== Sockets Asynchronous =========================================================*/
int SocketSelect(SOCKET sock, bool &readable, bool &writable, bool &error)
{
	fd_set readfds, writefds, exceptfds;
	int rtn;

	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	FD_ZERO(&exceptfds);

	FD_SET(sock, &readfds);
	FD_SET(sock, &writefds);
	FD_SET(sock, &exceptfds);

	struct timeval tv;
	tv.tv_sec=0;
	tv.tv_usec=0;

	rtn = select(sock+1, &readfds, &writefds, &exceptfds, &tv);

	readable = FD_ISSET(sock, &readfds) != 0;
	writable = FD_ISSET(sock, &writefds) != 0;
	error	 = FD_ISSET(sock, &exceptfds) != 0;

	return rtn;
}

void StartFrame()
{
	if(g_CountSockets && (g_NextCheck < gpGlobals->time))
	{
		SocketAMXX *s; SOCKET cl_sock;
		bool readable, writable, error;

		g_NextCheck = gpGlobals->time + 0.03;
		
		for(size_t slot = 0; slot < g_SockAMXX.size(); slot++)
		{
			s = g_SockAMXX[slot];
			if(!s) continue;

			SocketSelect(s->sock, readable, writable, error);

			if(error)
			{
				FreeSocket(s);
				continue;
			}

			if(s->tcp)
			{
				if(s->state == STATE_NEW)
					continue;

				if(s->state == STATE_CONNECTING)
				{
					if(writable)
					{
						s->state = STATE_CONNECTED;
						MF_ExecuteForward(fwsockConnected, s->id+1, s->customID);
					}
				}
				else if(s->state == STATE_LISTEN)
				{
					if(readable)
					{
						struct sockaddr_in cliaddr;
						socklen_t clilen = sizeof cliaddr;

						cl_sock = accept(s->sock, (struct sockaddr *)&cliaddr, &clilen);
						if(cl_sock > 0)
						{
							cl_sock = NewSocket(cl_sock, s->sock, true, 0);
							g_SockAMXX[cl_sock-1]->state = STATE_CLIENT;

							const char *IP = inet_ntoa(cliaddr.sin_addr);
							if(!IP) IP="";

							MF_ExecuteForward(fwsockAccepted, s->id+1, s->customID, cl_sock, IP, (cell)cliaddr.sin_port);
						}
						else
						{
							MF_ExecuteForward(fwsockReadable, s->id+1, s->customID, SOCK_TYPE_TCP);
						}
					}
				}
				else if(readable)
				{
					MF_ExecuteForward(fwsockReadable, s->id+1, s->customID, s->parent?SOCK_TYPE_CHILD:SOCK_TYPE_TCP);
				}
			}
			else
			{
				if(readable)
				{
					MF_ExecuteForward(fwsockReadable, s->id+1, s->customID, SOCK_TYPE_UDP);
				}
			}
		}
	}

	RETURN_META(MRES_IGNORED);
}
