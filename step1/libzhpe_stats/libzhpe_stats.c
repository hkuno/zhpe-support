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

struct zhpe_stats_record {
    size_t    profileid;
    uint32_t    subid;
    uint32_t    op_flag;
    uint64_t    val1;
    uint64_t    val2;
    uint64_t    val3;
    uint64_t    val4;
    uint64_t    val5;
};

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

static void stats_nop_stamp(struct zhpe_stats *stats, uint32_t dum,
                            uint32_t dum2, uint64_t *dum3)
{
}

static void stats_nop_stats(struct zhpe_stats *stats)
{
}

static void stats_nop_stats_uint32(struct zhpe_stats *stats, uint32_t dum)
{
}

static void stats_nop_uint16(uint16_t dum)
{
}

static void stats_nop_void(void)
{
};

static void stats_nop_voidp(void *dum)
{
};

static struct zhpe_stats_ops zhpe_stats_nops = {
    .open               = stats_nop_uint16,
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
};

struct zhpe_stats_ops *zhpe_stats_ops = &zhpe_stats_nops;

#ifdef HAVE_ZHPE_STATS

#include <zhpe_stats.h>
#define ZHPE_STATS_BUF_COUNT_MAX 2064

/* Common definitions/code */

static char             *zhpe_stats_dir;
static char             *zhpe_stats_unique;
static pthread_key_t    zhpe_stats_key;
static pthread_mutex_t  zhpe_stats_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool             zhpe_stats_init_once;
static uint64_t         zhpe_stats_buf_count;
static size_t           zhpe_stats_profile;

/* forward declarations */
void zhpe_stats_flush(struct zhpe_stats *stats);
static struct zhpe_stats *zhpe_stats_recordme(uint32_t subid, uint32_t opflag);

static void stats_cmn_finalize(void)
{
    free(zhpe_stats_dir);
    zhpe_stats_dir = NULL;
    free(zhpe_stats_unique);
    zhpe_stats_unique = NULL;
    zhpe_stats_ops = &zhpe_stats_nops;
}

