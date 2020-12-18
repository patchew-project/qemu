#include <assert.h>
#include <errno.h>
#include <linux/net_tstamp.h>
#include <linux/types.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifdef __kernel_old_timeval
#define kernel_old_timeval __kernel_old_timeval
#else
struct kernel_old_timeval {
    __kernel_long_t tv_sec;
    __kernel_long_t tv_usec;
};
#endif

struct kernel_sock_timeval {
    int64_t tv_sec;
    int64_t tv_usec;
};

struct kernel_old_timespec {
    __kernel_long_t tv_sec;
    long            tv_nsec;
};

struct kernel_timespec {
    int64_t   tv_sec;
    long long tv_nsec;
};

struct scm_timestamping {
    struct timespec ts[3];
};

struct scm_old_timestamping {
    struct kernel_old_timespec ts[3];
};

struct scm_timestamping64 {
    struct kernel_timespec ts[3];
};

const int so_timestamping_flags =
    SOF_TIMESTAMPING_RX_HARDWARE |
    SOF_TIMESTAMPING_RX_SOFTWARE |
    SOF_TIMESTAMPING_SOFTWARE;

int create_udp_socket(struct sockaddr_in *sockaddr)
{
    socklen_t sockaddr_len;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        int err = errno;
        fprintf(stderr, "Failed to create server socket: %s\n", strerror(err));
        exit(err);
    }

    memset(sockaddr, 0, sizeof(*sockaddr));
    sockaddr->sin_family = AF_INET;
    sockaddr->sin_port = htons(0);  /* let kernel select a port for us */
    sockaddr->sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(sock, (struct sockaddr *)sockaddr, sizeof(*sockaddr)) < 0) {
        int err = errno;
        fprintf(stderr, "Failed to bind server socket: %s\n", strerror(err));
        exit(err);
    }

    sockaddr_len = sizeof(*sockaddr);
    if (getsockname(sock, (struct sockaddr *)sockaddr, &sockaddr_len) < 0) {
        int err = errno;
        fprintf(stderr, "Failed to get socket name: %s\n", strerror(err));
        exit(err);
    }
    return sock;
}

/*
 * Checks that the timestamp in the message is not after the reception timestamp
 * as well as the reception time is within 10 seconds of the message time.
 */
void check_timestamp_difference(const struct timespec *msg_ts,
                                const struct timespec *pkt_ts)
{
    if (pkt_ts->tv_sec < msg_ts->tv_sec ||
        (pkt_ts->tv_sec == msg_ts->tv_sec && pkt_ts->tv_nsec < msg_ts->tv_nsec))
    {
        fprintf(stderr,
                "Packet received before sent: %lld.%06lld < %lld.%09lld\n",
                (long long)pkt_ts->tv_sec, (long long)pkt_ts->tv_nsec,
                (long long)msg_ts->tv_sec, (long long)msg_ts->tv_nsec);
        exit(-1);
    }

    if (pkt_ts->tv_sec > msg_ts->tv_sec + 10 ||
        (pkt_ts->tv_sec == msg_ts->tv_sec + 10 &&
         pkt_ts->tv_nsec > msg_ts->tv_nsec)) {
        fprintf(stderr,
                "Packet received more than 10 seconds after sent: "
                "%lld.%06lld > %lld.%09lld + 10\n",
                (long long)pkt_ts->tv_sec, (long long)pkt_ts->tv_nsec,
                (long long)msg_ts->tv_sec, (long long)msg_ts->tv_nsec);
        exit(-1);
    }
}

void send_current_time(int sock, struct sockaddr_in server_sockaddr)
{
    struct timespec ts = {0, 0};
    clock_gettime(CLOCK_REALTIME, &ts);
#ifdef MSG_CONFIRM
    const int flags = MSG_CONFIRM;
#else
    const int flags = 0;
#endif
    sendto(sock, &ts, sizeof(ts), flags, (struct sockaddr *)&server_sockaddr,
           sizeof(server_sockaddr));
}

typedef void (*get_timespec_t)(const struct cmsghdr *cmsg, struct timespec *tv);

