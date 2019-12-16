/*
 * Copyright (C) 2019 Hewlett Packard Enterprise Development LP.
 * All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <zhpeq_util.h>

#include <zhpe_stats_types.h>

#include <sys/syscall.h>

#include <asm/bitsperlong.h>

#include <linux/perf_event.h>
#include <sys/prctl.h>

#include <math.h>

#define WHERETIMEGO 0

static_assert(sizeof(struct zhpe_stats_record)%64 == 0, "foo");


#define rdpmc(counter,low, high) \
     __asm__ __volatile__("rdpmc" \
        : "=a" (low), "=d" (high) \
        : "c" (counter))

/* copied from perf_event_open man page */
static long perf_event_open(struct perf_event_attr *pea, pid_t pid,
               int cpu, int group_fd, unsigned long flags)
{
    int ret;

    ret = syscall(__NR_perf_event_open, pea, pid, cpu,
                  group_fd, flags);
    return ret;
}

/* John Byrne's asm magic */
#define __XMMCLOBBER03  : "%xmm0", "%xmm1", "%xmm2", "%xmm3"
#define __XMMCLOBBERA   __XMMCLOBBER03, "%xmm4", "%xmm5", "%xmm6", "%xmm7"

#define __XMM_XFER_ALIGN        (16)
#define __XMM_CACHE_SIZE        (64)
#define __XMM_XFER_LOOP         (128)

#define __vmemcpy(_d, _s, _to, _from, _len)                             \
do {                                                                    \
    for (; _len >= __XMM_XFER_LOOP; _len -= __XMM_XFER_LOOP) {          \
        asm volatile (                                                  \
            #_s "    (%0),  %%xmm0\n"                                   \
            #_s "  16(%0),  %%xmm1\n"                                   \
            #_s "  32(%0),  %%xmm2\n"                                   \
            #_s "  48(%0),  %%xmm3\n"                                   \
            #_s "  64(%0),  %%xmm4\n"                                   \
            #_s "  80(%0),  %%xmm5\n"                                   \
            #_s "  96(%0),  %%xmm6\n"                                   \
            #_s " 112(%0),  %%xmm7\n"                                   \
            #_d "  %%xmm0,    (%1)\n"                                   \
            #_d "  %%xmm1,  16(%1)\n"                                   \
            #_d "  %%xmm2,  32(%1)\n"                                   \
            #_d "  %%xmm3,  48(%1)\n"                                   \
            #_d "  %%xmm4,  64(%1)\n"                                   \
            #_d "  %%xmm5,  80(%1)\n"                                   \
            #_d "  %%xmm6,  96(%1)\n"                                   \
            #_d "  %%xmm7, 112(%1)\n"                                   \
            : : "r" (_from), "r" (_to) __XMMCLOBBERA);                  \
        _from += __XMM_XFER_LOOP;                                       \
        _to += __XMM_XFER_LOOP;                                         \
    }                                                                   \
    for (; _len >= __XMM_CACHE_SIZE; _len -= __XMM_CACHE_SIZE) {        \
        asm volatile (                                                  \
            #_s "    (%0),  %%xmm0\n"                                   \
            #_s "  16(%0),  %%xmm1\n"                                   \
            #_s "  32(%0),  %%xmm2\n"                                   \
            #_s "  48(%0),  %%xmm3\n"                                   \
            #_d "  %%xmm0,    (%1)\n"                                   \
            #_d "  %%xmm1,  16(%1)\n"                                   \
            #_d "  %%xmm2,  32(%1)\n"                                   \
            #_d "  %%xmm3,  48(%1)\n"                                   \
            : : "r" (_from), "r" (_to) __XMMCLOBBER03);                 \
        _from += __XMM_CACHE_SIZE;                                      \
        _to += __XMM_CACHE_SIZE;                                        \
    }                                                                   \
    if (_len)                                                           \
         memcpy(_to, _from, _len);                                      \
} while(0)                                                              \

struct zhpe_stats {
    int                         fd;
    uint16_t                    uid;
    uint32_t                    num_slots;
    size_t                      head;
    struct zhpe_stats_record    *buffer;
    uint8_t                     state:4;
    uint8_t                     pause_all:1;
    uint8_t                     enabled:1;
};

enum {
    ZHPE_STATS_STOPPED,
    ZHPE_STATS_RUNNING,
    ZHPE_STATS_PAUSED,
};

static struct zhpe_stats *stats_nop_null(void)
{
    return NULL;
}

static struct zhpe_stats *stats_nop_open(uint16_t dum)
{
    return NULL;
}

static void stats_nop_stamp(struct zhpe_stats *stats, uint32_t dum,
                            uint32_t dum2, uint64_t *dum3)
{
}

static void stats_nop_setvals(struct zhpe_stats_record *rec)
{
}

static void stats_nop_stats(struct zhpe_stats *stats)
{
}

static void stats_nop_saveme(char *dest, char *src)
{
}

static void stats_nop_stats_uint32(struct zhpe_stats *stats, uint32_t dum)
{
}

static void stats_nop_void(void)
{
};

static void stats_nop_voidp(void *dum)
{
};

static struct zhpe_stats_ops zhpe_stats_nops = {
    .open               = stats_nop_open,
    .close              = stats_nop_void,
    .enable             = stats_nop_void,
    .disable            = stats_nop_void,
    .get_zhpe_stats     = stats_nop_null,
    .stop_all           = stats_nop_stats,
    .pause_all          = stats_nop_stats,
    .restart_all        = stats_nop_void,
    .start              = stats_nop_stats_uint32,
    .stop               = stats_nop_stats_uint32,
    .pause              = stats_nop_stats_uint32,
    .finalize           = stats_nop_void,
    .key_destructor     = stats_nop_voidp,
    .stamp              = stats_nop_stamp,
    .setvals            = stats_nop_setvals,
    .saveme             = stats_nop_saveme,
};

struct zhpe_stats_ops *zhpe_stats_ops = &zhpe_stats_nops;

#ifdef HAVE_ZHPE_STATS

#include <zhpe_stats.h>
#define ZHPE_STATS_BUF_COUNT_MAX 1048576

#define CALIBRATE_ITERATIONS 1000

/* Common definitions/code */

static char             *zhpe_stats_dir;
static char             *zhpe_stats_unique;
static pthread_key_t    zhpe_stats_key;
static pthread_mutex_t  zhpe_stats_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool             zhpe_stats_init_once;
static uint64_t         zhpe_stats_buf_count;
static uint64_t         zhpe_stats_buf_mask;
static size_t           zhpe_stats_profile;

int hw_instr_fd;
uint64_t hw_instr_counter;

int cpu_cyc_fd;
uint64_t cpu_cyc_counter;

/* forward declarations */
void zhpe_stats_flush(struct zhpe_stats *stats);

static void stats_cmn_finalize(void)
{
    free(zhpe_stats_dir);
    zhpe_stats_dir = NULL;
    free(zhpe_stats_unique);
    zhpe_stats_unique = NULL;
    zhpe_stats_ops = &zhpe_stats_nops;
}

static struct zhpe_stats *stats_open(uint16_t uid)
{
    char                *fname = NULL;
    struct zhpe_stats   *stats;
    int res, bufsize;

    struct zhpe_stats_metadata metadata;
    metadata.profileid = zhpe_stats_profile;

