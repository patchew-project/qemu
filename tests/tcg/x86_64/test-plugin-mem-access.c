#include <emmintrin.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

static void *data;

#define DEFINE_STORE(name, type, value) \
static void store_##name(void)          \
{                                       \
    *((type *)data) = value;            \
}

#define DEFINE_ATOMIC_OP(name, type, value)                 \
static void atomic_op_##name(void)                          \
{                                                           \
    *((type *)data) = 0x42;                                 \
    __sync_val_compare_and_swap((type *)data, 0x42, value); \
}

#define DEFINE_LOAD(name, type)                         \
static void load_##name(void)                           \
{                                                       \
    register type var asm("eax") = *((type *) data);    \
    (void)var;                                          \
}

DEFINE_STORE(u8, uint8_t, 0xf1)
DEFINE_ATOMIC_OP(u8, uint8_t, 0xf1)
DEFINE_LOAD(u8, uint8_t)
DEFINE_STORE(u16, uint16_t, 0xf123)
DEFINE_ATOMIC_OP(u16, uint16_t, 0xf123)
DEFINE_LOAD(u16, uint16_t)
DEFINE_STORE(u32, uint32_t, 0xff112233)
DEFINE_ATOMIC_OP(u32, uint32_t, 0xff112233)
DEFINE_LOAD(u32, uint32_t)
DEFINE_STORE(u64, uint64_t, 0xf123456789abcdef)
DEFINE_ATOMIC_OP(u64, uint64_t, 0xf123456789abcdef)
DEFINE_LOAD(u64, uint64_t)

static void store_u128(void)
{
    _mm_store_si128(data, _mm_set_epi32(0xf1223344, 0x55667788,
                                        0xf1234567, 0x89abcdef));
}

static void load_u128(void)
{
    __m128i var = _mm_load_si128(data);
    (void)var;
}

static void *f(void *p)
{
    return NULL;
}

int main(void)
{
    /*
     * We force creation of a second thread to enable cpu flag CF_PARALLEL.
     * This will generate atomic operations when needed.
     */
    pthread_t thread;
    pthread_create(&thread, NULL, &f, NULL);
    pthread_join(thread, NULL);

    data = malloc(sizeof(__m128i));
    atomic_op_u8();
    store_u8();
    load_u8();

    atomic_op_u16();
    store_u16();
    load_u16();

    atomic_op_u32();
    store_u32();
    load_u32();

    atomic_op_u64();
    store_u64();
    load_u64();

    store_u128();
    load_u128();

    free(data);
}
