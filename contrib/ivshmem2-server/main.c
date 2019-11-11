/*
 * Copyright 6WIND S.A., 2014
 * Copyright (c) Siemens AG, 2019
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/option.h"
#include "ivshmem2-server.h"

#define IVSHMEM_SERVER_DEFAULT_FOREGROUND     0
#define IVSHMEM_SERVER_DEFAULT_PID_FILE       "/var/run/ivshmem-server.pid"

#define IVSHMEM_SERVER_DEFAULT_VERBOSE        0
#define IVSHMEM_SERVER_DEFAULT_UNIX_SOCK_PATH "/tmp/ivshmem_socket"
#define IVSHMEM_SERVER_DEFAULT_SHM_PATH       "ivshmem"
#define IVSHMEM_SERVER_DEFAULT_SHM_SIZE       (4*1024*1024)
#define IVSHMEM_SERVER_DEFAULT_OUTPUT_SEC_SZ  0
#define IVSHMEM_SERVER_DEFAULT_MAX_PEERS      2
#define IVSHMEM_SERVER_DEFAULT_VECTORS        1
#define IVSHMEM_SERVER_DEFAULT_PROTOCOL       0

/* used to quit on signal SIGTERM */
static int ivshmem_server_quit;

static bool foreground = IVSHMEM_SERVER_DEFAULT_FOREGROUND;
static const char *pid_file = IVSHMEM_SERVER_DEFAULT_PID_FILE;

static void
ivshmem_server_usage(const char *progname)
{
    printf("Usage: %s [OPTION]...\n"
           "  -h: show this help\n"
           "  -v: verbose mode\n"
           "  -F: foreground mode (default is to daemonize)\n"
           "  -p <pid-file>: path to the PID file (used in daemon mode only)\n"
           "     default " IVSHMEM_SERVER_DEFAULT_PID_FILE "\n"
           "  -S <unix-socket-path>: path to the unix socket to listen to\n"
           "     default " IVSHMEM_SERVER_DEFAULT_UNIX_SOCK_PATH "\n"
           "  -M <shm-name>: POSIX shared memory object to use\n"
           "     default " IVSHMEM_SERVER_DEFAULT_SHM_PATH "\n"
           "  -m <dir-name>: where to create shared memory\n"
           "  -l <size>: size of shared memory in bytes\n"
           "     suffixes K, M and G can be used, e.g. 1K means 1024\n"
           "     default %u\n"
           "  -o <size>: size of each output section in bytes "
                "(suffixes supported)\n"
           "     default %u\n"
           "  -n <peers>: maximum number of peers\n"
           "     default %u\n"
           "  -V <vectors>: number of vectors\n"
           "     default %u\n"
           "  -P <protocol>: 16-bit protocol to be advertised\n"
           "     default 0x%04x\n"
           "     When using virtio (0x8000...0xffff), only two peers are "
           "supported, peer 0\n"
           "     will become backend, peer 1 frontend\n",
           progname, IVSHMEM_SERVER_DEFAULT_SHM_SIZE,
           IVSHMEM_SERVER_DEFAULT_OUTPUT_SEC_SZ,
           IVSHMEM_SERVER_DEFAULT_MAX_PEERS, IVSHMEM_SERVER_DEFAULT_VECTORS,
           IVSHMEM_SERVER_DEFAULT_PROTOCOL);
}

static void
ivshmem_server_help(const char *progname)
{
    fprintf(stderr, "Try '%s -h' for more information.\n", progname);
}

/* parse the program arguments, exit on error */
static void
ivshmem_server_parse_args(IvshmemServerArgs *args, int argc, char *argv[])
{
    int c;
    unsigned long long v;
    Error *err = NULL;

    while ((c = getopt(argc, argv, "hvFp:S:m:M:l:o:n:V:P:")) != -1) {

        switch (c) {
        case 'h': /* help */
            ivshmem_server_usage(argv[0]);
            exit(0);
            break;

        case 'v': /* verbose */
            args->verbose = 1;
            break;

        case 'F': /* foreground */
            foreground = 1;
            break;

        case 'p': /* pid file */
            pid_file = optarg;
            break;

        case 'S': /* unix socket path */
            args->unix_socket_path = optarg;
            break;

        case 'M': /* shm name */
        case 'm': /* dir name */
            args->shm_path = optarg;
            args->use_shm_open = c == 'M';
            break;

        case 'l': /* shm size */
            parse_option_size("shm_size", optarg, &args->shm_size, &err);
            if (err) {
                error_report_err(err);
                ivshmem_server_help(argv[0]);
                exit(1);
            }
            break;

        case 'o': /* output section size */
            parse_option_size("output_section_size", optarg,
                              &args->output_section_size, &err);
            if (err) {
                error_report_err(err);
                ivshmem_server_help(argv[0]);
                exit(1);
            }
            break;

        case 'n': /* maximum number of peers */
            if (parse_uint_full(optarg, &v, 0) < 0) {
                fprintf(stderr, "cannot parse max-peers\n");
                ivshmem_server_help(argv[0]);
                exit(1);
            }
            args->max_peers = v;
            break;

        case 'V': /* number of vectors */
            if (parse_uint_full(optarg, &v, 0) < 0) {
                fprintf(stderr, "cannot parse vectors\n");
                ivshmem_server_help(argv[0]);
                exit(1);
            }
            args->vectors = v;
            break;

        case 'P': /* protocol */
            if (parse_uint_full(optarg, &v, 0) < 0) {
                fprintf(stderr, "cannot parse protocol\n");
                ivshmem_server_help(argv[0]);
                exit(1);
            }
            args->protocol = v;
            break;

        default:
            ivshmem_server_usage(argv[0]);
            exit(1);
            break;
        }
    }

    if (args->vectors > IVSHMEM_SERVER_MAX_VECTORS) {
        fprintf(stderr, "too many requested vectors (max is %d)\n",
                IVSHMEM_SERVER_MAX_VECTORS);
        ivshmem_server_help(argv[0]);
        exit(1);
    }

    if (args->protocol >= 0x8000 && args->max_peers > 2) {
        fprintf(stderr, "virtio protocols only support 2 peers\n");
        ivshmem_server_help(argv[0]);
        exit(1);
    }

    if (args->verbose == 1 && foreground == 0) {
        fprintf(stderr, "cannot use verbose in daemon mode\n");
        ivshmem_server_help(argv[0]);
        exit(1);
    }
}

