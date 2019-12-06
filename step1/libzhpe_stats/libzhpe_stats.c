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

static struct zhpe_stats_record *stats_nop_nextslot(struct zhpe_stats *stats)
{
  return NULL;
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
    .stop_counters      = stats_nop_null,
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
    .nextslot           = stats_nop_nextslot,
    .saveme             = stats_nop_saveme,
};

struct zhpe_stats_ops *zhpe_stats_ops = &zhpe_stats_nops;

#ifdef HAVE_ZHPE_STATS

#include <zhpe_stats.h>
#define ZHPE_STATS_BUF_COUNT_MAX 10000

/* Common definitions/code */

static char             *zhpe_stats_dir;
static char             *zhpe_stats_unique;
static pthread_key_t    zhpe_stats_key;
static pthread_mutex_t  zhpe_stats_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool             zhpe_stats_init_once;
static uint64_t         zhpe_stats_buf_count;
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


void zhpe_stats_test(uint16_t uid)
{

    zhpe_stats_open(uid);
    zhpe_stats_enable();


    zhpe_stats_start(0);
    zhpe_stats_stop(0);

    zhpe_stats_start(10);
    zhpe_stats_stop(10);

    zhpe_stats_start(20);
    zhpe_stats_pause(20);
    zhpe_stats_stop(20);

    zhpe_stats_start(30);
    nop();
    zhpe_stats_stop(30);

    zhpe_stats_start(40);
    nop();
    zhpe_stats_pause(40);
    zhpe_stats_start(40);
    nop();
    nop();
    zhpe_stats_stop(40);

    zhpe_stats_start(50);
    nop();
    nop();
    zhpe_stats_pause_all();
    zhpe_stats_restart_all();
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
    zhpe_stats_pause_all();
    nop();
    zhpe_stats_restart_all();
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
    zhpe_stats_pause(140);
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

static struct zhpe_stats_record *stats_simple_nextslot(struct zhpe_stats *stats)
{
    assert(stats);
    assert(stats->buffer);
    struct zhpe_stats_record *rec;

    assert(stats->head < stats->num_slots - 1);

    rec = &(stats->buffer[stats->head]);

    stats->head++;

    return rec;
}

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
}

static void stats_cpu_setvals(struct zhpe_stats_record *rec)
{
    unsigned int cnt2low, cnt2high;
    unsigned int cnt3low, cnt3high;

    rec->val1 = do_rdtscp();
    rdpmc(hw_instr_counter, cnt2low, cnt2high);
    rdpmc(cpu_cyc_counter, cnt3low, cnt3high);

    rec->val2 = ((long long)cnt2low) | ((long long)cnt2high ) << 32;
    rec->val3 = ((long long)cnt3low) | ((long long)cnt3high ) << 32;
    rec->val4 = 0;
    rec->val5 = 0;
}

static void stats_cache_setvals(struct zhpe_stats_record *rec)
{
    rec->val1 = do_rdtscp();
    rec->val2 = 0;
    rec->val3 = 0;
    rec->val4 = 0;
    rec->val5 = 0;
}

static void stats_recordme(struct zhpe_stats *stats, uint32_t subid, uint32_t opflag)
{
    struct zhpe_stats_record *dest;
    struct zhpe_stats_record tmp;

    tmp.subid = subid;
    tmp.op_flag = opflag;

    zhpe_stats_ops->setvals(&tmp);

    dest = zhpe_stats_ops->nextslot(stats);
    zhpe_stats_ops->saveme((char *)dest, (char *)&tmp);
}

static inline void stats_vmemcpy_saveme(char * dest, char * src)
{
    uint64_t len =  sizeof(struct zhpe_stats_record);
    __vmemcpy(movntdq, movntdqa, dest, src, len);
}

/* single thread, no need to lock */
void zhpe_stats_flush(struct zhpe_stats *stats)
{
    assert(stats);
    assert(stats->buffer);
    struct zhpe_stats_record *dest;

    ssize_t     res;
    uint64_t    bufsize;

    struct zhpe_stats_record tmp;

    zhpe_stats_ops->setvals(&tmp);
    tmp.subid = 0;
    tmp.op_flag = ZHPE_STATS_FLUSH_START;

    dest = &(stats->buffer[stats->head]);
    zhpe_stats_ops->saveme((char *)dest, (char *)&tmp);

    bufsize = sizeof(struct zhpe_stats_record) * stats->head;
    io_wmb();
    res = write(stats->fd, stats->buffer, bufsize);
    if (check_func_ion(__func__, __LINE__, "write", bufsize, false,
                       bufsize, res, 0) < 0)
        abort();

    stats->head = 0;

    zhpe_stats_ops->setvals(&tmp);
    tmp.op_flag = ZHPE_STATS_FLUSH_STOP;
    dest = &(stats->buffer[stats->head]);
    zhpe_stats_ops->saveme((char *)dest,(char *)&tmp);

    stats->head++;
}

