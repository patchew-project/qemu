/*
 * QEMU KVM support -- x86 virtual energy-related MSR.
 *
 * Copyright 2023 Red Hat, Inc. 2023
 *
 *  Author:
 *      Anthony Harivel <aharivel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "vmsr_energy.h"
#include "qapi/error.h"
#include "io/channel.h"
#include "io/channel-socket.h"
#include "hw/boards.h"

#define MAX_PATH_LEN 256
#define MAX_LINE_LEN 500

static char *compute_default_paths(void)
{
    g_autofree char *state = qemu_get_local_state_dir();

    return g_build_filename(state, "run", "qemu-vmsr-helper.sock", NULL);
}

static int vmsr_helper_socket_read(QIOChannel *ioc,
                                  void *buf, int sz, Error **errp)
{
    ssize_t r = qio_channel_read_all(ioc, buf, sz, errp);

    if (r < 0) {
        object_unref(OBJECT(ioc));
        ioc = NULL;
        return -EINVAL;
    }

    return 0;
}

static int vmsr_helper_socket_write(QIOChannel *ioc,
                                   int fd,
                                   const void *buf, int sz, Error **errp)
{
    size_t nfds = (fd != -1);
    while (sz > 0) {
        struct iovec iov;
        ssize_t n_written;

        iov.iov_base = (void *)buf;
        iov.iov_len = sz;
        n_written = qio_channel_writev_full(QIO_CHANNEL(ioc), &iov, 1,
                                            nfds ? &fd : NULL, nfds, 0, errp);

        if (n_written <= 0) {
            assert(n_written != QIO_CHANNEL_ERR_BLOCK);
            object_unref(OBJECT(ioc));
            ioc = NULL;
            return n_written < 0 ? -EINVAL : 0;
        }

        nfds = 0;
        buf += n_written;
        sz -= n_written;
    }

    return 0;
}

uint64_t vmsr_read_msr(uint32_t reg, unsigned int cpu_id, uint32_t tid,
                       const char *path)
{
    uint64_t data = 0;
    char *socket_path = NULL;
    unsigned int buffer[3];

    if (path == NULL) {
        socket_path = compute_default_paths();
    } else {
        socket_path = g_strdup(path);
    }

    SocketAddress saddr = {
        .type = SOCKET_ADDRESS_TYPE_UNIX,
        .u.q_unix.path = socket_path
    };
    QIOChannelSocket *sioc = qio_channel_socket_new();
    Error *local_err = NULL;

    int r;

    qio_channel_set_name(QIO_CHANNEL(sioc), "vmsr-helper");
    qio_channel_socket_connect_sync(sioc,
                                    &saddr,
                                    &local_err);
    g_free(socket_path);
    if (local_err) {
        goto out_close;
    }

    /*
     * Send the required arguments:
     * 1. RAPL MSR register to read
     * 2. On which CPU ID
     * 3. From which vCPU (Thread ID)
     */
    buffer[0] = reg;
    buffer[1] = cpu_id;
    buffer[2] = tid;

    r = vmsr_helper_socket_write(QIO_CHANNEL(sioc),
                                 -1,
                                 &buffer, sizeof(buffer),
                                 &local_err);
    if (r < 0) {
        goto out_close;
    }

    r = vmsr_helper_socket_read(QIO_CHANNEL(sioc),
                                &data, sizeof(data),
                                &local_err);
    if (r < 0) {
        data = 0;
        goto out_close;
    }

out_close:
    /* Close socket. */
    qio_channel_close(QIO_CHANNEL(sioc), NULL);
    object_unref(OBJECT(sioc));
    return data;
}

/* Retrieve the max number of physical CPU on the package */
unsigned int vmsr_get_maxcpus(unsigned int package_num)
{
    int k, ncpus;
    unsigned int maxcpus;
    struct bitmask *cpus;

    cpus = numa_allocate_cpumask();
    ncpus = cpus->size;

    if (numa_node_to_cpus(package_num, cpus) < 0) {
        return 0;
    }

    maxcpus = 0;
    for (k = 0; k < ncpus; k++) {
        if (numa_bitmask_isbitset(cpus, k)) {
            maxcpus++;
        }
    }

    return maxcpus;
}

