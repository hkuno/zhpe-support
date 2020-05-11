/*
 * Copyright (C) 2017-2020 Hewlett Packard Enterprise Development LP.
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

/*
 * John said that the goal is to fill the command buffers and send them without
 * involving the command queue.
 *
 *      - Unidirectional
 *      - Instead of reserving entries, fill them all and leave them populated.
 *      - At set up time, allocate the command buffer (zhpeq_tq_alloc) and
 *        then populate all the wq entries, using zhpe_tq_puti.
 *      - Make ztq_write check if the tail is 1/2 the size of the array
 *        and do a zhpeq_tq_commit if so.
 *      - Keep size < ZHPEQ_MAX_IMM
 *      - Clean up options/usage/extraneous functions
 *      - Launch servers and clients separately using MPI, and set port number to
 *        initial port number + rank id.
 *      - After that works, then launch servers and clients at once
 *        using MPI, and set port number to port number + rank id%(worldsize/2)
 *      - Use MPI instead of socket communication.
 *      - Use a single server (rank 0).
 *      - Delete extraneous args.
 *      - Simplify:
 *          - Server needs only to allocate/share receive queue.
 *          - Client does not need transmit nor receive queue.
 *          - Client does not need transmit nor receive queue.
 *          - Server needs tell clients the sa.
 *      - Have rank 0 allocate a single buffer big enough for everyone,
 *        broadcast the information, and have the other ranks compute their spot.
 *        Exchange addresses and insert the remote address in the domain.
 *
 *      - make stride a parameter
 *
 * TIME:
 *      John Byrne:
 *        - Take the do_barrier function in mpi_barrier and use it to
 *          measure the skew of your barrier.
 *        - Take a look in mpi_zbw to see how MPI_Comm_split gets used.
 *        - You can make a communicator for 1-64 to synchronize the start
 *          and use MPI_COMM_WORLD for the end.
 *        - Also sample the wall clock after the cycles at the end of
 *          each rank and sort and show the maximum skew.
 *          Instead of passing ops_completed to cycles_to_usec() pass in 1
 *          to get the wall clock time and adjust the ops-per-second
 *          calculation accordingly.
 *          Print out both versions of the time from the cycles and the clock.
 *          Look in the tqinfo and print out the slice and queue.
 */

#include <zhpeq.h>
#include <zhpeq_util.h>

#include <sys/queue.h>

#include <mpi.h>

#define BACKLOG         (10)
#ifdef DEBUG
#define TIMEOUT         (-1)
#else
#define TIMEOUT         (10000)
#endif
#define WARMUP_MIN      (1024)
#define RX_WINDOW       (64)
#define TX_WINDOW       (64)
#define L1_CACHELINE    ((size_t)64)
#define ZTQ_LEN         (1023)

/* global variables */
int  my_rank;
int  worldsize;

#define MPI_CALL(_func, ...)                                    \
do {                                                            \
    int                 __rc = _func(__VA_ARGS__);              \
                                                                \
    if (unlikely(__rc != MPI_SUCCESS)) {                        \
        zhpeu_print_err("%s,%u:%d:%s() returned %d\n",          \
                        __func__, __LINE__, my_rank, #_func,    \
                        __rc);                                  \
        MPI_Abort(MPI_COMM_WORLD, 1);                           \
    }                                                           \
} while (0)

static struct zhpeq_attr zhpeq_attr;

/* server-only */
struct rx_queue {
    STAILQ_ENTRY(rx_queue) list;
    union {
        void            *buf;
        uint64_t        idx;
    };
};

/* server-only */
STAILQ_HEAD(rx_queue_head, rx_queue);

struct args {
    uint64_t            bufaddr;
    uint64_t            ring_entry_len;
    uint64_t            ring_entries;
    uint64_t            ring_ops;
    uint32_t            stride;
    bool                once_mode;
};

