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
 *
 * */

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

/* restrict to 32 Bytes or less look for define. */

static struct zhpeq_attr zhpeq_attr;

struct cli_wire_msg {
    uint64_t            ring_entry_len;
    uint64_t            ring_entries;
    uint64_t            tx_avail;
    bool                aligned_mode;
    bool                copy_mode;
    bool                once_mode;
    bool                unidir_mode;
};

struct mem_wire_msg {
    uint64_t            ztq_remote_rx_addr;
};

struct rx_queue {
    STAILQ_ENTRY(rx_queue) list;
    union {
        void            *buf;
        uint64_t        idx;
    };
};

enum {
    TX_NONE = 0,
    TX_WARMUP,
    TX_RUNNING,
    TX_LAST,
};

STAILQ_HEAD(rx_queue_head, rx_queue);

struct args {
    const char          *node;
    const char          *baseservice;
    const char          *service;
    uint64_t            bufaddr;
    uint64_t            ring_entry_len;
    uint64_t            ring_entries;
    uint64_t            ring_ops;
/* remove later */
    uint64_t            tx_avail;
    uint64_t            warmup;
    bool                aligned_mode;
    bool                copy_mode;
    bool                once_mode;
    bool                seconds_mode;
    bool                unidir_mode;
};

struct stuff {
    const struct args   *args;
    struct zhpeq_dom    *zqdom;
    struct zhpeq_tq     *ztq;
    struct zhpeq_rq     *zrq;
    struct zhpeq_key_data *ztq_local_kdata;
    struct zhpeq_key_data *ztq_remote_kdata;
    uint64_t            ztq_remote_rx_zaddr;
    int                 sock_fd;
    void                *tx_addr;
    void                *rx_addr;
    uint64_t            *ring_timestamps;
    uint64_t            ops_completed;
    struct rx_queue     *rx_rcv;
    void                *rx_data;
    size_t              ring_entry_aligned;
    size_t              ring_ops;
    size_t              ring_warmup;
    size_t              ring_end_off;
    size_t              tx_avail;
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
    zhpeq_domain_remove_addr(stuff->zqdom, stuff->addr_cookie);
    zhpeq_rq_free(stuff->zrq);
    zhpeq_tq_free(stuff->ztq);
    zhpeq_domain_free(stuff->zqdom);

    free(stuff->rx_rcv);
    free(stuff->ring_timestamps);
    if (stuff->tx_addr)
        munmap(stuff->tx_addr, stuff->ring_end_off * 2);

    FD_CLOSE(stuff->sock_fd);
}

static int do_mem_setup(struct stuff *conn)
{
    int                 ret = -EEXIST;
    const struct args   *args = conn->args;
    size_t              mask = L1_CACHELINE - 1;
    size_t              req;
    size_t              off;

    if (args->aligned_mode)
        conn->ring_entry_aligned = (args->ring_entry_len + mask) & ~mask;
    else
        conn->ring_entry_aligned = args->ring_entry_len;

    /* Size of an array of entries plus a tail index. */
    req = conn->ring_entry_aligned * args->ring_entries;
    off = conn->ring_end_off = req;
    req *= 2;
    conn->tx_addr = mmap((void *)(uintptr_t)args->bufaddr, req,
                         PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED,
                         -1 , 0);
    if (conn->tx_addr == MAP_FAILED) {
         conn->tx_addr = NULL;
         print_func_errn(__func__, __LINE__, "mmap", req, false, ret);
         goto done;
    }
    memset(conn->tx_addr, TX_NONE, req);
    conn->rx_addr = VPTR(conn->tx_addr, off);

    ret = zhpeq_mr_reg(conn->zqdom, conn->tx_addr, req,
                       (ZHPEQ_MR_GET | ZHPEQ_MR_PUT |
                        ZHPEQ_MR_GET_REMOTE | ZHPEQ_MR_PUT_REMOTE),
                       &conn->ztq_local_kdata);
    if (ret < 0) {
        print_func_err(__func__, __LINE__, "zhpeq_mr_reg", "", ret);
        goto done;
    }

    req = sizeof(*conn->ring_timestamps) * args->ring_entries;
    ret = -posix_memalign((void **)&conn->ring_timestamps, page_size, req);
    if (ret < 0) {
        conn->ring_timestamps = NULL;
        print_func_errn(__func__, __LINE__, "posix_memalign", true,
                        req, ret);
        goto done;
    }

    if (!args->copy_mode)
        goto done;

    req = sizeof(*conn->rx_rcv) * args->ring_entries + conn->ring_end_off;
    ret = -posix_memalign((void **)&conn->rx_rcv, page_size, req);
    if (ret < 0) {
        conn->rx_rcv = NULL;
        print_func_errn(__func__, __LINE__, "posix_memalign", true,
                        req, ret);
        goto done;
    }
    conn->rx_data = (void *)(conn->rx_rcv + args->ring_entries);

 done:
    return ret;
}