/* Retrieve the maximum number of physical packages */
unsigned int vmsr_get_max_physical_package(unsigned int max_cpus)
{
    unsigned int packageCount = 0;
    const char *dir = "/sys/devices/system/cpu/";
    int *uniquePackages;

    char *filePath;
    FILE *file;

    uniquePackages = g_new0(int, max_cpus);

    for (int i = 0; i < max_cpus; i++) {
        filePath = g_build_filename(dir, g_strdup_printf("cpu%d", i),
                                     "topology/physical_package_id", NULL);

        file = fopen(filePath, "r");

        if (file == NULL) {
            perror("Error opening file");
            g_free(filePath);
            g_free(uniquePackages);
            return 0;
        }

        char packageId[10];
        if (fgets(packageId, sizeof(packageId), file) == NULL) {
            packageCount = 0;
        }

        fclose(file);

        int currentPackageId = atoi(packageId);

        bool isUnique = true;
        for (int j = 0; j < packageCount; j++) {
            if (uniquePackages[j] == currentPackageId) {
                isUnique = false;
                break;
            }
        }

        if (isUnique) {
            uniquePackages[packageCount] = currentPackageId;
            packageCount++;

            if (packageCount >= max_cpus) {
                break;
            }
        }
    }

    g_free(filePath);
    g_free(uniquePackages);
    return (packageCount == 0) ? 1 : packageCount;
}
int vmsr_read_thread_stat(struct thread_stat *thread, int pid, int index)
{
    char *path;
    path = g_new0(char, MAX_PATH_LEN);

    path = g_build_filename(g_strdup_printf("/proc/%u/task/%d/stat", pid, \
            thread->thread_id), NULL);

    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return -1;
    }

    if (fscanf(file, "%*d (%*[^)]) %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u"
        " %llu %llu %*d %*d %*d %*d %*d %*d %*u %*u %*d %*u %*u"
        " %*u %*u %*u %*u %*u %*u %*u %*u %*u %*d %*u %*u %u",
           &thread->utime[index], &thread->stime[index], &thread->cpu_id) != 3)
        return -1;

    fclose(file);
    return 0;
}

/* Read QEMU stat task folder to retrieve all QEMU threads ID */
pid_t *vmsr_get_thread_ids(pid_t pid, unsigned int *num_threads)
{
    char *path = g_build_filename("/proc", g_strdup_printf("%d/task", pid), NULL);

    DIR *dir = opendir(path);
    if (dir == NULL) {
        perror("opendir");
        g_free(path);
        return NULL;
    }

    pid_t *thread_ids = NULL;
    unsigned int thread_count = 0;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        pid_t tid = atoi(ent->d_name);
        if (pid != tid) {
            thread_ids = g_renew(pid_t, thread_ids, (thread_count + 1));
            thread_ids[thread_count] = tid;
            thread_count++;
        }
    }

    closedir(dir);

    *num_threads = thread_count;
    g_free(path);
    return thread_ids;
}

void vmsr_delta_ticks(thread_stat *thd_stat, int i)
{
    thd_stat[i].delta_ticks = (thd_stat[i].utime[1] + thd_stat[i].stime[1])
                            - (thd_stat[i].utime[0] + thd_stat[i].stime[0]);
}

double vmsr_get_ratio(package_energy_stat *pkg_stat,
                        thread_stat *thd_stat,
                        int maxticks, int i) {

    return (pkg_stat[thd_stat[i].numa_node_id].e_delta / 100.0)
            * ((100.0 / maxticks) * thd_stat[i].delta_ticks);
}

void vmsr_init_topo_info(X86CPUTopoInfo *topo_info,
                           const MachineState *ms)
{
    topo_info->dies_per_pkg = ms->smp.dies;
    topo_info->cores_per_die = ms->smp.cores;
    topo_info->threads_per_core = ms->smp.threads;
}
