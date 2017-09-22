#define TARGET_SOL_SOCKET                        0xffff

#define TARGET_SO_DEBUG                          0x0001
#define TARGET_SO_PASSCRED                       0x0002
#define TARGET_SO_REUSEADDR                      0x0004
#define TARGET_SO_KEEPALIVE                      0x0008
#define TARGET_SO_DONTROUTE                      0x0010
#define TARGET_SO_BROADCAST                      0x0020
#define TARGET_SO_PEERCRED                       0x0040
#define TARGET_SO_LINGER                         0x0080
#define TARGET_SO_OOBINLINE                      0x0100
#define TARGET_SO_REUSEPORT                      0x0200
#define TARGET_SO_BSDCOMPAT                      0x0400
#define TARGET_SO_RCVLOWAT                       0x0800
#define TARGET_SO_SNDLOWAT                       0x1000
#define TARGET_SO_RCVTIMEO                       0x2000
#define TARGET_SO_SNDTIMEO                       0x4000
#define TARGET_SO_ACCEPTCONN                     0x8000
#define TARGET_SO_SNDBUF                         0x1001
#define TARGET_SO_RCVBUF                         0x1002
#define TARGET_SO_SNDBUFFORCE                    0x100a
#define TARGET_SO_RCVBUFFORCE                    0x100b
#define TARGET_SO_ERROR                          0x1007
#define TARGET_SO_TYPE                           0x1008
#define TARGET_SO_PROTOCOL                       0x1028
#define TARGET_SO_DOMAIN                         0x1029
#define TARGET_SO_NO_CHECK                       0x000b
#define TARGET_SO_PRIORITY                       0x000c
#define TARGET_SO_BINDTODEVICE                   0x000d
#define TARGET_SO_ATTACH_FILTER                  0x001a
#define TARGET_SO_DETACH_FILTER                  0x001b
#define TARGET_SO_GET_FILTER                     TARGET_SO_ATTACH_FILTER
#define TARGET_SO_PEERNAME                       0x001c
#define TARGET_SO_TIMESTAMP                      0x001d
#define TARGET_SCM_TIMESTAMP                     TARGET_SO_TIMESTAMP
#define TARGET_SO_PEERSEC                        0x001e
#define TARGET_SO_PASSSEC                        0x001f
#define TARGET_SO_TIMESTAMPNS                    0x0021
#define TARGET_SCM_TIMESTAMPNS                   TARGET_SO_TIMESTAMPNS
#define TARGET_SO_MARK                           0x0022
#define TARGET_SO_TIMESTAMPING                   0x0023
#define TARGET_SCM_TIMESTAMPING                  TARGET_SO_TIMESTAMPING
#define TARGET_SO_RXQ_OVFL                       0x0024
#define TARGET_SO_WIFI_STATUS                    0x0025
#define TARGET_SCM_WIFI_STATUS                   TARGET_SO_WIFI_STATUS
#define TARGET_SO_PEEK_OFF                       0x0026
#define TARGET_SO_NOFCS                          0x0027
#define TARGET_SO_LOCK_FILTER                    0x0028
#define TARGET_SO_SELECT_ERR_QUEUE               0x0029
#define TARGET_SO_BUSY_POLL                      0x0030
#define TARGET_SO_MAX_PACING_RATE                0x0031
#define TARGET_SO_BPF_EXTENSIONS                 0x0032
#define TARGET_SO_INCOMING_CPU                   0x0033
#define TARGET_SO_ATTACH_BPF                     0x0034
#define TARGET_SO_DETACH_BPF                     TARGET_SO_DETACH_FILTER
#define TARGET_SO_ATTACH_REUSEPORT_CBPF          0x0035
#define TARGET_SO_ATTACH_REUSEPORT_EBPF          0x0036
#define TARGET_SO_CNX_ADVICE                     0x0037
#define TARGET_SCM_TIMESTAMPING_OPT_STATS        0x0038
#define TARGET_SO_MEMINFO                        0x0039
#define TARGET_SO_INCOMING_NAPI_ID               0x003a
#define TARGET_SO_COOKIE                         0x003b
#define TARGET_SCM_TIMESTAMPING_PKTINFO          0x003c
#define TARGET_SO_PEERGROUPS                     0x003d
#define TARGET_SO_ZEROCOPY                       0x003e
#define TARGET_SO_SECURITY_AUTHENTICATION        0x5001
#define TARGET_SO_SECURITY_ENCRYPTION_TRANSPORT  0x5002
#define TARGET_SO_SECURITY_ENCRYPTION_NETWORK    0x5004

/** sock_type - Socket types
 *
 * Please notice that for binary compat reasons SPARC has to
 * override the enum sock_type in include/linux/net.h, so
 * we define ARCH_HAS_SOCKET_TYPES here.
 *
 * @SOCK_STREAM - stream (connection) socket
 * @SOCK_DGRAM - datagram (conn.less) socket
 * @SOCK_RAW - raw socket
 * @SOCK_RDM - reliably-delivered message
 * @SOCK_SEQPACKET - sequential packet socket
 * @SOCK_DCCP - Datagram Congestion Control Protocol socket
 * @SOCK_PACKET - linux specific way of getting packets at the dev level.
 *                For writing rarp and other similar things on the user
 *                level.
 * @SOCK_CLOEXEC - sets the close-on-exec (FD_CLOEXEC) flag.
 * @SOCK_NONBLOCK - sets the O_NONBLOCK file status flag.
 */

enum sock_type {
    TARGET_SOCK_STREAM      = 1,
    TARGET_SOCK_DGRAM       = 2,
    TARGET_SOCK_RAW         = 3,
    TARGET_SOCK_RDM         = 4,
    TARGET_SOCK_SEQPACKET   = 5,
    TARGET_SOCK_DCCP        = 6,
    TARGET_SOCK_PACKET      = 10,
    TARGET_SOCK_CLOEXEC     = 0x400000,
    TARGET_SOCK_NONBLOCK    = 0x4000,
};

#define TARGET_SOCK_MAX (TARGET_SOCK_PACKET + 1)
#define TARGET_SOCK_TYPE_MASK 0xf  /* Covers up to TARGET_SOCK_MAX - 1. */

#define ARCH_HAS_SOCKET_TYPES 1
