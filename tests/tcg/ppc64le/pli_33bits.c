#include <assert.h>
#include <unistd.h>
#include <signal.h>

int main(void)
{
    long int var;
    struct sigaction action;

    action.sa_handler = _exit;
    sigaction(SIGABRT, &action, NULL);

    asm(" pli %0,0x1FFFFFFFF\n"
        : "=r"(var));
    assert(var == 0x1FFFFFFFF);

    asm(" pli %0,-0x1FFFFFFFF\n"
       : "=r"(var));
    assert(var == -0x1FFFFFFFF);

    return 0;
}