static void stats_open(uint16_t uid)
{
    char                *fname = NULL;
    struct zhpe_stats   *stats;

    stats = calloc(1, sizeof(*stats));
    if (!stats)
        abort();

    stats->uid = uid;
    stats->num_slots = zhpe_stats_buf_count;
    stats->head = 0;
    stats->buffer = calloc(stats->num_slots, sizeof(struct zhpe_stats_record));

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

    abort_posix(pthread_setspecific, zhpe_stats_key, stats);
    stats->state = ZHPE_STATS_STOPPED;
    stats->enabled = false;

    free(fname);
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

static struct zhpe_stats_record *next_slot(struct zhpe_stats *stats)
{
    assert(stats);
    assert(stats->buffer);
    struct zhpe_stats_record *rec;

    assert(stats->head < stats->num_slots - 1);

    rec = &(stats->buffer[stats->head]);

    stats->head++;

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

static void stats_set_vals(struct zhpe_stats_record *rec)
{
    switch(rec->profileid)
    {
        case ZHPE_STATS_RDTSCP:
            rec->val1 = do_rdtscp();
            break;
    }
}

static struct zhpe_stats *zhpe_stats_record(struct zhpe_stats *stats, uint32_t subid, uint32_t opflag)
{
    struct zhpe_stats_record *rec;

    rec = next_slot(stats);
    rec->profileid = zhpe_stats_profile;
    rec->subid = subid;
    rec->op_flag = opflag;
    stats_set_vals(rec);
    return(stats);
}

static struct zhpe_stats *zhpe_stats_recordme(uint32_t subid, uint32_t opflag)
{
    struct zhpe_stats   *stats;

    stats = pthread_getspecific(zhpe_stats_key);
    assert (stats != NULL);

    zhpe_stats_record(stats, subid, opflag);
    return(stats);
}

/* single thread, no need to lock */
void zhpe_stats_flush(struct zhpe_stats *stats)
{
    ssize_t     res;
    uint64_t    bufsize;

    zhpe_stats_record(stats, ZHPE_STATS_SUBID_ALL, ZHPE_STATS_FLUSH);

    bufsize = sizeof(struct zhpe_stats_record) * stats->head;
    res = write(stats->fd, stats->buffer, bufsize);
    if (check_func_ion(__func__, __LINE__, "write", bufsize, false,
                       bufsize, res, 0) < 0)
        abort();

    stats->head = 0;
}

/* don't really close. Just log an event and flush stats. */
static void stats_closeme(struct zhpe_stats *stats)
{
    if (!stats)
        return;

    zhpe_stats_record(stats, ZHPE_STATS_SUBID_ALL, ZHPE_STATS_CLOSE);
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
}

static void stats_start(struct zhpe_stats *stats, uint32_t subid)
{
    if (stats == NULL)
        zhpe_stats_open(0);
    stats->state = ZHPE_STATS_RUNNING;

    zhpe_stats_record(stats, subid, ZHPE_STATS_START);
}

static void stats_stop(struct zhpe_stats *stats, uint32_t subid)
{
    if (stats == NULL)
        zhpe_stats_open(0);
    stats->state = ZHPE_STATS_STOPPED;
    zhpe_stats_record(stats, subid, ZHPE_STATS_STOP);
}

static struct zhpe_stats *stats_stop_counters(void)
{
    struct zhpe_stats   *stats;
    stats = pthread_getspecific(zhpe_stats_key);
    if (!stats)
        return NULL;

    if (!stats->enabled)
        return NULL;

    zhpe_stats_record(stats, ZHPE_STATS_SUBID_ALL, ZHPE_STATS_STOP_COUNTERS);

    return stats;
}

static void stats_pause(struct zhpe_stats *stats, uint32_t subid)
{
    zhpe_stats_record(stats, subid, ZHPE_STATS_PAUSE);
}

static void stats_pause_all(struct zhpe_stats *stats)
{
    zhpe_stats_record(stats, ZHPE_STATS_SUBID_ALL, ZHPE_STATS_PAUSE);
}

static void stats_restart_all(void)
{
    zhpe_stats_recordme(ZHPE_STATS_SUBID_ALL, ZHPE_STATS_RESTART);
}

static void stats_stop_all(struct zhpe_stats *stats)
{
    zhpe_stats_record(stats, ZHPE_STATS_SUBID_ALL, ZHPE_STATS_STOP);
}

static void stats_enable()
{
    struct zhpe_stats   *stats;
    stats = pthread_getspecific(zhpe_stats_key);
    if (!stats)
        return;

    stats->enabled = true;

    zhpe_stats_recordme(ZHPE_STATS_SUBID_ALL, ZHPE_STATS_ENABLE);
}

static void stats_disable()
{
    struct zhpe_stats   *stats;
    stats = pthread_getspecific(zhpe_stats_key);
    if (!stats)
        return;

    stats->enabled = false;

    zhpe_stats_recordme(ZHPE_STATS_SUBID_ALL, ZHPE_STATS_DISABLE);
}

static void stats_stamp()
{
    zhpe_stats_recordme(ZHPE_STATS_SUBID_ALL, ZHPE_STATS_DISABLE);
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
};

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
        if (strcmp("rdtscp",tmp)) {
            zhpe_stats_profile = ZHPE_STATS_RDTSCP;
            zhpe_stats_ops = &stats_ops_perf_event;
            zhpe_stats_buf_count=0;
            tmp = getenv("ZHPE_STATS_BUF_COUNT");
            if (tmp != NULL)
                zhpe_stats_buf_count=atoi(tmp);
            if ((zhpe_stats_buf_count <= 0) || (zhpe_stats_buf_count > ZHPE_STATS_BUF_COUNT_MAX))
                zhpe_stats_buf_count=ZHPE_STATS_BUF_COUNT_MAX;
        }
    }

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
    print_err("%s,%u:libzhpe_stats built without stats support\n",
              __func__, __LINE__);
}

void zhpe_stats_test(uint16_t uid)
{
}

#endif /* HAVE_ZHPE_STATS */
