/*
 * Emulation for Rasp PI GPIO via Server connected to via Socket
 *
 */
#include "qemu/osdep.h"

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#ifdef __MINGW32__
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include "qemu/PanelEmu.h"

typedef enum
{
    PROTOCOLDESCFROMQEMU = 0,
    PROTOCOLDESCFROMPANEL = 1,
    PINSTOPANEL = 2,
    READREQ = 3,
    PINCOUNT = 4,
    ENABLEMAP = 5,
    INPUTMAP = 6,
    OUTPUTMAP = 7,
    PINSTOQEMU = 8
} PacketType;

#define MINPROTOCOL 0
#define MAXPROTOCOL 0

#define MAXPACKET   255

#define PACKETLEN   0  /* Includes Packet Length */
#define PACKETTYPE  1

typedef struct
{
    unsigned short int Data[MAXPACKET];
} CommandPacket;

static void panel_command(panel_connection_t *h, CommandPacket *Pkt);

static void panel_send_protocol_command(panel_connection_t *h)
{
    CommandPacket Pkt;

    Pkt.Data[PACKETLEN] = 8;
    Pkt.Data[PACKETTYPE] = PROTOCOLDESCFROMQEMU;
    Pkt.Data[2] = MINPROTOCOL;
    Pkt.Data[3] = MAXPROTOCOL;

    panel_command(h, &Pkt);
}

void panel_send_read_command(panel_connection_t *h)
{
    CommandPacket Pkt;

    Pkt.Data[PACKETLEN] = 4;
    Pkt.Data[PACKETTYPE] = READREQ;

    panel_command(h, &Pkt);
}

/* Set a pin to a specified value */
void senddatatopanel(panel_connection_t *h, uint64_t pin, bool val)
{
    CommandPacket Pkt;

    Pkt.Data[PACKETLEN] = (char *)&Pkt.Data[6 + 1] - (char *)&Pkt.Data[0];
    Pkt.Data[PACKETTYPE] = PINSTOPANEL;
    Pkt.Data[2] = (unsigned short int)(pin & 0xFFFF);
    Pkt.Data[3] = (unsigned short int)((pin >> 16) & 0xFFFF);
    Pkt.Data[4] = (unsigned short int)(pin >> 32 & 0xFFFF);
    Pkt.Data[5] = (unsigned short int)((pin >> 48) & 0xFFFF);
    Pkt.Data[6] = val;

    panel_command(h, &Pkt);
}

void sendpincount(panel_connection_t *h, int val)
{
    CommandPacket Pkt;

    Pkt.Data[PACKETLEN] = (char *)&Pkt.Data[2 + 1] - (char *)&Pkt.Data[0];
    Pkt.Data[PACKETTYPE] = PINCOUNT;
    Pkt.Data[2] = val;

    panel_command(h, &Pkt);
}

void sendenabledmap(panel_connection_t *h, uint64_t pin)
{
    CommandPacket Pkt;

    Pkt.Data[PACKETLEN] = (char *)&Pkt.Data[5 + 1] - (char *)&Pkt.Data[0];
    Pkt.Data[PACKETTYPE] = ENABLEMAP;
    Pkt.Data[2] = (unsigned short int)(pin & 0xFFFF);
    Pkt.Data[3] = (unsigned short int)((pin >> 16) & 0xFFFF);
    Pkt.Data[4] = (unsigned short int)(pin >> 32 & 0xFFFF);
    Pkt.Data[5] = (unsigned short int)((pin >> 48) & 0xFFFF);

    panel_command(h, &Pkt);
}

void sendinputmap(panel_connection_t *h, uint64_t pin)
{
    CommandPacket Pkt;

    Pkt.Data[PACKETLEN] = (char *)&Pkt.Data[5 + 1] - (char *)&Pkt.Data[0];
    Pkt.Data[PACKETTYPE] = INPUTMAP;
    Pkt.Data[2] = (unsigned short int)(pin & 0xFFFF);
    Pkt.Data[3] = (unsigned short int)((pin >> 16) & 0xFFFF);
    Pkt.Data[4] = (unsigned short int)(pin >> 32 & 0xFFFF);
    Pkt.Data[5] = (unsigned short int)((pin >> 48) & 0xFFFF);

    panel_command(h, &Pkt);
}

void sendoutputmap(panel_connection_t *h, uint64_t pin)
{
    CommandPacket Pkt;

    Pkt.Data[PACKETLEN] = (char *)&Pkt.Data[5 + 1] - (char *)&Pkt.Data[0];
    Pkt.Data[PACKETTYPE] = OUTPUTMAP;
    Pkt.Data[2] = (unsigned short int)(pin & 0xFFFF);
    Pkt.Data[3] = (unsigned short int)((pin >> 16) & 0xFFFF);
    Pkt.Data[4] = (unsigned short int)(pin >> 32 & 0xFFFF);
    Pkt.Data[5] = (unsigned short int)((pin >> 48) & 0xFFFF);

    panel_command(h, &Pkt);
}

static void panel_command(panel_connection_t *h, CommandPacket *Pkt)
{
    if (send(h->socket, (char *)Pkt, Pkt->Data[PACKETLEN], 0) == -1) {
        perror(PANEL_NAME "send");
#ifdef __MINGW32__
        closesocket(h->socket);
#else
        close(h->socket);
#endif
        h->socket = -1; /* act like we never connected */
    }
}

