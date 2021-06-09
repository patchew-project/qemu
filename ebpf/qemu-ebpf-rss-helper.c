/*
 * eBPF RSS Helper
 *
 * Developed by Daynix Computing LTD (http://www.daynix.com)
 *
 * Authors:
 *  Andrew Melnychenko <andrew@daynix.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Description: This is helper program for libvirtd.
 *              It loads eBPF RSS program and passes fds through unix socket.
 *              Built by meson, target - 'qemu-ebpf-rss-helper'.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>
#include <memory.h>
#include <errno.h>
#include <sys/socket.h>

#include "ebpf_rss.h"

static int send_fds(int socket, int *fds, int n)
{
    struct msghdr msg = {};
    struct cmsghdr *cmsg = NULL;
    char buf[CMSG_SPACE(n * sizeof(int))];
    char dummy_buffer = 0;
    struct iovec io = { .iov_base = &dummy_buffer,
                        .iov_len = sizeof(dummy_buffer) };

    memset(buf, 0, sizeof(buf));

    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);

    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(n * sizeof(int));

    memcpy(CMSG_DATA(cmsg), fds, n * sizeof(int));

    return sendmsg(socket, &msg, 0);
}

static void print_help_and_exit(const char *prog, int exitcode)
{
    fprintf(stderr, "%s - load eBPF RSS program for qemu and pass eBPF fds"
            " through unix socket.\n", prog);
    fprintf(stderr, "\t--fd <num>, -f <num> - unix socket file descriptor"
            " used to pass eBPF fds.\n");
    fprintf(stderr, "\t--help, -h - this help.\n");
    exit(exitcode);
}

int main(int argc, char **argv)
{
    char *fd_string = NULL;
    int unix_fd = 0;
    struct EBPFRSSContext ctx = {};
    int fds[EBPF_RSS_MAX_FDS] = {};
    int ret = -1;

    for (;;) {
        int c;
        static struct option long_options[] = {
                {"help",  no_argument, 0, 'h'},
                {"fd",  required_argument, 0, 'f'},
                {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "hf:",
                long_options, NULL);

        if (c == -1) {
            break;
        }

        switch (c) {
        case 'f':
            fd_string = optarg;
            break;
        case 'h':
        default:
            print_help_and_exit(argv[0],
                    c == 'h' ? EXIT_SUCCESS : EXIT_FAILURE);
        }
    }

    if (!fd_string) {
        fprintf(stderr, "Unix file descriptor not present.\n");
        print_help_and_exit(argv[0], EXIT_FAILURE);
    }

    fprintf(stderr, "FD %s\n", fd_string);

    unix_fd = atoi(fd_string);

    if (!unix_fd) {
        fprintf(stderr, "Unix file descriptor is invalid.\n");
        return EXIT_FAILURE;
    }

    ebpf_rss_init(&ctx);
    if (!ebpf_rss_load(&ctx)) {
        fprintf(stderr, "Can't load ebpf.\n");
        return EXIT_FAILURE;
    }
    fds[0] = ctx.program_fd;
    fds[1] = ctx.map_configuration;
    fds[2] = ctx.map_toeplitz_key;
    fds[3] = ctx.map_indirections_table;

    ret = send_fds(unix_fd, fds, EBPF_RSS_MAX_FDS);
    if (ret < 0) {
        fprintf(stderr, "Issue while sending fds: %s.\n", strerror(errno));
    }

    ebpf_rss_unload(&ctx);

    return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

