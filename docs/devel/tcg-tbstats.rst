============
TBStatistics
============

What is TBStatistics
====================

TBStatistics (tb_stats) is a tool to gather various internal information of TCG
during binary translation, this allows us to identify such as hottest TBs,
guest to host instruction translation ratio, number of spills during register
allocation and more.

What does TBStatistics collect
===============================

TBStatistics mainly collects the following stats:

* TB exec stats, e.g. the execution count of each TB
* TB jit stats, e.g. guest insn count, tcg ops, tcg spill etc.
* opcount of each instruction, use 'info opcount' to show it


How to use TBStatistics
=======================

1. HMP interface
----------------

TBStatistics provides HMP interface, you can try the following examples after
connecting to the monitor.

* First check the help info::

    (qemu) help tb_stats
    tb_stats command [flag] -- Control tb statistics collection:tb_stats (start|stop|status) [all|jit|exec]

    (qemu) help info tb-list
    info tb-list [number sortedby] -- show a [number] translated blocks sorted by [sortedby]sortedby opts: hotness hg spills

    (qemu) help info tb
    info tb id [flag1,flag2,...] -- show information about one translated block by id.dump flags can be used to set dump code level: out_asm in_asm op

* Enable TBStatistics::

    (qemu) tb_stats start all
    (qemu)

* Get interested TB list::

    (qemu) info tb-list 2
    TB id:0 | phys:0x79bca0 virt:0xffffffff8059bca0 flags:0x01024001 0 inv/1
            | exec:1464084/0 guest inst cov:0.15%
            | trans:1 inst: g:3 op:16 op_opt:15 spills:0
            | h/g (host bytes / guest insts): 64.000000

    TB id:1 | phys:0x2adf0c virt:0xffffffff800adf0c flags:0x01024001 0 inv/1
            | exec:1033298/0 guest inst cov:0.28%
            | trans:1 inst: g:8 op:35 op_opt:33 spills:0
            | h/g (host bytes / guest insts): 86.000000

* Dive into the specific TB::

    (qemu) info tb 0
    ------------------------------

    TB id:0 | phys:0x63474e virt:0x0000000000000000 flags:0x01028001 0 inv/1
            | exec:131719290/0 guest inst cov:8.44%
            | trans:1 ints: g:9 op:36 op_opt:34 spills:0
            | h/g (host bytes / guest insts): 51.555557

    0x0063474e:  00194a83          lbu                     s5,1(s2)
    0x00634752:  00094803          lbu                     a6,0(s2)
    0x00634756:  0b09              addi                    s6,s6,2
    0x00634758:  008a9a9b          slliw                   s5,s5,8
    0x0063475c:  01586833          or                      a6,a6,s5
    0x00634760:  ff0b1f23          sh                      a6,-2(s6)
    0x00634764:  1c7d              addi                    s8,s8,-1
    0x00634766:  0909              addi                    s2,s2,2
    0x00634768:  fe0c13e3          bnez                    s8,-26                  # 0x63474e

    ------------------------------

* Stop TBStatistics after investigation, this will disable TBStatistics completely.::

    (qemu) tb_stats stop
    (qemu)

* Definitely, TBStatistics can be restarted for another round of investigation.::

    (qemu) tb_stats start all
    (qemu)


2. Start TBStatistics with command line
---------------------------------------

If you don't want to missing anything at guest starting, command line option is
provided to start TBStatistics at start:::

    -d tb_stats_{all,jit,exec}


3. Dump hottest at exit
-----------------------

TBStatistics is able to dump hottest TB information at exit as follows:::

    -d tb_stats_{all,jit,exec}[:dump_num_at_exit]

e.g. starting qemu like this:::

    -d tb_stats_all:2

QEMU prints the following at exit:::

    TB id:0 | phys:0x242b8 virt:0x00000000000242b8 flags:0x00024078 0 inv/0
            | exec:1161/0 guest inst cov:10.36%
            | trans:1 ints: g:2 op:20 op_opt:19 spills:0
            | h/g (host bytes / guest insts): 61.500000

    TB id:1 | phys:0x242be virt:0x00000000000242be flags:0x00024078 0 inv/0
            | exec:1161/0 guest inst cov:10.36%
            | trans:1 ints: g:2 op:20 op_opt:18 spills:0
            | h/g (host bytes / guest insts): 59.500000

This is particularly useful for user mode QEMU.
