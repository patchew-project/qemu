#include <assert.h>
#include <unistd.h>
#include <signal.h>

int main(void)
{
    long unsigned int label, addr;
    struct sigaction action;

    action.sa_handler = _exit;
    sigaction(SIGABRT, &action, NULL);

    asm("insn:\n"
        " lis    %0, insn@highest\n"
        " addi   %0, %0, insn@higher\n"
        " rldicr %0, %0, 32, 31\n"
        " oris   %0, %0, insn@h\n"
        " ori    %0, %0, insn@l\n"
        " pla    %1, %2\n"
        : "=r" (label), "=r" (addr)
        : "i" (-5 * 4)); /* number of instruction between label and pla */
    assert(addr == label);

    return 0;
}