/* don't really close. Just log an event and flush stats. */
static void stats_closeme(struct zhpe_stats *stats)
{
    if (!stats)
        return;

    stats_recordme(stats, 0, ZHPE_STATS_CLOSE);
    zhpe_stats_flush(stats);
}

static void stats_close()
{
    struct zhpe_stats   *stats;
    stats = pthread_getspecific(zhpe_stats_key);
    if (!stats)
        return ;
    stats_closeme(stats);
}

static void stats_key_destructor(void *vstats)
{
    struct zhpe_stats   *stats = vstats;

    if (!stats)
        return;

    if (!stats->enabled)
        return;

    stats_closeme(stats);
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

static void stats_start(struct zhpe_stats *stats, uint32_t subid)
{
    if (stats == NULL)
        zhpe_stats_open(0);
    stats->state = ZHPE_STATS_RUNNING;
    stats_recordme(stats, subid, ZHPE_STATS_START);
}

static void stats_stop(struct zhpe_stats *stats, uint32_t subid)
{
    if (stats == NULL)
        zhpe_stats_open(0);
    stats->state = ZHPE_STATS_STOPPED;
    stats_recordme(stats, subid, ZHPE_STATS_STOP);
}

static struct zhpe_stats *stats_stop_counters(void)
{
    struct zhpe_stats   *stats;
    stats = pthread_getspecific(zhpe_stats_key);
    if (!stats)
        return NULL;

    if (!stats->enabled)
        return NULL;

    // stats_recordme(stats, 0, ZHPE_STATS_STOP_COUNTERS);

    return stats;
}

static void stats_pause(struct zhpe_stats *stats, uint32_t subid)
{
    stats_recordme(stats, subid, ZHPE_STATS_PAUSE);
}

static void stats_pause_all(struct zhpe_stats *stats)
{
    stats_recordme(stats, 0, ZHPE_STATS_PAUSE);
}

static void stats_restart_all(void)
{
    struct zhpe_stats   *stats;
    stats = pthread_getspecific(zhpe_stats_key);
    if (!stats)
        return;

    stats_recordme(stats, 0, ZHPE_STATS_RESTART);
}

static void stats_stop_all(struct zhpe_stats *stats)
{
    stats_recordme(stats, 0, ZHPE_STATS_STOP);
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

static void stats_stamp()
{
    struct zhpe_stats   *stats;
    stats = pthread_getspecific(zhpe_stats_key);
    if (!stats)
        return;

    stats_recordme(stats, 0, ZHPE_STATS_DISABLE);
}

static struct zhpe_stats_ops stats_ops_perf_event = {
    .open               = stats_open,
    .close              = stats_close,
    .enable             = stats_enable,
    .disable            = stats_disable,
    .stop_counters      = stats_stop_counters,
    .stop_all           = stats_stop_all,
    .pause_all          = stats_pause_all,
    .restart_all        = stats_restart_all,
    .start              = stats_start,
    .stop               = stats_stop,
    .pause              = stats_pause,
    .finalize           = stats_finalize,
    .key_destructor     = stats_key_destructor,
    .stamp              = stats_stamp,
    .setvals            = stats_nop_setvals,
    .nextslot           = stats_nop_nextslot,
    .saveme             = stats_nop_saveme,
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
            zhpe_stats_ops = &stats_ops_perf_event;
            zhpe_stats_ops->setvals = stats_rdtscp_setvals;
            zhpe_stats_ops->nextslot = stats_flushing_nextslot;
            zhpe_stats_ops->saveme = stats_vmemcpy_saveme;
        }

        if (!strcmp("cpu",tmp)) {
            zhpe_stats_profile = ZHPE_STATS_CPU;
            zhpe_stats_ops = &stats_ops_perf_event;
            zhpe_stats_ops->setvals = stats_cpu_setvals;
            zhpe_stats_ops->nextslot = stats_flushing_nextslot;
            zhpe_stats_ops->saveme = stats_vmemcpy_saveme;
            init_cpu_profile();
        }

        if (!strcmp("cache",tmp)) {
            zhpe_stats_profile = ZHPE_STATS_CACHE;
            zhpe_stats_ops = &stats_ops_perf_event;
            zhpe_stats_ops->setvals = stats_cache_setvals;
            zhpe_stats_ops->nextslot = stats_simple_nextslot;
            zhpe_stats_ops->saveme = stats_vmemcpy_saveme;
        }
    }

    zhpe_stats_buf_count=0;
    tmp = getenv("ZHPE_STATS_BUF_COUNT");
    if (tmp != NULL)
        zhpe_stats_buf_count=atoi(tmp);

    if ((zhpe_stats_buf_count <= 0) || (zhpe_stats_buf_count > ZHPE_STATS_BUF_COUNT_MAX))
        zhpe_stats_buf_count=ZHPE_STATS_BUF_COUNT_MAX;

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

void zhpe_stats_test(uint16_t uid)
{
}

#endif /* HAVE_ZHPE_STATS */
