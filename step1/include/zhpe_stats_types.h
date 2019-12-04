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

#include <zhpe_externc.h>

_EXTERN_C_BEG

struct zhpe_stats_metadata {
    uint32_t    profileid;
};

struct zhpe_stats_record {
    uint32_t    op_flag;
    uint32_t    subid;
    uint64_t    val1;
    uint64_t    val2;
    uint64_t    val3;
    uint64_t    val4;
    uint64_t    val5;
} CACHE_ALIGNED;

struct zhpe_stats_ops {
    struct zhpe_stats           *(*open)(uint16_t uid);
    void                        (*close)(void);
    void                        (*enable)(void);
    void                        (*disable)(void);
    struct zhpe_stats           *(*stop_counters)(void);
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

enum {
    ZHPE_STATS_SUBID_ZHPQ        = 40,
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
    ZHPE_STATS_STOP_COUNTERS     = 13,
};

enum {
    ZHPE_STATS_CARBON       = 0,
    ZHPE_STATS_RDTSCP       = 1000,
    ZHPE_STATS_CPU          = 2000,
    ZHPE_STATS_CACHE        = 3000,
};

_EXTERN_C_END

#endif /* _ZHPE_STATS_TYPES_H_ */
