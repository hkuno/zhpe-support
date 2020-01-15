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
int my_perf_event_open(struct perf_event_attr *pea, pid_t pid,
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

/* Common definitions/code */
static char             *zhpe_stats_dir;
static char             *zhpe_stats_unique;
static pthread_key_t    zhpe_stats_key;
static pthread_mutex_t  zhpe_stats_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool             zhpe_stats_init_once;
static uint64_t         zhpe_stats_buf_count;
static uint64_t         zhpe_stats_buf_mask;
static size_t           zhpe_stats_profile;

int zhpe_stats_num_counters=0;
int *zhpe_stats_fd_list;
uint64_t *zhpe_stats_cntr_list;
uint64_t *zhpe_stats_config_list;
__u32 perf_typeid;

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

static void stats_write_metadata(struct zhpe_stats *stats)
{
    int i, bufsize, res;

    struct zhpe_stats_metadata metadata;
    metadata.profileid = zhpe_stats_profile;
    metadata.perf_typeid = perf_typeid;
    metadata.config_count =  zhpe_stats_num_counters;
    for (i=0;i< zhpe_stats_num_counters;i++){
        metadata.config_list[i] = zhpe_stats_config_list[i];
    }

    bufsize = sizeof(struct zhpe_stats_metadata);
    res = write(stats->fd, &metadata, bufsize);
    if (check_func_ion(__func__, __LINE__, "write", bufsize, false,
                       bufsize, res, 0) < 0)
        abort();
}

static struct zhpe_stats *stats_open(uint16_t uid)
{
    char                *fname = NULL;
    struct zhpe_stats   *stats;

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

    stats_write_metadata(stats);

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

static void stats_setvals_0_rdpmc(struct zhpe_stats_record *rec)
{
    rec->val0 = do_rdtscp();
    rec->val1 = 0;
    rec->val2 = 0;
    rec->val3 = 0;
    rec->val4 = 0;
    rec->val5 = 0;
    rec->val6 = 0;
}

static void stats_setvals_2_rdpmc(struct zhpe_stats_record *rec)
{
    unsigned int cnt1low, cnt1high;
    unsigned int cnt2low, cnt2high;

    rec->val0 = do_rdtscp();
    rdpmc(zhpe_stats_cntr_list[0], cnt1low, cnt1high);
    rdpmc(zhpe_stats_cntr_list[1], cnt2low, cnt2high);
    rec->val1 = ((long long)cnt1low) | ((long long)cnt1high ) << 32;
    rec->val2 = ((long long)cnt2low) | ((long long)cnt2high ) << 32;
    rec->val3 = 0;
    rec->val4 = 0;
    rec->val5 = 0;
    rec->val6 = 0;
}

static void stats_setvals_3_rdpmc(struct zhpe_stats_record *rec)
{
    unsigned int cnt1low, cnt1high;
    unsigned int cnt2low, cnt2high;
    unsigned int cnt3low, cnt3high;

    rec->val0 = do_rdtscp();
    rdpmc(zhpe_stats_cntr_list[0], cnt1low, cnt1high);
    rdpmc(zhpe_stats_cntr_list[1], cnt2low, cnt2high);
    rdpmc(zhpe_stats_cntr_list[2], cnt3low, cnt3high);
    rec->val1 = ((long long)cnt1low) | ((long long)cnt1high ) << 32;
    rec->val2 = ((long long)cnt2low) | ((long long)cnt2high ) << 32;
    rec->val3 = ((long long)cnt3low) | ((long long)cnt3high ) << 32;
    rec->val4 = 0;
    rec->val5 = 0;
    rec->val6 = 0;
}

static void stats_setvals_4_rdpmc(struct zhpe_stats_record *rec)
{
    unsigned int cnt1low, cnt1high;
    unsigned int cnt2low, cnt2high;
    unsigned int cnt3low, cnt3high;
    unsigned int cnt4low, cnt4high;

    rec->val0 = do_rdtscp();
    rdpmc(zhpe_stats_cntr_list[0], cnt1low, cnt1high);
    rdpmc(zhpe_stats_cntr_list[1], cnt2low, cnt2high);
    rdpmc(zhpe_stats_cntr_list[2], cnt3low, cnt3high);
    rdpmc(zhpe_stats_cntr_list[3], cnt4low, cnt4high);
    rec->val1 = ((long long)cnt1low) | ((long long)cnt1high ) << 32;
    rec->val2 = ((long long)cnt2low) | ((long long)cnt2high ) << 32;
    rec->val3 = ((long long)cnt3low) | ((long long)cnt3high ) << 32;
    rec->val4 = ((long long)cnt4low) | ((long long)cnt4high ) << 32;
    rec->val5 = 0;
    rec->val6 = 0;
}

static void stats_setvals_5_rdpmc(struct zhpe_stats_record *rec)
{
    unsigned int cnt1low, cnt1high;
    unsigned int cnt2low, cnt2high;
    unsigned int cnt3low, cnt3high;
    unsigned int cnt4low, cnt4high;
    unsigned int cnt5low, cnt5high;

    rec->val0 = do_rdtscp();
    rdpmc(zhpe_stats_cntr_list[0], cnt1low, cnt1high);
    rdpmc(zhpe_stats_cntr_list[1], cnt2low, cnt2high);
    rdpmc(zhpe_stats_cntr_list[2], cnt3low, cnt3high);
    rdpmc(zhpe_stats_cntr_list[3], cnt4low, cnt4high);
    rdpmc(zhpe_stats_cntr_list[4], cnt5low, cnt5high);
    rec->val1 = ((long long)cnt1low) | ((long long)cnt1high ) << 32;
    rec->val2 = ((long long)cnt2low) | ((long long)cnt2high ) << 32;
    rec->val3 = ((long long)cnt3low) | ((long long)cnt3high ) << 32;
    rec->val4 = ((long long)cnt4low) | ((long long)cnt4high ) << 32;
    rec->val5 = ((long long)cnt5low) | ((long long)cnt5high ) << 32;
}

static void stats_setvals_6_rdpmc(struct zhpe_stats_record *rec)
{
    unsigned int cnt1low, cnt1high;
    unsigned int cnt2low, cnt2high;
    unsigned int cnt3low, cnt3high;
    unsigned int cnt4low, cnt4high;
    unsigned int cnt5low, cnt5high;
    unsigned int cnt6low, cnt6high;

    rec->val0 = do_rdtscp();
    rdpmc(zhpe_stats_cntr_list[0], cnt1low, cnt1high);
    rdpmc(zhpe_stats_cntr_list[1], cnt2low, cnt2high);
    rdpmc(zhpe_stats_cntr_list[2], cnt3low, cnt3high);
    rdpmc(zhpe_stats_cntr_list[3], cnt4low, cnt4high);
    rdpmc(zhpe_stats_cntr_list[4], cnt5low, cnt5high);
    rdpmc(zhpe_stats_cntr_list[5], cnt6low, cnt6high);
    rec->val1 = ((long long)cnt1low) | ((long long)cnt1high ) << 32;
    rec->val2 = ((long long)cnt2low) | ((long long)cnt2high ) << 32;
    rec->val3 = ((long long)cnt3low) | ((long long)cnt3high ) << 32;
    rec->val4 = ((long long)cnt4low) | ((long long)cnt4high ) << 32;
    rec->val5 = ((long long)cnt5low) | ((long long)cnt5high ) << 32;
    rec->val6 = ((long long)cnt6low) | ((long long)cnt6high ) << 32;
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

    stats_setvals_0_rdpmc(&tmp);
    dest = stats_simple_nextslot(stats);
    stats_memcpy_saveme((char *)dest, (char *)&tmp);
}

static void stats_recordme(struct zhpe_stats *stats, uint32_t subid, uint32_t opflag)
{
    struct zhpe_stats_record *dest;
    struct zhpe_stats_record tmp;

    tmp.subid = subid;
    tmp.op_flag = opflag;

    zhpe_stats_ops->setvals(&tmp);
    dest = stats_simple_nextslot(stats);
    stats_memcpy_saveme((char *)dest, (char *)&tmp);
}


void zhpe_stats_test(uint16_t uid)
{
    int b=0;

    zhpe_stats_open(uid);
    zhpe_stats_enable();

    zhpe_stats_start(0);
    zhpe_stats_stop(0);
    zhpe_stats_start(0);
    zhpe_stats_stop(0);
    zhpe_stats_start(0);
    zhpe_stats_stop(0);
    zhpe_stats_start(0);
    zhpe_stats_stop(0);
    zhpe_stats_start(0);
    zhpe_stats_stop(0);
    zhpe_stats_start(0);
    zhpe_stats_stop(0);
    zhpe_stats_start(0);
    zhpe_stats_stop(0);
    zhpe_stats_start(0);
    zhpe_stats_stop(0);

    zhpe_stats_start(1);
    zhpe_stats_stop(1);
    zhpe_stats_start(1);
    zhpe_stats_stop(1);
    zhpe_stats_start(1);
    zhpe_stats_stop(1);
    zhpe_stats_start(1);
    zhpe_stats_stop(1);
    zhpe_stats_start(1);
    zhpe_stats_stop(1);

    zhpe_stats_start(1);
    zhpe_stats_stop(1);
    zhpe_stats_start(1);
    zhpe_stats_stop(1);
    zhpe_stats_start(1);
    zhpe_stats_stop(1);
    zhpe_stats_start(1);
    zhpe_stats_stop(1);
    zhpe_stats_start(1);
    zhpe_stats_stop(1);

    zhpe_stats_stamp(89888786, 89, 88, 87, 86);
    zhpe_stats_stamp(89888786, 89, 88, 87, 86);
    zhpe_stats_stamp(89888786, 89, 88, 87, 86);

    zhpe_stats_start(2);
    zhpe_stats_stamp(89888786, 89, 88, 87, 86);
    zhpe_stats_stop(2);

    zhpe_stats_start(2);
    zhpe_stats_stamp(89888786, 89, 88, 87, 86);
    zhpe_stats_stop(2);

    zhpe_stats_start(2);
    zhpe_stats_stamp(89888786, 89, 88, 87, 86);
    zhpe_stats_stop(2);

    zhpe_stats_start(2);
    zhpe_stats_stamp(89888786, 89, 88, 87, 86);
    zhpe_stats_stop(2);

    zhpe_stats_start(2);
    zhpe_stats_stamp(89888786, 89, 88, 87, 86);
    zhpe_stats_stop(2);

    zhpe_stats_start(2);
    zhpe_stats_stamp(89888786, 89, 88, 87, 86);
    zhpe_stats_stop(2);

    zhpe_stats_start(2);
    zhpe_stats_stamp(89888786, 89, 88, 87, 86);
    zhpe_stats_stop(2);

    zhpe_stats_start(2);
    zhpe_stats_stamp(89888786, 89, 88, 87, 86);
    zhpe_stats_stop(2);

    zhpe_stats_start(2);
    zhpe_stats_stamp(89888786, 89, 88, 87, 86);
    zhpe_stats_stop(2);

    zhpe_stats_start(2);
    zhpe_stats_stamp(89888786, 89, 88, 87, 86);
    zhpe_stats_stop(2);

    zhpe_stats_start(3);
    zhpe_stats_start(0);
    zhpe_stats_stop(0);
    zhpe_stats_stop(3);

    zhpe_stats_start(3);
    zhpe_stats_start(0);
    zhpe_stats_stop(0);
    zhpe_stats_stop(3);

    zhpe_stats_start(3);
    zhpe_stats_start(0);
    zhpe_stats_stop(0);
    zhpe_stats_stop(3);

    zhpe_stats_start(3);
    zhpe_stats_start(0);
    zhpe_stats_stop(0);
    zhpe_stats_stop(3);

    zhpe_stats_start(3);
    zhpe_stats_start(0);
    zhpe_stats_stop(0);
    zhpe_stats_stop(3);

    zhpe_stats_start(3);
    zhpe_stats_start(0);
    zhpe_stats_stop(0);
    zhpe_stats_stop(3);

    zhpe_stats_start(3);
    zhpe_stats_start(0);
    zhpe_stats_stop(0);
    zhpe_stats_stop(3);

    zhpe_stats_start(3);
    zhpe_stats_start(0);
    zhpe_stats_stop(0);
    zhpe_stats_stop(3);

    zhpe_stats_start(3);
    zhpe_stats_start(0);
    zhpe_stats_stop(0);
    zhpe_stats_stop(3);

    zhpe_stats_start(3);
    zhpe_stats_start(0);
    zhpe_stats_stop(0);
    zhpe_stats_stop(3);

    nop();
    nop();
    nop();
    nop();

    zhpe_stats_start(4);
    nop();
    zhpe_stats_stop(4);

    zhpe_stats_start(4);
    nop();
    zhpe_stats_stop(4);

    zhpe_stats_start(4);
    nop();
    zhpe_stats_stop(4);

    atm_inc(&b);
    atm_inc(&b);
    atm_inc(&b);
    zhpe_stats_start(5);
    atm_inc(&b);
    zhpe_stats_stop(5);

    zhpe_stats_start(5);
    atm_inc(&b);
    zhpe_stats_stop(5);

    zhpe_stats_start(5);
    atm_inc(&b);
    zhpe_stats_stop(5);


    zhpe_stats_start(7);
    zhpe_stats_start(20);
    nop();
    zhpe_stats_stop(20);
    zhpe_stats_stop(7);

    zhpe_stats_start(7);
    zhpe_stats_start(20);
    nop();
    zhpe_stats_stop(20);
    zhpe_stats_stop(7);

    zhpe_stats_start(7);
    zhpe_stats_start(20);
    nop();
    zhpe_stats_stop(20);
    zhpe_stats_stop(7);

    zhpe_stats_start(8);
    zhpe_stats_start(9);
    atm_inc(&b);
    zhpe_stats_stop(9);
    zhpe_stats_stop(8);

    zhpe_stats_start(8);
    zhpe_stats_start(9);
    atm_inc(&b);
    zhpe_stats_stop(9);
    zhpe_stats_stop(8);

    zhpe_stats_start(8);
    zhpe_stats_start(9);
    atm_inc(&b);
    zhpe_stats_stop(9);
    zhpe_stats_stop(8);

    zhpe_stats_start(10);
    zhpe_stats_start(10);
    zhpe_stats_start(10);
    zhpe_stats_start(10);

    zhpe_stats_stop_all();
    zhpe_stats_close();
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

    /* setting just rdtscp because flush should happen only at end*/
    stats_setvals_0_rdpmc(&tmp);

    tmp.subid = 0;
    tmp.op_flag = ZHPE_STATS_FLUSH_START;

    dest = stats_simple_nextslot(stats);
    stats_memcpy_saveme((char *)dest, (char *)&tmp);

    assert(stats->head < stats->num_slots - 1);
    bufsize = (zhpe_stats_buf_mask & stats->head) * (sizeof(struct zhpe_stats_record));
    io_wmb();
    res = write(stats->fd, stats->buffer, bufsize);
    if (check_func_ion(__func__, __LINE__, "write", bufsize, false,
                       bufsize, res, 0) < 0)
        abort();

    stats->head = 0;

    stats_setvals_0_rdpmc(&tmp);
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

    stats_recordme(stats, 0, ZHPE_STATS_CLOSE);
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

    for (int i=0;i<zhpe_stats_num_counters;i++)
    {
        if (zhpe_stats_fd_list[i] != -1)
        {
           close(zhpe_stats_fd_list[i]);
           zhpe_stats_fd_list[i]=-1;
        }
    }
}

/* cpu profile */
static void stats_start(struct zhpe_stats *stats, uint32_t subid)
{
    stats_recordme(stats, subid, ZHPE_STATS_START);
}

static void stats_stop(struct zhpe_stats *stats, uint32_t subid)
{
    stats_recordme(stats, subid, ZHPE_STATS_STOP);
}

static void stats_enable()
{
    struct zhpe_stats   *stats;
    stats = pthread_getspecific(zhpe_stats_key);
    if (!stats)
        return;

    stats->enabled = true;
    do_rdtscp();
    prctl(PR_TASK_PERF_EVENTS_ENABLE);
    stats_recordme(stats, 0, ZHPE_STATS_ENABLE);
}

static void stats_disable()
{
    struct zhpe_stats   *stats;
    stats = pthread_getspecific(zhpe_stats_key);
    if (!stats)
        return;

    do_rdtscp();
    stats->enabled = false;
    stats_recordme(stats, 0, ZHPE_STATS_DISABLE);
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

static struct zhpe_stats_ops stats_ops_0_rdpmc = {
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
    .setvals            = stats_setvals_0_rdpmc,
    .saveme             = stats_memcpy_saveme,
};

static struct zhpe_stats_ops stats_ops_2_rdpmc = {
    .open               = stats_open,
    .close              = stats_close,
    .enable             = stats_enable,
    .disable            = stats_disable,
    .get_zhpe_stats     = get_zhpe_stats,
    .stop_all           = stats_nop_stats,
    .pause_all          = stats_nop_stats,
    .restart_all        = stats_nop_void,
    .start              = stats_start,
    .stop               = stats_stop,
    .pause              = stats_nop_stats_uint32,
    .finalize           = stats_finalize,
    .key_destructor     = stats_key_destructor,
    .stamp              = stats_stamp,
    .setvals            = stats_setvals_2_rdpmc,
    .saveme             = stats_memcpy_saveme,
};

static struct zhpe_stats_ops stats_ops_3_rdpmc = {
    .open               = stats_open,
    .close              = stats_close,
    .enable             = stats_enable,
    .disable            = stats_disable,
    .get_zhpe_stats     = get_zhpe_stats,
    .stop_all           = stats_nop_stats,
    .pause_all          = stats_nop_stats,
    .restart_all        = stats_nop_void,
    .start              = stats_start,
    .stop               = stats_stop,
    .pause              = stats_nop_stats_uint32,
    .finalize           = stats_finalize,
    .key_destructor     = stats_key_destructor,
    .stamp              = stats_stamp,
    .setvals            = stats_setvals_3_rdpmc,
    .saveme             = stats_memcpy_saveme,
};

static struct zhpe_stats_ops stats_ops_4_rdpmc = {
    .open               = stats_open,
    .close              = stats_close,
    .enable             = stats_enable,
    .disable            = stats_disable,
    .get_zhpe_stats     = get_zhpe_stats,
    .stop_all           = stats_nop_stats,
    .pause_all          = stats_nop_stats,
    .restart_all        = stats_nop_void,
    .start              = stats_start,
    .stop               = stats_stop,
    .pause              = stats_nop_stats_uint32,
    .finalize           = stats_finalize,
    .key_destructor     = stats_key_destructor,
    .stamp              = stats_stamp,
    .setvals            = stats_setvals_4_rdpmc,
    .saveme             = stats_memcpy_saveme,
};

static struct zhpe_stats_ops stats_ops_5_rdpmc = {
    .open               = stats_open,
    .close              = stats_close,
    .enable             = stats_enable,
    .disable            = stats_disable,
    .get_zhpe_stats     = get_zhpe_stats,
    .stop_all           = stats_nop_stats,
    .pause_all          = stats_nop_stats,
    .restart_all        = stats_nop_void,
    .start              = stats_start,
    .stop               = stats_stop,
    .pause              = stats_nop_stats_uint32,
    .finalize           = stats_finalize,
    .key_destructor     = stats_key_destructor,
    .stamp              = stats_stamp,
    .setvals            = stats_setvals_5_rdpmc,
    .saveme             = stats_memcpy_saveme,
};

static struct zhpe_stats_ops stats_ops_6_rdpmc = {
    .open               = stats_open,
    .close              = stats_close,
    .enable             = stats_enable,
    .disable            = stats_disable,
    .get_zhpe_stats     = get_zhpe_stats,
    .stop_all           = stats_nop_stats,
    .pause_all          = stats_nop_stats,
    .restart_all        = stats_nop_void,
    .start              = stats_start,
    .stop               = stats_stop,
    .pause              = stats_nop_stats_uint32,
    .finalize           = stats_finalize,
    .key_destructor     = stats_key_destructor,
    .stamp              = stats_stamp,
    .setvals            = stats_setvals_6_rdpmc,
    .saveme             = stats_memcpy_saveme,
};

static void init_zhpe_stats_profile(__u32 petype, int count, ...)
{
    va_list args;
    va_start(args, count);

    zhpe_stats_fd_list = calloc(count, sizeof(int));
    zhpe_stats_cntr_list = calloc(count, sizeof(uint64_t));
    zhpe_stats_config_list = calloc(count, sizeof(uint64_t));

    struct perf_event_attr pe;

    int         err;
    void        *addr;
    uint64_t    index, offset;
    __u64       peconfig;

    struct perf_event_mmap_page * buf;

    for (int i=0; i<count; i++)
    {
        peconfig = va_arg(args, __u64);

        memset(&pe, 0, sizeof(struct perf_event_attr));
        pe.size = sizeof(struct perf_event_attr);
        pe.type = petype;
        pe.config = peconfig;
        pe.exclude_kernel = 1;

        zhpe_stats_config_list[zhpe_stats_num_counters] = peconfig;

        zhpe_stats_fd_list[zhpe_stats_num_counters] = my_perf_event_open(&pe, 0, -1, -1, 0);
        if (zhpe_stats_fd_list[zhpe_stats_num_counters] < 0) {
            err = errno;
            fprintf(stderr, "Error zhpe_stats_fd == %d; my_perf_event_open %llx returned error %d:%s\n", zhpe_stats_fd_list[zhpe_stats_num_counters], pe.config,
                err, strerror(err));
            exit(EXIT_FAILURE);
        }

        addr = mmap(NULL, 4096, PROT_READ, MAP_SHARED, zhpe_stats_fd_list[zhpe_stats_num_counters], 0);
        if (addr == (void *)(-1)) {
            err = errno;
            fprintf(stderr, "Error mmap() syscall returned%llx\n", (unsigned long long)addr);
            exit(EXIT_FAILURE);
        }

        buf = (struct perf_event_mmap_page *) addr;
        index = buf->index;
        if (index < 0) {
            fprintf(stderr, "Error: buf->index == %lu\n", index);
            exit(EXIT_FAILURE);
        }
        offset = buf->offset;

        zhpe_stats_cntr_list[zhpe_stats_num_counters++] = index + offset;
    }
    va_end(args);
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
    print_err("ZHPE_STATS_PROFILE set to %s.\n",tmp);

    if (tmp == NULL)
        zhpe_stats_profile = ZHPE_STATS_CPU;

    if (!strcmp("cpu",tmp)) {
            zhpe_stats_profile = ZHPE_STATS_CPU;
            perf_typeid = PERF_TYPE_HARDWARE;
            zhpe_stats_ops = &stats_ops_6_rdpmc;
            init_zhpe_stats_profile(PERF_TYPE_HARDWARE,6,PERF_COUNT_HW_INSTRUCTIONS,PERF_COUNT_HW_CPU_CYCLES,PERF_COUNT_HW_BRANCH_INSTRUCTIONS,PERF_COUNT_HW_BRANCH_MISSES,PERF_COUNT_HW_STALLED_CYCLES_FRONTEND,PERF_COUNT_HW_STALLED_CYCLES_BACKEND);
    } else {
        if (!strcmp("raw_l2_dc",tmp)) {
            zhpe_stats_profile = ZHPE_STATS_L2_DC;
            zhpe_stats_ops = &stats_ops_6_rdpmc;
            perf_typeid = PERF_TYPE_RAW;
            init_zhpe_stats_profile(perf_typeid, 6, ALL_DC_ACCESSES, ALL_L2_CACHE_MISSES1, ALL_L2_CACHE_MISSES2, ALL_L2_CACHE_MISSES3,L2_CACHE_ACCESS_FROM_DC_MISS_INCLUDING_PREFETCH,L2_CACHE_MISS_FROM_DC_MISS);
        } else {
        if (!strcmp("hwc_l1_dc",tmp)) {
            zhpe_stats_profile = ZHPE_STATS_L1_DC;
            zhpe_stats_ops = &stats_ops_5_rdpmc;
            perf_typeid = PERF_TYPE_RAW;
            init_zhpe_stats_profile(perf_typeid, 5, L1_DTLB_MISSES, L2_DTLB_MISSES_AND_PAGE_WALK, L1_DC_READ_MISS, L1_DC_WRITE_MISS, L1_DC_PREFETCH_MISS);
        } else {
        if (!strcmp("shut-up-compiler",tmp)) {
            zhpe_stats_ops = &stats_ops_0_rdpmc;
            zhpe_stats_ops = &stats_ops_2_rdpmc;
            zhpe_stats_ops = &stats_ops_3_rdpmc;
            zhpe_stats_ops = &stats_ops_4_rdpmc;
            zhpe_stats_ops = &stats_ops_5_rdpmc;
        } else {
            print_err("%s,%u: ZHPE_STATS_PROFILE set but not valid.\n", __func__, __LINE__);
            zhpe_stats_profile = ZHPE_STATS_CPU;
            perf_typeid = PERF_TYPE_HARDWARE;
            zhpe_stats_ops = &stats_ops_6_rdpmc;
            init_zhpe_stats_profile(PERF_TYPE_HARDWARE,6,PERF_COUNT_HW_INSTRUCTIONS,PERF_COUNT_HW_CPU_CYCLES,PERF_COUNT_HW_BRANCH_INSTRUCTIONS,PERF_COUNT_HW_BRANCH_MISSES,PERF_COUNT_HW_STALLED_CYCLES_FRONTEND,PERF_COUNT_HW_STALLED_CYCLES_BACKEND);
        }
    }}}

    zhpe_stats_buf_count=0;
    tmp = getenv("ZHPE_STATS_BUF_COUNT");
    if (tmp != NULL)
        zhpe_stats_buf_count=atoi(tmp);

    if (zhpe_stats_buf_count & (zhpe_stats_buf_count -1)) {
        zhpe_stats_buf_count = pow(2,ceil(log(zhpe_stats_buf_count)/log(2)));
        print_err("%s,%u: rounded ZHPE_STATS_BUF_COUNT up to a power of 2: %lu\n",
                  __func__, __LINE__, zhpe_stats_buf_count);
    }

    if ((zhpe_stats_buf_count <= 0) || (zhpe_stats_buf_count > ZHPE_STATS_BUF_COUNT_MAX)) {
        zhpe_stats_buf_count=ZHPE_STATS_BUF_COUNT_MAX;
        print_err("%s,%u: ZHPE_STATS_BUF_COUNT not in range. Setting to %lu.\n", __func__, __LINE__, zhpe_stats_buf_count);
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