static int do_mem_xchg(struct stuff *conn)
{
    int                 ret;
    char                blob[ZHPEQ_MAX_KEY_BLOB];
    struct mem_wire_msg mem_msg;
    size_t              blob_len;

    blob_len = sizeof(blob);
    ret = zhpeq_qkdata_export(conn->ztq_local_kdata,
                              conn->ztq_local_kdata->z.access, blob, &blob_len);
    if (ret < 0) {
        print_func_err(__func__, __LINE__, "zhpeq_qkdata_export", "", ret);
        goto done;
    }

    mem_msg.ztq_remote_rx_addr = htobe64((uintptr_t)conn->rx_addr);

    ret = sock_send_blob(conn->sock_fd, &mem_msg, sizeof(mem_msg));
    if (ret < 0)
        goto done;
    ret = sock_send_blob(conn->sock_fd, blob, blob_len);
    if (ret < 0)
        goto done;
    ret = sock_recv_fixed_blob(conn->sock_fd, &mem_msg, sizeof(mem_msg));
    if (ret < 0)
        goto done;
    ret = sock_recv_fixed_blob(conn->sock_fd, blob, blob_len);
    if (ret < 0)
        goto done;

    mem_msg.ztq_remote_rx_addr = be64toh(mem_msg.ztq_remote_rx_addr);

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
                               mem_msg.ztq_remote_rx_addr, conn->ring_end_off,
                               0, &conn->ztq_remote_rx_zaddr);
    if (ret < 0) {
        print_func_err(__func__, __LINE__, "zhpeq_rem_key_access",
                       "", ret);
        goto done;
    }

 done:
    return ret;
}

static int ztq_completions(struct stuff *conn)
{
    ssize_t             ret = 0;
    struct zhpe_cq_entry *cqe;
    struct zhpe_cq_entry cqe_copy;

    while ((cqe = zhpeq_tq_cq_entry(conn->ztq))) {
        /* unlikely() to optimize the no-error case. */
        if (unlikely(cqe->status != ZHPE_HW_CQ_STATUS_SUCCESS)) {
            cqe_copy = *cqe;
            zhpeq_tq_cq_entry_done(conn->ztq, cqe);
            ret = -EIO;
            print_err("ERROR: %s,%u:index 0x%x status 0x%x\n", __func__, __LINE__,
                      cqe_copy.index, cqe_copy.status);
            break;
        }
        conn->ztq->cq_head++;
        conn->ops_completed++;
        ret++;
    }

    return ret;
}

/* check for completions, send as many as we've got, and ring doorbell. */
static int ztq_write(struct zhpeq_tq *ztq )
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

/* Instead of filling the command buffer, just use existing entries. */
static int do_client_unidir(struct stuff *conn)
{
    int                 ret = 0;
    const struct args   *args = conn->args;
    uint64_t            start;
    uint64_t            now;
    double              optime;

    MPI_Barrier(MPI_COMM_WORLD);

    start = get_cycles(NULL);

    while (conn->ops_completed < args->ring_ops) {
        ret = ztq_completions(conn);
        ret = ztq_write(conn->ztq);
    }
    while (conn->ztq->wq_tail_commit - conn->ztq->cq_head  > 0)
        ret = ztq_completions(conn);

    now = get_cycles(NULL);

    zhpeq_print_tq_info(conn->ztq);
    optime = cycles_to_usec(now - start, conn->ops_completed);
    printf("%s:time in usec:%.3f; ops count:%"PRIu64"; ops per sec:%.3f\n",
               appname,
               optime,
               conn->ops_completed,
               1000000.0/optime
              );
    return ret;
}

