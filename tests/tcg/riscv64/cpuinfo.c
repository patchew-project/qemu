#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define BUFFER_SIZE 1024

int main(void)
{
    char buffer[BUFFER_SIZE];
    FILE *fp = fopen("/proc/cpuinfo", "r");
    assert(fp != NULL);

    while (fgets(buffer, BUFFER_SIZE, fp) != NULL) {
        if (strstr(buffer, "processor") != NULL) {
            assert(strstr(buffer, "processor\t: ") == buffer);
        } else if (strstr(buffer, "hart") != NULL) {
            assert(strstr(buffer, "hart\t\t: ") == buffer);
        } else if (strstr(buffer, "isa") != NULL) {
            assert(strcmp(buffer, "isa\t\t: rv64imafdc_zicsr_zifencei\n") == 0);
        } else if (strstr(buffer, "mmu") != NULL) {
            assert(strcmp(buffer, "mmu\t\t: sv48\n") == 0);
        } else if (strstr(buffer, "uarch") != NULL) {
            assert(strcmp(buffer, "uarch\t\t: qemu\n") == 0);
        }
    }

    fclose(fp);
    return 0;
}
