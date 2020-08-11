#include <asm-generic/errno.h>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <linux/ethtool.h>
#include <linux/if.h>
#include <linux/sockios.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

const int number_of_entries_to_print = 10;
const uint32_t protected_memory_pattern[] = {
      0xdeadc0de, 0xb0bb1e, 0xfacade, 0xfeeb1e };

static void fail_with(const char *action, const char *cmd_name, int cmd,
                      int err)
{
    if (errno == EOPNOTSUPP) {
        printf("Unsupported operation: %s; errno = %d: %s.\n"
               "TEST SKIPPED (%s = 0x%x).\n",
               action, err, strerror(err), cmd_name, cmd);
        return;
    }
    if (err) {
        fprintf(stderr,
                "Failed to %s (%s = 0x%x): errno = %d: %s\n",
                action, cmd_name, cmd, err, strerror(err));
    } else {
        fprintf(stderr,
                "Failed to %s (%s = 0x%x): no errno\n",
                action, cmd_name, cmd);
    }
    exit(err);
}
#define FAIL(action, cmd) fail_with(action, #cmd, cmd, errno)

/*
 * `calloc_protected` and `protected_memory_changed` can be used to verify that
 * a system call does not write pass intended memory boundary.
 *
 * `ptr = calloc_protected(n)` will allocate extra memory after `n` bytes and
 * populate it with a memory pattern. The first `n` bytes are still guaranteed
 * to be zeroed out like `calloc(1, n)`. `protected_memory_changed(ptr, n)`
 * takes the pointer and the original size `n` and checks that the memory
 * pattern is intact.
 */
uint8_t *calloc_protected(size_t struct_size)
{
    uint8_t *buf = (uint8_t *) calloc(
        1,
        struct_size + sizeof(protected_memory_pattern));
    memcpy(buf + struct_size, protected_memory_pattern,
           sizeof(protected_memory_pattern));
    return buf;
}

bool protected_memory_changed(const uint8_t *ptr, size_t struct_size)
{
    return memcmp(ptr + struct_size, protected_memory_pattern,
                  sizeof(protected_memory_pattern)) != 0;
}

void print_entries(const char *fmt, int len, uint32_t *entries)
{
    int i;
    for (i = 0; i < len && i < number_of_entries_to_print; ++i) {
        printf(fmt, entries[i]);
    }
    if (len > number_of_entries_to_print) {
        printf(" (%d more omitted)", len - number_of_entries_to_print);
    }
}

void basic_test(int socketfd, struct ifreq ifr)
{
    struct ethtool_drvinfo drvinfo;
    drvinfo.cmd = ETHTOOL_GDRVINFO;
    ifr.ifr_data = (void *)&drvinfo;
    if (ioctl(socketfd, SIOCETHTOOL, &ifr) == -1) {
        FAIL("get driver info", ETHTOOL_GDRVINFO);
        return;
    }
    printf("Driver: %s (version %s)\n", drvinfo.driver, drvinfo.version);
}

/* Test flexible array. */
void test_get_stats(int socketfd, struct ifreq ifr, int n_stats)
{
    int i;
    struct ethtool_stats *stats = (struct ethtool_stats *)calloc(
        1, sizeof(*stats) + sizeof(stats->data[0]) * n_stats);
    stats->cmd = ETHTOOL_GSTATS;
    stats->n_stats = n_stats;
    ifr.ifr_data = (void *)stats;
    if (ioctl(socketfd, SIOCETHTOOL, &ifr) == -1) {
        FAIL("get statastics", ETHTOOL_GSTATS);
        free(stats);
        return;
    }
    if (stats->n_stats != n_stats) {
        FAIL("get consistent number of statistics", ETHTOOL_GSTATS);
    }
    for (i = 0; i < stats->n_stats && i < number_of_entries_to_print; ++i) {
        printf("stats[%d] = %llu\n", i, (unsigned long long)stats->data[i]);
    }
    if (stats->n_stats > number_of_entries_to_print) {
        printf("(%d more omitted)\n",
               stats->n_stats - number_of_entries_to_print);
    }
    free(stats);
}

