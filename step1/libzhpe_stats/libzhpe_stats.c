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
    struct zhpe_stats_record    *buffer;
    uint64_t                    *sim_buf;
    uint32_t                    num_slots;
    int                         fd;
    uint16_t                    uid;
    size_t                      head;
    uint8_t                     state:4;
    uint8_t                     enabled:1;
};

static void stats_nop_stamp(uint32_t dum,
                            uint64_t dum1,
                            uint64_t dum2,
                            uint64_t dum3,
                            uint64_t dum4,
                            uint64_t dum5,
                            uint64_t dum6)
{
}

static void stats_nop_setvals(struct zhpe_stats_record *rec)
{
}

static void stats_nop_saveme(char *dest, char *src)
{
}

static void stats_nop_uint32(uint32_t dum)
{
}

static void stats_nop_void(void)
{
};

static void stats_minimal_open(uint16_t uid);

static struct zhpe_stats_ops zhpe_stats_nops = {
    .open               = stats_minimal_open,
    .close              = stats_nop_void,
    .enable             = stats_nop_void,
    .disable            = stats_nop_void,
    .pause_all          = stats_nop_void,
    .restart_all        = stats_nop_void,
    .stop_all           = stats_nop_void,
    .start              = stats_nop_uint32,
    .stop               = stats_nop_uint32,
    .finalize           = stats_nop_void,
    .stamp              = stats_nop_stamp,
    .setvals            = stats_nop_setvals,
    .saveme             = stats_nop_saveme,
};


__thread struct zhpe_stats_ops *zhpe_stats_ops = &zhpe_stats_nops;
__thread struct zhpe_stats_ops *saved_zhpe_stats_ops = &zhpe_stats_nops;
__thread struct zhpe_stats_ops *disabled_zhpe_stats_ops = &zhpe_stats_nops;

__thread struct zhpe_stats *zhpe_stats = NULL;

#ifdef HAVE_ZHPE_STATS
#include <hpe_sim_api_linux64.h>
#include <zhpe_stats.h>
#define ZHPE_STATS_BUF_COUNT_MAX 1048576

/* Common definitions/code */
static char             *zhpe_stats_dir;
static char             *zhpe_stats_unique;
static pthread_mutex_t  zhpe_stats_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t         zhpe_stats_buf_count;
static uint64_t         zhpe_stats_buf_mask;
static size_t           zhpe_stats_profile=0;

int zhpe_stats_num_counters=0;
int *zhpe_stats_fd_list;
uint64_t *zhpe_stats_cntr_list;
uint64_t *zhpe_stats_config_list;
__u32 perf_typeid=0;

/* forward declarations */
void zhpe_stats_flush();

static void stats_cmn_enable(void)
{
    if (!zhpe_stats)
        return;

    zhpe_stats->enabled = true;
    zhpe_stats_ops = saved_zhpe_stats_ops;
    zhpe_stats_restart_all();
}

static void stats_cmn_disable(void)
{
    zhpe_stats_pause_all();
    if (!zhpe_stats)
        return;

    zhpe_stats->enabled = false;
    zhpe_stats_ops = disabled_zhpe_stats_ops;
}

static void stats_cmn_finalize(void)
{
    free(zhpe_stats_dir);
    zhpe_stats_dir = NULL;
    free(zhpe_stats_unique);
    zhpe_stats_unique = NULL;
    zhpe_stats_ops = &zhpe_stats_nops;
}

static void stats_write_metadata()
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
    res = write(zhpe_stats->fd, &metadata, bufsize);
    if (check_func_ion(__func__, __LINE__, "write", bufsize, false,
                       bufsize, res, 0) < 0)
        abort();
}

/* overwrite when full */
/* todo: check compilation output for NDEBUG option when compile with 5 */
static struct zhpe_stats_record *stats_simple_nextslot()
{
    assert(zhpe_stats);
    assert(zhpe_stats->buffer);
    struct zhpe_stats_record *rec;

    assert(zhpe_stats->head < zhpe_stats->num_slots - 1);

    rec = &(zhpe_stats->buffer[(zhpe_stats_buf_mask & zhpe_stats->head++)]);

    return rec;
}

