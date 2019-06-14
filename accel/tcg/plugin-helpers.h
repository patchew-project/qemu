#ifdef CONFIG_PLUGIN
/* Note: no TCG flags because those are overwritten later */
DEF_HELPER_2(plugin_vcpu_udata_cb, void, i32, ptr)
DEF_HELPER_4(plugin_vcpu_mem_cb, void, i32, i32, i64, ptr)
DEF_HELPER_5(plugin_vcpu_mem_haddr_cb, void, i32, i32, i64, ptr, ptr)
#endif
