#ifndef QEMU_ID_H
#define QEMU_ID_H

typedef enum IdSubSystems {
    ID_QDEV,
    ID_BLOCK,
    ID_MAX      /* last element, used as array size */
} IdSubSystems;

/**
 * id_generate: Generates an ID of the form PREFIX SUBSYSTEM NUMBER
 *  where:
 *
 *  - PREFIX is the reserved character '#'
 *  - SUBSYSTEM identifies the subsystem creating the ID
 *  - NUMBER is a decimal number unique within SUBSYSTEM.
 *
 *    Example: "#block146"
 *
 * Returns the generated id string for the subsystem
 *
 * @id: the subsystem to generate an id for
 */
char *id_generate(IdSubSystems id);

/**
 * id_wellformed: checks that an id starts with a letter
 *  followed by numbers, digits, '-','.', or '_'
 *
 * Returns %true if the id is well-formed
 *
 * @id: the id to be checked
 */
bool id_wellformed(const char *id);

#endif