/* Test flexible array with char array as elements. */
void test_get_strings(int socketfd, struct ifreq ifr, int n_stats)
{
    int i;
    struct ethtool_gstrings *gstrings =
        (struct ethtool_gstrings *)calloc(
            1, sizeof(*gstrings) + ETH_GSTRING_LEN * n_stats);
    gstrings->cmd = ETHTOOL_GSTRINGS;
    gstrings->string_set = ETH_SS_STATS;
    gstrings->len = n_stats;
    ifr.ifr_data = (void *)gstrings;
    if (ioctl(socketfd, SIOCETHTOOL, &ifr) == -1) {
        FAIL("get string set", ETHTOOL_GSTRINGS);
        free(gstrings);
        return;
    }
    if (gstrings->len != n_stats) {
        FAIL("get consistent number of statistics", ETHTOOL_GSTRINGS);
    }
    for (i = 0; i < gstrings->len && i < number_of_entries_to_print; ++i) {
        printf("stat_names[%d] = %.*s\n",
               i, ETH_GSTRING_LEN, gstrings->data + i * ETH_GSTRING_LEN);
    }
    if (gstrings->len > number_of_entries_to_print) {
        printf("(%d more omitted)\n",
               gstrings->len - number_of_entries_to_print);
    }
    free(gstrings);
}

/*
 * Testing manual implementation of converting `struct ethtool_sset_info`, also
 * info for subsequent tests.
 */
int test_get_sset_info(int socketfd, struct ifreq ifr)
{
    const int n_sset = 2;
    int n_stats;
    struct ethtool_sset_info *sset_info =
        (struct ethtool_sset_info *)calloc(
            1, sizeof(*sset_info) + sizeof(sset_info->data[0]) * n_sset);
    sset_info->cmd = ETHTOOL_GSSET_INFO;
    sset_info->sset_mask = 1 << ETH_SS_TEST | 1 << ETH_SS_STATS;
    assert(__builtin_popcount(sset_info->sset_mask) == n_sset);
    ifr.ifr_data = (void *)sset_info;
    if (ioctl(socketfd, SIOCETHTOOL, &ifr) == -1) {
        fail_with("get string set info", "ETHTOOL_GSSET_INFO",
                  ETHTOOL_GSSET_INFO, errno);
        free(sset_info);
        return 0;
    }
    if ((sset_info->sset_mask & (1 << ETH_SS_STATS)) == 0) {
        puts("No stats string set info, SKIPPING dependent tests");
        free(sset_info);
        return 0;
    }
    n_stats = (sset_info->sset_mask & (1 << ETH_SS_TEST)) ?
        sset_info->data[1] :
        sset_info->data[0];
    printf("n_stats = %d\n", n_stats);
    free(sset_info);
    return n_stats;
}

/*
 * Test manual implementation of converting `struct ethtool_rxnfc`, focusing on
 * the case where only the first three fields are present. (The original struct
 * definition.)
 */
void test_get_rxfh(int socketfd, struct ifreq ifr)
{
    struct ethtool_rxnfc *rxnfc;
    const int rxnfc_first_three_field_size =
        sizeof(rxnfc->cmd) + sizeof(rxnfc->flow_type) + sizeof(rxnfc->data);
    rxnfc = (struct ethtool_rxnfc *)calloc_protected(
        rxnfc_first_three_field_size);
    rxnfc->cmd = ETHTOOL_GRXFH;
    rxnfc->flow_type = TCP_V4_FLOW;
    ifr.ifr_data = (void *)rxnfc;
    if (ioctl(socketfd, SIOCETHTOOL, &ifr) == -1) {
        FAIL("get RX flow classification rules", ETHTOOL_GRXFH);
        free(rxnfc);
        return;
    }
    if (protected_memory_changed((const uint8_t *)rxnfc,
                                 rxnfc_first_three_field_size)) {
        FAIL("preserve memory after the first three fields", ETHTOOL_GRXFH);
    }
    printf("Flow hash bitmask (flow_type = TCP v4): 0x%llx\n",
           (unsigned long long)rxnfc->data);
    free(rxnfc);
}