/* wait for events on listening server unix socket and connected client
 * sockets */
static int
ivshmem_server_poll_events(IvshmemServer *server)
{
    fd_set fds;
    int ret = 0, maxfd;

    while (!ivshmem_server_quit) {

        FD_ZERO(&fds);
        maxfd = 0;
        ivshmem_server_get_fds(server, &fds, &maxfd);

        ret = select(maxfd, &fds, NULL, NULL, NULL);

        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }

            fprintf(stderr, "select error: %s\n", strerror(errno));
            break;
        }
        if (ret == 0) {
            continue;
        }

        if (ivshmem_server_handle_fds(server, &fds, maxfd) < 0) {
            fprintf(stderr, "ivshmem_server_handle_fds() failed\n");
            break;
        }
    }

    return ret;
}

static void
ivshmem_server_quit_cb(int signum)
{
    ivshmem_server_quit = 1;
}

int
main(int argc, char *argv[])
{
    IvshmemServer server = {
        .args = {
            .verbose = IVSHMEM_SERVER_DEFAULT_VERBOSE,
            .unix_socket_path = IVSHMEM_SERVER_DEFAULT_UNIX_SOCK_PATH,
            .shm_path = IVSHMEM_SERVER_DEFAULT_SHM_PATH,
            .use_shm_open = true,
            .shm_size = IVSHMEM_SERVER_DEFAULT_SHM_SIZE,
            .output_section_size = IVSHMEM_SERVER_DEFAULT_OUTPUT_SEC_SZ,
            .max_peers = IVSHMEM_SERVER_DEFAULT_MAX_PEERS,
            .vectors = IVSHMEM_SERVER_DEFAULT_VECTORS,
            .protocol = IVSHMEM_SERVER_DEFAULT_PROTOCOL,
        },
    };
    struct sigaction sa, sa_quit;
    int ret = 1;

    /*
     * Do not remove this notice without adding proper error handling!
     * Start with handling ivshmem_server_send_one_msg() failure.
     */
    printf("*** Example code, do not use in production ***\n");

    /* parse arguments, will exit on error */
    ivshmem_server_parse_args(&server.args, argc, argv);

    /* Ignore SIGPIPE, see this link for more info:
     * http://www.mail-archive.com/libevent-users@monkey.org/msg01606.html */
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    if (sigemptyset(&sa.sa_mask) == -1 ||
        sigaction(SIGPIPE, &sa, 0) == -1) {
        perror("failed to ignore SIGPIPE; sigaction");
        goto err;
    }

    sa_quit.sa_handler = ivshmem_server_quit_cb;
    sa_quit.sa_flags = 0;
    if (sigemptyset(&sa_quit.sa_mask) == -1 ||
        sigaction(SIGTERM, &sa_quit, 0) == -1 ||
        sigaction(SIGINT, &sa_quit, 0) == -1) {
        perror("failed to add signal handler; sigaction");
        goto err;
    }

    /* init the ivshms structure */
    ivshmem_server_init(&server);

    /* start the ivshmem server (open shm & unix socket) */
    if (ivshmem_server_start(&server) < 0) {
        fprintf(stderr, "cannot bind\n");
        goto err;
    }

    /* daemonize if asked to */
    if (!foreground) {
        FILE *fp;

        if (qemu_daemon(1, 1) < 0) {
            fprintf(stderr, "cannot daemonize: %s\n", strerror(errno));
            goto err_close;
        }

        /* write pid file */
        fp = fopen(pid_file, "w");
        if (fp == NULL) {
            fprintf(stderr, "cannot write pid file: %s\n", strerror(errno));
            goto err_close;
        }

        fprintf(fp, "%d\n", (int) getpid());
        fclose(fp);
    }

    ivshmem_server_poll_events(&server);
    fprintf(stdout, "server disconnected\n");
    ret = 0;

err_close:
    ivshmem_server_close(&server);
err:
    return ret;
}
