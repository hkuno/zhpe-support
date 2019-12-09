#include <inttypes.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#include <x86intrin.h>

#define atm_add(_p, _v)                                         \
    atomic_fetch_add_explicit(_p, _v, memory_order_acq_rel)

#define atm_inc(_p)     atm_add(_p, 1)

int                     b;


static uint64_t tsc_cycles(void)
{
    uint32_t lo;
    uint32_t hi;
    uint32_t cpu;

    asm volatile("lfence;rdtscp;lfence" : "=a" (lo), "=d" (hi) : :);

    return ((uint64_t)hi << 32 | lo);
}

static uint64_t tscp_cycles(void)
{
    uint32_t lo;
    uint32_t hi;
    uint32_t cpu;

    asm volatile("rdtscp" : "=a" (lo), "=d" (hi) : : "ecx");

    return ((uint64_t)hi << 32 | lo);
}

static void foo(uint64_t (*cycles)(void))
{
    uint64_t            v1;
    uint64_t            v2;
    uint64_t            v3;
    uint64_t            v4;
    uint64_t            v5;
    uint64_t            v6;
    uint64_t            v7;

    cycles();
    v1 = cycles();
    v2 = cycles();
    _mm_sfence();
    v3 = cycles();
    atm_inc(&b);
    v4 = cycles();
    atm_inc(&b);
    v5 = cycles();
    atm_inc(&b);
    atm_inc(&b);
    atm_inc(&b);
    atm_inc(&b);
    atm_inc(&b);
    atm_inc(&b);
    atm_inc(&b);
    atm_inc(&b);
    atm_inc(&b);
    atm_inc(&b);
    v6 = cycles();
    atm_inc(&b);
    atm_inc(&b);
    atm_inc(&b);
    atm_inc(&b);
    atm_inc(&b);
    atm_inc(&b);
    atm_inc(&b);
    atm_inc(&b);
    atm_inc(&b);
    atm_inc(&b);
    _mm_sfence();
    v7 = cycles();

    printf("v2 - v1 = %" PRIu64 "\n", v2 - v1);
    printf("v3 - v2 = %" PRIu64 "\n", v3 - v2);
    printf("v4 - v3 = %" PRIu64 "\n", v4 - v3);
    printf("v5 - v4 = %" PRIu64 "\n", v5 - v4);
    printf("v6 - v5 = %" PRIu64 "\n", v6 - v5);
    printf("v7 - v6 = %" PRIu64 "\n", v7 - v6);
}

int main(void)
{
    foo(tsc_cycles);
    foo(tscp_cycles);

    return 0;
}
