#include "qemu/osdep.h"
#include "sysemu/sysemu.h"

#ifdef CONFIG_SDL
#if defined(__APPLE__) || defined(main)
#include <SDL.h>
int qemu_main(int argc, char **argv, char **envp);
int main(int argc, char **argv)
{
    return qemu_main(argc, argv, NULL);
}
#undef main
#define main qemu_main
#endif
#endif /* CONFIG_SDL */

int main(int argc, char **argv, char **envp)
{
    int ret = qemu_init(argc, argv, envp);
    if (ret != 0) {
        return ret;
    }

    main_loop();

    qemu_cleanup();

    return 0;
}