struct stuff {
    const struct args   *args;
    struct zhpeq_dom    *zqdom;
    struct zhpeq_tq     *ztq;
    struct zhpeq_rq     *zrq;
    struct zhpeq_key_data *ztq_local_kdata;  /* server-only */
    struct zhpeq_key_data *ztq_remote_kdata; /* client-only */
    uint64_t            ztq_remote_rx_zaddr; /* client-only */
    void                *rx_addr;            /* server-only */
    uint64_t            ops_completed;       /* client-only */
    size_t              ring_entry_aligned;
    size_t              ring_ops;
    size_t              ring_end_off;
    uint32_t            cmdq_entries;        /* server-only */
    void                *addr_cookie;
};

static void stuff_free(struct stuff *stuff)
{
    if (!stuff)
        return;

    if (stuff->ztq) {
        zhpeq_qkdata_free(stuff->ztq_remote_kdata);
        zhpeq_qkdata_free(stuff->ztq_local_kdata);
    }
    if (my_rank > 0) {
        zhpeq_domain_remove_addr(stuff->zqdom, stuff->addr_cookie);
        if (stuff->zrq)
            zhpeq_rq_free(stuff->zrq);
    } else {
        zhpeq_tq_free(stuff->ztq);
    }
    zhpeq_domain_free(stuff->zqdom);

    if ((my_rank == 0) && (stuff->rx_addr))
        munmap(stuff->rx_addr, stuff->ring_end_off);
}

/* server allocates and broadcasts ztq_remote_rx_addr to all clients */
static int do_mem_setup(struct stuff *conn)
{
    int                 ret;
    char                blob[ZHPEQ_MAX_KEY_BLOB];
    size_t              blob_len;
    uint64_t            ztq_remote_rx_addr;

    const struct args   *args = conn->args;
    size_t              mask = L1_CACHELINE - 1;
    size_t              req;
    union zhpe_hw_wq_entry *wqe;
    int                 i;

    ret = -EEXIST;

    /* everyone needs this */
    conn->ring_entry_aligned = (args->ring_entry_len + mask) & ~mask;

    /* only server sets up rx_addr */
    if (my_rank == 0) {
        req = conn->ring_entry_aligned * conn->cmdq_entries * (worldsize - 1);
        conn->ring_end_off = req;

        conn->rx_addr = mmap((void *)(uintptr_t)args->bufaddr, req,
                         PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED,
                         -1 , 0);

        ret = zhpeq_mr_reg(conn->zqdom, conn->rx_addr, req,
                           (ZHPEQ_MR_GET | ZHPEQ_MR_PUT |
                            ZHPEQ_MR_GET_REMOTE | ZHPEQ_MR_PUT_REMOTE),
                           &conn->ztq_local_kdata);

        blob_len = sizeof(blob);
        ret = zhpeq_qkdata_export(conn->ztq_local_kdata,
                                  conn->ztq_local_kdata->z.access,
                                  blob, &blob_len);
        if (ret < 0) {
                print_func_err(__func__, __LINE__, "zhpeq_qkdata_export", "", ret);
                goto done;
        }
    }

    MPI_CALL(MPI_Bcast, blob, ZHPEQ_MAX_KEY_BLOB, MPI_CHAR, 0, MPI_COMM_WORLD);

    MPI_CALL(MPI_Bcast, &blob_len, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);

    ztq_remote_rx_addr = (uintptr_t)conn->rx_addr;

    MPI_CALL(MPI_Bcast, &ztq_remote_rx_addr, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);

    /* only clients set up ztq_remote_rx_addr */
    if (my_rank > 0) {
        ztq_remote_rx_addr += conn->ring_entry_aligned *
                              conn->cmdq_entries * (my_rank - 1);
        ret = zhpeq_qkdata_import(conn->zqdom, conn->addr_cookie, blob, blob_len,
                              &conn->ztq_remote_kdata);
        if (ret < 0) {
            print_func_err(__func__, __LINE__, "zhpeq_qkdata_import", "", ret);
            goto done;
        }
        ret = zhpeq_zmmu_reg(conn->ztq_remote_kdata);
        if (ret < 0) {
            print_func_err(__func__, __LINE__, "zhpeq_zmmu_reg", "", ret);
            goto done;

        }
        ret = zhpeq_rem_key_access(conn->ztq_remote_kdata,
                               ztq_remote_rx_addr, conn->ring_end_off,
                               0, &conn->ztq_remote_rx_zaddr);
        if (ret < 0) {
            print_func_err(__func__, __LINE__, "zhpeq_rem_key_access",
                           "", ret);
            goto done;
        }

        if (my_rank > 0) {
            /* Loop and fill in my commmand buffers with zeros. */
            for (i =0; i<conn->cmdq_entries; i++) {
                wqe = &conn->ztq->wq[i];
                memset(zhpeq_tq_puti(wqe, 0, args->ring_entry_len,
                       conn->ztq_remote_rx_zaddr+(i*conn->ring_entry_aligned)),
                       0, args->ring_entry_len);
                wqe->hdr.cmp_index = i;
            }
        }
    }
 done:
    return ret;
}