/* Test manual implementation of converting `struct ethtool_link_settings`. */
void test_get_link_settings(int socketfd, struct ifreq ifr)
{
    int link_mode_masks_nwords;
    struct ethtool_link_settings *link_settings_header =
        (struct ethtool_link_settings *) calloc_protected(
            sizeof(*link_settings_header));
    link_settings_header->cmd = ETHTOOL_GLINKSETTINGS;
    link_settings_header->link_mode_masks_nwords = 0;
    ifr.ifr_data = (void *)link_settings_header;
    if (ioctl(socketfd, SIOCETHTOOL, &ifr) == -1) {
        FAIL("get link settings mask sizes", ETHTOOL_GLINKSETTINGS);
        free(link_settings_header);
        return;
    }
    if (protected_memory_changed((const uint8_t *)link_settings_header,
                                 sizeof(*link_settings_header))) {
        FAIL("preserve link_mode_masks", ETHTOOL_GLINKSETTINGS);
    }
    if (link_settings_header->link_mode_masks_nwords >= 0) {
        FAIL("complete handshake", ETHTOOL_GLINKSETTINGS);
    }
    link_mode_masks_nwords = -link_settings_header->link_mode_masks_nwords;

    struct ethtool_link_settings *link_settings =
        (struct ethtool_link_settings *)calloc(
            1,
            sizeof(*link_settings) +
            sizeof(link_settings_header->link_mode_masks[0]) *
            link_mode_masks_nwords * 3);
    link_settings->cmd = ETHTOOL_GLINKSETTINGS;
    link_settings->link_mode_masks_nwords = link_mode_masks_nwords;
    ifr.ifr_data = (void *)link_settings;
    if (ioctl(socketfd, SIOCETHTOOL, &ifr) == -1) {
        FAIL("get link settings", ETHTOOL_GLINKSETTINGS);
        free(link_settings_header);
        free(link_settings);
        return;
    }
    if (link_settings->link_mode_masks_nwords != link_mode_masks_nwords) {
        FAIL("have consistent number of mode masks", ETHTOOL_GLINKSETTINGS);
    }

    printf("Link speed: %d MB\n", link_settings->speed);
    printf("Number of link mode masks: %d\n",
           link_settings->link_mode_masks_nwords);
    if (link_settings->link_mode_masks_nwords > 0) {
        printf("Supported bitmap:");
        print_entries(" 0x%08x",
                      link_settings->link_mode_masks_nwords,
                      link_settings->link_mode_masks);
        putchar('\n');

        printf("Advertising bitmap:");
        print_entries(" 0x%08x",
                      link_settings->link_mode_masks_nwords,
                      link_settings->link_mode_masks +
                      link_settings->link_mode_masks_nwords);
        putchar('\n');

        printf("Lp advertising bitmap:");
        print_entries(" 0x%08x",
                      link_settings->link_mode_masks_nwords,
                      link_settings->link_mode_masks +
                      2 * link_settings->link_mode_masks_nwords);
        putchar('\n');
    }

    free(link_settings_header);
    free(link_settings);
}

/* Test manual implementation of converting `struct ethtool_per_queue_op`. */
void test_perqueue(int socketfd, struct ifreq ifr)
{
    const int n_queue = 2;
    int i;
    struct ethtool_per_queue_op *per_queue_op =
        (struct ethtool_per_queue_op *)calloc(
            1,
            sizeof(*per_queue_op) + sizeof(struct ethtool_coalesce) * n_queue);
    per_queue_op->cmd = ETHTOOL_PERQUEUE;
    per_queue_op->sub_command = ETHTOOL_GCOALESCE;
    per_queue_op->queue_mask[0] = 0x3;
    ifr.ifr_data = (void *)per_queue_op;
    if (ioctl(socketfd, SIOCETHTOOL, &ifr) == -1) {
        FAIL("get coalesce per queue", ETHTOOL_PERQUEUE);
        free(per_queue_op);
        return;
    }
    for (i = 0; i < n_queue; ++i) {
        struct ethtool_coalesce *coalesce = (struct ethtool_coalesce *)(
            per_queue_op->data + sizeof(*coalesce) * i);
        if (coalesce->cmd != ETHTOOL_GCOALESCE) {
            fprintf(stderr,
                    "ETHTOOL_PERQUEUE (%d) sub_command ETHTOOL_GCOALESCE (%d) "
                    "fails to set entry %d's cmd to ETHTOOL_GCOALESCE, got %d "
                    "instead\n",
                    ETHTOOL_PERQUEUE, ETHTOOL_GCOALESCE, i,
                    coalesce->cmd);
            exit(-1);
        }
        printf("rx_coalesce_usecs[%d] = %u\nrx_max_coalesced_frames[%d] = %u\n",
               i, coalesce->rx_coalesce_usecs,
               i, coalesce->rx_max_coalesced_frames);
    }

    free(per_queue_op);
}