static uint64_t do_rdtscp()
{
    uint32_t            lo;
    uint32_t            hi;
    uint32_t            cpu;

    asm volatile("rdtscp\n\t": "=a" (lo), "=d" (hi), "=c" (cpu) : :);

    return ((uint64_t)hi << 32 | lo);
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
    rec->val1 = ((long long)cnt1low) | ((long long)cnt1high ) << 32;
    rdpmc(zhpe_stats_cntr_list[1], cnt2low, cnt2high);
    rec->val2 = ((long long)cnt2low) | ((long long)cnt2high ) << 32;
    rdpmc(zhpe_stats_cntr_list[2], cnt3low, cnt3high);
    rec->val3 = ((long long)cnt3low) | ((long long)cnt3high ) << 32;
    rdpmc(zhpe_stats_cntr_list[3], cnt4low, cnt4high);
    rec->val4 = ((long long)cnt4low) | ((long long)cnt4high ) << 32;
    rdpmc(zhpe_stats_cntr_list[4], cnt5low, cnt5high);
    rec->val5 = ((long long)cnt5low) | ((long long)cnt5high ) << 32;
    rdpmc(zhpe_stats_cntr_list[5], cnt6low, cnt6high);
    rec->val6 = ((long long)cnt6low) | ((long long)cnt6high ) << 32;
}

