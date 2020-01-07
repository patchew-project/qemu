/*
 * Copyright 6WIND S.A., 2014
 * Copyright (c) Siemens AG, 2019
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef IVSHMEM2_SERVER_H
#define IVSHMEM2_SERVER_H

/**
 * The ivshmem server is a daemon that creates a unix socket in listen
 * mode. The ivshmem clients (qemu or ivshmem-client) connect to this
 * unix socket. For each client, the server will create some eventfd
 * (see EVENTFD(2)), one per vector. These fd are transmitted to all
 * clients using the SCM_RIGHTS cmsg message. Therefore, each client is
 * able to send a notification to another client without being
 * "profixied" by the server.
 *
 * We use this mechanism to send interruptions between guests.
 * qemu is able to transform an event on a eventfd into a PCI MSI-x
 * interruption in the guest.
 *
 * The ivshmem server is also able to share the file descriptor
 * associated to the ivshmem shared memory.
 */

#include <sys/select.h>

#include "qemu/event_notifier.h"
#include "qemu/queue.h"
#include "hw/misc/ivshmem2.h"

/**
 * Maximum number of notification vectors supported by the server
 */
#define IVSHMEM_SERVER_MAX_VECTORS 64

/**
 * Structure storing a peer
 *
 * Each time a client connects to an ivshmem server, a new
 * IvshmemServerPeer structure is created. This peer and all its
 * vectors are advertised to all connected clients through the connected
 * unix sockets.
 */
typedef struct IvshmemServerPeer {
    QTAILQ_ENTRY(IvshmemServerPeer) next;    /**< next in list*/
    int sock_fd;                             /**< connected unix sock */
    int64_t id;                              /**< the id of the peer */
    EventNotifier vectors[IVSHMEM_SERVER_MAX_VECTORS]; /**< one per vector */
    unsigned vectors_count;                  /**< number of vectors */
} IvshmemServerPeer;

/**
 * Structure describing ivshmem server arguments
 */
typedef struct IvshmemServerArgs {
    bool verbose;                   /**< true to enable verbose mode */
    const char *unix_socket_path;   /**< pointer to unix socket file name */
    const char *shm_path;           /**< Path to the shared memory; path
                                         corresponds to a POSIX shm name or a
                                         hugetlbfs mount point. */
    bool use_shm_open;              /**< true to use shm_open, false for
                                         file-backed shared memory */
    uint64_t shm_size;              /**< total size of shared memory */
    uint64_t output_section_size;   /**< size of each output section */
    unsigned max_peers;             /**< maximum number of peers */
    unsigned vectors;               /**< interrupt vectors per client */
    unsigned protocol;              /**< protocol advertised to all clients */
} IvshmemServerArgs;

/**
 * Structure describing an ivshmem server
 *
 * This structure stores all information related to our server: the name
 * of the server unix socket and the list of connected peers.
 */
typedef struct IvshmemServer {
    IvshmemServerArgs args;          /**< server arguments */
    int sock_fd;                     /**< unix sock file descriptor */
    int shm_fd;                      /**< shm file descriptor */
    uint32_t *state_table;           /**< mapped state table */
    QTAILQ_HEAD(, IvshmemServerPeer) peer_list; /**< list of peers */
} IvshmemServer;

/**
 * Initialize an ivshmem server
 *
 * @server:         A pointer to an uninitialized IvshmemServer structure
 */
void ivshmem_server_init(IvshmemServer *server);

/**
 * Open the shm, then create and bind to the unix socket
 *
 * @server: The pointer to the initialized IvshmemServer structure
 *
 * Returns: 0 on success, or a negative value on error
 */
int ivshmem_server_start(IvshmemServer *server);

/**
 * Close the server
 *
 * Close connections to all clients, close the unix socket and the
 * shared memory file descriptor. The structure remains initialized, so
 * it is possible to call ivshmem_server_start() again after a call to
 * ivshmem_server_close().
 *
 * @server: The ivshmem server
 */
void ivshmem_server_close(IvshmemServer *server);

/**
 * Fill a fd_set with file descriptors to be monitored
 *
 * This function will fill a fd_set with all file descriptors that must
 * be polled (unix server socket and peers unix socket). The function
 * will not initialize the fd_set, it is up to the caller to do it.
 *
 * @server: The ivshmem server
 * @fds:    The fd_set to be updated
 * @maxfd:  Must be set to the max file descriptor + 1 in fd_set. This value is
 *          updated if this function adds a greater fd in fd_set.
 */
void
ivshmem_server_get_fds(const IvshmemServer *server, fd_set *fds, int *maxfd);

/**
 * Read and handle new messages
 *
 * Given a fd_set (for instance filled by a call to select()), handle
 * incoming messages from peers.
 *
 * @server: The ivshmem server
 * @fds:    The fd_set containing the file descriptors to be checked. Note that
 *          file descriptors that are not related to our server are ignored.
 * @maxfd:  The maximum fd in fd_set, plus one.
 *
 * Returns: 0 on success, or a negative value on error
 */
int ivshmem_server_handle_fds(IvshmemServer *server, fd_set *fds, int maxfd);

/**
 * Search a peer from its identifier
 *
 * @server:  The ivshmem server
 * @peer_id: The identifier of the peer structure
 *
 * Returns:  The peer structure, or NULL if not found
 */
IvshmemServerPeer *
ivshmem_server_search_peer(IvshmemServer *server, int64_t peer_id);

#endif /* IVSHMEM2_SERVER_H */
