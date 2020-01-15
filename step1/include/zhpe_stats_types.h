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

#ifndef _ZHPE_STATS_TYPES_H_
#define _ZHPE_STATS_TYPES_H_

#include <stdbool.h>
#include <stdint.h>
#include <linux/perf_event.h>

#include <zhpe_externc.h>

_EXTERN_C_BEG

struct zhpe_stats_metadata {
    uint32_t    profileid;
    uint32_t    perf_typeid;
    int         config_count;
    uint64_t    config_list[6];
};


enum {

/* magic raw counters from John */
    L1_RESULT_ACCESS                                = 0x40,
    L1_RESULT_MISS                                  = 0xc860,
    DISPATCH_RESOURCE_STALL_CYCLES                  = 0xaf | ((0x8) << 8),

/* raw counters from PPR for AMD Family 17h Model 31h B0 */
    ALL_DC_ACCESSES                                 = 0x430729,

    ALL_L2_CACHE_ACCESSES1                          = 0x43F960,
    ALL_L2_CACHE_ACCESSES2                          = 0x431F70,
    ALL_L2_CACHE_ACCESSES3                          = 0x431F71,
    ALL_L2_CACHE_ACCESSES4                          = 0x431F72,

    L2_CACHE_ACCESS_FROM_IC_MISS_INCLUDING_PREFETCH = 0x431060,
    L2_CACHE_ACCESS_FROM_DC_MISS_INCLUDING_PREFETCH = 0x43C860,

    L2_CACHE_ACCESS_FROM_L2_HWPF1                   = 0x431F70,
    L2_CACHE_ACCESS_FROM_L2_HWPF2                   = 0x431F71,
    L2_CACHE_ACCESS_FROM_L2_HWPF3                   = 0x431F72,

    ALL_L2_CACHE_MISSES1                            = 0x430964,
    ALL_L2_CACHE_MISSES2                            = 0x431F71,
    ALL_L2_CACHE_MISSES3                            = 0x431F72,

    L2_CACHE_MISS_FROM_IC_MISS                      = 0x430164,

    L2_CACHE_MISS_FROM_DC_MISS                      = 0x430864,

    L2_CACHE_MISS_FROM_L2_HWPF1                     = 0x431F71,
    L2_CACHE_MISS_FROM_L2_HWPF2                     = 0x431F72,

    ALL_L2_CACHE_HITS1                              = 0x43F664,
    ALL_L2_CACHE_HITS2                              = 0x431F70,

    L2_CACHE_HIT_FROM_IC_MISS                       = 0x430664,

    L2_CACHE_HIT_FROM_DC_MISS                       = 0x437064,

    L2_CACHE_HIT_FROM_L2_HWPF                       = 0x431F70,

    L1_DTLB_MISSES                                  = 0x43FF45,
    L2_DTLB_MISSES_AND_PAGE_WALK                    = 0x43FF45,

    L1_DC_READ_MISS                                 = (PERF_COUNT_HW_CACHE_L1D) | (PERF_COUNT_HW_CACHE_OP_READ  << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16),

    L1_DC_WRITE_MISS                                = (PERF_COUNT_HW_CACHE_L1D) | (PERF_COUNT_HW_CACHE_OP_WRITE  << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16),

    L1_DC_PREFETCH_MISS                             = (PERF_COUNT_HW_CACHE_L1D) | (PERF_COUNT_HW_CACHE_OP_PREFETCH  << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16),

};

struct zhpe_stats_record {
    uint32_t    op_flag;
    uint32_t    subid;
    uint64_t    val0;
    uint64_t    val1;
    uint64_t    val2;
    uint64_t    val3;
    uint64_t    val4;
    uint64_t    val5;
    uint64_t    val6;
} CACHE_ALIGNED;

struct zhpe_stats_ops {
    struct zhpe_stats           *(*open)(uint16_t uid);
    void                        (*close)(void);
    void                        (*enable)(void);
    void                        (*disable)(void);
    struct zhpe_stats           *(*get_zhpe_stats)(void);
    void                        (*stop_all)(struct zhpe_stats *stats);
    void                        (*pause_all)(struct zhpe_stats *stats);
    void                        (*restart_all)(void);
    void                        (*start)(struct zhpe_stats *stats, uint32_t subid);
    void                        (*stop)(struct zhpe_stats *stats, uint32_t subid);
    void                        (*pause)(struct zhpe_stats * stats, uint32_t subid);
    void                        (*finalize)(void);
    void                        (*key_destructor)(void *vstats);
    void                        (*stamp)(struct zhpe_stats *stats, uint32_t subid,
                                         uint32_t items, uint64_t *data);
    void                        (*setvals)(struct zhpe_stats_record *rec);
    struct zhpe_stats_record    *(*nextslot)(struct zhpe_stats *stats);
    void                        (*saveme)(char *dest, char *src);
};

/* Ultimately these should be moved to other repos. */
enum {
    ZHPE_STATS_SUBID_RECV        = 20,
    ZHPE_STATS_SUBID_RMA         = 30,
    ZHPE_STATS_SUBID_SEND        = 40,
    ZHPE_STATS_SUBID_ZHPQ        = 50,
};

enum {
    ZHPE_STATS_START             = 1,
    ZHPE_STATS_STOP              = 2,
    ZHPE_STATS_PAUSE             = 3,
    ZHPE_STATS_RESUME            = 4,
    ZHPE_STATS_ENABLE            = 5,
    ZHPE_STATS_DISABLE           = 6,
    ZHPE_STATS_RESTART           = 7,
    ZHPE_STATS_STAMP             = 8,
    ZHPE_STATS_OPEN              = 9,
    ZHPE_STATS_CLOSE             = 10,
    ZHPE_STATS_FLUSH_START       = 11,
    ZHPE_STATS_FLUSH_STOP        = 12,
};


/* for measuring overheads */
enum {
    ZHPE_STATS_SUBID_STARTSTOP      = 1,
    ZHPE_STATS_SUBID_S_STAMP_S      = 2,
    ZHPE_STATS_SUBID_S_SS_S         = 3,
    ZHPE_STATS_SUBID_S_NOP_S        = 4,
    ZHPE_STATS_SUBID_S_AINC_S       = 5,
    ZHPE_STATS_SUBID_SS_NOP_SS      = 6,
    ZHPE_STATS_SUBID_SS_AINC_SS     = 7,
    ZHPE_STATS_SUBID_SS_SS_SS       = 8,
    ZHPE_STATS_SUBID_SSS_SS_SSS     = 9,
    ZHPE_STATS_SUBID_S_DCA_S        = 10,
};

enum {
    ZHPE_STATS_CARBON       = 100,
    ZHPE_STATS_RDTSCP       = 1000,
    ZHPE_STATS_CPU          = 2000,
    ZHPE_STATS_L1_DC        = 3000,
    ZHPE_STATS_L2_DC        = 4000,
};

_EXTERN_C_END

#endif /* _ZHPE_STATS_TYPES_H_ */
