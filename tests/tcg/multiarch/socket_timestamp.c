#include <assert.h>
#include <errno.h>
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
void check_timestamp_difference(const struct timeval *msg_tv,
                                const struct timeval *pkt_tv)
{
    if (pkt_tv->tv_sec < msg_tv->tv_sec ||
        (pkt_tv->tv_sec == msg_tv->tv_sec && pkt_tv->tv_usec < msg_tv->tv_usec))
    {
        fprintf(stderr,
                "Packet received before sent: %lld.%06lld < %lld.%06lld\n",
                (long long)pkt_tv->tv_sec, (long long)pkt_tv->tv_usec,
                (long long)msg_tv->tv_sec, (long long)msg_tv->tv_usec);
        exit(-1);
    }

    if (pkt_tv->tv_sec > msg_tv->tv_sec + 10 ||
        (pkt_tv->tv_sec == msg_tv->tv_sec + 10 &&
         pkt_tv->tv_usec > msg_tv->tv_usec)) {
        fprintf(stderr,
                "Packet received more than 10 seconds after sent: "
                "%lld.%06lld > %lld.%06lld + 10\n",
                (long long)pkt_tv->tv_sec, (long long)pkt_tv->tv_usec,
                (long long)msg_tv->tv_sec, (long long)msg_tv->tv_usec);
        exit(-1);
    }
}

void send_current_time(int sock, struct sockaddr_in server_sockaddr)
{
    struct timeval tv = {0, 0};
    gettimeofday(&tv, NULL);
    sendto(sock, &tv, sizeof(tv), 0, (struct sockaddr *)&server_sockaddr,
           sizeof(server_sockaddr));
}

typedef void (*get_timeval_t)(const struct cmsghdr *cmsg, struct timeval *tv);


void receive_packet(int sock, get_timeval_t get_timeval)
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
    struct timeval msg_tv, pkt_tv;

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
    memcpy(&msg_tv, iov.iov_base, sizeof(msg_tv));
    printf("Message timestamp: %lld.%06lld\n",
           (long long)msg_tv.tv_sec, (long long)msg_tv.tv_usec);

    cmsg = CMSG_FIRSTHDR(&msg);
    assert(cmsg);
    (*get_timeval)(cmsg, &pkt_tv);
    printf("Packet timestamp: %lld.%06lld\n",
           (long long)pkt_tv.tv_sec, (long long)pkt_tv.tv_usec);

    check_timestamp_difference(&msg_tv, &pkt_tv);
}

void get_timeval_from_so_timestamp(const struct cmsghdr *cmsg,
                                   struct timeval *tv)
{
    assert(cmsg->cmsg_level == SOL_SOCKET);
    assert(cmsg->cmsg_type == SCM_TIMESTAMP);
    assert(cmsg->cmsg_len == CMSG_LEN(sizeof(struct timeval)));
    memcpy(tv, CMSG_DATA(cmsg), sizeof(*tv));
}

#ifdef SO_TIMESTAMP_OLD
void get_timeval_from_so_timestamp_old(const struct cmsghdr *cmsg,
                                       struct timeval *tv)
{
    struct kernel_old_timeval old_tv;
    assert(cmsg->cmsg_level == SOL_SOCKET);
    assert(cmsg->cmsg_type == SO_TIMESTAMP_OLD);
    assert(cmsg->cmsg_len == CMSG_LEN(sizeof(old_tv)));

    memcpy(&old_tv, CMSG_DATA(cmsg), sizeof(old_tv));
    tv->tv_sec = old_tv.tv_sec;
    tv->tv_usec = old_tv.tv_usec;
}

#ifdef SO_TIMESTAMP_NEW
void get_timeval_from_so_timestamp_new(const struct cmsghdr *cmsg,
                                       struct timeval *tv)
{
    struct kernel_sock_timeval sock_tv;
    assert(cmsg->cmsg_level == SOL_SOCKET);
    assert(cmsg->cmsg_type == SO_TIMESTAMP_NEW);
    assert(cmsg->cmsg_len == CMSG_LEN(sizeof(sock_tv)));