void receive_packet(int sock, get_timespec_t get_timespec)
{
    struct msghdr msg = {0};

    char iobuf[1024];
    struct iovec iov;

    union {
        /*
         * 128 bytes are enough for all existing
         * timeval/timespec/scm_timestamping structures.
         */
        char cmsg_buf[CMSG_SPACE(128)];
        struct cmsghdr align;
    } u;
    struct cmsghdr *cmsg;
    struct timespec msg_ts, pkt_ts;

    int res;

    iov.iov_base = iobuf;
    iov.iov_len = sizeof(iobuf);

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = (caddr_t)u.cmsg_buf;
    msg.msg_controllen = sizeof(u.cmsg_buf);

    res = recvmsg(sock, &msg, 0);
    if (res < 0) {
        int err = errno;
        fprintf(stderr, "Failed to receive packet: %s\n", strerror(err));
        exit(err);
    }

    assert(res == sizeof(struct timeval));
    assert(iov.iov_base == iobuf);
    memcpy(&msg_ts, iov.iov_base, sizeof(msg_ts));
    printf("Message timestamp: %lld.%09lld\n",
           (long long)msg_ts.tv_sec, (long long)msg_ts.tv_nsec);

    cmsg = CMSG_FIRSTHDR(&msg);
    assert(cmsg);
    (*get_timespec)(cmsg, &pkt_ts);
    printf("Packet timestamp: %lld.%09lld\n",
           (long long)pkt_ts.tv_sec, (long long)pkt_ts.tv_nsec);

    check_timestamp_difference(&msg_ts, &pkt_ts);
}

void get_timespec_from_so_timestamp(const struct cmsghdr *cmsg,
                                    struct timespec *ts)
{
    struct timeval tv;
    assert(cmsg->cmsg_level == SOL_SOCKET);
    assert(cmsg->cmsg_type == SCM_TIMESTAMP);
    assert(cmsg->cmsg_len == CMSG_LEN(sizeof(tv)));

    memcpy(&tv, CMSG_DATA(cmsg), sizeof(tv));
    ts->tv_sec = tv.tv_sec;
    ts->tv_nsec = tv.tv_usec * 1000LL;
}

#ifdef SO_TIMESTAMP_OLD
void get_timespec_from_so_timestamp_old(const struct cmsghdr *cmsg,
                                        struct timespec *ts)
{
    struct kernel_old_timeval old_tv;
    assert(cmsg->cmsg_level == SOL_SOCKET);
    assert(cmsg->cmsg_type == SO_TIMESTAMP_OLD);
    assert(cmsg->cmsg_len == CMSG_LEN(sizeof(old_tv)));

    memcpy(&old_tv, CMSG_DATA(cmsg), sizeof(old_tv));
    ts->tv_sec = old_tv.tv_sec;
    ts->tv_nsec = old_tv.tv_usec * 1000LL;
}

#ifdef SO_TIMESTAMP_NEW
void get_timespec_from_so_timestamp_new(const struct cmsghdr *cmsg,
                                        struct timespec *ts)
{
    struct kernel_sock_timeval sock_tv;
    assert(cmsg->cmsg_level == SOL_SOCKET);
    assert(cmsg->cmsg_type == SO_TIMESTAMP_NEW);
    assert(cmsg->cmsg_len == CMSG_LEN(sizeof(sock_tv)));

    memcpy(&sock_tv, CMSG_DATA(cmsg), sizeof(sock_tv));
    ts->tv_sec = sock_tv.tv_sec;
    ts->tv_nsec = sock_tv.tv_usec * 1000LL;
}
#endif /* defined(SO_TIMESTAMP_NEW) */
#endif /* defined(SO_TIMESTAMP_OLD) */

void get_timespec_from_so_timestampns(const struct cmsghdr *cmsg,
                                      struct timespec *ts)
{
    assert(cmsg->cmsg_level == SOL_SOCKET);
    assert(cmsg->cmsg_type == SCM_TIMESTAMPNS);
    assert(cmsg->cmsg_len == CMSG_LEN(sizeof(*ts)));

    memcpy(ts, CMSG_DATA(cmsg), sizeof(*ts));
}

#ifdef SO_TIMESTAMPNS_OLD
void get_timespec_from_so_timestampns_old(const struct cmsghdr *cmsg,
                                          struct timespec *ts)
{
    struct kernel_old_timespec old_ts;
    assert(cmsg->cmsg_level == SOL_SOCKET);
    assert(cmsg->cmsg_type == SO_TIMESTAMPNS_OLD);
    assert(cmsg->cmsg_len == CMSG_LEN(sizeof(old_ts)));

