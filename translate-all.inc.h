/* Inline implementations for translate-all.h */

static inline size_t tb_caches_count(void)
{
    return 1ULL << TRACE_VCPU_EVENT_COUNT;
}

static inline struct qht *tb_caches_get(TBContext *tb_ctx,
                                        unsigned long *bitmap)
{
    unsigned long idx = *bitmap;
    return &tb_ctx->htables[idx];
}
