#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/error-report.h"

#include "qapi/error.h"
#include "qapi/qapi-types-firmware.h"
#include "qapi/qapi-visit-firmware.h"
#include "qapi/qobject-input-visitor.h"

int main(int argc, char *argv[])
{
    Error *err = NULL;
    FirmwareMappingFlash *flash;
    Firmware *fw;
    Visitor *v;
    int fd, size, rc;
    char *buf;

    if (argc != 2) {
        fprintf(stderr, "usage: qemu-firmware <firmware-desc.json>\n");
        exit(1);
    }

    /* read file */
    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", argv[1], strerror(errno));
        exit(1);
    }
    size = lseek(fd, 0, SEEK_END);
    if (size < 0) {
        perror("lseek");
        exit(1);
    }
    lseek(fd, 0, SEEK_SET);
    buf = malloc(size+1);
    rc = read(fd, buf, size);
    if (rc != size) {
        fprintf(stderr, "file read error\n");
        exit(1);
    }
    buf[size] = 0;
    close(fd);

    /* parse file */
    v = qobject_input_visitor_new_str(buf, "", &err);
    if (!v) {
        error_report_err(err);
        exit(1);
    }
    visit_type_Firmware(v, NULL, &fw, &error_fatal);
    visit_free(v);

    /* print cmdline */
    switch (fw->mapping->device) {
    case FIRMWARE_DEVICE_FLASH:
        /*
         * FIXME: nvram should be a per-guest copy.
         *        How to handle that best here?
         */
        flash = &fw->mapping->u.flash;
        printf("-drive if=pflash,index=0,format=%s,file=%s\n",
               BlockdevDriver_str(flash->executable->format),
               flash->executable->pathname);
        printf("-drive if=pflash,index=1,format=%s,file=%s\n",
               BlockdevDriver_str(flash->nvram_template->format),
               flash->nvram_template->pathname);
        break;
    case FIRMWARE_DEVICE_MEMORY:
        printf("-bios %s\n", fw->mapping->u.memory.pathname);
        break;
    case FIRMWARE_DEVICE_KERNEL:
        printf("-kernel %s\n", fw->mapping->u.kernel.pathname);
        break;
    default:
        fprintf(stderr, "TODO\n");
        break;
    }

    exit(0);
}