static inline struct zhpe_cq_entry *tq_cq_entry(struct zhpeq_tq *ztq,
                                                uint32_t off)
{
    uint32_t            qmask = ztq->tqinfo.cmplq.ent - 1;
    uint32_t            qindex = ztq->cq_head + off;
    struct zhpe_cq_entry *cqe = zhpeq_q_entry(ztq->cq, qindex, qmask);

    /* likely() to optimize the success case. */
    if (likely(zhpeq_cmp_valid(cqe, qindex, qmask)))
        return cqe;

    return NULL;
}

static int ztq_completions(struct stuff *conn)
{
    ssize_t     ret = 0;
    struct      zhpe_cq_entry *cqe;
    uint32_t    mystride;

    mystride = min((uint32_t)conn->args->stride, (uint32_t)(conn->ztq->wq_tail_commit - conn->ztq->cq_head));
    if (unlikely(mystride == 0)) {
        return ret;
    }
    while ((cqe = tq_cq_entry(conn->ztq, mystride-1))) {
        /* unlikely() to optimize the no-error case. */
        if (unlikely(cqe->status != ZHPE_HW_CQ_STATUS_SUCCESS)) {
            ret = -EIO;
            print_err("ERROR: %s,%u:index 0x%x status 0x%x\n", __func__, __LINE__,
                      cqe->index, cqe->status);
        }
        conn->ztq->cq_head += mystride;
        conn->ops_completed += mystride;
        ret+=mystride;
    }

    return ret;
}

/* check for completions, send as many as we've got, and ring doorbell. */
static int ztq_write(struct zhpeq_tq *ztq)
{
    uint32_t            qmask;
    uint32_t            avail;

    qmask = ztq->tqinfo.cmdq.ent - 1;
    avail = qmask - (ztq->wq_tail_commit - ztq->cq_head);

    if (unlikely(avail > qmask / 2)) {
        ztq->wq_tail_commit += avail;
        qcmwrite64(ztq->wq_tail_commit & qmask,
                   ztq->qcm, ZHPE_XDM_QCM_CMD_QUEUE_TAIL_OFFSET);
    }

    return 0;
}

/* Use existing pre-populated command buffer. */
static int do_client_unidir(struct stuff *conn)
{
    int                 ret = 0;
    const struct args   *args = conn->args;
    uint64_t            start;
    uint64_t            now;
    double              cycletime;
    uint64_t            clocktime;
    struct timespec     start_clocktime;
    struct timespec     now_clocktime;

    /* time the barrier */
    MPI_CALL(MPI_Barrier, MPI_COMM_WORLD);

    clock_gettime(CLOCK_REALTIME, &start_clocktime);
    start = get_cycles(NULL);

    while (conn->ops_completed < args->ring_ops) {
        ret = ztq_write(conn->ztq);
        if (ret < 0)
            goto done;
        ret = ztq_completions(conn);
        if (ret < 0)
            goto done;
    }

    while ((int32_t)(conn->ztq->wq_tail_commit - conn->ztq->cq_head)  > 0) {
        ret = ztq_completions(conn);
        if (ret < 0)
            goto done;
    }

    now = get_cycles(NULL);
    clock_gettime(CLOCK_REALTIME, &now_clocktime);

    printf("queue size:%"PRIu32"\n",conn->ztq->tqinfo.cmdq.ent);
    cycletime = cycles_to_usec(now - start, 1);
    clocktime = ((uint64_t)now_clocktime.tv_sec * NSEC_PER_SEC + now_clocktime.tv_nsec) -
                ((uint64_t)start_clocktime.tv_sec * NSEC_PER_SEC + start_clocktime.tv_nsec); 

    printf("%s:ops count:%"PRIu64":;",appname,conn->ops_completed);
        printf("slice:%d:;",conn->ztq->tqinfo.slice);
        printf("queue:%d:;",conn->ztq->tqinfo.queue);
        printf("cycletime in usec:%.3f:;", cycletime);
        printf("clocktime in nsec:%"PRIu64":;", clocktime);
        printf("ops per usec cycletime:%.3f:;", conn->ops_completed/cycletime);
        printf("ops per usec clocktime:%"PRId64":;", conn->ops_completed/(clocktime/1000));
        printf("\n");
   done:
    return ret;
}