/* Wait for values to be read back from panel */
bool panel_read(panel_connection_t *h, uint64_t* Data)
{
    fd_set rfds, efds;
    int LengthInBuffer;
    int select_res = 0;

    CommandPacket *PktPtr = (CommandPacket *)malloc(sizeof(CommandPacket));
    CommandPacket *Pkt;
    bool NoError = true;
    bool NewData = false;
    bool NoData = false;
    struct timeval timeout;

    int ReadStart = 0;

    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    if (h->socket != -1) {
        rfds = h->fds;
        efds = h->fds;

        Pkt = PktPtr;
        while (NoError&&! NoData) {
            select_res = select(h->socket + 1, &rfds, NULL, &efds, &timeout);
            if (select_res > 0) {
                if (FD_ISSET(h->socket, &rfds)) {
                    /* receive more data */
                    LengthInBuffer = recv(h->socket, (char *)&Pkt[ReadStart],
                                    sizeof(*Pkt) - ReadStart, 0);
                    if (LengthInBuffer > 0) {
                        LengthInBuffer += ReadStart;
                        for (int i = 0; LengthInBuffer > 0; i ++) {
                            if (LengthInBuffer >= Pkt->Data[i + PACKETLEN]) {
                                switch (Pkt->Data[i + PACKETTYPE]) {
                                case PINSTOQEMU:
                                    *Data = (uint64_t)Pkt->Data[i + 2];
                                    *Data |= ((uint64_t)Pkt->Data[i + 3]) << 16;
                                    *Data |= ((uint64_t)Pkt->Data[i + 4]) << 32;
                                    *Data |= ((uint64_t)Pkt->Data[i + 5]) << 48;

                                    NewData = true;
                                    break;

                                case PROTOCOLDESCFROMPANEL:
                                    h->ProtocolInUse = (int)Pkt->Data[i + 2];
                                    if (h->ProtocolInUse != -1) {
                                        printf(PANEL_NAME "Protocol %d\n",
                                               h->ProtocolInUse);
                                    } else {
                                        printf(PANEL_NAME "No Common Pcol\n");
                                    }
                                    break;

                                default:
                                    printf(PANEL_NAME "Invalid data receive\n");
                                    break;
                                }
                                LengthInBuffer -= Pkt->Data[PACKETLEN];
                                i += Pkt->Data[PACKETLEN];
                            } else {
                                ReadStart = LengthInBuffer;
                                for (int j = 0; j < LengthInBuffer; j ++) {
                                    Pkt->Data[j] = Pkt->Data[i + j];
                                }
                                printf(PANEL_NAME "Partial Packet Read");
                            }
                        }
                    } else {
                        if (LengthInBuffer < 0) {
                            if (errno != EINTR) {
                                printf(PANEL_NAME "recv");
                                NoError = FALSE;
                            }
                        } else {
                            printf(PANEL_NAME "closed connection\n");
                            NoError = FALSE;
                        }
                    }
                }
            } else if (select_res == 0) {
                NoData = true;
            } else if (errno != EINTR) {
#ifdef __MINGW32__
                closesocket(h->socket);
#else
                close(h->socket);
#endif
                h->socket = -1; /* act like we never connected */
                perror(PANEL_NAME "select error");
                NoError = FALSE;
            }
        }
    }

    free(PktPtr);

    return NewData;
}

bool panel_open(panel_connection_t *h)
{
    int rv;
#ifdef __MINGW32__
    struct sockaddr_in remote;
#else
    struct sockaddr_in remote;
#endif

    bool returnval = false;

#ifdef __MINGW32__
    printf("__MINGW32__\n");
#else
    printf("NOT __MINGW32__\n");
#endif

    h->socket = -1;
    h->ProtocolInUse = -1;

#ifdef __MINGW32__
    WSADATA wsadata;
    if (WSAStartup(MAKEWORD(1, 1), &wsadata) == SOCKET_ERROR) {
        printf("Error creating socket.\n");
    } else {
#endif
        h->socket = socket(AF_INET, SOCK_STREAM, 0);
        if (h->socket != -1) {
#ifdef __MINGW32__
            memset((char *)&remote, 0, sizeof(remote));
            remote.sin_family = AF_INET;
            remote.sin_port = htons(DEFAULT_PORT);
            remote.sin_addr.s_addr = inet_addr("127.0.0.1");
#else
            memset((char *)&remote, 0, sizeof(remote));
            remote.sin_family = AF_INET;
            remote.sin_port = htons(DEFAULT_PORT);
            remote.sin_addr.s_addr = inet_addr("127.0.0.1");
#endif
            rv = connect(h->socket,
                            (struct sockaddr *)&remote, sizeof(remote));
            if (rv != -1) {
#ifdef __MINGW32__
                char value = 1;
                setsockopt(h->socket, IPPROTO_TCP, TCP_NODELAY,
                           &value, sizeof(value));
#endif
                FD_ZERO(&h->fds);

                /* Set our connected socket */
                FD_SET(h->socket, &h->fds);

                printf(PANEL_NAME "Connected OK %d\n", rv);

                panel_send_protocol_command(h);

                returnval = true;
            } else {
                printf(PANEL_NAME "connection Fails %d\n", rv);
#ifdef __MINGW32__
                closesocket(h->socket);
#else
                close(h->socket);
#endif
                h->socket = -1;
            }
        }
#ifdef __MINGW32__
    }
#endif
    return returnval;
}