    memcpy(&old_ts, CMSG_DATA(cmsg), sizeof(old_ts));
    ts->tv_sec = old_ts.tv_sec;
    ts->tv_nsec = old_ts.tv_nsec;
}

#ifdef SO_TIMESTAMPNS_NEW
void get_timespec_from_so_timestampns_new(const struct cmsghdr *cmsg,
                                          struct timespec *ts)
{
    struct kernel_timespec sock_ts;
    assert(cmsg->cmsg_level == SOL_SOCKET);
    assert(cmsg->cmsg_type == SO_TIMESTAMPNS_NEW);
    assert(cmsg->cmsg_len == CMSG_LEN(sizeof(sock_ts)));

    memcpy(&sock_ts, CMSG_DATA(cmsg), sizeof(sock_ts));
    ts->tv_sec = sock_ts.tv_sec;
    ts->tv_nsec = sock_ts.tv_nsec;
}
#endif /* defined(SO_TIMESTAMPNS_NEW) */
#endif /* defined(SO_TIMESTAMPNS_OLD) */

void get_timespec_from_so_timestamping(const struct cmsghdr *cmsg,
                                       struct timespec *ts)
{
    int i;
    struct scm_timestamping tss;
    assert(cmsg->cmsg_level == SOL_SOCKET);
    assert(cmsg->cmsg_type == SCM_TIMESTAMPING);
    assert(cmsg->cmsg_len == CMSG_LEN(sizeof(tss)));

    memcpy(&tss, CMSG_DATA(cmsg), sizeof(tss));

    for (i = 0; i < 3; ++i) {
        if (tss.ts[i].tv_sec || tss.ts[i].tv_nsec) {
            *ts = tss.ts[i];
            return;
        }
    }
    assert(!"All three entries in scm_timestamping are empty");
}

#ifdef SO_TIMESTAMPING_OLD
void get_timespec_from_so_timestamping_old(const struct cmsghdr *cmsg,
                                           struct timespec *ts)
{
    int i;
    struct scm_old_timestamping tss;
    assert(cmsg->cmsg_level == SOL_SOCKET);
    assert(cmsg->cmsg_type == SO_TIMESTAMPING_OLD);
    assert(cmsg->cmsg_len == CMSG_LEN(sizeof(tss)));

    memcpy(&tss, CMSG_DATA(cmsg), sizeof(tss));

    for (i = 0; i < 3; ++i) {
        if (tss.ts[i].tv_sec || tss.ts[i].tv_nsec) {
            ts->tv_sec = tss.ts[i].tv_sec;
            ts->tv_nsec = tss.ts[i].tv_nsec;
            return;
        }
    }
    assert(!"All three entries in scm_old_timestamping are empty");
}

#ifdef SO_TIMESTAMPING_NEW
void get_timespec_from_so_timestamping_new(const struct cmsghdr *cmsg,
                                           struct timespec *ts)
{
    int i;
    struct scm_timestamping64 tss;
    assert(cmsg->cmsg_level == SOL_SOCKET);
    assert(cmsg->cmsg_type == SO_TIMESTAMPING_NEW);
    assert(cmsg->cmsg_len == CMSG_LEN(sizeof(tss)));

    memcpy(&tss, CMSG_DATA(cmsg), sizeof(tss));
    for (i = 0; i < 3; ++i) {
        if (tss.ts[i].tv_sec || tss.ts[i].tv_nsec) {
            ts->tv_sec = tss.ts[i].tv_sec;
            ts->tv_nsec = tss.ts[i].tv_nsec;
            return;
        }
    }
    assert(!"All three entries in scm_timestamp64 are empty");
}
#endif /* defined(SO_TIMESTAMPING_NEW) */
#endif /* defined(SO_TIMESTAMPING_OLD) */

