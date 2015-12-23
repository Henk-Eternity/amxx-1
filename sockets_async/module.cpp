#include "sockets.h"

extern AMX_NATIVE_INFO sockets_natives[];
extern float g_NextCheck;
extern void DestroySockets();

/* Forwards */
extern int fwsockConnected;
extern int fwsockClosed;
extern int fwsockAccepted;
extern int fwsockReadable;
extern int fwsockWritable;

void OnAmxxAttach()
{
	#ifdef _WIN32
		short wVersionRequested;
		WSADATA wsaData;
		wVersionRequested = MAKEWORD (2, 0);
		if(WSAStartup(wVersionRequested, &wsaData) != 0)
		{
			MF_Log("Sockets Async: WSAStartup ERROR");
			return;
		}
	#endif

	MF_AddNatives(sockets_natives);
}

void OnAmxxDetach()
{
	#ifdef _WIN32
		WSACleanup();
	#endif

	DestroySockets();
}

/*
forward fw_sockConnected(SOCKET:socket, customID)
forward fw_sockClosed(SOCKET:socket, customID, error)
forward fw_sockAccepted(SOCKET:socket, customID, SOCKET:cl_sock, cl_ip[], cl_port)
forward fw_sockReadable(SOCKET:socket, customID, type)
forward fw_sockWritable(SOCKET:socket, customID, type)
*/
void OnPluginsLoaded()
{
	fwsockConnected = 0;
	fwsockClosed	= 0;
	fwsockAccepted	= 0;
	fwsockReadable	= 0;
	fwsockWritable	= 0;

	g_NextCheck		= 0.0f;

	fwsockConnected	= MF_RegisterForward("fw_sockConnected", ET_IGNORE, FP_CELL, FP_CELL, FP_DONE);
	fwsockClosed	= MF_RegisterForward("fw_sockClosed", ET_IGNORE, FP_CELL, FP_CELL, FP_CELL, FP_DONE);
	fwsockAccepted	= MF_RegisterForward("fw_sockAccepted", ET_IGNORE, FP_CELL, FP_CELL, FP_CELL, FP_STRING, FP_CELL, FP_DONE);
	fwsockReadable	= MF_RegisterForward("fw_sockReadable", ET_IGNORE, FP_CELL, FP_CELL, FP_CELL, FP_DONE);
	fwsockWritable	= MF_RegisterForward("fw_sockWritable", ET_IGNORE, FP_CELL, FP_CELL, FP_CELL, FP_DONE);
	DestroySockets();
}