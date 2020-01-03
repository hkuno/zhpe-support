/*
 * Copyright (C) 2017-2018 Hewlett Packard Enterprise Development LP.
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

#include <zhpe_stats.h>

/* Calibration SubIDs */
enum {
    ZHPE_STATS_SUBID_B2B         = 1,
    ZHPE_STATS_SUBID_NOP         = 2,
    ZHPE_STATS_SUBID_ATM_INC     = 3,
    ZHPE_STATS_SUBID_STAMP       = 4,
    ZHPE_STATS_SUBID_START       = 5,
    ZHPE_STATS_SUBID_STARTSTOP   = 6,
};


#define NOP1    \
do {            \
    nop();      \
} while (0)

#define NOP10    \
do {            \
    NOP1;       \
    NOP1;       \
    NOP1;       \
    NOP1;       \
    NOP1;       \
    NOP1;       \
    NOP1;       \
    NOP1;       \
    NOP1;       \
    NOP1;       \
} while (0)

#define NOP100    \
do {            \
    NOP10;       \
    NOP10;       \
    NOP10;       \
    NOP10;       \
    NOP10;       \
    NOP10;       \
    NOP10;       \
    NOP10;       \
    NOP10;       \
    NOP10;       \
} while (0)

#define NOP1000    \
do {            \
    NOP100;       \
    NOP100;       \
    NOP100;       \
    NOP100;       \
    NOP100;       \
    NOP100;       \
    NOP100;       \
    NOP100;       \
    NOP100;       \
    NOP100;       \
} while (0)


static void usage(char * arg0)
{
    printf("Usage: %s <statdir> <basefilename>\n",arg0);
}

int main(int argc, char **argv)
{
    bool                 ret = 1;

    if (argc != 3) {
        usage(argv[0]);
        exit(-1);
    }

    ret = zhpe_stats_init(argv[1], argv[2]);
    if (! ret) {
        usage(argv[0]);
        exit(ret);
    }

    zhpe_stats_open(1);
    zhpe_stats_enable();

    zhpe_stats_calibrate_cpu_b2b(ZHPE_STATS_CALIBRATE,ZHPE_STATS_SUBID_B2B);
    zhpe_stats_calibrate_cpu_nop(ZHPE_STATS_CALIBRATE,ZHPE_STATS_SUBID_NOP);
    zhpe_stats_calibrate_cpu_atm_inc(ZHPE_STATS_CALIBRATE,ZHPE_STATS_SUBID_ATM_INC);
    zhpe_stats_calibrate_cpu_stamp(ZHPE_STATS_CALIBRATE,ZHPE_STATS_SUBID_STAMP);
    zhpe_stats_calibrate_cpu_start(ZHPE_STATS_CALIBRATE,ZHPE_STATS_SUBID_START);
    zhpe_stats_calibrate_cpu_startstop(ZHPE_STATS_CALIBRATE,ZHPE_STATS_SUBID_STARTSTOP);

    //zhpe_stats_test_saveme(888,8);

    zhpe_stats_stop_all();
    zhpe_stats_close();

    zhpe_stats_test(2);

    zhpe_stats_finalize();

    ret = 0;

    return ret;
}

