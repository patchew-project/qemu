/****************************************************************************/
/*                           the diy toolsuite                              */
/*                                                                          */
/* Jade Alglave, University College London, UK.                             */
/* Luc Maranget, INRIA Paris-Rocquencourt, France.                          */
/*                                                                          */
/* This C source is a product of litmus7 and includes source that is        */
/* governed by the CeCILL-B license.                                        */
/****************************************************************************/
/* Parameters */
#define SIZE_OF_TEST 100000
#define NUMBER_OF_RUN 10
#define AVAIL 0
#define STRIDE 1
#define MAX_LOOP 0
#define N 4
#define AFF_INCR (0)
/* Includes */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <time.h>
#include <limits.h>
#include "utils.h"
#include "outs.h"
#include "affinity.h"

/* params */
typedef struct {
  int verbose;
  int size_of_test,max_run;
  int stride;
  aff_mode_t aff_mode;
  int ncpus, ncpus_used;
  int do_change;
} param_t;


/* Full memory barrier */
inline static void mbar(void) {
  asm __volatile__ ("dsb sy" ::: "memory");
}

/* Barriers macros */
inline static void barrier_wait(unsigned int id, unsigned int k, int volatile *b) {
  if ((k % N) == id) {
    *b = 1 ;
  } else {
    while (*b == 0) ;
  }
}

/**********************/
/* Context definition */
/**********************/


typedef struct {
/* Shared variables */
  int *y;
  int *x;
/* Final content of observed  registers */
  int *out_1_x0;
  int *out_1_x2;
  int *out_3_x0;
  int *out_3_x2;
/* Check data */
  pb_t *fst_barrier;
/* Barrier for litmus loop */
  int volatile *barrier;
/* Instance seed */
  st_t seed;
/* Parameters */
  param_t *_p;
} ctx_t;

inline static int final_cond(int _out_1_x0,int _out_1_x2,int _out_3_x0,int _out_3_x2) {
  switch (_out_1_x0) {
  case 1:
    switch (_out_1_x2) {
    case 0:
      switch (_out_3_x0) {
      case 1:
        switch (_out_3_x2) {
        case 0:
          return 1;
        default:
          return 0;
        }
      default:
        return 0;
      }
    default:
      return 0;
    }
  default:
    return 0;
  }
}

inline static int final_ok(int cond) {
  return cond;
}

/**********************/
/* Outcome collection */
/**********************/
#define NOUTS 4
typedef intmax_t outcome_t[NOUTS];

static const int out_1_x0_f = 0 ;
static const int out_1_x2_f = 1 ;
static const int out_3_x0_f = 2 ;
static const int out_3_x2_f = 3 ;


typedef struct hist_t {
  outs_t *outcomes ;
  count_t n_pos,n_neg ;
} hist_t ;

static hist_t *alloc_hist(void) {
  hist_t *p = malloc_check(sizeof(*p)) ;
  p->outcomes = NULL ;
  p->n_pos = p->n_neg = 0 ;
  return p ;
}

static void free_hist(hist_t *h) {
  free_outs(h->outcomes) ;
  free(h) ;
}

static void add_outcome(hist_t *h, count_t v, outcome_t o, int show) {
  h->outcomes = add_outcome_outs(h->outcomes,o,NOUTS,v,show) ;
}

static void merge_hists(hist_t *h0, hist_t *h1) {
  h0->n_pos += h1->n_pos ;
  h0->n_neg += h1->n_neg ;
  h0->outcomes = merge_outs(h0->outcomes,h1->outcomes,NOUTS) ;
}

static count_t sum_hist(hist_t *h) {
  return sum_outs(h->outcomes) ;
}


static void do_dump_outcome(FILE *fhist, intmax_t *o, count_t c, int show) {
  fprintf(fhist,"%-6"PCTR"%c>1:X0=%i; 1:X2=%i; 3:X0=%i; 3:X2=%i;\n",c,show ? '*' : ':',(int)o[out_1_x0_f],(int)o[out_1_x2_f],(int)o[out_3_x0_f],(int)o[out_3_x2_f]);
}

static void just_dump_outcomes(FILE *fhist, hist_t *h) {
  outcome_t buff ;
  dump_outs(fhist,do_dump_outcome,h->outcomes,buff,NOUTS) ;
}

/*******************************************************/
/* Context allocation, freeing and reinitialization    */
/*******************************************************/

