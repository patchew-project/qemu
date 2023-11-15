uefi_vars_ss = ss.source_set()
uefi_vars_ss.add(when: 'CONFIG_UEFI_VARS',
                 if_true: files('var-service-core.c',
                                'var-service-json.c',
                                'var-service-vars.c',
                                'var-service-auth.c',
                                'var-service-guid.c',
                                'var-service-policy.c',
                                'var-service-sysbus.c'))

uefi_vars_isa_ss = ss.source_set()
uefi_vars_isa_ss.add(when: 'CONFIG_UEFI_VARS_ISA',
                     if_true: files('var-service-isa.c'))

modules += { 'hw-uefi' : {
    'vars'     : uefi_vars_ss,
    'vars-isa' : uefi_vars_isa_ss,
}}