void set_socket_option(int sock, int sockopt, int set_to)
{
    socklen_t len;
    int val = set_to;
    if (setsockopt(sock, SOL_SOCKET, sockopt, &val, sizeof(val)) < 0) {
        int err = errno;
        fprintf(stderr, "Failed at setsockopt(%d, SOL_SOCKET, %d, %d): %s\n",
                sock, sockopt, set_to, strerror(err));
        exit(err);
    }

#ifdef SO_TIMESTAMPING_NEW
    if (sockopt == SO_TIMESTAMPING_NEW) {
        /*
         * `getsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING_NEW)` not implemented
         * as of linux kernel v5.8-rc4.
         */
        return;
    }
#endif

    len = sizeof(val);
    val = -1;
    if (getsockopt(sock, SOL_SOCKET, sockopt, &val, &len) < 0) {
        int err = errno;
        fprintf(stderr, "Failed at getsockopt(%d, SOL_SOCKET, %d): %s\n",
                sock, sockopt, strerror(err));
        exit(err);
    }
    assert(len == sizeof(val));
    assert(val == set_to);
}

void child_steps(int sock, struct sockaddr_in addr, int run_old)
{
    /* Test 1: SO_TIMESTAMP */
    send_current_time(sock, addr);

    /* Test 2: SO_TIMESTAMPNS */
    printf("Test 2: SO_TIMESTAMPNS\n");
    set_socket_option(sock, SO_TIMESTAMPNS, 1);
    receive_packet(sock, &get_timespec_from_so_timestampns);
    set_socket_option(sock, SO_TIMESTAMPNS, 0);

    /* Test 3: SO_TIMESTAMPING */
    send_current_time(sock, addr);

    if (!run_old) {
        return;
    }

#ifdef SO_TIMESTAMP_OLD
    if (SO_TIMESTAMP_OLD != SO_TIMESTAMP) {
        /* Test 4a: SO_TIMESTAMP_OLD */
        printf("Test 4a: SO_TIMESTAMP_OLD\n");
        set_socket_option(sock, SO_TIMESTAMP_OLD, 1);
        receive_packet(sock, &get_timespec_from_so_timestamp_old);
        set_socket_option(sock, SO_TIMESTAMP_OLD, 0);
    }
#ifdef SO_TIMESTAMP_NEW
    else {
        /* Test 4b: SO_TIMESTAMP_NEW */
        printf("Test 4b: SO_TIMESTAMP_NEW\n");
        set_socket_option(sock, SO_TIMESTAMP_NEW, 1);
        receive_packet(sock, &get_timespec_from_so_timestamp_new);
        set_socket_option(sock, SO_TIMESTAMP_NEW, 0);
    }
#endif /* defined(SO_TIMESTAMP_NEW) */
#endif /* defined(SO_TIMESTAMP_OLD) */

#ifdef SO_TIMESTAMPNS_OLD
    if (SO_TIMESTAMPNS_OLD != SO_TIMESTAMPNS) {
        /* Test 5a: SO_TIMESTAMPNS_OLD */
        send_current_time(sock, addr);
    }
#ifdef SO_TIMESTAMPNS_NEW
    else {
        /* Test 5b: SO_TIMESTAMPNS_NEW */
        send_current_time(sock, addr);
    }
#endif /* defined(SO_TIMESTAMPNS_NEW) */
#endif /* defined(SO_TIMESTAMPNS_OLD) */

#ifdef SO_TIMESTAMPING_OLD
    if (SO_TIMESTAMPING_OLD != SO_TIMESTAMPING) {
        /* Test 6a: SO_TIMESTAMPING_OLD */
        printf("Test 6a: SO_TIMESTAMPING_OLD\n");
        set_socket_option(sock, SO_TIMESTAMPING_OLD, so_timestamping_flags);
        receive_packet(sock, &get_timespec_from_so_timestamping_old);
        set_socket_option(sock, SO_TIMESTAMPING_OLD, 0);
    }
#ifdef SO_TIMESTAMPING_NEW
    else {
        /* Test 6b: SO_TIMESTAMPING_NEW */
        printf("Test 6b: SO_TIMESTAMPING_NEW\n");
        set_socket_option(sock, SO_TIMESTAMPING_NEW, so_timestamping_flags);
        receive_packet(sock, &get_timespec_from_so_timestamping_new);
        set_socket_option(sock, SO_TIMESTAMPING_NEW, 0);
    }
#endif /* defined(SO_TIMESTAMPING_NEW) */
#endif /* defined(SO_TIMESTAMPING_OLD) */
}