static void init(ctx_t *_a) {
  int size_of_test = _a->_p->size_of_test;

  _a->seed = rand();
  _a->out_1_x0 = malloc_check(size_of_test*sizeof(*(_a->out_1_x0)));
  _a->out_1_x2 = malloc_check(size_of_test*sizeof(*(_a->out_1_x2)));
  _a->out_3_x0 = malloc_check(size_of_test*sizeof(*(_a->out_3_x0)));
  _a->out_3_x2 = malloc_check(size_of_test*sizeof(*(_a->out_3_x2)));
  _a->y = malloc_check(size_of_test*sizeof(*(_a->y)));
  _a->x = malloc_check(size_of_test*sizeof(*(_a->x)));
  _a->fst_barrier = pb_create(N);
  _a->barrier = malloc_check(size_of_test*sizeof(*(_a->barrier)));
}

static void finalize(ctx_t *_a) {
  free((void *)_a->y);
  free((void *)_a->x);
  free((void *)_a->out_1_x0);
  free((void *)_a->out_1_x2);
  free((void *)_a->out_3_x0);
  free((void *)_a->out_3_x2);
  pb_free(_a->fst_barrier);
  free((void *)_a->barrier);
}

static void reinit(ctx_t *_a) {
  for (int _i = _a->_p->size_of_test-1 ; _i >= 0 ; _i--) {
    _a->y[_i] = 0;
    _a->x[_i] = 0;
    _a->out_1_x0[_i] = -239487;
    _a->out_1_x2[_i] = -239487;
    _a->out_3_x0[_i] = -239487;
    _a->out_3_x2[_i] = -239487;
    _a->barrier[_i] = 0;
  }
}

/**************************************/
/* Prefetch (and check) global values */
/**************************************/

static void check_globals(ctx_t *_a) {
  int *y = _a->y;
  int *x = _a->x;
  for (int _i = _a->_p->size_of_test-1 ; _i >= 0 ; _i--) {
    if (rand_bit(&(_a->seed)) && y[_i] != 0) fatal("ARMARM06+AP+AP, check_globals failed");
    if (rand_bit(&(_a->seed)) && x[_i] != 0) fatal("ARMARM06+AP+AP, check_globals failed");
  }
  pb_wait(_a->fst_barrier);
}

/***************/
/* Litmus code */
/***************/

typedef struct {
  int th_id; /* I am running on this thread */
  int *cpu; /* On this cpu */
  ctx_t *_a;   /* In this context */
} parg_t;





static void *P0(void *_vb) {
  mbar();
  parg_t *_b = (parg_t *)_vb;
  ctx_t *_a = _b->_a;
  int _ecpu = _b->cpu[_b->th_id];
  force_one_affinity(_ecpu,AVAIL,_a->_p->verbose,"ARMARM06+AP+AP");
  check_globals(_a);
  int _th_id = _b->th_id;
  int volatile *barrier = _a->barrier;
  int _size_of_test = _a->_p->size_of_test;
  int _stride = _a->_p->stride;
  for (int _j = _stride ; _j > 0 ; _j--) {
    for (int _i = _size_of_test-_j ; _i >= 0 ; _i -= _stride) {
      barrier_wait(_th_id,_i,&barrier[_i]);
      int trashed_x0;
asm __volatile__ (
"\n"
"#START _litmus_P0\n"
"#_litmus_P0_0\n\t"
"mov %w[x0],#1\n"
"#_litmus_P0_1\n\t"
"stlr %w[x0],[%[x1]]\n"
"#END _litmus_P0\n\t"
:[x0] "=&r" (trashed_x0)
:[x1] "r" (&_a->x[_i])
:"cc","memory"
);
    }
  }
  mbar();
  return NULL;
}

static void *P1(void *_vb) {
  mbar();
  parg_t *_b = (parg_t *)_vb;
  ctx_t *_a = _b->_a;
  int _ecpu = _b->cpu[_b->th_id];
  force_one_affinity(_ecpu,AVAIL,_a->_p->verbose,"ARMARM06+AP+AP");
  check_globals(_a);
  int _th_id = _b->th_id;
  int volatile *barrier = _a->barrier;
  int _size_of_test = _a->_p->size_of_test;
  int _stride = _a->_p->stride;
  int *out_1_x0 = _a->out_1_x0;
  int *out_1_x2 = _a->out_1_x2;
  for (int _j = _stride ; _j > 0 ; _j--) {
    for (int _i = _size_of_test-_j ; _i >= 0 ; _i -= _stride) {
      barrier_wait(_th_id,_i,&barrier[_i]);
asm __volatile__ (
"\n"
"#START _litmus_P1\n"
"#_litmus_P1_0\n\t"
"ldar %w[x0],[%[x1]]\n"
"#_litmus_P1_1\n\t"
"ldr %w[x2],[%[x3]]\n"
"#END _litmus_P1\n\t"
:[x2] "=&r" (out_1_x2[_i]),[x0] "=&r" (out_1_x0[_i])
:[x1] "r" (&_a->x[_i]),[x3] "r" (&_a->y[_i])
:"cc","memory"
);
    }
  }
  mbar();
  return NULL;
}

