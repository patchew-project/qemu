#ifndef MMU_H
#define MMU_H

#ifndef CONFIG_USER_ONLY

/* Partition Table Entry */
struct patb_entry {
    uint64_t patbe0, patbe1;
};

#endif /* CONFIG_USER_ONLY */

#endif /* MMU_H */