/* Test manual implementation of ETHTOOL_GRSSH. */
void test_get_rssh(int socketfd, struct ifreq ifr)
{
    int i;
    struct ethtool_rxfh *rxfh_header =
        (struct ethtool_rxfh *)calloc_protected(sizeof(*rxfh_header));
    rxfh_header->cmd = ETHTOOL_GRSSH;
    ifr.ifr_data = (void *)rxfh_header;
    if (ioctl(socketfd, SIOCETHTOOL, &ifr) == -1) {
        FAIL("get RX flow hash indir and hash key size", ETHTOOL_GRSSH);
        free(rxfh_header);
        return;
    }
    if (protected_memory_changed((const uint8_t *)rxfh_header,
                                 sizeof(*rxfh_header))) {
        FAIL("preserve rss_config", ETHTOOL_GRSSH);
    }
    printf("RX flow hash indir size = %d\nRX flow hash key size = %d\n",
           rxfh_header->indir_size, rxfh_header->key_size);

    struct ethtool_rxfh *rxfh = (struct ethtool_rxfh *)calloc(
        1,
        sizeof(*rxfh) + 4 * rxfh_header->indir_size + rxfh_header->key_size);
    *rxfh = *rxfh_header;
    ifr.ifr_data = (void *)rxfh;
    if (ioctl(socketfd, SIOCETHTOOL, &ifr) == -1) {
        FAIL("get RX flow hash indir and hash key", ETHTOOL_GRSSH);
        free(rxfh_header);
        free(rxfh);
        return;
    }

    if (rxfh->indir_size == 0) {
        printf("No RX flow hash indir\n");
    } else {
        printf("RX flow hash indir:");
        print_entries(" 0x%08x", rxfh->indir_size, rxfh->rss_config);
        putchar('\n');
    }

    if (rxfh->key_size == 0) {
        printf("No RX flow hash key\n");
    } else {
        char *key = (char *)(rxfh->rss_config + rxfh->indir_size);
        printf("RX flow hash key:");
        for (i = 0;  i < rxfh->key_size; ++i) {
            if (i % 2 == 0) {
                putchar(' ');
            }
            printf("%02hhx", key[i]);
        }
        putchar('\n');
    }
    free(rxfh_header);
    free(rxfh);
}

int main(int argc, char **argv)
{
    int socketfd, n_stats, i;
    struct ifreq ifr;

    socketfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socketfd == -1) {
        int err = errno;
        fprintf(stderr,
                "Failed to open socket: errno = %d: %s\n",
                err, strerror(err));
        return err;
    }

    for (i = 1;; ++i) {
        ifr.ifr_ifindex = i;
        if (ioctl(socketfd, SIOCGIFNAME, &ifr) == -1) {
            puts("Could not find a non-loopback interface, SKIPPING");
            return 0;
        }
        if (strncmp(ifr.ifr_name, "lo", IFNAMSIZ) != 0) {
            break;
        }
    }
    printf("Interface index: %d\nInterface name: %.*s\n",
           ifr.ifr_ifindex, IFNAMSIZ, ifr.ifr_name);

    basic_test(socketfd, ifr);

    n_stats = test_get_sset_info(socketfd, ifr);
    if (n_stats > 0) {
        /* Testing lexible arrays. */
        test_get_stats(socketfd, ifr, n_stats);
        test_get_strings(socketfd, ifr, n_stats);
    }

    /* Testing manual implementations of structure convertions. */
    test_get_rxfh(socketfd, ifr);
    test_get_link_settings(socketfd, ifr);
    test_perqueue(socketfd, ifr);

    /* Testing manual implementations of operations. */
    test_get_rssh(socketfd, ifr);

    return 0;
}