int do_ztq_setup(struct stuff *conn)
{
    int                 i;
    int                 ret;
    const struct args   *args = conn->args;
    union sockaddr_in46 sa;
    size_t              sa_len = sizeof(sa);

    union zhpe_hw_wq_entry *wqe;

    ret = -EINVAL;
    conn->tx_avail = args->tx_avail;
    if (conn->tx_avail) {
        if (conn->tx_avail > zhpeq_attr.z.max_tx_qlen)
            goto done;
    } else
        conn->tx_avail = ZTQ_LEN;

    /* Allocate domain. */
    ret = zhpeq_domain_alloc(&conn->zqdom);
    if (ret < 0) {
        print_func_err(__func__, __LINE__, "zhpeq_domain_alloc", "", ret);
        goto done;
    }
    /* Allocate zqueues. */
    ret = zhpeq_tq_alloc(conn->zqdom, conn->tx_avail + 1, conn->tx_avail + 1,
                         0, 0, 0,  &conn->ztq);
    if (ret < 0) {
        print_func_err(__func__, __LINE__, "zhpeq_tq_qalloc", "", ret);
        goto done;
    }
    ret = zhpeq_rq_alloc(conn->zqdom, 1, 0, &conn->zrq);
    if (ret < 0) {
        print_func_err(__func__, __LINE__, "zhpeq_rq_qalloc", "", ret);
        goto done;
    }
    /* Exchange addresses and insert the remote address in the domain. */
    ret = zhpeq_rq_xchg_addr(conn->zrq, conn->sock_fd, &sa, &sa_len);
    if (ret < 0) {
        print_func_err(__func__, __LINE__, "zhpeq_tq_xchg_addr",
                       "", ret);
        goto done;
    }
    ret = zhpeq_domain_insert_addr(conn->zqdom, &sa, &conn->addr_cookie);
    if (ret < 0) {
        print_func_err(__func__, __LINE__, "zhpeq_domain_insert_addr", "", ret);
        goto done;
    }
    /* Now let's exchange the memory parameters to the other side. */
    ret = do_mem_setup(conn);
    if (ret < 0)
        goto done;
    ret = do_mem_xchg(conn);
    if (ret < 0)
        goto done;
    /* Loop and fill in all commmand buffers with zeros. */
    for (i =0; i<conn->ztq->tqinfo.cmdq.ent; i++) {
        wqe = &conn->ztq->wq[i];
        memset(zhpeq_tq_puti(wqe, 0, args->ring_entry_len,
               conn->ztq_remote_rx_zaddr+(i*args->ring_entry_len)),
               0, args->ring_entry_len);
    }
 done:
    return ret;
}

static int do_server_one(const struct args *oargs, int conn_fd)
{
    int                 ret;
    struct args         one_args = *oargs;
    struct args         *args = &one_args;
    struct stuff        conn = {
        .args           = args,
        .sock_fd        = conn_fd,
    };
    struct cli_wire_msg cli_msg;

    MPI_Barrier(MPI_COMM_WORLD);

    /* Let's take a moment to get the client parameters over the socket. */
    ret = sock_recv_fixed_blob(conn.sock_fd, &cli_msg, sizeof(cli_msg));
    if (ret < 0)
        goto done;

    args->ring_entry_len = be64toh(cli_msg.ring_entry_len);
    args->ring_entries = be64toh(cli_msg.ring_entries);
    args->tx_avail = be64toh(cli_msg.tx_avail);
    args->aligned_mode = !!cli_msg.aligned_mode;
    args->copy_mode = !!cli_msg.copy_mode;
    args->once_mode = !!cli_msg.once_mode;
    args->unidir_mode = !!cli_msg.unidir_mode;

    /* Dummy for ordering. */
    ret = sock_send_blob(conn.sock_fd, NULL, 0);
    if (ret < 0)
        goto done;

    ret = do_ztq_setup(&conn);
    if (ret < 0)
        goto done;

    /* Completion handshake. */
    ret = sock_recv_fixed_blob(conn.sock_fd, NULL, 0);
    if (ret < 0)
        goto done;
    ret = sock_send_blob(conn.sock_fd, NULL, 0);
    if (ret < 0)
        goto done;

 done:
    stuff_free(&conn);

    if (ret >= 0)
        ret = (cli_msg.once_mode ? 1 : 0);

    return ret;
}

