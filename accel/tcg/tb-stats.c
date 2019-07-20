#include "qemu/osdep.h"

#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg.h"

#include "qemu/qemu-print.h"

struct jit_profile_info {
    uint64_t translations;
    uint64_t aborted;
    uint64_t ops;
    unsigned ops_max;
    uint64_t del_ops;
    uint64_t temps;
    unsigned temps_max;
    uint64_t host;
    uint64_t guest;
    uint64_t host_ins;
    uint64_t search_data;
};

/* accumulate the statistics from all TBs */
static void collect_jit_profile_info(void *p, uint32_t hash, void *userp)
{
    struct jit_profile_info *jpi = userp;
    TBStatistics *tbs = p;

    jpi->translations += tbs->translations.total;
    jpi->ops += tbs->code.num_tcg_ops;
    if (tbs->translations.total && tbs->code.num_tcg_ops / tbs->translations.total
            > jpi->ops_max) {
        jpi->ops_max = tbs->code.num_tcg_ops / tbs->translations.total;
    }
    jpi->del_ops += tbs->code.deleted_ops;
    jpi->temps += tbs->code.temps;
    if (tbs->translations.total && tbs->code.temps / tbs->translations.total >
            jpi->temps_max) {
        jpi->temps_max = tbs->code.temps / tbs->translations.total;
    }
    jpi->host += tbs->code.out_len;
    jpi->guest += tbs->code.in_len;
    jpi->host_ins += tbs->code.num_host_inst;
    jpi->search_data += tbs->code.search_out_len;
}

/* dump JIT statisticis using TCGProfile and TBStats */
void dump_jit_profile_info(TCGProfile *s)
{
    if (!tb_stats_collection_enabled()) {
        return;
    }

    struct jit_profile_info *jpi = g_new0(struct jit_profile_info, 1);

    qht_iter(&tb_ctx.tb_stats, collect_jit_profile_info, jpi);

    if (jpi->translations) {
        qemu_printf("translated TBs      %" PRId64 "\n", jpi->translations);
        qemu_printf("avg ops/TB          %0.1f max=%d\n",
                jpi->ops / (double) jpi->translations, jpi->ops_max);
        qemu_printf("deleted ops/TB      %0.2f\n",
                jpi->del_ops / (double) jpi->translations);
        qemu_printf("avg temps/TB        %0.2f max=%d\n",
                jpi->temps / (double) jpi->translations, jpi->temps_max);
        qemu_printf("avg host code/TB    %0.1f\n",
                jpi->host / (double) jpi->translations);
        qemu_printf("avg host ins/TB     %0.1f\n",
                jpi->host_ins / (double) jpi->translations);
        qemu_printf("avg search data/TB  %0.1f\n",
                jpi->search_data / (double) jpi->translations);

        if (s) {
            int64_t tot = s->interm_time + s->code_time;
            qemu_printf("JIT cycles          %" PRId64 " (%0.3f s at 2.4 GHz)\n",
                            tot, tot / 2.4e9);
            qemu_printf("cycles/op           %0.1f\n",
                        jpi->ops ? (double)tot / jpi->ops : 0);
            qemu_printf("cycles/in byte      %0.1f\n",
                        jpi->guest ? (double)tot / jpi->guest : 0);
            qemu_printf("cycles/out byte     %0.1f\n",
                        jpi->host ? (double)tot / jpi->host : 0);
            qemu_printf("cycles/out inst     %0.1f\n",
                        jpi->host_ins ? (double)tot / jpi->host_ins : 0);
            qemu_printf("cycles/search byte     %0.1f\n",
                        jpi->search_data ? (double)tot / jpi->search_data : 0);
            if (tot == 0) {
                tot = 1;
            }
            qemu_printf("  gen_interm time   %0.1f%%\n",
                        (double)s->interm_time / tot * 100.0);
            qemu_printf("  gen_code time     %0.1f%%\n",
                        (double)s->code_time / tot * 100.0);
            qemu_printf("optim./code time    %0.1f%%\n",
                        (double)s->opt_time / (s->code_time ? s->code_time : 1)
                        * 100.0);
            qemu_printf("liveness/code time  %0.1f%%\n",
                    (double)s->la_time / (s->code_time ? s->code_time : 1) * 100.0);
            qemu_printf("cpu_restore count   %" PRId64 "\n",
                    s->restore_count);
            qemu_printf("  avg cycles        %0.1f\n",
                    s->restore_count ? (double)s->restore_time / s->restore_count : 0);
        }
    }
}