static void *P2(void *_vb) {
  mbar();
  parg_t *_b = (parg_t *)_vb;
  ctx_t *_a = _b->_a;
  int _ecpu = _b->cpu[_b->th_id];
  force_one_affinity(_ecpu,AVAIL,_a->_p->verbose,"ARMARM06+AP+AP");
  check_globals(_a);
  int _th_id = _b->th_id;
  int volatile *barrier = _a->barrier;
  int _size_of_test = _a->_p->size_of_test;
  int _stride = _a->_p->stride;
  for (int _j = _stride ; _j > 0 ; _j--) {
    for (int _i = _size_of_test-_j ; _i >= 0 ; _i -= _stride) {
      barrier_wait(_th_id,_i,&barrier[_i]);
      int trashed_x0;
asm __volatile__ (
"\n"
"#START _litmus_P2\n"
"#_litmus_P2_0\n\t"
"mov %w[x0],#1\n"
"#_litmus_P2_1\n\t"
"stlr %w[x0],[%[x1]]\n"
"#END _litmus_P2\n\t"
:[x0] "=&r" (trashed_x0)
:[x1] "r" (&_a->y[_i])
:"cc","memory"
);
    }
  }
  mbar();
  return NULL;
}

static void *P3(void *_vb) {
  mbar();
  parg_t *_b = (parg_t *)_vb;
  ctx_t *_a = _b->_a;
  int _ecpu = _b->cpu[_b->th_id];
  force_one_affinity(_ecpu,AVAIL,_a->_p->verbose,"ARMARM06+AP+AP");
  check_globals(_a);
  int _th_id = _b->th_id;
  int volatile *barrier = _a->barrier;
  int _size_of_test = _a->_p->size_of_test;
  int _stride = _a->_p->stride;
  int *out_3_x0 = _a->out_3_x0;
  int *out_3_x2 = _a->out_3_x2;
  for (int _j = _stride ; _j > 0 ; _j--) {
    for (int _i = _size_of_test-_j ; _i >= 0 ; _i -= _stride) {
      barrier_wait(_th_id,_i,&barrier[_i]);
asm __volatile__ (
"\n"
"#START _litmus_P3\n"
"#_litmus_P3_0\n\t"
"ldar %w[x0],[%[x1]]\n"
"#_litmus_P3_1\n\t"
"ldr %w[x2],[%[x3]]\n"
"#END _litmus_P3\n\t"
:[x2] "=&r" (out_3_x2[_i]),[x0] "=&r" (out_3_x0[_i])
:[x1] "r" (&_a->y[_i]),[x3] "r" (&_a->x[_i])
:"cc","memory"
);
    }
  }
  mbar();
  return NULL;
}

typedef struct {
  pm_t *p_mutex;
  pb_t *p_barrier;
  param_t *_p;
  int z_id;
  int *cpus;
} zyva_t;

#define NT N