static int do_server(const struct args *args)
{
    int                 ret;
    int                 listener_fd = -1;
    int                 conn_fd = -1;
    struct addrinfo     *resp = NULL;
    int                 oflags = 1;

    printf("FOOBAR3: my port is %s\n",args->service);
    ret = do_getaddrinfo(NULL, args->service,
                         AF_INET6, SOCK_STREAM, true, &resp);
    if (ret < 0)
        goto done;
    listener_fd = socket(resp->ai_family, resp->ai_socktype,
                         resp->ai_protocol);
    if (listener_fd == -1) {
        ret = -errno;
        print_func_err(__func__, __LINE__, "socket", "", ret);
        goto done;
    }
    if (setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR,
                   &oflags, sizeof(oflags)) == -1) {
        ret = -errno;
        print_func_err(__func__, __LINE__, "setsockopt", "", ret);
        goto done;
    }
    /* None of the usual: no polling; no threads; no cloexec; no nonblock. */
    if (bind(listener_fd, resp->ai_addr, resp->ai_addrlen) == -1) {
        ret = -errno;
        print_func_err(__func__, __LINE__, "bind", "", ret);
        goto done;
    }
    if (listen(listener_fd, BACKLOG) == -1) {
        ret = -errno;
        print_func_err(__func__, __LINE__, "listen", "", ret);
        goto done;
    }
    for (ret = 0; !ret;) {
        conn_fd = accept(listener_fd, NULL, NULL);
        if (conn_fd == -1) {
            ret = -errno;
            print_func_err(__func__, __LINE__, "accept", "", ret);
            goto done;
        }
        ret = do_server_one(args, conn_fd);
    }

 done:
    if (listener_fd != -1)
        close(listener_fd);
    if (resp)
        freeaddrinfo(resp);

    return ret;
}

static int do_client(const struct args *args)
{
    int                 ret;
    struct stuff        conn = {
        .args           = args,
        .sock_fd        = -1,
        .ring_ops       = args->ring_ops,
    };
    struct cli_wire_msg cli_msg;

    ret = connect_sock(args->node, args->service);
    if (ret < 0)
        goto done;
    conn.sock_fd = ret;

    /* Write the ring parameters to the server. */
    cli_msg.ring_entry_len = htobe64(args->ring_entry_len);
    cli_msg.ring_entries = htobe64(args->ring_entries);
    cli_msg.tx_avail = htobe64(args->tx_avail);
    cli_msg.aligned_mode = args->aligned_mode;
    cli_msg.copy_mode = args->copy_mode;
    cli_msg.once_mode = args->once_mode;
    cli_msg.unidir_mode = args->unidir_mode;

    ret = sock_send_blob(conn.sock_fd, &cli_msg, sizeof(cli_msg));
    if (ret < 0)
        goto done;

    /* Dummy for ordering. */
    ret = sock_recv_fixed_blob(conn.sock_fd, NULL, 0);
    if (ret < 0)
        goto done;

    ret = do_ztq_setup(&conn);
    if (ret < 0)
        goto done;

    conn.ring_warmup = args->warmup;
    /* Compute warmup operations. */
    if (args->seconds_mode) {
        if (conn.ring_warmup == SIZE_MAX)
            conn.ring_warmup = 1;
        conn.ring_ops += conn.ring_warmup;
        conn.ring_warmup *= get_tsc_freq();
        conn.ring_ops *= get_tsc_freq();
    } else if (conn.ring_warmup == SIZE_MAX) {
        conn.ring_warmup = conn.ring_ops / 10;
        if (conn.ring_warmup < args->ring_entries)
            conn.ring_warmup = args->ring_entries;
        if (conn.ring_warmup < WARMUP_MIN)
            conn.ring_warmup = WARMUP_MIN;
        conn.ring_ops += conn.ring_warmup;
    }

    /* Send ops */
    ret = do_client_unidir(&conn);

    /* ask John why completion handshake needed? */
    /* Completion handshake. */
    ret = sock_send_blob(conn.sock_fd, NULL, 0);
    if (ret < 0)
        goto done;
    ret = sock_recv_fixed_blob(conn.sock_fd, NULL, 0);
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
        "Usage:%s [-acosu] [-t <ttqlen>] [-b <address>]\n"
        "    <port> [<node> <entry_len> <ring_entries>"
        " <op_count/seconds>]\n"
        "All sizes may be postfixed with [kmgtKMGT] to specify the"
        " base units.\n"
        "Lower case is base 10; upper case is base 2.\n"
        "Server requires just port; client requires all 5 arguments.\n"
        "Client only options:\n"
        " -a : cache line align entries\n"
        " -b <address> : try to allocate buffer at address\n"
        " -c : copy mode\n"
        " -o : run once and then server will exit\n"
        " -s : treat the final argument as seconds\n"
        " -t <ttqlen> : length of tx request queue\n"
        " -w <ops> : number of warmup operations\n",
        appname);

    if (help)
        zhpeq_print_tq_info(NULL);

    exit(help ? 0 : 255);
}