void parent_steps(int sock, struct sockaddr_in addr, int run_old)
{
    /* Test 1: SO_TIMESTAMP */
    printf("Test 1: SO_TIMESTAMP\n");
    set_socket_option(sock, SO_TIMESTAMP, 1);
    receive_packet(sock, &get_timespec_from_so_timestamp);
    set_socket_option(sock, SO_TIMESTAMP, 0);

    /* Test 2: SO_TIMESTAMPNS */
    send_current_time(sock, addr);

    /* Test 3: SO_TIMESTAMPING */
    printf("Test 3: SO_TIMESTAMPING\n");
    set_socket_option(sock, SO_TIMESTAMPING, so_timestamping_flags);
    receive_packet(sock, &get_timespec_from_so_timestamping);
    set_socket_option(sock, SO_TIMESTAMPING, 0);

    if (!run_old) {
        return;
    }

#ifdef SO_TIMESTAMP_OLD
    if (SO_TIMESTAMP_OLD != SO_TIMESTAMP) {
        /* Test 4a: SO_TIMESTAMP_OLD */
        send_current_time(sock, addr);
    }
#ifdef SO_TIMESTAMP_NEW
    else {
        /* Test 4b: SO_TIMESTAMP_NEW */
        send_current_time(sock, addr);
    }
#endif /* defined(SO_TIMESTAMP_NEW) */
#endif /* defined(SO_TIMESTAMP_OLD) */

#ifdef SO_TIMESTAMPNS_OLD
    if (SO_TIMESTAMPNS_OLD != SO_TIMESTAMPNS) {
        /* Test 5a: SO_TIMESTAMPNS_OLD */
        printf("Test 5a: SO_TIMESTAMPNS_OLD\n");
        set_socket_option(sock, SO_TIMESTAMPNS_OLD, 1);
        receive_packet(sock, &get_timespec_from_so_timestampns_old);
        set_socket_option(sock, SO_TIMESTAMPNS_OLD, 0);
    }
#ifdef SO_TIMESTAMPNS_NEW
    else {
        /* Test 5b: SO_TIMESTAMPNS_NEW */
        printf("Test 5b: SO_TIMESTAMPNS_NEW\n");
        set_socket_option(sock, SO_TIMESTAMPNS_NEW, 1);
        receive_packet(sock, &get_timespec_from_so_timestampns_new);
        set_socket_option(sock, SO_TIMESTAMPNS_NEW, 0);
    }
#endif /* defined(SO_TIMESTAMPNS_NEW) */
#endif /* defined(SO_TIMESTAMPNS_OLD) */

#ifdef SO_TIMESTAMPING_OLD
    if (SO_TIMESTAMPING_OLD != SO_TIMESTAMPING) {
        /* Test 6a: SO_TIMESTAMPING_OLD */
        send_current_time(sock, addr);
    }
#ifdef SO_TIMESTAMPING_NEW
    else {
        /* Test 6b: SO_TIMESTAMPING_NEW */
        send_current_time(sock, addr);
    }
#endif /* defined(SO_TIMESTAMPING_NEW) */
#endif /* defined(SO_TIMESTAMPING_OLD) */
}

int main(int argc, char **argv)
{
    int parent_sock, child_sock;
    struct sockaddr_in parent_sockaddr, child_sockaddr;
    int pid, run_old;
    struct timeval tv = {0, 0};
    gettimeofday(&tv, NULL);

    /* Too close to y2038 old systems may not work. */
    run_old = tv.tv_sec < 0x7fffff00;

    parent_sock = create_udp_socket(&parent_sockaddr);
    child_sock = create_udp_socket(&child_sockaddr);

    printf("Parent sock bound to port %d\nChild sock bound to port %d\n",
           parent_sockaddr.sin_port, child_sockaddr.sin_port);

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "SKIPPED. Failed to fork: %s\n", strerror(errno));
    } else if (pid == 0) {
        close(parent_sock);
        child_steps(child_sock, parent_sockaddr, run_old);
        close(child_sock);
    } else {
        int child_status;

        close(child_sock);
        parent_steps(parent_sock, child_sockaddr, run_old);
        close(parent_sock);

        if (waitpid(pid, &child_status, 0) < 0) {
            int err = errno;
            fprintf(stderr, "Final wait() failed: %s\n", strerror(err));
            return err;
        }
        return child_status;
    }
    return 0;
}