int do_queue_setup(struct stuff *conn)
{
    int                  ret;
    const struct args    *args = conn->args;
    struct sockaddr_zhpe sa;
    size_t               sa_len = sizeof(sa);
    uint32_t             ent_sum, ent_max;

    ret = -EINVAL;

    /* Allocate domain. */
    ret = zhpeq_domain_alloc(&conn->zqdom);
    if (ret < 0) {
        print_func_err(__func__, __LINE__, "zhpeq_domain_alloc", "", ret);
        goto done;
    }

    /* Only clients get a ztq. */
    if (my_rank > 0) {
        ret = zhpeq_tq_alloc(conn->zqdom, args->ring_entries,
                     args->ring_entries,
                     0, 0, 0,  &conn->ztq);
        if (ret < 0) {
            print_func_err(__func__, __LINE__, "zhpeq_tq_qalloc", "", ret);
            goto done;
        }
        conn->cmdq_entries = conn->ztq->tqinfo.cmplq.ent;
    }

    /* verify everyone's command queue is same size */
    MPI_CALL(MPI_Reduce, &conn->cmdq_entries, &ent_sum, 1, MPI_UINT32_T,
                        MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_CALL(MPI_Reduce, &conn->cmdq_entries, &ent_max, 1, MPI_UINT32_T,
                        MPI_MAX, 0, MPI_COMM_WORLD);

    if (my_rank == 0) {
        if ((ent_max * (worldsize -1))  != ent_sum) {
           print_func_err(__func__, __LINE__, "ent_min != ent_max", "", ent_sum);
           print_func_err(__func__, __LINE__, "ent_min != ent_max", "", ent_max);
           goto done;
        }
        conn->cmdq_entries = ent_max;

        /* only server gets a zrq */
        ret = zhpeq_rq_alloc(conn->zqdom, 1, 0, &conn->zrq);
        if (ret < 0) {
            print_func_err(__func__, __LINE__, "zhpeq_rq_qalloc", "", ret);
            goto done;
        }

        ret = zhpeq_rq_get_addr(conn->zrq, &sa, &sa_len);
        if (ret < 0)
            goto done;
    }

    /* server sends remote address to clients */
    MPI_CALL(MPI_Bcast, &sa_len, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);
    MPI_CALL(MPI_Bcast, &sa, (int)sa_len, MPI_BYTE, 0, MPI_COMM_WORLD);

    /* clients insert the remote address in the domain. */
    if (my_rank > 0) {
        ret = zhpeq_domain_insert_addr(conn->zqdom, &sa, &conn->addr_cookie);
        if (ret < 0) {
            print_func_err(__func__, __LINE__,
                           "zhpeq_domain_insert_addr", "", ret);
            goto done;
        }
    }

    /* server allocates memory and tells clients memory parameters . */
    /* clients set up and initialize memory. */
    ret = do_mem_setup(conn);
    if (ret < 0) {
        print_func_err(__func__, __LINE__, "do_mem_xchg", "", ret);
        goto done;
    }
 done:
    return ret;
}

static int do_server(const struct args *oargs)
{
    int                 ret;
    struct args         one_args = *oargs;
    struct args         *args = &one_args;
    struct stuff        conn = {
        .args           = args,
    };

    ret = do_queue_setup(&conn);
    if (ret < 0)
        goto done;

    MPI_CALL(MPI_Barrier, MPI_COMM_WORLD);
 done:
    stuff_free(&conn);

    if (ret >= 0)
        ret = (oargs->once_mode ? 1 : 0);

    return ret;
}

static int do_client(const struct args *args)
{
    int                 ret;
    struct stuff        conn = {
        .args           = args,
        .ring_ops       = args->ring_ops,
    };

    ret = do_queue_setup(&conn);
    if (ret < 0)
        goto done;

    ret = do_client_unidir(&conn);
    if (ret < 0)
        goto done;


 done:
    stuff_free(&conn);

    return ret;
}

static void usage(bool help) __attribute__ ((__noreturn__));

static void usage(bool help)
{
    print_usage(
        help,
        "Usage:%s [-o] [-b <address>] [-s stride]\n"
        "    <entry_len> <ring_entries> <op_count>\n"
        "All sizes may be postfixed with [kmgtKMGT] to specify the"
        " base units.\n"
        "Lower case is base 10; upper case is base 2.\n"
        "All three arguments required.\n"
        "Options:\n"
        " -b <address> : try to allocate buffer at address\n"
        " -o : run once and then server will exit\n"
        " -s <stride>: stride for checking completions\n",
        appname);

    if (help)
        zhpeq_print_tq_info(NULL);

    exit(help ? 0 : 255);
}

int main(int argc, char **argv)
{
    int                 ret = 1;
    struct args         args;
    int                 opt;
    int                 rc;
    uint64_t            stride64;

    MPI_CALL(MPI_Init, &argc,&argv);
    MPI_CALL(MPI_Comm_rank, MPI_COMM_WORLD, &my_rank);
    MPI_CALL(MPI_Comm_size, MPI_COMM_WORLD, &worldsize);

    args.once_mode = false;

    zhpeq_util_init(argv[0], LOG_INFO, false);

    rc = zhpeq_init(ZHPEQ_API_VERSION, &zhpeq_attr);
    if (rc < 0) {
        print_func_err(__func__, __LINE__, "zhpeq_init", "", rc);
        goto done;
    }

    if (argc == 1)
        usage(true);

    while ((opt = getopt(argc, argv, "b:o")) != -1) {

        switch (opt) {

        case 'b':
            if (args.bufaddr)
                usage(false);
            if (parse_kb_uint64_t(__func__, __LINE__, "bufaddr",
                                  optarg, &args.bufaddr, 0, 1,
                                  SIZE_MAX, PARSE_KB | PARSE_KIB) < 0)
                usage(false);
            break;

        case 'o':
            if (args.once_mode)
                usage(false);
            args.once_mode = true;
            break;


        case 's':
            if (args.stride)
                usage(false);
            if (parse_kb_uint64_t(__func__, __LINE__, "stride",
                                  optarg, &stride64, 0, 1,
                                  SIZE_MAX, PARSE_KB | PARSE_KIB) < 0)
                usage(false);
            if (stride64 > UINT32_MAX)
                usage(false);
            else
                args.stride=(uint32_t)(stride64);
            break;


        default:
            usage(false);

        }
    }

    if (! args.stride)
        args.stride=64;

    opt = argc - optind;

    if (opt != 3)
        usage(false);

    if (parse_kb_uint64_t(__func__, __LINE__, "entry_len",
                          argv[optind++], &args.ring_entry_len, 0,
                          sizeof(uint8_t), ZHPEQ_MAX_IMM,
                          PARSE_KB | PARSE_KIB) < 0 ||
               parse_kb_uint64_t(__func__, __LINE__, "ring_entries",
                          argv[optind++], &args.ring_entries, 0, 1,
                             SIZE_MAX, PARSE_KB | PARSE_KIB) < 0 ||
               parse_kb_uint64_t(__func__, __LINE__,
                          "op_counts",
                          argv[optind++], &args.ring_ops, 0, 1,
                          SIZE_MAX,
                          PARSE_KB | PARSE_KIB) < 0)
        usage(false);

    if (my_rank == 0) {
        if (do_server(&args) < 0)
            goto done;
    } else {
        if (do_client(&args) < 0)
            goto done;
    }

    ret = 0;

 done:
    MPI_CALL(MPI_Finalize);
    return ret;
}