int main(int argc, char **argv)
{
    int                 ret = 1;
    struct args         args = {
        .warmup         = SIZE_MAX,
    };
    bool                client_opt = false;
    int                 opt;
    int                 rc;
    int                 myrank;
    int                 portnum;
    char                *buffer;

    MPI_Init(&argc,&argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &myrank);
    printf("FOOBAR1: my rank is %d\n",myrank);

    zhpeq_util_init(argv[0], LOG_INFO, false);

    rc = zhpeq_init(ZHPEQ_API_VERSION, &zhpeq_attr);
    if (rc < 0) {
        print_func_err(__func__, __LINE__, "zhpeq_init", "", rc);
        goto done;
    }

    if (argc == 1)
        usage(true);

    args.unidir_mode = true;
    while ((opt = getopt(argc, argv, "ab:cost:w:")) != -1) {

        /* All opts are client only, now. */
        client_opt = true;

        switch (opt) {

        case 'a':
            if (args.aligned_mode)
                usage(false);
            args.aligned_mode = true;
            break;

        case 'b':
            if (args.bufaddr)
                usage(false);
            if (parse_kb_uint64_t(__func__, __LINE__, "bufaddr",
                                  optarg, &args.bufaddr, 0, 1,
                                  SIZE_MAX, PARSE_KB | PARSE_KIB) < 0)
                usage(false);
            break;

        case 'c':
            if (args.copy_mode)
                usage(false);
            args.copy_mode = true;
            break;

        case 'o':
            if (args.once_mode)
                usage(false);
            args.once_mode = true;
            break;

        case 's':
            if (args.seconds_mode)
                usage(false);
            args.seconds_mode = true;
            break;

        case 't':
            if (args.tx_avail)
                usage(false);
            if (parse_kb_uint64_t(__func__, __LINE__, "tx_avail",
                                  optarg, &args.tx_avail, 0, 1,
                                  SIZE_MAX, PARSE_KB | PARSE_KIB) < 0)
                usage(false);
            break;

        default:
            usage(false);

        }
    }

    if (args.copy_mode && args.unidir_mode)
        usage(false);

    opt = argc - optind;

    buffer = calloc(5,sizeof(char));
    if (opt == 1) {
        args.baseservice=argv[optind++];
        portnum=(atoi(args.baseservice) + myrank);
        sprintf(buffer,"%d",portnum);
        args.service=buffer;
        printf("FOOBAR2: my rank is %d; my baseservice is %s\n",myrank,args.baseservice);
        printf("FOOBAR2: my rank is %d; my port is %s\n",myrank,args.service);
        if (client_opt)
            usage(false);
        if (do_server(&args) < 0)
            goto done;
    } else if (opt == 5) {
        args.baseservice=argv[optind++];
        portnum=(atoi(args.baseservice) + myrank);
        sprintf(buffer,"%d",portnum);
        args.service=buffer;
        printf("my rank is %d; my port is %s\n",myrank,args.service); 
        args.node = argv[optind++];
        if (parse_kb_uint64_t(__func__, __LINE__, "entry_len",
                              argv[optind++], &args.ring_entry_len, 0,
                              sizeof(uint8_t), ZHPEQ_MAX_IMM,
                              PARSE_KB | PARSE_KIB) < 0 ||
            parse_kb_uint64_t(__func__, __LINE__, "ring_entries",
                              argv[optind++], &args.ring_entries, 0, 1,
                              SIZE_MAX, PARSE_KB | PARSE_KIB) < 0 ||
            parse_kb_uint64_t(__func__, __LINE__,
                              (args.seconds_mode ? "seconds" : "op_counts"),
                              argv[optind++], &args.ring_ops, 0, 1,
                              (args.seconds_mode ? 1000000 : SIZE_MAX),
                              PARSE_KB | PARSE_KIB) < 0)
            usage(false);
        if (do_client(&args) < 0)
            goto done;
    } else
        usage(false);

    ret = 0;

 done:
    MPI_Finalize();
    return ret;
}