    stats = calloc(1, sizeof(*stats));
    if (!stats)
        abort();

    stats->uid = uid;
    stats->num_slots = zhpe_stats_buf_count;
    stats->head = 0;
    stats->buffer = calloc_cachealigned(stats->num_slots, sizeof(struct zhpe_stats_record));

    stats->fd = -1;
    if (zhpeu_asprintf(&fname, "%s/%s.%ld.%d",
                       zhpe_stats_dir, zhpe_stats_unique,
                       syscall(SYS_gettid), uid) == -1) {
        print_func_err(__func__, __LINE__, "zhpeu_asprintf", "", -ENOMEM);
        abort();
    }

    stats->fd = open(fname, O_RDWR | O_CREAT | O_TRUNC,
                     S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (stats->fd == -1) {
        print_func_err(__func__, __LINE__, "open", fname, -errno);
        abort();
    }

    bufsize=sizeof(struct zhpe_stats_metadata);
    res = write(stats->fd, &metadata, bufsize);
    if (check_func_ion(__func__, __LINE__, "write", bufsize, false,
                       bufsize, res, 0) < 0)
        abort();

    abort_posix(pthread_setspecific, zhpe_stats_key, stats);
    stats->state = ZHPE_STATS_STOPPED;
    stats->enabled = false;

    free(fname);
    return stats;
}

/* overwrite when full */
static struct zhpe_stats_record *stats_simple_nextslot(struct zhpe_stats *stats)
{
    assert(stats);
    assert(stats->buffer);
    struct zhpe_stats_record *rec;

    assert(stats->head < stats->num_slots - 1);

    rec = &(stats->buffer[(zhpe_stats_buf_mask & stats->head++)]);

