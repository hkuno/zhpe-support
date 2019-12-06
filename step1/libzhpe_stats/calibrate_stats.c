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
    int                 ret = 1;
    uint64_t            i;

    if (argc != 3) {
        usage(argv[0]);
        exit(-1);
    }

    zhpe_stats_init(argv[1], argv[2]);
    zhpe_stats_open(1);
    zhpe_stats_enable();

    /* 0 nops */
    for (i=0;i<10;i++) {
        zhpe_stats_start(0);
        zhpe_stats_stop(0);
    }

    /* 1 nop */
    for (i=0;i<10;i++) {
        zhpe_stats_start(1);
        NOP1;
        zhpe_stats_stop(1);
    }

    /* 2 nop */
    for (i=0;i<10;i++) {
        zhpe_stats_start(2);
        NOP1;
        NOP1;
        zhpe_stats_stop(2);
    }

    /* 3 nop */
    for (i=0;i<10;i++) {
        zhpe_stats_start(3);
        NOP1;
        NOP1;
        NOP1;
        zhpe_stats_stop(3);
    }


    /* 5 nop */
    for (i=0;i<10;i++) {
        zhpe_stats_start(5);
        NOP1;
        NOP1;
        NOP1;
        NOP1;
        NOP1;
        zhpe_stats_stop(5);
    }

    /* 10 nop */
    for (i=0;i<10;i++) {
        zhpe_stats_start(100);
        NOP10;
        zhpe_stats_stop(100);
    }

    /* 100 nop */
    for (i=0;i<10;i++) {
        zhpe_stats_start(100);
        NOP100;
        zhpe_stats_stop(100);
    }

    /* 1000 nop */
    for (i=0;i<10;i++) {
        zhpe_stats_start(1000);
        NOP1000;
        zhpe_stats_stop(1000);
    }



    zhpe_stats_stop_all();
    zhpe_stats_close();
    zhpe_stats_finalize();

    return ret;
}
