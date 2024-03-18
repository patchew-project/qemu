/*
 * QEMU KVM support -- x86 virtual RAPL msr
 *
 * Copyright 2024 Red Hat, Inc. 2024
 *
 *  Author:
 *      Anthony Harivel <aharivel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "vmsr_energy.h"
#include "io/channel.h"
#include "io/channel-socket.h"
#include "hw/boards.h"
#include "cpu.h"
#include "host-cpu.h"

static char *compute_default_paths(void)
{
    g_autofree char *state = qemu_get_local_state_dir();

    return g_build_filename(state, "run", "qemu-vmsr-helper.sock", NULL);
}

bool is_host_cpu_intel(void)
{
    int family, model, stepping;
    char vendor[CPUID_VENDOR_SZ + 1];

    host_cpu_vendor_fms(vendor, &family, &model, &stepping);

    return strcmp(vendor, CPUID_VENDOR_INTEL);
}

int is_rapl_enabled(void)
{
    const char *path = "/sys/class/powercap/intel-rapl/enabled";
    FILE *file = fopen(path, "r");
    int value = 0;

    if (file != NULL) {
        if (fscanf(file, "%d", &value) != 1) {
            error_report("INTEL RAPL not enabled");
        }
        fclose(file);
    } else {
        error_report("Error opening %s", path);
    }

    return value;
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

uint64_t vmsr_read_msr(uint32_t reg, unsigned int cpu_id, uint32_t tid,
                       const char *path)
{
    uint64_t data = 0;
    char *socket_path = NULL;
    char buffer[3];

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

/* Retrieve the max number of physical package */
unsigned int vmsr_get_max_physical_package(unsigned int max_cpus)
{
    const char *dir = "/sys/devices/system/cpu/";
    const char *topo_path = "topology/physical_package_id";
    g_autofree int *uniquePackages = 0;
    unsigned int packageCount = 0;
    FILE *file = NULL;

    uniquePackages = g_new0(int, max_cpus);

    for (int i = 0; i < max_cpus; i++) {
        g_autofree char *filePath = NULL;
        g_autofree char *cpuid = g_strdup_printf("cpu%d", i);

        filePath = g_build_filename(dir, cpuid, topo_path, NULL);

        file = fopen(filePath, "r");

        if (file == NULL) {
            error_report("Error opening physical_package_id file");
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

    return (packageCount == 0) ? 1 : packageCount;
}

/* Retrieve the max number of physical cpu on the host */
unsigned int vmsr_get_maxcpus(void)
{
    GDir *dir;
    const gchar *entry_name;
    unsigned int cpu_count = 0;
    const char *path = "/sys/devices/system/cpu/";

    dir = g_dir_open(path, 0, NULL);
    if (dir == NULL) {
        error_report("Unable to open cpu directory");
        return -1;
    }

    while ((entry_name = g_dir_read_name(dir)) != NULL) {
        if (g_ascii_strncasecmp(entry_name, "cpu", 3) == 0 &&
            isdigit(entry_name[3])) {
            cpu_count++;
        }
    }

    g_dir_close(dir);

    return cpu_count;
}

/* Count the number of physical cpu on each packages */
unsigned int vmsr_count_cpus_per_package(unsigned int *package_count,
                                         unsigned int max_pkgs)
{
    g_autofree char *file_contents = NULL;
    g_autofree char *path = NULL;
    gsize length;

    /* Iterate over cpus and count cpus in each package */
    for (int cpu_id = 0; ; cpu_id++) {
        path = g_build_filename(
                g_strdup_printf("/sys/devices/system/cpu/cpu%d/"
                    "topology/physical_package_id", cpu_id), NULL);

        if (!g_file_get_contents(path, &file_contents, &length, NULL)) {
            break; /* No more cpus */
        }

        /* Get the physical package ID for this CPU */
        int package_id = atoi(file_contents);

        /* Check if the package ID is within the known number of packages */
        if (package_id >= 0 && package_id < max_pkgs) {
            /* If yes, count the cpu for this package*/
            package_count[package_id]++;
        }
    }

    return 0;
}

/* Get the physical package id from a given cpu id */
int vmsr_get_physical_package_id(int cpu_id)
{
    g_autofree char *file_contents = NULL;
    g_autofree char *file_path = NULL;
    int package_id = -1;
    gsize length;

    file_path = g_strdup_printf("/sys/devices/system/cpu/cpu%d\
        /topology/physical_package_id", cpu_id);

    if (!g_file_get_contents(file_path, &file_contents, &length, NULL)) {
        goto out;
    }

    package_id = atoi(file_contents);

out:
    return package_id;
}

/* Read the scheduled time for a given thread of a give pid */
void vmsr_read_thread_stat(pid_t pid,
                      unsigned int thread_id,
                      unsigned long long *utime,
                      unsigned long long *stime,
                      unsigned int *cpu_id)
{
    g_autofree char *path = NULL;

    path = g_build_filename(g_strdup_printf("/proc/%u/task/%d/stat", pid, \
            thread_id), NULL);

    FILE *file = fopen(path, "r");
    if (file == NULL) {
        pid = -1;
        return;
    }

    if (fscanf(file, "%*d (%*[^)]) %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u"
        " %llu %llu %*d %*d %*d %*d %*d %*d %*u %*u %*d %*u %*u"
        " %*u %*u %*u %*u %*u %*u %*u %*u %*u %*d %*u %*u %u",
           utime, stime, cpu_id) != 3)
    {
        pid = -1;
        return;
    }

    fclose(file);
    return;
}

/* Read QEMU stat task folder to retrieve all QEMU threads ID */
pid_t *vmsr_get_thread_ids(pid_t pid, unsigned int *num_threads)
{
    g_autofree char *path = g_build_filename("/proc",
        g_strdup_printf("%d/task", pid), NULL);

    DIR *dir = opendir(path);
    if (dir == NULL) {
        error_report("Error opening /proc/qemu/task");
        return NULL;
    }

    pid_t *thread_ids = NULL;
    unsigned int thread_count = 0;

    g_autofree struct dirent *ent = NULL;
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
    return thread_ids;
}

void vmsr_delta_ticks(thread_stat *thd_stat, int i)
{
    thd_stat[i].delta_ticks = (thd_stat[i].utime[1] + thd_stat[i].stime[1])
                            - (thd_stat[i].utime[0] + thd_stat[i].stime[0]);
}

double vmsr_get_ratio(uint64_t e_delta,
                      unsigned long long delta_ticks,
                      unsigned int maxticks)
{
    return (e_delta / 100.0) * ((100.0 / maxticks) * delta_ticks);
}

void vmsr_init_topo_info(X86CPUTopoInfo *topo_info,
                           const MachineState *ms)
{
    topo_info->dies_per_pkg = ms->smp.dies;
    topo_info->cores_per_die = ms->smp.cores;
    topo_info->threads_per_core = ms->smp.threads;
}
