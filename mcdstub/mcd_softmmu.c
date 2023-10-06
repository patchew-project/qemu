/*
#if defined(WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
//#pragma comment(lib, "Ws2_32.lib")
#define ISVALIDSOCKET(s) ((s) != INVALID_SOCKET)
#define CLOSESOCKET(s) closesocket(s)
#define GETSOCKETERRNO() (WSAGetLastError())
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
//#include <errno.h>
#define SOCKET int
#define ISVALIDSOCKET(s) ((s) >= 0)
#define CLOSESOCKET(s) close(s)
#define GETSOCKETERRNO() (errno)
#endif

#define SA struct sockaddr



#include "exec/mcdstub.h"
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "gdbstub/syscalls.h"
#include "exec/hwaddr.h"
#include "exec/tb-flush.h"
#include "sysemu/cpus.h"
#include "sysemu/runstate.h"
#include "sysemu/replay.h"
#include "hw/core/cpu.h"
#include "hw/cpu/cluster.h"
#include "hw/boards.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "monitor/monitor.h"
#include "internals.h"

//here only deprecated code:

int old_mcdserver_start(const char *device)
{
    //the device is a char array. if its "default" we use tcp with the default DEFAULT_MCDSTUB_PORT. Otherwise it has to look like "tcp::<tcpport>"
    char tcp_port[MX_INPUT_LENGTH];
    int error;
    error = mcd_extract_tcp_port_num(device, tcp_port);
    if (error != 0) {
        return -1;
    }
    int tcp_port_num = atoi(tcp_port);
        
    if (!mcdserver_state.init) {
        mcd_init_mcdserver_state();
    }
    return mcd_open_tcp_socket(tcp_port_num);
}

int mcd_open_tcp_socket(int tcp_port)
//soon to be deprecated (hopefully)
{
    SOCKET socked_fd, connect_fd;
	struct sockaddr_in server_address, client_address;

#if defined(WIN32)
	WSADATA d;
	if (WSAStartup(MAKEWORD(2, 2), &d)) {
    	return -1;
	}
	int len;
#else
	unsigned int len;
#endif

	// socket create and verification
	socked_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (!ISVALIDSOCKET(socked_fd)) {
		return -1;
	}
	memset(&server_address, 0, sizeof(server_address));

	// assign IP, PORT
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(tcp_port);
	server_address.sin_addr.s_addr = htonl(INADDR_ANY);

	// Binding newly created socket to given IP and verification
	if ((bind(socked_fd, (SA*)&server_address, sizeof(server_address))) != 0) {
		CLOSESOCKET(socked_fd);
		return -1;
	}

	// Now server is ready to listen and verification
	if ((listen(socked_fd, 5)) != 0) {
		CLOSESOCKET(socked_fd);
		return -1;
	}
	else {
		printf("TCP server listening on port %d\n", tcp_port);
	}

	//accepting connection
	len = sizeof(client_address);
	connect_fd = accept(socked_fd, (SA*)&client_address, &len);
    if (!ISVALIDSOCKET(connect_fd)) {
		CLOSESOCKET(socked_fd);
        return -1;
    }

	//lets do the handshake

	char buff[MCD_TCP_DATALEN];
	char expected_buff[MCD_TCP_DATALEN];

	memset(buff, 0, sizeof(buff));
	memset(expected_buff, 0, sizeof(buff));
	strcpy((char*)expected_buff, "initializing handshake");

    // read the message from client
    recv(connect_fd, buff, MCD_TCP_DATALEN, 0);
	
	if (strcmp(buff, expected_buff)==0) {
		strcpy((char*)buff, "shaking your hand");
		send(connect_fd, buff, MCD_TCP_DATALEN, 0);
		printf("handshake complete\n");
		return 0;
	}
	else {
		CLOSESOCKET(socked_fd);
		CLOSESOCKET(connect_fd);
		return -1;
	}
}

int mcd_extract_tcp_port_num(const char *in_string, char *out_string)
{
    int string_length = strlen(in_string);
    if (string_length>MX_INPUT_LENGTH+1) {
        return -1;
    }

    const char default_str[] = "default";

    if ((string_length==strlen(default_str)) && (strcmp(default_str, in_string)==0)) {
        strcpy((char*)out_string, DEFAULT_MCDSTUB_PORT);
        return 0;
    }
    else if (strcmp("tcp::", in_string)==0) {
            for (int index = 5; index < string_length; index++) {
                if (!isdigit(in_string[index])) {
                    return -1;
                }
            }
    }
    else {
        return -1;
    }
    strcpy((char*)out_string, in_string+5);
    return 0;
}

*/