#define TARGET_SOL_SOCKET     0xffff

#define TARGET_SO_DEBUG       0x0001 /* Record debugging information.  */
#define TARGET_SO_REUSEADDR   0x0004 /* Allow reuse of local addresses.  */
#define TARGET_SO_KEEPALIVE   0x0008 /* Keep connections alive and send
                                        SIGPIPE when they die. */
#define TARGET_SO_DONTROUTE   0x0010 /* Don't do local routing. */
#define TARGET_SO_BROADCAST   0x0020 /* Allow transmission of
                                        broadcast messages. */
#define TARGET_SO_LINGER      0x0080 /* Block on close of a reliable
                                        socket to transmit pending data. */
#define TARGET_SO_OOBINLINE   0x0100 /* Receive out-of-band data in-band. */
#define TARGET_SO_REUSEPORT   0x0200 /* Allow local address and port reuse. */
#define TARGET_SO_TYPE        0x1008 /* Compatible name for TARGET_SO_STYLE. */
#define TARGET_SO_STYLE       TARGET_SO_TYPE /* Synonym */
#define TARGET_SO_ERROR       0x1007         /* get error status and clear */
#define TARGET_SO_SNDBUF      0x1001         /* Send buffer size. */
#define TARGET_SO_RCVBUF      0x1002         /* Receive buffer. */
#define TARGET_SO_SNDLOWAT    0x1003         /* send low-water mark */
#define TARGET_SO_RCVLOWAT    0x1004         /* receive low-water mark */
#define TARGET_SO_SNDTIMEO    0x1005         /* send timeout */
#define TARGET_SO_RCVTIMEO    0x1006         /* receive timeout */
#define TARGET_SO_ACCEPTCONN  0x1009
#define TARGET_SO_PROTOCOL    0x1028         /* protocol type */
#define TARGET_SO_DOMAIN      0x1029         /* domain/socket family */
#define TARGET_SO_NO_CHECK                      11
#define TARGET_SO_PRIORITY                      12
#define TARGET_SO_BSDCOMPAT                     14
#define TARGET_SO_PASSCRED                      17
#define TARGET_SO_PEERCRED                      18
#define TARGET_SO_SECURITY_AUTHENTICATION       22
#define TARGET_SO_SECURITY_ENCRYPTION_TRANSPORT 23
#define TARGET_SO_SECURITY_ENCRYPTION_NETWORK   24
#define TARGET_SO_BINDTODEVICE                  25
#define TARGET_SO_ATTACH_FILTER                 26
#define TARGET_SO_DETACH_FILTER                 27
#define TARGET_SO_GET_FILTER                    TARGET_SO_ATTACH_FILTER
#define TARGET_SO_PEERNAME                      28
#define TARGET_SO_TIMESTAMP                     29
#define TARGET_SCM_TIMESTAMP                    TARGET_SO_TIMESTAMP
#define TARGET_SO_PEERSEC                       30
#define TARGET_SO_SNDBUFFORCE                   31
#define TARGET_SO_RCVBUFFORCE                   33
#define TARGET_SO_PASSSEC                       34
#define TARGET_SO_TIMESTAMPNS                   35
#define TARGET_SCM_TIMESTAMPNS                  TARGET_SO_TIMESTAMPNS
#define TARGET_SO_MARK                          36
#define TARGET_SO_TIMESTAMPING                  37
#define TARGET_SCM_TIMESTAMPING                 TARGET_SO_TIMESTAMPING
#define TARGET_SO_RXQ_OVFL                      40
#define TARGET_SO_WIFI_STATUS                   41
#define TARGET_SCM_WIFI_STATUS                  TARGET_SO_WIFI_STATUS
#define TARGET_SO_PEEK_OFF                      42
#define TARGET_SO_NOFCS                         43
#define TARGET_SO_LOCK_FILTER                   44
#define TARGET_SO_SELECT_ERR_QUEUE              45
#define TARGET_SO_BUSY_POLL                     46
#define TARGET_SO_MAX_PACING_RATE               47
#define TARGET_SO_BPF_EXTENSIONS                48
#define TARGET_SO_INCOMING_CPU                  49
#define TARGET_SO_ATTACH_BPF                    50
#define TARGET_SO_DETACH_BPF                    TARGET_SO_DETACH_FILTER
#define TARGET_SO_ATTACH_REUSEPORT_CBPF         51
#define TARGET_SO_ATTACH_REUSEPORT_EBPF         52
#define TARGET_SO_CNX_ADVICE                    53
#define TARGET_SCM_TIMESTAMPING_OPT_STATS       54
#define TARGET_SO_MEMINFO                       55
#define TARGET_SO_INCOMING_NAPI_ID              56
#define TARGET_SO_COOKIE                        57
#define TARGET_SCM_TIMESTAMPING_PKTINFO         58
#define TARGET_SO_PEERGROUPS                    59
#define TARGET_SO_ZEROCOPY                      60

/** sock_type - Socket types
 *
 * Please notice that for binary compat reasons MIPS has to
 * override the enum sock_type in include/linux/net.h, so
 * we define ARCH_HAS_SOCKET_TYPES here.
 *
 * @SOCK_DGRAM - datagram (conn.less) socket
 * @SOCK_STREAM - stream (connection) socket
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
    TARGET_SOCK_DGRAM       = 1,
    TARGET_SOCK_STREAM      = 2,
    TARGET_SOCK_RAW         = 3,
    TARGET_SOCK_RDM         = 4,
    TARGET_SOCK_SEQPACKET   = 5,
    TARGET_SOCK_DCCP        = 6,
    TARGET_SOCK_PACKET      = 10,
    TARGET_SOCK_CLOEXEC     = 02000000,
    TARGET_SOCK_NONBLOCK    = 0x0080,
};

#define TARGET_SOCK_MAX (TARGET_SOCK_PACKET + 1)
#define TARGET_SOCK_TYPE_MASK 0xf  /* Covers up to TARGET_SOCK_MAX - 1. */

#define ARCH_HAS_SOCKET_TYPES 1
