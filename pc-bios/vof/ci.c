#include "vof.h"

struct prom_args {
        uint32_t service;
        uint32_t nargs;
        uint32_t nret;
        uint32_t args[10];
};

#define ADDR(x) (uint32_t)(x)

extern uint32_t ci_entry(uint32_t params);

extern unsigned long hv_rtas(unsigned long params);
extern unsigned int hv_rtas_size;

bool prom_handle(struct prom_args *pargs)
{
	void *rtasbase;
	uint32_t rtassize = 0;
	phandle rtas;

	if (strcmp("call-method", (void *)(unsigned long) pargs->service))
		return false;

	if (strcmp("instantiate-rtas", (void *)(unsigned long) pargs->args[0]))
		return false;

	rtas = ci_finddevice("/rtas");
	ci_getprop(rtas, "rtas-size", &rtassize, sizeof(rtassize));
	if (rtassize < hv_rtas_size)
		return false;

	rtasbase = (void *)(unsigned long) pargs->args[2];

	memcpy(rtasbase, hv_rtas, hv_rtas_size);
	pargs->args[pargs->nargs] = 0;
	pargs->args[pargs->nargs + 1] = pargs->args[2];

	return true;
}

void prom_entry(uint32_t args)
{
	if (!prom_handle((void *)(unsigned long) args))
		ci_entry(args);
}

int call_prom(const char *service, int nargs, int nret, ...)
{
        int i;
        struct prom_args args;
        va_list list;

        args.service = ADDR(service);
        args.nargs = nargs;
        args.nret = nret;

        va_start(list, nret);
        for (i = 0; i < nargs; i++)
                args.args[i] = va_arg(list, prom_arg_t);
        va_end(list);

        for (i = 0; i < nret; i++)
                args.args[nargs+i] = 0;

        if (ci_entry((uint32_t)(&args)) < 0)
                return PROM_ERROR;

        return (nret > 0) ? args.args[nargs] : 0;
}

void ci_panic(const char *str)
{
	call_prom("exit", 0, 0);
}

phandle ci_finddevice(const char *path)
{
	return call_prom("finddevice", 1, 1, path);
}

uint32_t ci_getprop(phandle ph, const char *propname, void *prop, int len)
{
	return call_prom("getprop", 4, 1, ph, propname, prop, len);
}

ihandle ci_open(const char *path)
{
	return call_prom("open", 1, 1, path);
}

void ci_close(ihandle ih)
{
	call_prom("close", 1, 0, ih);
}

void *ci_claim(void *virt, uint32_t size, uint32_t align)
{
	uint32_t ret = call_prom("claim", 3, 1, ADDR(virt), size, align);

	return (void *) (unsigned long) ret;
}

uint32_t ci_release(void *virt, uint32_t size)
{
	return call_prom("release", 2, 1, ADDR(virt), size);
}
