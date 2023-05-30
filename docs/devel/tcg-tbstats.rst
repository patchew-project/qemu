============
TBStatistics
============

What is TBStatistics
====================

TBStatistics (tb_stats) is a tool to gather various internal information of TCG
during binary translation, this allows us to identify such as hottest TBs,
guest to host instruction translation ratio, number of spills during register
allocation and more.


How to use TBStatistics
=======================

1. HMP interface
----------------

TBStatistics provides HMP interface, you can try the following examples after
connecting to the monitor.

* First check the help info::

    (qemu) help tb_stats
    tb_stats command [stats_level] -- Control tb statistics collection:tb_stats (start|pause|stop|filter) [all|jit_stats|exec_stats]

    (qemu) help info tb-list
    info tb-list [number sortedby] -- show a [number] translated blocks sorted by [sortedby]sortedby opts: hotness hg spills

    (qemu) help info tb
    info tb id [flag1,flag2,...] -- show information about one translated block by id.dump flags can be used to set dump code level: out_asm in_asm op

* Enable TBStatistics::

    (qemu) tb_stats start all
    (qemu)

* Get interested TB list::

    (qemu) info tb-list 2
    TB id:1 | phys:0x79bca0 virt:0xffffffff8059bca0 flags:0x01024001 0 inv/1
            | exec:1464084/0 guest inst cov:0.15%
            | trans:1 ints: g:3 op:16 op_opt:15 spills:0
            | h/g (host bytes / guest insts): 64.000000
            | time to gen at 2.4GHz => code:607.08(ns) IR:250.83(ns)

    TB id:2 | phys:0x2adf0c virt:0xffffffff800adf0c flags:0x01024001 0 inv/1
            | exec:1033298/0 guest inst cov:0.28%
            | trans:1 ints: g:8 op:35 op_opt:33 spills:0
            | h/g (host bytes / guest insts): 86.000000
            | time to gen at 2.4GHz => code:1429.58(ns) IR:545.42(ns)

* Dive into the specific TB::

    (qemu) info tb 1 op
    ------------------------------

    TB id:1 | phys:0x79bca0 virt:0xffffffff8059bca0 flags:0x01024001 7 inv/19
            | exec:2038349/0 guest inst cov:0.15%
            | trans:19 ints: g:3 op:16 op_opt:15 spills:0
            | h/g (host bytes / guest insts): 64.000000
            | time to gen at 2.4GHz => code:133434.17(ns) IR:176988.33(ns)

    OP:
     ld_i32 loc1,env,$0xfffffffffffffff0
     brcond_i32 loc1,$0x0,lt,$L0
     mov_i64 loc3,$0x7f3c70b3a4e0
     call inc_exec_freq,$0x1,$0,loc3

     ---- ffffffff8059bca0 0000000000006422
     add_i64 loc5,x2/sp,$0x8
     qemu_ld_i64 x8/s0,loc5,un+leq,1

     ---- ffffffff8059bca2 0000000000000000
     add_i64 x2/sp,x2/sp,$0x10

     ---- ffffffff8059bca4 0000000000000000
     mov_i64 pc,x1/ra
     and_i64 pc,pc,$0xfffffffffffffffe
     call lookup_tb_ptr,$0x6,$1,tmp9,env
     goto_ptr tmp9
     set_label $L0
     exit_tb $0x7f3e887a8043

    ------------------------------

* Stop TBStatistics after investigation, this will disable TBStatistics completely.::

    (qemu) tb_stats stop
    (qemu)

* Alternatively, TBStatistics can be paused, the previous collected TBStatistics
  are not cleared but there is no TBStatistics recorded for new TBs.::

    (qemu) tb_stats pause
    (qemu)

* Definitely, TBStatistics can be restarted for another round of investigation.::

    (qemu) tb_stats start all
    (qemu)


2. Dump at exit
---------------

New command line options have been added to enable dump TB information at exit:::

    -d tb_stats[[,level=(+all+jit+exec+time)][,dump_limit=<number>]]

e.g. starting qemu like this:::

    -d tb_stats,dump_limit=2

Qemu prints the following at exit:::

    QEMU: Terminated
    TB id:1 | phys:0x61be02 virt:0xffffffff8041be02 flags:0x01024001 0 inv/1
            | exec:72739176/0 guest inst cov:20.22%
            | trans:1 ints: g:9 op:35 op_opt:33 spills:0
            | h/g (host bytes / guest insts): 51.555557
            | time to gen at 2.4GHz => code:1065.42(ns) IR:554.17(ns)

    TB id:2 | phys:0x61bc66 virt:0xffffffff8041bc66 flags:0x01024001 0 inv/1
            | exec:25069507/0 guest inst cov:0.77%
            | trans:1 ints: g:1 op:15 op_opt:14 spills:0
            | h/g (host bytes / guest insts): 128.000000
            | time to gen at 2.4GHz => code:312.50(ns) IR:152.08(ns)
