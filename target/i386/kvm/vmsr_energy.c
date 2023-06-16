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

#define MAX_PATH_LEN 50
#define MAX_LINE_LEN 500

uint64_t vmsr_read_msr(uint32_t reg, unsigned int cpu_id)
{
    int fd;
    uint64_t data;

    char path[MAX_PATH_LEN];
    snprintf(path, MAX_PATH_LEN, "/dev/cpu/%u/msr", cpu_id);

    fd = open(path , O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    if (pread(fd, &data, sizeof data, reg) != sizeof data) {
        data = 0;
    }

    close(fd);
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

/* Retrieve the maximum number of physical package */
unsigned int vmsr_get_max_physical_package(unsigned int max_cpus)
{
    unsigned int packageCount = 0;
    int *uniquePackages;

    char filePath[256];
    FILE *file;

    uniquePackages = g_malloc0(max_cpus * sizeof(int));

    for (int i = 0; ; i++) {
        snprintf(filePath, sizeof(filePath),
            "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", i);
        file = fopen(filePath, "r");

        if (file == NULL) {
            break;
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
        }
    }

    g_free(uniquePackages);
    return packageCount;
}

int vmsr_read_thread_stat(struct thread_stat *thread, int pid, int index)
{
    char path[MAX_PATH_LEN];
    snprintf(path, MAX_PATH_LEN, "/proc/%u/task/%d/stat", pid, \
             thread->thread_id);

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
pid_t *vmsr_get_thread_ids(pid_t pid, int *num_threads)
{
    char path[100];
    sprintf(path, "/proc/%d/task", pid);

    DIR *dir = opendir(path);
    if (dir == NULL) {
        perror("opendir");
        return NULL;
    }

    pid_t *thread_ids = NULL;
    int thread_count = 0;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        pid_t tid = atoi(ent->d_name);
        if (pid != tid) {
            thread_ids = g_realloc(thread_ids,
                                 (thread_count + 1) * sizeof(pid_t));
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

double vmsr_get_ratio(package_energy_stat *pkg_stat,
                        thread_stat *thd_stat,
                        int maxticks, int i) {

    return (pkg_stat[thd_stat[i].numa_node_id].e_delta / 100.0)
            * ((100.0 / maxticks) * thd_stat[i].delta_ticks);
}