    return rec;
}

/*
static struct zhpe_stats_record *stats_flushing_nextslot(struct zhpe_stats *stats)
{
    assert(stats);
    assert(stats->buffer);
    struct zhpe_stats_record *dest;

    if (stats->head >= (stats->num_slots - 2))
        zhpe_stats_flush(stats);

    dest = &(stats->buffer[stats->head]);
    stats->head++;

    return dest;
}
*/

static uint64_t do_rdtscp()
{
    uint32_t            lo;
    uint32_t            hi;
    uint32_t            cpu;

    asm volatile("rdtscp\n\t": "=a" (lo), "=d" (hi), "=c" (cpu) : :);

    return ((uint64_t)hi << 32 | lo);
}

static void stats_rdtscp_setvals(struct zhpe_stats_record *rec)
{
    rec->val1 = do_rdtscp();
    rec->val2 = 0;
    rec->val3 = 0;
    rec->val4 = 0;
    rec->val5 = 0;
    rec->val6 = 0;
    rec->pad = 0;
}

static void stats_cpu_setvals(struct zhpe_stats_record *rec)
{
    unsigned int cnt2low, cnt2high;
    unsigned int cnt3low, cnt3high;

    rec->val1 = do_rdtscp();
    rdpmc(cpu_cyc_counter, cnt2low, cnt2high);
    rdpmc(hw_instr_counter, cnt3low, cnt3high);
    rec->val2 = ((long long)cnt2low) | ((long long)cnt2high ) << 32;
    rec->val3 = ((long long)cnt3low) | ((long long)cnt3high ) << 32;
    rec->val5 = 0;
    rec->val6 = 0;
    rec->pad = 0;
}

static inline void stats_vmemcpy_saveme(char * dest, char * src)
{
    uint64_t len =  sizeof(struct zhpe_stats_record);
    __vmemcpy(movntdq, movntdqa, dest, src, len);
}

static inline void stats_memcpy_saveme(char * dest, char * src)
{
    uint64_t len =  sizeof(struct zhpe_stats_record);
    memcpy(dest, src, len);
}

static void rdtscp_stats_recordme(struct zhpe_stats *stats, uint32_t subid, uint32_t opflag)
{
    struct zhpe_stats_record *dest;
    struct zhpe_stats_record tmp;

    tmp.subid = subid;
    tmp.op_flag = opflag;

    stats_rdtscp_setvals(&tmp);
    dest = stats_simple_nextslot(stats);
    stats_memcpy_saveme((char *)dest, (char *)&tmp);
}

static void cpu_stats_recordme(struct zhpe_stats *stats, uint32_t subid, uint32_t opflag)
{
    struct zhpe_stats_record *dest;
    struct zhpe_stats_record tmp;

    tmp.subid = subid;
    tmp.op_flag = opflag;

    stats_cpu_setvals(&tmp);
    dest = stats_simple_nextslot(stats);
    stats_memcpy_saveme((char *)dest, (char *)&tmp);
}

void zhpe_stats_calibrate_cpu_atm_inc(uint32_t opflag, uint32_t subid)
{
    uint64_t v1, v2;
    struct zhpe_stats_record *dest;
    struct zhpe_stats *stats;
    stats = pthread_getspecific(zhpe_stats_key);
    int i;
    unsigned int cntlow_v1, cnthigh_v1;
    unsigned int cntlow_v2, cnthigh_v2;

    uint64_t avg_rdtscp;
    uint64_t avg_cpu_cyc;
    uint64_t avg_hw_instr;

    uint64_t avg_b2b_rdtscp;
    uint64_t avg_b2b_cpu_cyc;
    uint64_t avg_b2b_hw_instr;

    uint64_t rdtscp_total=0;
    uint64_t cyc_total=0;
    uint64_t inst_total=0;

    uint64_t b2b_rdtscp_total=0;
    uint64_t b2b_rdpmc_c_total=0;
    uint64_t b2b_rdpmc_i_total=0;

    int b=0;

    /* rdtscp */
    for (i=0;i<CALIBRATE_ITERATIONS;i++)
    {
        atm_inc(&b);
        do_rdtscp();
        atm_inc(&b);
        atm_inc(&b);
        v1 = do_rdtscp();
        atm_inc(&b);
        v2 = do_rdtscp();
        rdtscp_total += v2 - v1;
        do_rdtscp();
        do_rdtscp();
        v1 = do_rdtscp();
        v2 = do_rdtscp();
        b2b_rdtscp_total += v2 - v1;
    }
    avg_b2b_rdtscp = b2b_rdtscp_total/i;
    avg_rdtscp = rdtscp_total/i;

    /* cpu_cyc_counter */
    for (i=0;i<CALIBRATE_ITERATIONS;i++)
    {
        atm_inc(&b);
        atm_inc(&b);
        rdpmc(cpu_cyc_counter, cntlow_v1, cnthigh_v1);
        atm_inc(&b);
        rdpmc(cpu_cyc_counter, cntlow_v2, cnthigh_v2);
        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        cyc_total += v2 - v1;
        atm_inc(&b);
        atm_inc(&b);
        rdpmc(cpu_cyc_counter, cntlow_v1, cnthigh_v1);
        rdpmc(cpu_cyc_counter, cntlow_v2, cnthigh_v2);
        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        b2b_rdpmc_c_total += v2 - v1;
    }
    avg_b2b_cpu_cyc = b2b_rdpmc_c_total/i;
    avg_cpu_cyc = cyc_total/i;

    /* hw_instr_counter */
    for (i=0;i<CALIBRATE_ITERATIONS;i++)
    {
        atm_inc(&b);
        atm_inc(&b);
        rdpmc(hw_instr_counter, cntlow_v1, cnthigh_v1);
        atm_inc(&b);
        rdpmc(hw_instr_counter, cntlow_v2, cnthigh_v2);
        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        inst_total += v2 - v1;
        atm_inc(&b);
        atm_inc(&b);
        rdpmc(hw_instr_counter, cntlow_v1, cnthigh_v1);
        rdpmc(hw_instr_counter, cntlow_v2, cnthigh_v2);
        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        b2b_rdpmc_i_total += v2 - v1;
    }
    avg_b2b_hw_instr = b2b_rdpmc_i_total/i;
    avg_hw_instr = inst_total/i;

    struct zhpe_stats_record tmp;
    tmp.subid= subid;
    tmp.op_flag = opflag;

    avg_b2b_rdtscp = b2b_rdtscp_total/i;
    avg_rdtscp = rdtscp_total/i;
    avg_hw_instr = inst_total/i;
    avg_cpu_cyc = cyc_total/i;
    tmp.val1 = avg_rdtscp;
    tmp.val2 = avg_cpu_cyc;
    tmp.val3 = avg_hw_instr;
    tmp.val4 = avg_b2b_rdtscp;
    tmp.val5 =  avg_b2b_cpu_cyc;
    tmp.val6 = avg_b2b_hw_instr;

    dest = stats_simple_nextslot(stats);
    stats_memcpy_saveme((char *)dest, (char *)&tmp);

    zhpe_stats_start(subid);
    zhpe_stats_stop(subid);

    zhpe_stats_start(subid);
    atm_inc(&b);
    zhpe_stats_stop(subid);

    zhpe_stats_start(subid);
    atm_inc(&b);
    atm_inc(&b);
    zhpe_stats_stop(subid);

    zhpe_stats_start(subid);
    atm_inc(&b);
    atm_inc(&b);
    atm_inc(&b);
    zhpe_stats_stop(subid);

    zhpe_stats_start(subid);
    atm_inc(&b);
    atm_inc(&b);
    atm_inc(&b);
    atm_inc(&b);
    zhpe_stats_stop(subid);

    zhpe_stats_start(subid);
    atm_inc(&b);
    atm_inc(&b);
    atm_inc(&b);
    atm_inc(&b);
    atm_inc(&b);
    zhpe_stats_stop(subid);
}


void zhpe_stats_calibrate_cpu_nop(uint32_t opflag, uint32_t subid)
{
    uint64_t v1, v2;
    struct zhpe_stats_record *dest;
    struct zhpe_stats *stats;
    stats = pthread_getspecific(zhpe_stats_key);
    int i;
    unsigned int cntlow_v1, cnthigh_v1;
    unsigned int cntlow_v2, cnthigh_v2;

    uint64_t avg_rdtscp;
    uint64_t avg_cpu_cyc;
    uint64_t avg_hw_instr;

    uint64_t avg_b2b_rdtscp;
    uint64_t avg_b2b_cpu_cyc;
    uint64_t avg_b2b_hw_instr;

    uint64_t rdtscp_total=0;
    uint64_t cyc_total=0;
    uint64_t inst_total=0;

    uint64_t b2b_rdtscp_total=0;
    uint64_t b2b_rdpmc_c_total=0;
    uint64_t b2b_rdpmc_i_total=0;

    /* rdtscp */
    for (i=0;i<CALIBRATE_ITERATIONS;i++)
    {
        nop();
        do_rdtscp();
        nop();
        nop();
        v1 = do_rdtscp();
        nop();
        v2 = do_rdtscp();
        rdtscp_total += v2 - v1;
        do_rdtscp();
        do_rdtscp();
        v1 = do_rdtscp();
        v2 = do_rdtscp();
        b2b_rdtscp_total += v2 - v1;
    }
    avg_b2b_rdtscp = b2b_rdtscp_total/i;
    avg_rdtscp = rdtscp_total/i;

    /* cpu_cyc_counter */
    for (i=0;i<CALIBRATE_ITERATIONS;i++)
    {
        nop();
        nop();
        rdpmc(cpu_cyc_counter, cntlow_v1, cnthigh_v1);
        nop();
        rdpmc(cpu_cyc_counter, cntlow_v2, cnthigh_v2);
        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        cyc_total += v2 - v1;
        nop();
        nop();
        rdpmc(cpu_cyc_counter, cntlow_v1, cnthigh_v1);
        rdpmc(cpu_cyc_counter, cntlow_v2, cnthigh_v2);
        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        b2b_rdpmc_c_total += v2 - v1;
    }
    avg_b2b_cpu_cyc = b2b_rdpmc_c_total/i;
    avg_cpu_cyc = cyc_total/i;

    /* hw_instr_counter */
    for (i=0;i<CALIBRATE_ITERATIONS;i++)
    {
        nop();
        nop();
        rdpmc(hw_instr_counter, cntlow_v1, cnthigh_v1);
        nop();
        rdpmc(hw_instr_counter, cntlow_v2, cnthigh_v2);
        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        inst_total += v2 - v1;
        nop();
        nop();
        rdpmc(hw_instr_counter, cntlow_v1, cnthigh_v1);
        rdpmc(hw_instr_counter, cntlow_v2, cnthigh_v2);
        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        b2b_rdpmc_i_total += v2 - v1;
    }
    avg_b2b_hw_instr = b2b_rdpmc_i_total/i;
    avg_hw_instr = inst_total/i;

    struct zhpe_stats_record tmp;
    tmp.subid=subid;
    tmp.op_flag = opflag;

    avg_b2b_rdtscp = b2b_rdtscp_total/i;
    avg_rdtscp = rdtscp_total/i;
    avg_hw_instr = inst_total/i;
    avg_cpu_cyc = cyc_total/i;
    tmp.val1 = avg_rdtscp;
    tmp.val2 = avg_cpu_cyc;
    tmp.val3 = avg_hw_instr;
    tmp.val4 = avg_b2b_rdtscp;
    tmp.val5 =  avg_b2b_cpu_cyc;
    tmp.val6 = avg_b2b_hw_instr;

    dest = stats_simple_nextslot(stats);
    stats_memcpy_saveme((char *)dest, (char *)&tmp);

    zhpe_stats_start(subid);
    nop();
    zhpe_stats_stop(subid);
}

void zhpe_stats_calibrate_rdtscp (uint32_t opflag, uint32_t subid)
{
    uint64_t v1, v2;
    struct zhpe_stats_record *dest;
    struct zhpe_stats *stats;
    stats = pthread_getspecific(zhpe_stats_key);
    int i;
    unsigned int cntlow_v1, cnthigh_v1;
    unsigned int cntlow_v2, cnthigh_v2;

    uint64_t avg_rdtscp;
    uint64_t avg_cpu_cyc;
    uint64_t avg_hw_instr;

    uint64_t rdtscp_total=0;
    uint64_t cyc_total=0;
    uint64_t inst_total=0;

    /* rdtscp */
    for (i=0;i<CALIBRATE_ITERATIONS;i++)
    {
        do_rdtscp();
        v1 = do_rdtscp();
        do_rdtscp();
        v2 = do_rdtscp();
        rdtscp_total += v2 - v1;
    }
    avg_rdtscp = rdtscp_total/i;

    /* cpu_cyc_counter */
    for (i=0;i<CALIBRATE_ITERATIONS;i++)
    {
        do_rdtscp();
        rdpmc(cpu_cyc_counter, cntlow_v1, cnthigh_v1);
        do_rdtscp();
        rdpmc(cpu_cyc_counter, cntlow_v2, cnthigh_v2);
        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        cyc_total += v2 - v1;
    }
    avg_cpu_cyc = cyc_total/i;

    /* hw_instr_counter */
    for (i=0;i<CALIBRATE_ITERATIONS;i++)
    {
        do_rdtscp();
        rdpmc(hw_instr_counter, cntlow_v1, cnthigh_v1);
        do_rdtscp();
        rdpmc(hw_instr_counter, cntlow_v2, cnthigh_v2);
        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        inst_total += v2 - v1;
    }
    avg_hw_instr = inst_total/i;

    struct zhpe_stats_record tmp;
    tmp.subid= subid;
    tmp.op_flag = opflag;

    tmp.val1 = avg_rdtscp;
    tmp.val2 = avg_cpu_cyc;
    tmp.val3 = avg_hw_instr;
    tmp.val4 = 0;
    tmp.val5 = 0;
    tmp.val6 = 0;

    dest = stats_simple_nextslot(stats);
    stats_memcpy_saveme((char *)dest, (char *)&tmp);

    zhpe_stats_start(subid);
    do_rdtscp();
    zhpe_stats_stop(subid);
}

void zhpe_stats_calibrate_rdpmc (uint32_t opflag, uint32_t subid)
{
    uint64_t v1, v2;
    uint64_t v3, v4;
    struct zhpe_stats_record *dest;
    struct zhpe_stats *stats;
    stats = pthread_getspecific(zhpe_stats_key);
    int i;
    unsigned int cntlow_v1, cnthigh_v1;
    unsigned int cntlow_v2, cnthigh_v2;
    unsigned int cntlow_v3, cnthigh_v3;
    unsigned int cntlow_v4, cnthigh_v4;

    uint64_t avg_rdtscp;
    uint64_t avg_cpu_cyc;
    uint64_t avg_hw_instr;

    uint64_t rdtscp_total=0;
    uint64_t cyc_total=0;
    uint64_t inst_total=0;

    /* rdtscp */
    for (i=0;i<CALIBRATE_ITERATIONS;i++)
    {
        /* warmup */
        rdpmc(cpu_cyc_counter, cntlow_v3, cnthigh_v1);
        rdpmc(cpu_cyc_counter, cntlow_v4, cnthigh_v1);

        v1 = do_rdtscp();
        rdpmc(cpu_cyc_counter, cntlow_v3, cnthigh_v3);
        rdpmc(cpu_cyc_counter, cntlow_v4, cnthigh_v4);
        v3 = ((long long)cntlow_v3) | ((long long)cnthigh_v3 ) << 32;
        v4 = ((long long)cntlow_v4) | ((long long)cnthigh_v4 ) << 32;
        v2 = do_rdtscp();

        rdtscp_total += v2 - v1;
    }
    avg_rdtscp = rdtscp_total/i;

    /* cpu_cyc_counter */
    for (i=0;i<CALIBRATE_ITERATIONS;i++)
    {
        rdpmc(cpu_cyc_counter, cntlow_v3, cnthigh_v3);
        rdpmc(cpu_cyc_counter, cntlow_v4, cnthigh_v4);

        rdpmc(cpu_cyc_counter, cntlow_v1, cnthigh_v1);
        rdpmc(cpu_cyc_counter, cntlow_v3, cnthigh_v3);
        rdpmc(cpu_cyc_counter, cntlow_v4, cnthigh_v4);
        v3 = ((long long)cntlow_v3) | ((long long)cnthigh_v3 ) << 32;
        v4 = ((long long)cntlow_v4) | ((long long)cnthigh_v4 ) << 32;
        rdpmc(cpu_cyc_counter, cntlow_v2, cnthigh_v2);

        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        cyc_total += v2 - v1;
    }
    avg_cpu_cyc = cyc_total/i;

    /* hw_instr_counter */
    for (i=0;i<CALIBRATE_ITERATIONS;i++)
    {
        rdpmc(cpu_cyc_counter, cntlow_v3, cnthigh_v1);
        rdpmc(hw_instr_counter, cntlow_v4, cnthigh_v4);

        rdpmc(cpu_cyc_counter, cntlow_v1, cnthigh_v1);
        rdpmc(cpu_cyc_counter, cntlow_v3, cnthigh_v3);
        rdpmc(hw_instr_counter, cntlow_v4, cnthigh_v4);
        v3 = ((long long)cntlow_v3) | ((long long)cnthigh_v3 ) << 32;
        v4 = ((long long)cntlow_v4) | ((long long)cnthigh_v4 ) << 32;
        rdpmc(hw_instr_counter, cntlow_v2, cnthigh_v2);

        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        inst_total += v2 - v1;
    }
    avg_hw_instr = inst_total/i;

    struct zhpe_stats_record tmp;
    tmp.subid= subid;
    tmp.op_flag = opflag;

    tmp.val1 = avg_rdtscp;
    tmp.val2 = avg_cpu_cyc;
    tmp.val3 = avg_hw_instr;
    tmp.val4 = v3;
    tmp.val5 = v4;
    tmp.val6 = v4-v3;

    dest = stats_simple_nextslot(stats);
    stats_memcpy_saveme((char *)dest, (char *)&tmp);

    rdpmc(cpu_cyc_counter, cntlow_v3, cnthigh_v3);
    rdpmc(cpu_cyc_counter, cntlow_v4, cnthigh_v4);
    zhpe_stats_start(subid);
    rdpmc(cpu_cyc_counter, cntlow_v3, cnthigh_v3);
    rdpmc(cpu_cyc_counter, cntlow_v4, cnthigh_v4);
    v3 = ((long long)cntlow_v3) | ((long long)cnthigh_v3 ) << 32;
    v4 = ((long long)cntlow_v4) | ((long long)cnthigh_v4 ) << 32;
    zhpe_stats_stop(subid);
}

void zhpe_stats_calibrate_cpu_start(uint32_t opflag, uint32_t subid)
{
    uint64_t v1, v2;
    struct zhpe_stats_record *dest;
    struct zhpe_stats *stats;
    stats = pthread_getspecific(zhpe_stats_key);
    int i;
    unsigned int cntlow_v1, cnthigh_v1;
    unsigned int cntlow_v2, cnthigh_v2;

    uint64_t avg_rdtscp;
    uint64_t avg_cpu_cyc;
    uint64_t avg_hw_instr;

    uint64_t avg_b2b_rdtscp;
    uint64_t avg_b2b_cpu_cyc;
    uint64_t avg_b2b_hw_instr;

    uint64_t rdtscp_total=0;
    uint64_t cyc_total=0;
    uint64_t inst_total=0;

    uint64_t b2b_rdtscp_total=0;
    uint64_t b2b_rdpmc_c_total=0;
    uint64_t b2b_rdpmc_i_total=0;


    /* rdtscp */
    for (i=0;i<CALIBRATE_ITERATIONS;i++)
    {
        zhpe_stats_start(86);
        zhpe_stats_stop(86);
        do_rdtscp();
        zhpe_stats_start(86);
        zhpe_stats_stop(86);
        v1 = do_rdtscp();
        zhpe_stats_start(86);
        v2 = do_rdtscp();
        rdtscp_total += v2 - v1;
        zhpe_stats_stop(86);
        do_rdtscp();
        do_rdtscp();
        v1 = do_rdtscp();
        v2 = do_rdtscp();
        b2b_rdtscp_total += v2 - v1;
    }
    avg_b2b_rdtscp = b2b_rdtscp_total/i;
    avg_rdtscp = rdtscp_total/i;

    /* cpu_cyc_counter */
    for (i=0;i<CALIBRATE_ITERATIONS;i++)
    {
        zhpe_stats_start(86);
        zhpe_stats_stop(86);
        rdpmc(cpu_cyc_counter, cntlow_v1, cnthigh_v1);
        zhpe_stats_start(86);
        rdpmc(cpu_cyc_counter, cntlow_v2, cnthigh_v2);
        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        cyc_total += v2 - v1;
        zhpe_stats_stop(86);
        zhpe_stats_start(86);
        zhpe_stats_stop(86);
        rdpmc(cpu_cyc_counter, cntlow_v1, cnthigh_v1);
        rdpmc(cpu_cyc_counter, cntlow_v2, cnthigh_v2);
        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        b2b_rdpmc_c_total += v2 - v1;
    }
    avg_b2b_cpu_cyc = b2b_rdpmc_c_total/i;
    avg_cpu_cyc = cyc_total/i;

    /* hw_instr_counter */
    for (i=0;i<CALIBRATE_ITERATIONS;i++)
    {
        zhpe_stats_start(86);
        zhpe_stats_stop(86);
        rdpmc(hw_instr_counter, cntlow_v1, cnthigh_v1);
        zhpe_stats_start(86);
        rdpmc(hw_instr_counter, cntlow_v2, cnthigh_v2);
        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        inst_total += v2 - v1;
        zhpe_stats_stop(86);
        zhpe_stats_start(86);
        zhpe_stats_stop(86);
        rdpmc(hw_instr_counter, cntlow_v1, cnthigh_v1);
        rdpmc(hw_instr_counter, cntlow_v2, cnthigh_v2);
        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        b2b_rdpmc_i_total += v2 - v1;
    }
    avg_b2b_hw_instr = b2b_rdpmc_i_total/i;
    avg_hw_instr = inst_total/i;

    struct zhpe_stats_record tmp;
    tmp.subid= subid;
    tmp.op_flag = opflag;

    avg_b2b_rdtscp = b2b_rdtscp_total/i;
    avg_rdtscp = rdtscp_total/i;
    avg_hw_instr = inst_total/i;
    avg_cpu_cyc = cyc_total/i;
    tmp.val1 = avg_rdtscp;
    tmp.val2 = avg_cpu_cyc;
    tmp.val3 = avg_hw_instr;
    tmp.val4 = avg_b2b_rdtscp;
    tmp.val5 =  avg_b2b_cpu_cyc;
    tmp.val6 = avg_b2b_hw_instr;

    dest = stats_simple_nextslot(stats);
    stats_memcpy_saveme((char *)dest, (char *)&tmp);

    zhpe_stats_start(subid);
    zhpe_stats_start(86);
    zhpe_stats_stop(86);
    zhpe_stats_stop(subid);
}

void zhpe_stats_calibrate_cpu_startstop(uint32_t opflag, uint32_t subid)
{
    uint64_t v1, v2;
    struct zhpe_stats_record *dest;
    struct zhpe_stats *stats;
    stats = pthread_getspecific(zhpe_stats_key);
    int i;
    unsigned int cntlow_v1, cnthigh_v1;
    unsigned int cntlow_v2, cnthigh_v2;

    uint64_t avg_rdtscp;
    uint64_t avg_cpu_cyc;
    uint64_t avg_hw_instr;

    uint64_t avg_b2b_rdtscp;
    uint64_t avg_b2b_cpu_cyc;
    uint64_t avg_b2b_hw_instr;

    uint64_t rdtscp_total=0;
    uint64_t cyc_total=0;
    uint64_t inst_total=0;

    uint64_t b2b_rdtscp_total=0;
    uint64_t b2b_rdpmc_c_total=0;
    uint64_t b2b_rdpmc_i_total=0;


    /* rdtscp */
    for (i=0;i<CALIBRATE_ITERATIONS;i++)
    {
        zhpe_stats_start(86);
        do_rdtscp();
        zhpe_stats_stop(86);
        zhpe_stats_start(86);
        zhpe_stats_stop(86);
        v1 = do_rdtscp();
        zhpe_stats_start(86);
        zhpe_stats_stop(86);
        v2 = do_rdtscp();
        rdtscp_total += v2 - v1;
        do_rdtscp();
        do_rdtscp();
        v1 = do_rdtscp();
        v2 = do_rdtscp();
        b2b_rdtscp_total += v2 - v1;
    }
    avg_b2b_rdtscp = b2b_rdtscp_total/i;
    avg_rdtscp = rdtscp_total/i;

    /* cpu_cyc_counter */
    for (i=0;i<CALIBRATE_ITERATIONS;i++)
    {
        zhpe_stats_start(86);
        zhpe_stats_stop(86);
        rdpmc(cpu_cyc_counter, cntlow_v1, cnthigh_v1);
        zhpe_stats_start(86);
        zhpe_stats_stop(86);
        rdpmc(cpu_cyc_counter, cntlow_v2, cnthigh_v2);
        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        cyc_total += v2 - v1;
        zhpe_stats_start(86);
        zhpe_stats_stop(86);
        rdpmc(cpu_cyc_counter, cntlow_v1, cnthigh_v1);
        rdpmc(cpu_cyc_counter, cntlow_v2, cnthigh_v2);
        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        b2b_rdpmc_c_total += v2 - v1;
    }
    avg_b2b_cpu_cyc = b2b_rdpmc_c_total/i;
    avg_cpu_cyc = cyc_total/i;

    /* hw_instr_counter */
    for (i=0;i<CALIBRATE_ITERATIONS;i++)
    {
        zhpe_stats_start(86);
        zhpe_stats_stop(86);
        rdpmc(hw_instr_counter, cntlow_v1, cnthigh_v1);
        zhpe_stats_start(86);
        zhpe_stats_stop(86);
        rdpmc(hw_instr_counter, cntlow_v2, cnthigh_v2);
        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        inst_total += v2 - v1;
        zhpe_stats_start(86);
        zhpe_stats_stop(86);
        rdpmc(hw_instr_counter, cntlow_v1, cnthigh_v1);
        rdpmc(hw_instr_counter, cntlow_v2, cnthigh_v2);
        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        b2b_rdpmc_i_total += v2 - v1;
    }
    avg_b2b_hw_instr = b2b_rdpmc_i_total/i;
    avg_hw_instr = inst_total/i;

    struct zhpe_stats_record tmp;
    tmp.subid= subid;
    tmp.op_flag = opflag;

    avg_b2b_rdtscp = b2b_rdtscp_total/i;
    avg_rdtscp = rdtscp_total/i;
    avg_hw_instr = inst_total/i;
    avg_cpu_cyc = cyc_total/i;
    tmp.val1 = avg_rdtscp;
    tmp.val2 = avg_cpu_cyc;
    tmp.val3 = avg_hw_instr;
    tmp.val4 = avg_b2b_rdtscp;
    tmp.val5 =  avg_b2b_cpu_cyc;
    tmp.val6 = avg_b2b_hw_instr;

    dest = stats_simple_nextslot(stats);
    stats_memcpy_saveme((char *)dest, (char *)&tmp);

    zhpe_stats_start(subid);
    zhpe_stats_start(86);
    zhpe_stats_stop(subid);
    zhpe_stats_stop(86);
}


void zhpe_stats_calibrate_cpu_stamp(uint32_t opflag, uint32_t subid)
{
    uint64_t v1, v2;
    struct zhpe_stats_record *dest;
    struct zhpe_stats *stats;
    stats = pthread_getspecific(zhpe_stats_key);
    int i;
    unsigned int cntlow_v1, cnthigh_v1;
    unsigned int cntlow_v2, cnthigh_v2;

    uint64_t avg_rdtscp;
    uint64_t avg_cpu_cyc;
    uint64_t avg_hw_instr;

    uint64_t avg_b2b_rdtscp;
    uint64_t avg_b2b_cpu_cyc;
    uint64_t avg_b2b_hw_instr;

    uint64_t rdtscp_total=0;
    uint64_t cyc_total=0;
    uint64_t inst_total=0;

    uint64_t b2b_rdtscp_total=0;
    uint64_t b2b_rdpmc_c_total=0;
    uint64_t b2b_rdpmc_i_total=0;


    /* rdtscp */
    for (i=0;i<CALIBRATE_ITERATIONS;i++)
    {
        do_rdtscp();
        v1 = do_rdtscp();
        zhpe_stats_stamp(89888786, 89, 88, 87, 86);
        v2 = do_rdtscp();
        rdtscp_total += v2 - v1;
        do_rdtscp();
        do_rdtscp();
        v1 = do_rdtscp();
        v2 = do_rdtscp();
        b2b_rdtscp_total += v2 - v1;
    }
    avg_b2b_rdtscp = b2b_rdtscp_total/i;
    avg_rdtscp = rdtscp_total/i;

    /* cpu_cyc_counter */
    for (i=0;i<CALIBRATE_ITERATIONS;i++)
    {
        rdpmc(cpu_cyc_counter, cntlow_v1, cnthigh_v1);
        zhpe_stats_stamp(99888786, 99, 88, 87, 86);
        rdpmc(cpu_cyc_counter, cntlow_v2, cnthigh_v2);
        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        cyc_total += v2 - v1;
        rdpmc(cpu_cyc_counter, cntlow_v1, cnthigh_v1);
        rdpmc(cpu_cyc_counter, cntlow_v2, cnthigh_v2);
        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        b2b_rdpmc_c_total += v2 - v1;
    }
    avg_b2b_cpu_cyc = b2b_rdpmc_c_total/i;
    avg_cpu_cyc = cyc_total/i;

    /* hw_instr_counter */
    for (i=0;i<CALIBRATE_ITERATIONS;i++)
    {
        rdpmc(hw_instr_counter, cntlow_v1, cnthigh_v1);
        zhpe_stats_stamp(99988786, 99, 98, 87, 86);
        rdpmc(hw_instr_counter, cntlow_v2, cnthigh_v2);
        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        inst_total += v2 - v1;
        rdpmc(hw_instr_counter, cntlow_v1, cnthigh_v1);
        rdpmc(hw_instr_counter, cntlow_v2, cnthigh_v2);
        v1 = ((long long)cntlow_v1) | ((long long)cnthigh_v1 ) << 32;
        v2 = ((long long)cntlow_v2) | ((long long)cnthigh_v2 ) << 32;
        b2b_rdpmc_i_total += v2 - v1;
    }
    avg_b2b_hw_instr = b2b_rdpmc_i_total/i;
    avg_hw_instr = inst_total/i;

    struct zhpe_stats_record tmp;
    tmp.subid= subid;
    tmp.op_flag = opflag;

    avg_b2b_rdtscp = b2b_rdtscp_total/i;
    avg_rdtscp = rdtscp_total/i;
    avg_hw_instr = inst_total/i;
    avg_cpu_cyc = cyc_total/i;
    tmp.val1 = avg_rdtscp;
    tmp.val2 = avg_cpu_cyc;
    tmp.val3 = avg_hw_instr;
    tmp.val4 = avg_b2b_rdtscp;
    tmp.val5 =  avg_b2b_cpu_cyc;
    tmp.val6 = avg_b2b_hw_instr;

    dest = stats_simple_nextslot(stats);
    stats_memcpy_saveme((char *)dest, (char *)&tmp);

    zhpe_stats_start(subid);
    zhpe_stats_stamp(99998786, 99, 99, 87, 86);
    zhpe_stats_stop(subid);
}

void zhpe_stats_test(uint16_t uid)
{
    zhpe_stats_open(uid);
    zhpe_stats_enable();

    zhpe_stats_start(0);
    zhpe_stats_stop(0);

    zhpe_stats_start(10);
    zhpe_stats_stop(10);

    zhpe_stats_start(20);
    zhpe_stats_stop(20);

    zhpe_stats_start(30);
    nop();
    zhpe_stats_stop(30);

    zhpe_stats_start(40);
    nop();
    zhpe_stats_start(40);
    nop();
    nop();
    zhpe_stats_stop(40);

    zhpe_stats_start(50);
    nop();
    nop();
    nop();
    zhpe_stats_stop_all();

    zhpe_stats_start(60);
    zhpe_stats_start(70);
    zhpe_stats_stop(70);
    zhpe_stats_stop(60);

    zhpe_stats_start(80);
    nop();
    zhpe_stats_start(90);
    nop();
    nop();
    zhpe_stats_stop(80);
    nop();
    zhpe_stats_stop_all();

    zhpe_stats_start(100);
    nop();
    nop();
    zhpe_stats_start(110);
    nop();
    nop();
    nop();
    zhpe_stats_stop_all();

    zhpe_stats_start(120);
    nop();
    zhpe_stats_start(130);
    nop();
    nop();

    zhpe_stats_start(140);
    nop();
    zhpe_stats_start(150);
    nop();
    nop();
    zhpe_stats_start(150);
    nop();
    zhpe_stats_stop(150);
    zhpe_stats_stop(140);

    zhpe_stats_start(160);
    zhpe_stats_start(170);
    nop();
    zhpe_stats_stop(170);
    zhpe_stats_stop(160);

    zhpe_stats_stop(150);
    zhpe_stats_stop(140);

    zhpe_stats_stop_all();

    zhpe_stats_close();
}

uint64_t dest1[8] CACHE_ALIGNED;

uint64_t src1[8] CACHE_ALIGNED;

void zhpe_stats_test_saveme(uint32_t opflag, uint32_t subid)
{
    uint64_t v1, v2;
    uint64_t v3, v4;

    uint64_t len = 64;
    char *foo1, *foo2;
    foo1=(char *)dest1;
    foo2=(char *)src1;

    v1 = do_rdtscp();
    sleep(1);
    v2 = do_rdtscp();
    printf("[sleep(1): %lu] %s,%u\n", v2-v1, __func__, __LINE__);

    zhpe_stats_calibrate_cpu_startstop(opflag, subid);
    zhpe_stats_calibrate_cpu_start(opflag, subid+1);

    printf("testing impact of vmemcpy:\n");
    int b=0;
    atm_inc(&b);
    atm_inc(&b);
    v1 = do_rdtscp();
    atm_inc(&b);
    v2 = do_rdtscp();
    __vmemcpy(movntdq, movntdqa, foo1, foo2, len);
    v3 = do_rdtscp();
    atm_inc(&b);
    v4 = do_rdtscp();
    printf("Before vmemcpy, atm_inc was %lu\n",v2-v1);
    printf("After vmemcpy, atm_inc was %lu\n",v4-v3);

    atm_inc(&b);
    atm_inc(&b);
    v1 = do_rdtscp();
    atm_inc(&b);
    v2 = do_rdtscp();
    memcpy(foo1, foo2, len);
    v3 = do_rdtscp();
    atm_inc(&b);
    v4 = do_rdtscp();
    printf("Before memcpy, atm_inc was %lu\n",v2-v1);
    printf("After memcpy, atm_inc was %lu\n",v4-v3);
}


/* single thread, no need to lock */
/* for flush, record cpu profile stats */
/* may later add cache records too */
void zhpe_stats_flush(struct zhpe_stats *stats)
{
    assert(stats);
    assert(stats->buffer);
    struct zhpe_stats_record *dest;

    ssize_t     res;
    uint64_t    bufsize;

    struct zhpe_stats_record tmp;

    stats_cpu_setvals(&tmp);
    tmp.subid = 0;
    tmp.op_flag = ZHPE_STATS_FLUSH_START;

    dest = stats_simple_nextslot(stats);
    stats_memcpy_saveme((char *)dest, (char *)&tmp);

    bufsize = (sizeof(struct zhpe_stats_record) * stats->head) ;
    io_wmb();
    res = write(stats->fd, stats->buffer, bufsize);
    if (check_func_ion(__func__, __LINE__, "write", bufsize, false,
                       bufsize, res, 0) < 0)
        abort();

    stats->head = 0;

    stats_cpu_setvals(&tmp);
    tmp.op_flag = ZHPE_STATS_FLUSH_STOP;
    dest = stats_simple_nextslot(stats);
    stats_memcpy_saveme((char *)dest,(char *)&tmp);
}

/* don't really close. Just log events for cpu profile and flush stats. */
static void stats_close()
{
    struct zhpe_stats   *stats;
    stats = pthread_getspecific(zhpe_stats_key);
    if (!stats)
        return ;

    cpu_stats_recordme(stats, 0, ZHPE_STATS_CLOSE);
    zhpe_stats_flush(stats);
}

static void stats_key_destructor(void *vstats)
{
    struct zhpe_stats   *stats = vstats;

    if (!stats)
        return;

    if (!stats->enabled)
        return;

    stats_close(stats);
}

void stats_finalize()
{
    stats_cmn_finalize();

    if (hw_instr_fd != -1)
    {
       close(hw_instr_fd);
       hw_instr_fd=-1;
    }

    if (cpu_cyc_fd != -1)
    {
       close(cpu_cyc_fd);
       cpu_cyc_fd=-1;
    }
}

/* cpu profile */
static void cpu_stats_start(struct zhpe_stats *stats, uint32_t subid)
{
    cpu_stats_recordme(stats, subid, ZHPE_STATS_START);
}

static void cpu_stats_stop(struct zhpe_stats *stats, uint32_t subid)
{
    cpu_stats_recordme(stats, subid, ZHPE_STATS_STOP);
}

static void cpu_stats_enable()
{
    struct zhpe_stats   *stats;
    stats = pthread_getspecific(zhpe_stats_key);
    if (!stats)
        return;

    stats->enabled = true;
    do_rdtscp();
    prctl(PR_TASK_PERF_EVENTS_ENABLE);
    cpu_stats_recordme(stats, 0, ZHPE_STATS_ENABLE);
}

static void cpu_stats_disable()
{
    struct zhpe_stats   *stats;
    stats = pthread_getspecific(zhpe_stats_key);
    if (!stats)
        return;

    do_rdtscp();
    stats->enabled = false;
    cpu_stats_recordme(stats, 0, ZHPE_STATS_DISABLE);
    prctl(PR_TASK_PERF_EVENTS_DISABLE);
}


/* rdtscp profile */
static void rdtscp_stats_start(struct zhpe_stats *stats, uint32_t subid)
{
    rdtscp_stats_recordme(stats, subid, ZHPE_STATS_START);
}

static void rdtscp_stats_stop(struct zhpe_stats *stats, uint32_t subid)
{
    rdtscp_stats_recordme(stats, subid, ZHPE_STATS_STOP);
}

static void rdtscp_stats_enable()
{
    struct zhpe_stats   *stats;
    stats = pthread_getspecific(zhpe_stats_key);
    if (!stats)
        return;

    stats->enabled = true;
    do_rdtscp();
    prctl(PR_TASK_PERF_EVENTS_ENABLE);
    rdtscp_stats_recordme(stats, 0, ZHPE_STATS_ENABLE);
}

static void rdtscp_stats_disable()
{
    struct zhpe_stats   *stats;
    stats = pthread_getspecific(zhpe_stats_key);
    if (!stats)
        return;

    do_rdtscp();
    stats->enabled = false;
    rdtscp_stats_recordme(stats, 0, ZHPE_STATS_DISABLE);
    prctl(PR_TASK_PERF_EVENTS_DISABLE);
}

/* generic */
static void stats_stamp(struct zhpe_stats *stats, uint32_t subid, uint32_t items, uint64_t *data)
{
    struct zhpe_stats_record    *dest;
    struct zhpe_stats_record    tmp;

    tmp.subid = subid;
    tmp.op_flag = ZHPE_STATS_STAMP;

    tmp.val1 = do_rdtscp();
    tmp.val2 = items > 0 ? ((uint64_t *)data)[0] : 0;
    tmp.val3 = items > 1 ? ((uint64_t *)data)[1] : 0;
    tmp.val4 = items > 2 ? ((uint64_t *)data)[2] : 0;
    tmp.val5 = items > 3 ? ((uint64_t *)data)[3] : 0;
    tmp.val6 = items > 4 ? ((uint64_t *)data)[4] : 0;

    dest = stats_simple_nextslot(stats);
    stats_memcpy_saveme((char *)dest, (char *)&tmp);
}

static inline struct zhpe_stats *get_zhpe_stats(void)
{
    struct zhpe_stats   *stats;
    stats = pthread_getspecific(zhpe_stats_key);
    return stats;
}

static struct zhpe_stats_ops stats_ops_cpu = {
    .open               = stats_open,
    .close              = stats_close,
    .enable             = cpu_stats_enable,
    .disable            = cpu_stats_disable,
    .get_zhpe_stats     = get_zhpe_stats,
    .stop_all           = stats_nop_stats,
    .pause_all          = stats_nop_stats,
    .restart_all        = stats_nop_void,
    .start              = cpu_stats_start,
    .stop               = cpu_stats_stop,
    .pause              = stats_nop_stats_uint32,
    .finalize           = stats_finalize,
    .key_destructor     = stats_key_destructor,
    .stamp              = stats_stamp,
    .setvals            = stats_cpu_setvals,
    .saveme             = stats_memcpy_saveme,
};


static struct zhpe_stats_ops stats_ops_rdtscp = {
    .open               = stats_open,
    .close              = stats_close,
    .enable             = rdtscp_stats_enable,
    .disable            = rdtscp_stats_disable,
    .get_zhpe_stats     = get_zhpe_stats,
    .stop_all           = stats_nop_stats,
    .pause_all          = stats_nop_stats,
    .restart_all        = stats_nop_void,
    .start              = rdtscp_stats_start,
    .stop               = rdtscp_stats_stop,
    .pause              = stats_nop_stats_uint32,
    .finalize           = stats_finalize,
    .key_destructor     = stats_key_destructor,
    .stamp              = stats_stamp,
    .setvals            = stats_rdtscp_setvals,
    .saveme             = stats_memcpy_saveme,
};

static void init_cpu_profile()
{
    struct perf_event_attr pe1, pe2;
    int            err;

    int hw_instr_fd,cpu_cyc_fd;
    void *addr1, *addr2;

    uint64_t index1, offset1;

    uint64_t index2, offset2;

    /* first counter is group leader */
    memset(&pe1, 0, sizeof(struct perf_event_attr));
    pe1.size = sizeof(struct perf_event_attr);
    pe1.type = PERF_TYPE_HARDWARE;
    pe1.config = PERF_COUNT_HW_INSTRUCTIONS;
    pe1.exclude_kernel = 1;

    hw_instr_fd = perf_event_open(&pe1, 0, -1, -1, 0);
    if (hw_instr_fd < 0) {
        err = errno;
        fprintf(stderr, "Error hw_instr_fd == %d; perf_event_open %llx returned error %d:%s\n", hw_instr_fd, pe1.config,
            err, strerror(err));
        exit(EXIT_FAILURE);
    }

    /* mmap the group leader file descriptor */
    addr1 = mmap(NULL, 4096, PROT_READ, MAP_SHARED, hw_instr_fd, 0);
    if (addr1 == (void *)(-1)) {
        err = errno;
        fprintf(stderr, "Error mmap() syscall returned%llx\n", (unsigned long long)addr1);
        exit(EXIT_FAILURE);
    }

    /* second counter */
    memset(&pe2, 0, sizeof(struct perf_event_attr));
    pe2.size = sizeof(struct perf_event_attr);
    pe2.type = PERF_TYPE_HARDWARE;
    pe2.config = PERF_COUNT_HW_CPU_CYCLES;
    pe2.exclude_kernel = 1;

    cpu_cyc_fd = perf_event_open(&pe2, 0, -1, -1, 0);
    if (cpu_cyc_fd < 0) {
        err = errno;
        fprintf(stderr, "Error cpu_cyc_fd == %d; perf_event_open %llx returned error %d:%s\n", cpu_cyc_fd, pe2.config,
            err, strerror(err));
        exit(EXIT_FAILURE);
    }

    /* mmap the second file descriptor */
    addr2 = mmap(NULL, 4096, PROT_READ, MAP_SHARED, cpu_cyc_fd, 0);
    if (addr2 == (void *)(-1)) {
        err = errno;
        fprintf(stderr, "Error mmap() syscall returned%llx\n", (unsigned long long)addr2);
        exit(EXIT_FAILURE);
    }

    struct perf_event_mmap_page * buf1 = (struct perf_event_mmap_page *) addr1;
    struct perf_event_mmap_page * buf2 = (struct perf_event_mmap_page *) addr2;

    index1 = buf1->index;
    if (index1 == 0) {
        printf("addr1  buffer index was 0\n");
        exit(EXIT_FAILURE);
    }
    offset1 = buf1->offset;

    index2 = buf2->index;
    if (index2 == 0) {
        printf("addr2  buffer index was 0\n");
        exit(EXIT_FAILURE);
    }
    offset2 = buf2->offset;

    hw_instr_counter = index1 + offset1;
    cpu_cyc_counter = index2 + offset2;
    prctl(PR_TASK_PERF_EVENTS_ENABLE);
}

bool zhpe_stats_init(const char *stats_dir, const char *stats_unique)
{
    bool                ret = false;
    int                 rc;
    char                *tmp;

    if (!stats_dir && !stats_unique)
        return ret;
    if (!stats_dir || !stats_unique) {
        print_err("%s,%u:missing %s\n", __func__, __LINE__,
                  stats_dir ? "stats_unique" : "stats_dir");
        return ret;
    }
    mutex_lock(&zhpe_stats_mutex);
    if (zhpe_stats_ops != &zhpe_stats_nops) {
        print_err("%s,%u:already initialized\n", __func__, __LINE__);
        goto done;
    }

    zhpe_stats_profile = 0;
    tmp = getenv("ZHPE_STATS_PROFILE");
    if (tmp != NULL)
    {
        if (!strcmp("rdtscp",tmp)) {
            zhpe_stats_profile = ZHPE_STATS_RDTSCP;
            zhpe_stats_ops = &stats_ops_rdtscp;
        }

        if (!strcmp("cpu",tmp)) {
            zhpe_stats_profile = ZHPE_STATS_CPU;
            zhpe_stats_ops = &stats_ops_cpu;
            init_cpu_profile();
        }

        if (!strcmp("cache",tmp)) {
            zhpe_stats_profile = ZHPE_STATS_CACHE;
            zhpe_stats_ops = &zhpe_stats_nops;
        }
    }

    zhpe_stats_buf_count=0;
    tmp = getenv("ZHPE_STATS_BUF_COUNT");
    if (tmp != NULL)
        zhpe_stats_buf_count=atoi(tmp);


    if ((zhpe_stats_buf_count <= 0) || (zhpe_stats_buf_count > ZHPE_STATS_BUF_COUNT_MAX))
        zhpe_stats_buf_count=ZHPE_STATS_BUF_COUNT_MAX;

    if (zhpe_stats_buf_count & (zhpe_stats_buf_count -1)) {
        zhpe_stats_buf_count = pow(2,ceil(log(zhpe_stats_buf_count)/log(2)));
        print_err("%s,%u: rounded ZHPE_STATS_BUF_COUNT up to a power of 2: %lu\n",
                  __func__, __LINE__, zhpe_stats_buf_count);
    }

    zhpe_stats_buf_mask=zhpe_stats_buf_count - 1;

    if (zhpe_stats_ops == &zhpe_stats_nops) {
        print_err("%s,%u:no statistics support available\n",
                  __func__, __LINE__);
        goto done;

    }

    zhpe_stats_dir = strdup_or_null(stats_dir);
    if (!zhpe_stats_dir)
        goto done;
    zhpe_stats_unique = strdup_or_null(stats_unique);
    if (!zhpe_stats_unique)
        goto done;

    if (!zhpe_stats_init_once) {
        rc = -pthread_key_create(&zhpe_stats_key,
                                 zhpe_stats_ops->key_destructor);
        if (rc < 0) {
            print_func_err(__func__, __LINE__, "pthread_key_create", "", rc);
            goto done;
        }
        zhpe_stats_init_once = true;
     }

    ret = true;
 done:
    mutex_unlock(&zhpe_stats_mutex);

    return ret;
}

#else /* HAVE_ZHPE_STATS */

void zhpe_stats_init(const char *stats_dir, const char *stats_unique)
{
    if (!stats_dir && !stats_unique)
        return;

#ifdef DEBUG
    print_err("%s,%u:libzhpe_stats built without stats support\n",
              __func__, __LINE__);
#endif
}

#endif /* HAVE_ZHPE_STATS */
