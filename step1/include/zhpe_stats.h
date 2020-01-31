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

#ifndef _ZHPE_STATS_H_
#define _ZHPE_STATS_H_

#include <zhpeq_util.h>
#include <zhpe_stats_types.h>

_EXTERN_C_BEG

#ifdef HAVE_ZHPE_STATS

extern __thread struct zhpe_stats_ops *zhpe_stats_ops;
bool zhpe_stats_init(const char *stats_dir, const char *stats_unique);
void zhpe_stats_test(uint16_t uid);
void zhpe_stats_test_saveme(uint32_t opflag, uint32_t subid);

static inline void zhpe_stats_finalize(void)
{
    zhpe_stats_ops->finalize();
}

static inline void zhpe_stats_open(uint16_t uid)
{
    zhpe_stats_ops->open(uid);
}

static inline void zhpe_stats_close(void)
{
    zhpe_stats_ops->close();
}

static inline void zhpe_stats_pause_all(void)
{
    zhpe_stats_ops->pause_all();
}

static inline void zhpe_stats_restart_all(void)
{
    zhpe_stats_ops->restart_all();
}

static inline void zhpe_stats_stop_all(void)
{
    zhpe_stats_ops->stop_all();
}

static inline void zhpe_stats_start(uint32_t subid)
{
    zhpe_stats_ops->start(subid);
}

static inline void zhpe_stats_stop(uint32_t subid)
{
    zhpe_stats_ops->stop(subid);
}

static inline void zhpe_stats_stamp(uint32_t subid,
                                    uint64_t d1,
                                    uint64_t d2,
                                    uint64_t d3,
                                    uint64_t d4,
                                    uint64_t d5,
                                    uint64_t d6)
{
    zhpe_stats_ops->stamp(subid, d1, d2, d3, d4, d5, d6);
}


#define zhpe_stats_subid(_name, _id)            \
    ((ZHPE_STATS_SUBID_##_name * 1000) + _id)

#else

static inline bool zhpe_stats_init(const char *stats_dir,
                                   const char *stats_unique)
{
    return false;
}

#define zhpe_stats_test(uid)            do {} while (0)
#define zhpe_stats_finalize()           do {} while (0)
#define zhpe_stats_open(uid)            do {} while (0)
#define zhpe_stats_close()              do {} while (0)
#define zhpe_stats_pause_all()          do {} while (0)
#define zhpe_stats_restart_all()         do {} while (0)
#define zhpe_stats_stop_all()           do {} while (0)
#define zhpe_stats_start(subid)         do {} while (0)
#define zhpe_stats_stop(subid)          do {} while (0)
#define zhpe_stats_stamp(_subid, _v1, _v2, _v3, _v4, _v5, _v6)   do {} while (0)
#define zhpe_stats_subid(_name, _id)

#endif

_EXTERN_C_END

#endif /* _ZHPE_STATS_H_ */