    memcpy(&sock_tv, CMSG_DATA(cmsg), sizeof(sock_tv));
    tv->tv_sec = sock_tv.tv_sec;
    tv->tv_usec = sock_tv.tv_usec;
}
#endif /* defined(SO_TIMESTAMP_NEW) */
#endif /* defined(SO_TIMESTAMP_OLD) */

void set_socket_option(int sock, int sockopt, int on)
{
    socklen_t len;
    int val = on;
    if (setsockopt(sock, SOL_SOCKET, sockopt, &val, sizeof(val)) < 0) {
        int err = errno;
        fprintf(stderr, "Failed to setsockopt %d (%s): %s\n",
                sockopt, on ? "on" : "off", strerror(err));
        exit(err);
    }

    len = sizeof(val);
    val = -1;
    if (getsockopt(sock, SOL_SOCKET, sockopt, &val, &len) < 0) {
        int err = errno;
        fprintf(stderr, "Failed to getsockopt (%d): %s\n", sock, strerror(err));
        exit(err);
    }
    assert(len == sizeof(val));
    assert(val == on);
}

int main(int argc, char **argv)
{
    int parent_sock, child_sock;
    struct sockaddr_in parent_sockaddr, child_sockaddr;
    int pid;
    struct timeval tv = {0, 0};
    gettimeofday(&tv, NULL);

    parent_sock = create_udp_socket(&parent_sockaddr);
    child_sock = create_udp_socket(&child_sockaddr);

    printf("Parent sock bound to port %d\nChild sock bound to port %d\n",
           parent_sockaddr.sin_port, child_sockaddr.sin_port);

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "SKIPPED. Failed to fork: %s\n", strerror(errno));
    } else if (pid == 0) {
        close(child_sock);

        /* Test 1: SO_TIMESTAMP */
        send_current_time(parent_sock, child_sockaddr);

        if (tv.tv_sec > 0x7fffff00) {
            /* Too close to y2038 problem, old system may not work. */
            close(parent_sock);
            return 0;
        }

#ifdef SO_TIMESTAMP_OLD
        if (SO_TIMESTAMP_OLD != SO_TIMESTAMP) {
            /* Test 2a: SO_TIMESTAMP_OLD */
            set_socket_option(parent_sock, SO_TIMESTAMP_OLD, 1);
            receive_packet(parent_sock, &get_timeval_from_so_timestamp_old);
            set_socket_option(parent_sock, SO_TIMESTAMP_OLD, 0);
        }
#ifdef SO_TIMESTAMP_NEW
        else {
            /* Test 2b: SO_TIMESTAMP_NEW */
            set_socket_option(parent_sock, SO_TIMESTAMP_NEW, 1);
            receive_packet(parent_sock, &get_timeval_from_so_timestamp_new);
            set_socket_option(parent_sock, SO_TIMESTAMP_NEW, 0);
        }
#endif /* defined(SO_TIMESTAMP_NEW) */
#endif /* defined(SO_TIMESTAMP_OLD) */

        close(parent_sock);
    } else {
        int child_status;
        close(parent_sock);

        /* Test 1: SO_TIMESTAMP */
        set_socket_option(child_sock, SO_TIMESTAMP, 1);
        receive_packet(child_sock, &get_timeval_from_so_timestamp);
        set_socket_option(child_sock, SO_TIMESTAMP, 0);

        if (tv.tv_sec > 0x7fffff00) {
            /* Too close to y2038 problem, old system may not work. */
            close(child_sock);
            return 0;
        }

#ifdef SO_TIMESTAMP_OLD
        if (SO_TIMESTAMP_OLD != SO_TIMESTAMP) {
            /* Test 2a: SO_TIMESTAMP_OLD */
            send_current_time(child_sock, parent_sockaddr);
        }
#ifdef SO_TIMESTAMP_NEW
        else {
            /* Test 2b: SO_TIMESTAMP_NEW */
            send_current_time(child_sock, parent_sockaddr);
        }
#endif /* defined(SO_TIMESTAMP_NEW) */
#endif /* defined(SO_TIMESTAMP_OLD) */

        close(child_sock);

        if (waitpid(pid, &child_status, 0) < 0) {
            int err = errno;
            fprintf(stderr, "Final wait() failed: %s\n", strerror(err));
            return err;
        }
        return child_status;
    }
    return 0;
}