static void *zyva(void *_va) {
  zyva_t *_a = (zyva_t *) _va;
  param_t *_b = _a->_p;
  pb_wait(_a->p_barrier);
  pthread_t thread[NT];
  parg_t parg[N];
  f_t *fun[] = {&P0,&P1,&P2,&P3};
  hist_t *hist = alloc_hist();
  ctx_t ctx;
  ctx._p = _b;

  init(&ctx);
  for (int _p = N-1 ; _p >= 0 ; _p--) {
    parg[_p].th_id = _p; parg[_p]._a = &ctx;
    parg[_p].cpu = &(_a->cpus[0]);
  }

  for (int n_run = 0 ; n_run < _b->max_run ; n_run++) {
    if (_b->aff_mode == aff_random) {
      pb_wait(_a->p_barrier);
      if (_a->z_id == 0) perm_prefix_ints(&ctx.seed,_a->cpus,_b->ncpus_used,_b->ncpus);
      pb_wait(_a->p_barrier);
    } else {
    }
    if (_b->verbose>1) fprintf(stderr,"Run %i of %i\r", n_run, _b->max_run);
    reinit(&ctx);
    if (_b->do_change) perm_funs(&ctx.seed,fun,N);
    for (int _p = NT-1 ; _p >= 0 ; _p--) {
      launch(&thread[_p],fun[_p],&parg[_p]);
    }
    if (_b->do_change) perm_threads(&ctx.seed,thread,NT);
    for (int _p = NT-1 ; _p >= 0 ; _p--) {
      join(&thread[_p]);
    }
    /* Log final states */
    for (int _i = _b->size_of_test-1 ; _i >= 0 ; _i--) {
      int _out_1_x0_i = ctx.out_1_x0[_i];
      int _out_1_x2_i = ctx.out_1_x2[_i];
      int _out_3_x0_i = ctx.out_3_x0[_i];
      int _out_3_x2_i = ctx.out_3_x2[_i];
      outcome_t o;
      int cond;

      cond = final_ok(final_cond(_out_1_x0_i,_out_1_x2_i,_out_3_x0_i,_out_3_x2_i));
      o[out_1_x0_f] = _out_1_x0_i;
      o[out_1_x2_f] = _out_1_x2_i;
      o[out_3_x0_f] = _out_3_x0_i;
      o[out_3_x2_f] = _out_3_x2_i;
      add_outcome(hist,1,o,cond);
      if (cond) { hist->n_pos++; } else { hist->n_neg++; }
    }
  }

  finalize(&ctx);
  return hist;
}

#define ENOUGH 10

static int postlude(FILE *out,cmd_t *cmd,hist_t *hist,count_t p_true,count_t p_false,tsc_t total) {
  fprintf(out,"Test ARMARM06+AP+AP Forbidden\n");
  fprintf(out,"Histogram (%i states)\n",finals_outs(hist->outcomes));
  just_dump_outcomes(out,hist);
  int cond = p_true == 0;
  fprintf(out,"%s\n",cond?"Ok":"No");
  fprintf(out,"\nWitnesses\n");
  fprintf(out,"Positive: %" PCTR ", Negative: %" PCTR "\n",p_false,p_true);
  fprintf(out,"Condition ~exists (1:X0=1 /\\ 1:X2=0 /\\ 3:X0=1 /\\ 3:X2=0) is %svalidated\n",cond ? "" : "NOT ");
  fprintf(out,"Hash=73c88d83e9bc423599f9750ed7d77ac2\n");
  fprintf(out,"Com=Rf Fr Rf Fr\n");
  fprintf(out,"Orig=RfeLA PodRRAP FrePL RfeLA PodRRAP FrePL\n");
  count_t cond_true = p_true;
  count_t cond_false = p_false;
  fprintf(out,"Observation ARMARM06+AP+AP %s %" PCTR " %" PCTR "\n",!cond_true ? "Never" : !cond_false ? "Always" : "Sometimes",cond_true,cond_false);
  if (p_true > 0) {
  }
  fprintf(out,"Time ARMARM06+AP+AP %.2f\n",total / 1000000.0);
  fflush(out);
  return cond;
}