static void stats_setvals_hpe_sim(struct zhpe_stats_record *rec)
{
    //uint64_t len =  sizeof(uint64_t);
    ProcCtlData *foo;
    rec->val0 = do_rdtscp();
    foo = (void *)zhpe_stats->sim_buf;

    assert(zhpe_stats->sim_buf);
    int64_t ret;

    ret = sim_api_data_rec(DATA_REC_PAUSE, (uint16_t)zhpe_stats->uid,
                                        (uintptr_t)zhpe_stats->sim_buf);
    if (ret)
    {
        print_func_err(__func__, __LINE__, "sim_api_data_rec",
                       "DATA_REC_PAUSE", ret);
        abort();
    }

   // memcpy(&rec->val1, &foo->cpl0ExecInstTotal, len);
   // memcpy(&rec->val2, &foo->cpl3ExecInstTotal, len);
    rec->val1 = foo->cpl0ExecInstTotal;
    rec->val2 = foo->cpl3ExecInstTotal;

    ret = sim_api_data_rec(DATA_REC_START, (uint16_t)zhpe_stats->uid,
                                        (uintptr_t)zhpe_stats->sim_buf);
    if (ret)
    {
        print_func_err(__func__, __LINE__, "sim_api_data_rec",
                       "DATA_REC_START", ret);
        abort();
    }
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

static void stats_recordme_memcpy(uint32_t subid, uint32_t opflag)
{
    struct zhpe_stats_record *dest;
    struct zhpe_stats_record tmp;

    tmp.subid = subid;
    tmp.op_flag = opflag;

    dest = stats_simple_nextslot();
    zhpe_stats_ops->setvals(&tmp);
    stats_memcpy_saveme((char *)dest, (char *)&tmp);
}

// todo: compare writing directly to dest vs using saveme
static void stats_recordme(uint32_t subid, uint32_t opflag)
{
    struct zhpe_stats_record *dest;

    dest = stats_simple_nextslot();
    dest->subid = subid;
    dest->op_flag = opflag;

    zhpe_stats_ops->setvals(dest);
}

void zhpe_stats_test(uint16_t uid)
{
    zhpe_stats_open(uid);
    zhpe_stats_start(0);
    zhpe_stats_stop(0);
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
void zhpe_stats_flush()
{
    assert(zhpe_stats);
    assert(zhpe_stats->buffer);

    ssize_t     res;
    uint64_t    bufsize;

    assert(zhpe_stats->head < zhpe_stats->num_slots - 1);
    bufsize = (zhpe_stats_buf_mask & zhpe_stats->head)
                * (sizeof(struct zhpe_stats_record));
    io_wmb();
    res = write(zhpe_stats->fd, zhpe_stats->buffer, bufsize);
    if (check_func_ion(__func__, __LINE__, "write", bufsize, false,
                       bufsize, res, 0) < 0)
        abort();

    zhpe_stats->head = 0;
}

static void stats_close()
{
//printf("In stats_close\n");
    if (!zhpe_stats)
        return ;

    zhpe_stats->enabled = false;

 //   printf("about to free zhpe_stats\n");
    free(zhpe_stats);
    zhpe_stats = NULL;
}

static void rdpmc_stats_close()
{
//printf("IN rdpmc_stats_close\n");
    stats_recordme(0, ZHPE_STATS_CLOSE);
    zhpe_stats_flush(zhpe_stats);

    for (int i=0;i<zhpe_stats_num_counters;i++)
    {
        if (zhpe_stats_fd_list[i] != -1)
        {
           close(zhpe_stats_fd_list[i]);
           zhpe_stats_fd_list[i]=-1;
        }
    }
    stats_close();
}

static void sim_stats_close()
{
    int64_t ret;
//printf("IN sim_stats_close\n");
//printf("about to DATA_REC_END\n");
    ret=sim_api_data_rec(DATA_REC_END, zhpe_stats->uid,
                                       (uintptr_t)zhpe_stats->sim_buf);
    if (ret)
        print_func_err(__func__, __LINE__, "sim_api_data_rec",
                       "DATA_REC_END", ret);
    zhpe_stats_flush(zhpe_stats);
    stats_close();
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

/* cache alt profile */

static void stats_start_memcpy(uint32_t subid)
{
    stats_recordme_memcpy(subid, ZHPE_STATS_START);
}

static void stats_stop_memcpy(uint32_t subid)
{
    stats_recordme_memcpy(subid, ZHPE_STATS_STOP);
}

static void stats_stop_all_memcpy(uint32_t subid)
{
    stats_recordme(subid, ZHPE_STATS_STOP_ALL);
}


static void stats_start(uint32_t subid)
{
//printf("IN stats_start\n");
    stats_recordme(subid, ZHPE_STATS_START);
}

static void stats_stop(uint32_t subid)
{
//printf("IN stats_stop\n");
    stats_recordme(subid, ZHPE_STATS_STOP);
}

static void stats_pause_all(uint32_t subid)
{
//printf("IN stats_pause_all\n");
    stats_recordme(subid, ZHPE_STATS_PAUSE_ALL);
}

static void stats_restart_all(uint32_t subid)
{
//printf("IN stats_restart_all\n");
    stats_recordme(subid, ZHPE_STATS_RESTART_ALL);
}

static void stats_stop_all(uint32_t subid)
{
//printf("IN stats_stop_all\n");
    stats_recordme(subid, ZHPE_STATS_STOP_ALL);
}

/* generic */
static void stats_stamp(uint32_t subid,
                                    uint64_t d1,
                                    uint64_t d2,
                                    uint64_t d3,
                                    uint64_t d4,
                                    uint64_t d5,
                                    uint64_t d6)

{
    struct zhpe_stats_record    *dest;

    dest = stats_simple_nextslot();
    dest->subid = subid;
    dest->op_flag = ZHPE_STATS_STAMP;

    dest->val0 = do_rdtscp();
    dest->val1 = d1;
    dest->val2 = d2;
    dest->val3 = d3;
    dest->val4 = d4;
    dest->val5 = d5;
    dest->val6 = d6;
}

static struct zhpe_stats_ops stats_ops_rdpmc = {
    .open               = stats_minimal_open,
    .close              = rdpmc_stats_close,
    .enable             = stats_cmn_enable,
    .disable            = stats_cmn_disable,
    .pause_all          = stats_pause_all,
    .restart_all        = stats_restart_all,
    .stop_all           = stats_stop_all,
    .start              = stats_start,
    .stop               = stats_stop,
    .finalize           = stats_finalize,
    .stamp              = stats_stamp,
    .setvals            = stats_setvals_6_rdpmc,
    .saveme             = stats_memcpy_saveme,
};

static struct zhpe_stats_ops stats_ops_rdpmc_disabled = {
    .open               = stats_minimal_open,
    .close              = rdpmc_stats_close,
    .enable             = stats_cmn_enable,
    .disable            = stats_cmn_disable,
    .pause_all          = stats_nop_void,
    .restart_all        = stats_nop_void,
    .stop_all           = stats_nop_void,
    .start              = stats_nop_uint32,
    .stop               = stats_nop_uint32,
    .finalize           = stats_finalize,
    .stamp              = stats_nop_stamp,
    .setvals            = stats_nop_setvals,
    .saveme             = stats_nop_saveme,
};

static struct zhpe_stats_ops stats_ops_rdpmc_memcpy = {
    .open               = stats_minimal_open,
    .close              = rdpmc_stats_close,
    .enable             = stats_cmn_enable,
    .disable            = stats_cmn_disable,
    .pause_all          = stats_pause_all,
    .restart_all        = stats_restart_all,
    .stop_all           = stats_stop_all_memcpy,
    .start              = stats_start_memcpy,
    .stop               = stats_stop_memcpy,
    .finalize           = stats_finalize,
    .stamp              = stats_stamp,
    .setvals            = stats_setvals_6_rdpmc,
    .saveme             = stats_memcpy_saveme,
};

static struct zhpe_stats_ops stats_ops_hpe_sim_disabled = {
    .open               = stats_minimal_open,
    .close              = sim_stats_close,
    .enable             = stats_cmn_enable,
    .disable            = stats_cmn_disable,
    .pause_all          = stats_nop_void,
    .restart_all        = stats_nop_void,
    .stop_all           = stats_nop_void,
    .start              = stats_nop_uint32,
    .stop               = stats_nop_uint32,
    .finalize           = stats_finalize,
    .stamp              = stats_stamp,
    .setvals            = stats_setvals_hpe_sim,
    .saveme             = stats_memcpy_saveme,
};

static struct zhpe_stats_ops stats_ops_hpe_sim = {
    .open               = stats_minimal_open,
    .close              = sim_stats_close,
    .enable             = stats_cmn_enable,
    .disable            = stats_cmn_disable,
    .pause_all          = stats_pause_all,
    .restart_all        = stats_restart_all,
    .stop_all           = stats_stop_all,
    .start              = stats_start,
    .stop               = stats_stop,
    .finalize           = stats_finalize,
    .stamp              = stats_stamp,
    .setvals            = stats_setvals_hpe_sim,
    .saveme             = stats_memcpy_saveme,
};

static void init_rdpmc_profile(__u32 petype, int count, ...)
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

        zhpe_stats_fd_list[zhpe_stats_num_counters] =
                                        my_perf_event_open(&pe, 0, -1, -1, 0);
        if (zhpe_stats_fd_list[zhpe_stats_num_counters] < 0) {
            err = errno;
            print_func_err(__func__, __LINE__, "perf_event_open fail", "", err);
            exit(EXIT_FAILURE);
        }

        addr = mmap(NULL, 4096, PROT_READ, MAP_SHARED,
                    zhpe_stats_fd_list[zhpe_stats_num_counters], 0);
        if (addr == (void *)(-1)) {
            err = errno;
            print_func_err(__func__, __LINE__, "mmap() syscall fail", "", err);
            exit(EXIT_FAILURE);
        }

        buf = (struct perf_event_mmap_page *) addr;
        index = buf->index;
        if (index < 0) {
            print_func_err(__func__, __LINE__, "bad buf->index", "", err);
            exit(EXIT_FAILURE);
        }
        offset = buf->offset;

        zhpe_stats_cntr_list[zhpe_stats_num_counters++] = index + offset;
    }
    va_end(args);
    prctl(PR_TASK_PERF_EVENTS_ENABLE);
}


static void stats_common_open(uint16_t uid)
{
    char *fname = NULL;

    if (zhpe_stats_profile == 0)
    {
        print_func_err(__func__, __LINE__,
                       "zhpe_stats_profile is NULL", "", -1);
        abort();
    }

    if (zhpe_stats != NULL)
    {
        print_func_err(__func__, __LINE__,
                       "zhpe_stats_open called when already open", "", -1);
        abort();
    }

    zhpe_stats = calloc(1, sizeof(struct zhpe_stats));
    if (!zhpe_stats)
        abort();

    zhpe_stats->uid = uid;
    zhpe_stats->num_slots = zhpe_stats_buf_count;
    zhpe_stats->head = 0;
    zhpe_stats->buffer = malloc_cachealigned(zhpe_stats->num_slots *
                                            sizeof(struct zhpe_stats_record));

    zhpe_stats->fd = -1;
    if (zhpeu_asprintf(&fname, "%s/%s.%ld.%d",
                       zhpe_stats_dir, zhpe_stats_unique,
                       syscall(SYS_gettid), uid) == -1) {
        print_func_err(__func__, __LINE__, "zhpeu_asprintf", "", -ENOMEM);
        abort();
    }

    zhpe_stats->fd = open(fname, O_RDWR | O_CREAT | O_TRUNC,
                     S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (zhpe_stats->fd == -1) {
        print_func_err(__func__, __LINE__, "open", fname, -errno);
        abort();
    }

    stats_write_metadata();

    free(fname);
}

bool zhpe_stats_init(const char *stats_dir, const char *stats_unique)
{
    bool                ret = false;
    char                *tmp;
    FILE                *file = NULL;
    char                platform[10];

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

    if (tmp == NULL)
        tmp = "";

    print_err("Setting ZHPE_STATS_PROFILE to %s.\n",tmp);

    file = fopen("/sys/module/zhpe/parameters/platform", "r");
    if (!file) {
        ret = -errno;
        print_func_err(__func__, __LINE__, "fopen",
                                "/sys/module/zhpe/parameters/platform", ret);
        goto done;
    }
    if (!fgets(platform, sizeof(platform), file)) {
        ret = -EIO;
        print_func_err(__func__, __LINE__, "fgets",
                                "/sys/module/zhpe/parameters/platform", ret);
        goto done;
    }
    if (!strcmp(platform, "carbon\n"))
    {
        if (!strcmp("carbon",tmp)) {
            zhpe_stats_profile = ZHPE_STATS_CARBON;
        } else {
            print_func_err(__func__, __LINE__, "Invalid profile: ", tmp, ret);
            goto done;
        }
    } else {
    if (!strcmp("cpu",tmp)) {
            zhpe_stats_profile = ZHPE_STATS_CPU;
            perf_typeid = PERF_TYPE_RAW;
            init_rdpmc_profile(PERF_TYPE_RAW, 6,
                                RAW_PERF_COUNT_HW_RETIRED_INSTRUCTIONS,
                                RAW_PERF_COUNT_HW_RETIRED_BRANCH_INSTRUCTIONS,
                                RAW_PERF_COUNT_HW_CPU_CYCLES,
                                DISPATCH_RESOURCE_STALL_CYCLES0,
                                DISPATCH_RESOURCE_STALL_CYCLES1,
                                RAW_PERF_COUNT_HW_BRANCH_MISSES);
    } else {
        if (!strcmp("cache",tmp)) {
            zhpe_stats_profile = ZHPE_STATS_CACHE;
            perf_typeid = PERF_TYPE_RAW;
            init_rdpmc_profile(perf_typeid, 6,
                                ALL_DC_ACCESSES,
                                L2_CACHE_MISS_FROM_DC_MISS,
                                L2_CACHE_HIT_FROM_DC_MISS,
                                L2_CACHE_MISS_FROM_L2_HWPF1,
                                L2_CACHE_MISS_FROM_L2_HWPF2,
                                L2_CACHE_HIT_FROM_L2_HWPF);
        } else {
        if (!strcmp("cache2",tmp)) {
            zhpe_stats_profile = ZHPE_STATS_CACHE_ALT;
            perf_typeid = PERF_TYPE_RAW;
            init_rdpmc_profile(perf_typeid, 6,
                                    ALL_DC_ACCESSES,
                                    L2_CACHE_MISS_FROM_DC_MISS,
                                    L2_CACHE_HIT_FROM_DC_MISS,
                                    L2_CACHE_MISS_FROM_L2_HWPF1,
                                    L2_CACHE_MISS_FROM_L2_HWPF2,
                                    L2_CACHE_HIT_FROM_L2_HWPF);
        } else {
        if (!strcmp("off",tmp)) {
            print_err("%s,%u: Disabling zhpe-stats.\n", __func__, __LINE__);
            zhpe_stats_profile = ZHPE_STATS_DISABLED;
        } else {
            print_err("%s,%u: Disabling zhpe-stats.\n", __func__, __LINE__);
            zhpe_stats_profile = ZHPE_STATS_DISABLED;
        }
    }}}}

    zhpe_stats_buf_count=0;
    tmp = getenv("ZHPE_STATS_BUF_COUNT");
    if (tmp != NULL)
        zhpe_stats_buf_count=atoi(tmp);

    if (zhpe_stats_buf_count & (zhpe_stats_buf_count -1)) {
        zhpe_stats_buf_count = pow(2,ceil(log(zhpe_stats_buf_count)/log(2)));
        print_err("%s,%u: rounded up ZHPE_STATS_BUF_COUNT to: %lu\n",
                  __func__, __LINE__, zhpe_stats_buf_count);
    }

    if ((zhpe_stats_buf_count <= 0) ||
            (zhpe_stats_buf_count > ZHPE_STATS_BUF_COUNT_MAX))
    {
        zhpe_stats_buf_count=ZHPE_STATS_BUF_COUNT_MAX;
        print_err("%s,%u: Setting ZHPE_STATS_BUF_COUNT to %lu.\n",
                     __func__, __LINE__, zhpe_stats_buf_count);
    }

    zhpe_stats_buf_mask=zhpe_stats_buf_count - 1;

    zhpe_stats_dir = strdup_or_null(stats_dir);
    if (!zhpe_stats_dir)
        goto done;
    zhpe_stats_unique = strdup_or_null(stats_unique);
    if (!zhpe_stats_unique)
        goto done;

    ret = true;
 done:
    mutex_unlock(&zhpe_stats_mutex);

    return ret;
}


/* create recording entry and start collecting data for uid */
static void stats_sim_open(uint16_t uid)
{
    uint64_t                    len;
    int64_t ret;
//printf("about to DATA_REC_CREAT for uid %d\n", uid);
    ret=sim_api_data_rec(DATA_REC_CREAT, uid,
                                      (uintptr_t)&len);
    if (ret) {
        print_func_err(__func__, __LINE__, "sim_api_data_rec",
                       "DATA_REC_CREAT", ret);
        abort();
    }

    zhpe_stats->sim_buf = calloc(1,len);

//printf("about to DATA_REC_START\n");
    ret=sim_api_data_rec(DATA_REC_START, uid, (uintptr_t)zhpe_stats->sim_buf);
    if (ret) {
        print_func_err(__func__, __LINE__, "sim_api_data_rec",
                       "DATA_REC_START", ret);
        abort();
    }
}


static void stats_common_open(uint16_t uid);

static void stats_minimal_open(uint16_t uid)
{
    stats_common_open(uid);

    switch(zhpe_stats_profile) {

        case ZHPE_STATS_CARBON:
                                zhpe_stats_ops = &stats_ops_hpe_sim;
                                saved_zhpe_stats_ops = &stats_ops_hpe_sim;
                                disabled_zhpe_stats_ops = &stats_ops_hpe_sim_disabled;
                                stats_sim_open(uid);
                                break;
        case ZHPE_STATS_CPU:
        case ZHPE_STATS_CACHE:
                                zhpe_stats_ops = &stats_ops_rdpmc;
                                saved_zhpe_stats_ops = &stats_ops_rdpmc;
                                disabled_zhpe_stats_ops = &stats_ops_rdpmc_disabled;
                                break;
        case ZHPE_STATS_CACHE_ALT:
                                zhpe_stats_ops = &stats_ops_rdpmc_memcpy;
                                saved_zhpe_stats_ops = &stats_ops_rdpmc_memcpy;
                                disabled_zhpe_stats_ops = &stats_ops_rdpmc_disabled;
                                break;
        default:
                  print_func_err(__func__, __LINE__,
                       "zhpe_stats_profile is not set", "", -1);
                  abort();
    }
    zhpe_stats->enabled = true;
}

#else /* HAVE_ZHPE_STATS */

static void stats_minimal_open(uint16_t uid)
{
}

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
