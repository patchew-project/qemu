#if defined(TARGET_MIPS) || defined(TARGET_MIPS64)
#include "mips/sockbits.h"
#elif defined(TARGET_ALPHA)
#include "alpha/sockbits.h"
#elif defined(TARGET_HPPA)
#include "hppa/sockbits.h"
#elif defined(TARGET_SPARC) || defined(TARGET_SPARC64)
#include "sparc/sockbits.h"
#else

    /* For setsockopt(2) */
    #define TARGET_SOL_SOCKET      1

    #define TARGET_SO_DEBUG        1
    #define TARGET_SO_REUSEADDR    2
    #define TARGET_SO_TYPE         3
    #define TARGET_SO_ERROR        4
    #define TARGET_SO_DONTROUTE    5
    #define TARGET_SO_BROADCAST    6
    #define TARGET_SO_SNDBUF       7
    #define TARGET_SO_RCVBUF       8
    #define TARGET_SO_SNDBUFFORCE  32
    #define TARGET_SO_RCVBUFFORCE  33
    #define TARGET_SO_KEEPALIVE    9
    #define TARGET_SO_OOBINLINE    10
    #define TARGET_SO_NO_CHECK     11
    #define TARGET_SO_PRIORITY     12
    #define TARGET_SO_LINGER       13
    #define TARGET_SO_BSDCOMPAT    14
    #define TARGET_SO_REUSEPORT    15
#if defined(TARGET_PPC) || defined(TARGET_PPC64)
    #define TARGET_SO_RCVLOWAT     16
    #define TARGET_SO_SNDLOWAT     17
    #define TARGET_SO_RCVTIMEO     18
    #define TARGET_SO_SNDTIMEO     19
    #define TARGET_SO_PASSCRED     20
    #define TARGET_SO_PEERCRED     21
#else
    #define TARGET_SO_PASSCRED     16
    #define TARGET_SO_PEERCRED     17
    #define TARGET_SO_RCVLOWAT     18
    #define TARGET_SO_SNDLOWAT     19
    #define TARGET_SO_RCVTIMEO     20
    #define TARGET_SO_SNDTIMEO     21
#endif

    /* Security levels - as per NRL IPv6 - don't actually do anything */
    #define TARGET_SO_SECURITY_AUTHENTICATION              22
    #define TARGET_SO_SECURITY_ENCRYPTION_TRANSPORT        23
    #define TARGET_SO_SECURITY_ENCRYPTION_NETWORK          24

    #define TARGET_SO_BINDTODEVICE            25

    /* Socket filtering */
    #define TARGET_SO_ATTACH_FILTER           26
    #define TARGET_SO_DETACH_FILTER           27
    #define TARGET_SO_GET_FILTER              TARGET_SO_ATTACH_FILTER

    #define TARGET_SO_PEERNAME                28
    #define TARGET_SO_TIMESTAMP               29
    #define TARGET_SCM_TIMESTAMP              TARGET_SO_TIMESTAMP

    #define TARGET_SO_ACCEPTCONN              30

    #define TARGET_SO_PEERSEC                 31
    #define TARGET_SO_PASSSEC                 34
    #define TARGET_SO_TIMESTAMPNS             35
    #define TARGET_SCM_TIMESTAMPNS            TARGET_SO_TIMESTAMPNS

    #define TARGET_SO_MARK                    36

    #define TARGET_SO_TIMESTAMPING            37
    #define TARGET_SCM_TIMESTAMPING           TARGET_SO_TIMESTAMPING

    #define TARGET_SO_PROTOCOL                38
    #define TARGET_SO_DOMAIN                  39

    #define TARGET_SO_RXQ_OVFL                40

    #define TARGET_SO_WIFI_STATUS             41
    #define TARGET_SCM_WIFI_STATUS            TARGET_SO_WIFI_STATUS
    #define TARGET_SO_PEEK_OFF                42

    #define TARGET_SO_NOFCS                   43
    #define TARGET_SO_LOCK_FILTER             44
    #define TARGET_SO_SELECT_ERR_QUEUE        45
    #define TARGET_SO_BUSY_POLL               46
    #define TARGET_SO_MAX_PACING_RATE         47
    #define TARGET_SO_BPF_EXTENSIONS          48
    #define TARGET_SO_INCOMING_CPU            49
    #define TARGET_SO_ATTACH_BPF              50
    #define TARGET_SO_DETACH_BPF              TARGET_SO_DETACH_FILTER
    #define TARGET_SO_ATTACH_REUSEPORT_CBPF   51
    #define TARGET_SO_ATTACH_REUSEPORT_EBPF   52
    #define TARGET_SO_CNX_ADVICE              53
    #define TARGET_SCM_TIMESTAMPING_OPT_STATS 54
    #define TARGET_SO_MEMINFO                 55
    #define TARGET_SO_INCOMING_NAPI_ID        56
    #define TARGET_SO_COOKIE                  57
    #define TARGET_SCM_TIMESTAMPING_PKTINFO   58
    #define TARGET_SO_PEERGROUPS              59
    #define TARGET_SO_ZEROCOPY                60

#endif

#ifndef ARCH_HAS_SOCKET_TYPES
    /** sock_type - Socket types - default values
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
           TARGET_SOCK_CLOEXEC     = 02000000,
           TARGET_SOCK_NONBLOCK    = 04000,
    };

    #define TARGET_SOCK_MAX (TARGET_SOCK_PACKET + 1)
    #define TARGET_SOCK_TYPE_MASK  0xf  /* Covers up to TARGET_SOCK_MAX - 1. */

#endif