static int run(cmd_t *cmd,cpus_t *def_all_cpus,FILE *out) {
  tsc_t start = timeofday();
  param_t prm ;
/* Set some parameters */
  prm.verbose = cmd->verbose;
  prm.size_of_test = cmd->size_of_test;
  prm.max_run = cmd->max_run;
  prm.stride = cmd->stride;
  prm.do_change = 1;
  if (cmd->fix) prm.do_change = 0;
/* Computes number of test concurrent instances */
  int n_avail = cmd->avail > 0 ? cmd->avail : cmd->aff_cpus->sz;
  if (n_avail >  cmd->aff_cpus->sz) log_error("Warning: avail=%i, available=%i\n",n_avail, cmd->aff_cpus->sz);
  int n_exe;
  if (cmd->n_exe > 0) {
    n_exe = cmd->n_exe;
  } else {
    n_exe = n_avail < N ? 1 : n_avail / N;
  }
/* Set affinity parameters */
  cpus_t *all_cpus = cmd->aff_cpus;
  int aff_cpus_sz = cmd->aff_mode == aff_random ? max(all_cpus->sz,N*n_exe) : N*n_exe;
  int aff_cpus[aff_cpus_sz];
  prm.aff_mode = cmd->aff_mode;
  prm.ncpus = aff_cpus_sz;
  prm.ncpus_used = N*n_exe;
/* Show parameters to user */
  if (prm.verbose) {
    log_error( "ARMARM06+AP+AP: n=%i, r=%i, s=%i",n_exe,prm.max_run,prm.size_of_test);
    log_error(", st=%i",prm.stride);
    if (cmd->aff_mode == aff_incr) {
      log_error( ", i=%i",cmd->aff_incr);
    } else if (cmd->aff_mode == aff_random) {
      log_error(", +ra");
    } else if (cmd->aff_mode == aff_custom) {
      log_error(", +ca");
    } else if (cmd->aff_mode == aff_scan) {
      log_error(", +sa");
    }
    log_error(", p='");
    cpus_dump(stderr,cmd->aff_cpus);
    log_error("'");
    log_error("\n");
  }
  if (cmd->aff_mode == aff_random) {
    for (int k = 0 ; k < aff_cpus_sz ; k++) {
      aff_cpus[k] = all_cpus->cpu[k % all_cpus->sz];
    }
  }
  hist_t *hist = NULL;
  int n_th = n_exe-1;
  pthread_t th[n_th];
  zyva_t zarg[n_exe];
  pm_t *p_mutex = pm_create();
  pb_t *p_barrier = pb_create(n_exe);
  int next_cpu = 0;
  int delta = cmd->aff_incr;
  if (delta <= 0) {
    for (int k=0 ; k < all_cpus->sz ; k++) all_cpus->cpu[k] = -1;
    delta = 1;
  } else {
    delta %= all_cpus->sz;
  }
  int start_scan=0, max_start=gcd(delta,all_cpus->sz);
  int *aff_p = aff_cpus;
  for (int k=0 ; k < n_exe ; k++) {
    zyva_t *p = &zarg[k];
    p->_p = &prm;
    p->p_mutex = p_mutex; p->p_barrier = p_barrier; 
    p->z_id = k;
    p->cpus = aff_p;
    if (cmd->aff_mode != aff_incr) {
      aff_p += N;
    } else {
      for (int i=0 ; i < N ; i++) {
        *aff_p = all_cpus->cpu[next_cpu]; aff_p++;
        next_cpu += delta; next_cpu %= all_cpus->sz;
        if (next_cpu == start_scan) {
          start_scan++ ; start_scan %= max_start;
          next_cpu = start_scan;
        }
      }
    }
    if (k < n_th) {
      launch(&th[k],zyva,p);
    } else {
      hist = (hist_t *)zyva(p);
    }
  }

  count_t n_outs = prm.size_of_test; n_outs *= prm.max_run;
  for (int k=0 ; k < n_th ; k++) {
    hist_t *hk = (hist_t *)join(&th[k]);
    if (sum_hist(hk) != n_outs || hk->n_pos + hk->n_neg != n_outs) {
      fatal("ARMARM06+AP+AP, sum_hist");
    }
    merge_hists(hist,hk);
    free_hist(hk);
  }
  cpus_free(all_cpus);
  tsc_t total = timeofday() - start;
  pm_free(p_mutex);
  pb_free(p_barrier);

  n_outs *= n_exe ;
  if (sum_hist(hist) != n_outs || hist->n_pos + hist->n_neg != n_outs) {
    fatal("ARMARM06+AP+AP, sum_hist") ;
  }
  count_t p_true = hist->n_pos, p_false = hist->n_neg;
  int cond = postlude(out,cmd,hist,p_true,p_false,total);
  free_hist(hist);
  return cond;
}


int main(int argc, char **argv) {
  cpus_t *def_all_cpus = read_force_affinity(AVAIL,0);
  if (def_all_cpus->sz < N) {
    cpus_free(def_all_cpus);
    return EXIT_SUCCESS;
  }
  cmd_t def = { 0, NUMBER_OF_RUN, SIZE_OF_TEST, STRIDE, AVAIL, 0, 0, aff_incr, 0, 0, AFF_INCR, def_all_cpus, NULL, -1, MAX_LOOP, NULL, NULL, -1, -1, -1, 0, 0};
  cmd_t cmd = def;
  parse_cmd(argc,argv,&def,&cmd);
  int cond = run(&cmd,def_all_cpus,stdout);
  if (def_all_cpus != cmd.aff_cpus) cpus_free(def_all_cpus);
  return cond ? EXIT_SUCCESS : EXIT_FAILURE;
}
