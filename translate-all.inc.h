/* Inline implementations for translate-all.h */

static inline size_t tb_caches_count(void)
{
    return 1ULL << trace_get_vcpu_event_count();
}

static inline struct qht *tb_caches_get(TBContext *tb_ctx,
                                        unsigned long *bitmap)
{
    unsigned long idx = *bitmap;
    return &tb_ctx->htables[idx];
}
