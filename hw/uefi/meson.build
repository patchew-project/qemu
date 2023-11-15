uefi_vars_ss = ss.source_set()
uefi_vars_ss.add(when: 'CONFIG_UEFI_VARS',
                 if_true: files('var-service-core.c',
                                'var-service-json.c',
                                'var-service-vars.c',
                                'var-service-auth.c',
                                'var-service-guid.c',
                                'var-service-policy.c'))

modules += { 'hw-uefi' : {
    'vars'     : uefi_vars_ss,
}}
