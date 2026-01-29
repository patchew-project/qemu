#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2026: Mauro Carvalho Chehab <mchehab@kernel.org>.

import re

from kdoc.kdoc_re import CFunction, KernRe

struct_args_pattern = r'([^,)]+)'

class CTransforms:
    """
    Data class containing a long set of transformations to turn
    structure member prefixes, and macro invocations and variables
    into something we can parse and generate kdoc for.
    """

    #: Transforms for structs and unions
    struct_xforms = [
        (CFunction("__attribute__"), ' '),
        (CFunction('__aligned'), ' '),
        (CFunction('__counted_by'), ' '),
        (CFunction('__counted_by_(le|be)'), ' '),
        (CFunction('__guarded_by'), ' '),
        (CFunction('__pt_guarded_by'), ' '),

        (KernRe(r'\s*__packed\s*', re.S), ' '),
        (KernRe(r'\s*CRYPTO_MINALIGN_ATTR', re.S), ' '),
        (KernRe(r'\s*__private', re.S), ' '),
        (KernRe(r'\s*__rcu', re.S), ' '),
        (KernRe(r'\s*____cacheline_aligned_in_smp', re.S), ' '),
        (KernRe(r'\s*____cacheline_aligned', re.S), ' '),

        (CFunction('__cacheline_group_(begin|end)'), ''),

        (CFunction('struct_group'), r'\2'),
        (CFunction('struct_group_attr'), r'\3'),
        (CFunction('struct_group_tagged'), r'struct \1 \2; \3'),
        (CFunction('__struct_group'), r'\4'),

        (CFunction('__ETHTOOL_DECLARE_LINK_MODE_MASK'), r'DECLARE_BITMAP(\1, __ETHTOOL_LINK_MODE_MASK_NBITS)'),
        (CFunction('DECLARE_PHY_INTERFACE_MASK',), r'DECLARE_BITMAP(\1, PHY_INTERFACE_MODE_MAX)'),
        (CFunction('DECLARE_BITMAP'), r'unsigned long \1[BITS_TO_LONGS(\2)]'),

        (CFunction('DECLARE_HASHTABLE'), r'unsigned long \1[1 << ((\2) - 1)]'),
        (CFunction('DECLARE_KFIFO'), r'\2 *\1'),
        (CFunction('DECLARE_KFIFO_PTR'), r'\2 *\1'),
        (CFunction('(?:__)?DECLARE_FLEX_ARRAY'), r'\1 \2[]'),
        (CFunction('DEFINE_DMA_UNMAP_ADDR'), r'dma_addr_t \1'),
        (CFunction('DEFINE_DMA_UNMAP_LEN'), r'__u32 \1'),
        (CFunction('VIRTIO_DECLARE_FEATURES'), r'union { u64 \1; u64 \1_array[VIRTIO_FEATURES_U64S]; }'),
    ]

    #: Transforms for function prototypes
    function_xforms = [
        (KernRe(r"^static +"), ""),
        (KernRe(r"^extern +"), ""),
        (KernRe(r"^asmlinkage +"), ""),
        (KernRe(r"^inline +"), ""),
        (KernRe(r"^__inline__ +"), ""),
        (KernRe(r"^__inline +"), ""),
        (KernRe(r"^__always_inline +"), ""),
        (KernRe(r"^noinline +"), ""),
        (KernRe(r"^__FORTIFY_INLINE +"), ""),
        (KernRe(r"__init +"), ""),
        (KernRe(r"__init_or_module +"), ""),
        (KernRe(r"__deprecated +"), ""),
        (KernRe(r"__flatten +"), ""),
        (KernRe(r"__meminit +"), ""),
        (KernRe(r"__must_check +"), ""),
        (KernRe(r"__weak +"), ""),
        (KernRe(r"__sched +"), ""),
        (KernRe(r"_noprof"), ""),
        (KernRe(r"__always_unused *"), ""),
        (KernRe(r"__printf\s*\(\s*\d*\s*,\s*\d*\s*\) +"), ""),
        (KernRe(r"__(?:re)?alloc_size\s*\(\s*\d+\s*(?:,\s*\d+\s*)?\) +"), ""),
        (KernRe(r"__diagnose_as\s*\(\s*\S+\s*(?:,\s*\d+\s*)*\) +"), ""),
        (KernRe(r"DECL_BUCKET_PARAMS\s*\(\s*(\S+)\s*,\s*(\S+)\s*\)"), r"\1, \2"),
        (KernRe(r"__no_context_analysis\s*"), ""),
        (KernRe(r"__attribute_const__ +"), ""),

        (CFunction("__cond_acquires"), ""),
        (CFunction("__cond_releases"), ""),
        (CFunction("__acquires"), ""),
        (CFunction("__releases"), ""),
        (CFunction("__must_hold"), ""),
        (CFunction("__must_not_hold"), ""),
        (CFunction("__must_hold_shared"), ""),
        (CFunction("__cond_acquires_shared"), ""),
        (CFunction("__acquires_shared"), ""),
        (CFunction("__releases_shared"), ""),
        (CFunction("__attribute__"), ""),
    ]

    #: Transforms for variables
    var_xforms = [
        (KernRe(r"__read_mostly"), ""),
        (KernRe(r"__ro_after_init"), ""),
        (KernRe(r'\s*__guarded_by\s*\([^\)]*\)', re.S), ""),
        (KernRe(r'\s*__pt_guarded_by\s*\([^\)]*\)', re.S), ""),
        (KernRe(r"LIST_HEAD\(([\w_]+)\)"), r"struct list_head \1"),
        (KernRe(r"(?://.*)$"), ""),
        (KernRe(r"(?:/\*.*\*/)"), ""),
        (KernRe(r";$"), ""),
    ]
