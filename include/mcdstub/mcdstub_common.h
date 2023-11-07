#ifndef MCDSTUB_COMMON_H
#define MCDSTUB_COMMON_H

#define ARGUMENT_STRING_LENGTH 64
#define TCP_CONFIG_STRING_LENGTH 128

typedef struct mcd_mem_space_st {
    const char *name;
    uint32_t id;
    uint32_t type;
    uint32_t bits_per_mau;
    uint8_t invariance;
    uint32_t endian;
    uint64_t min_addr;
    uint64_t max_addr;
    uint32_t supported_access_options;
    /* internal */
    bool is_secure;
    bool is_physical;
} mcd_mem_space_st;

typedef struct mcd_reg_st {
    /* xml info */
    char name[ARGUMENT_STRING_LENGTH];
    char group[ARGUMENT_STRING_LENGTH];
    char type[ARGUMENT_STRING_LENGTH];
    uint32_t bitsize;
    uint32_t id; /* id used by the mcd interface */
    uint32_t internal_id; /* id inside reg type */
    uint8_t reg_type;
    /* mcd metadata */
    uint32_t mcd_reg_group_id;
    uint32_t mcd_mem_space_id;
    uint32_t mcd_reg_type;
    uint32_t mcd_hw_thread_id;
    /* data for op-code */
    uint32_t opcode;
} mcd_reg_st;

typedef struct mcd_reg_group_st {
    const char *name;
    uint32_t id;
} mcd_reg_group_st;

/**
 * parse_reg_xml() -  Parses a GDB register XML file
 *
 * This fuction extracts all registers from the provided xml file and stores
 * them into the registers GArray. It extracts the register name, bitsize, type
 * and group if they are set.
 * @xml: String with contents of the XML file.
 * @registers: GArray with stored registers.
 * @reg_type: Register type (depending on file).
 * @size: Number of characters in the xml string.
 */
void parse_reg_xml(const char *xml, int size, GArray* registers,
    uint8_t reg_type, uint32_t reg_id_offset);

#endif /* MCDSTUB_COMMON_H */
