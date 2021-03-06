/*
 * Copyright (C) 2017-2019 Hewlett Packard Enterprise Development LP.
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

#define _GNU_SOURCE
#include <search.h>

#include <internal.h>

#define NODE_CHUNKS     (128)

static int              dev_fd = -1;
static pthread_mutex_t  dev_mutex = PTHREAD_MUTEX_INITIALIZER;
static void             *dev_uuid_tree;
static void             *dev_mr_tree;

struct dev_uuid_tree_entry {
    uuid_t              uuid;
    int32_t             use_count;
    bool                fam;
};

struct dev_mr_tree_entry {
    struct zhpeq_key_data *qkdata;
    int32_t             use_count;
};

static struct zhpe_global_shared_data *shared_global;
static struct zhpe_local_shared_data *shared_local;

struct zdom_data {
    pthread_mutex_t     node_mutex;
    int32_t             node_idx;
    struct zdom_node {
        struct dev_uuid_tree_entry *uue;
        uint32_t        sz_queue;
    } *nodes;
};

/* For the moment, we will do all driver I/O synchronously.*/

static int __driver_cmd(union zhpe_op *op, size_t req_len, size_t rsp_len)
{
    int                 ret = 0;
    int                 opcode = op->hdr.opcode;
    ssize_t             res;

    op->hdr.version = ZHPE_OP_VERSION;
    op->hdr.index = 0;

    res = write(dev_fd, op, req_len);
    ret = check_func_io(__func__, __LINE__, "write", DEV_NAME,
                        req_len, res, 0);
    if (ret < 0)
        goto done;

    res = read(dev_fd, op, rsp_len);
    ret = check_func_io(__func__, __LINE__, "read", DEV_NAME,
                        rsp_len, res, 0);
    if (ret < 0)
        goto done;
    ret = -EIO;
    if (res < sizeof(op->hdr)) {
        print_err("%s,%u:Unexpected short read %lu\n",
                  __func__, __LINE__, res);
        goto done;
    }
    ret = -EINVAL;
    if (!expected_saw("version", ZHPE_OP_VERSION, op->hdr.version))
        goto done;
    if (!expected_saw("opcode", opcode | ZHPE_OP_RESPONSE, op->hdr.opcode))
        goto done;
    if (!expected_saw("index", 0, op->hdr.index))
        goto done;
    ret = op->hdr.status;
    if (ret < 0)
        print_err("%s,%u:zhpe command 0x%02x returned error %d:%s\n",
                  __func__, __LINE__, op->hdr.opcode,
                  -ret, strerror(-ret));

 done:

    return ret;
}

static int driver_cmd(union zhpe_op *op, size_t req_len, size_t rsp_len)
{
    int                 ret;

    mutex_lock(&dev_mutex);
    ret = __driver_cmd(op, req_len, rsp_len);
    mutex_unlock(&dev_mutex);

    return ret;
}

static int zhpe_lib_init(struct zhpeq_attr *attr)
{
    int                 ret = -EINVAL;
    union zhpe_op       op;
    union zhpe_req      *req = &op.req;
    union zhpe_rsp      *rsp = &op.rsp;

    dev_fd = open(DEV_NAME, O_RDWR);
    if (dev_fd == -1) {
        ret = -errno;
        print_func_err(__func__, __LINE__, "open", DEV_NAME, ret);
        goto done;
    }

    req->hdr.opcode = ZHPE_OP_INIT;
    ret = driver_cmd(&op, sizeof(req->init), sizeof(rsp->init));
    if (ret < 0)
        goto done;

    shared_global = do_mmap(NULL, rsp->init.global_shared_size, PROT_READ,
                            MAP_SHARED, dev_fd, rsp->init.global_shared_offset,
                            &ret);
    if (!shared_global)
        goto done;
    shared_local = do_mmap(NULL, rsp->init.local_shared_size, PROT_READ,
                           MAP_SHARED, dev_fd, rsp->init.local_shared_offset,
                           &ret);
    if (!shared_local)
        goto done;
    ret = -EINVAL;
    if (!expected_saw("global_magic", ZHPE_MAGIC, shared_global->magic))
        goto done;
    if (!expected_saw("global_version", ZHPE_GLOBAL_SHARED_VERSION,
                      shared_global->version))
        goto done;
    if (!expected_saw("local_magic", ZHPE_MAGIC, shared_local->magic))
        goto done;
    if (!expected_saw("local_version", ZHPE_LOCAL_SHARED_VERSION,
                      shared_local->version))
        goto done;
    memcpy(zhpeq_uuid, rsp->init.uuid, sizeof(zhpeq_uuid));

    attr->backend = ZHPEQ_BACKEND_ZHPE;
    attr->z = shared_global->default_attr;

    ret = 0;
 done:

    return ret;
}

static int compare_uuid(const void *key1, const void *key2)
{
    const uuid_t        *u1 = key1;
    const uuid_t        *u2 = key2;

    return uuid_compare(*u1, *u2);
}

static int uuid_free(uuid_t *uu)
{
    int                 ret = 0;
    union zhpe_op       op;
    union zhpe_req      *req = &op.req;
    union zhpe_rsp      *rsp = &op.rsp;
    void                **tval;
    struct dev_uuid_tree_entry *uue;

    mutex_lock(&dev_mutex);
    tval = tfind(uu, &dev_uuid_tree, compare_uuid);
    if (tval) {
        uue = *tval;
        if (!--(uue->use_count)) {
            (void)tdelete(uu, &dev_uuid_tree, compare_uuid);
            if (uuid_compare(*uu, zhpeq_uuid)) {
                req->hdr.opcode = ZHPE_OP_UUID_FREE;
                memcpy(req->uuid_free.uuid, *uu, sizeof(req->uuid_free.uuid));
                ret =__driver_cmd(&op, sizeof(req->uuid_free),
                                  sizeof(rsp->uuid_free));
            }
            free(uue);
        }
    } else
        ret = -ENOENT;
    mutex_unlock(&dev_mutex);

    return ret;
}

static int zhpe_domain_free(struct zhpeq_dom *zdom)
{
    int                 ret = 0;
    struct zdom_data    *bdom = zdom->backend_data;
    struct dev_uuid_tree_entry *uue;
    uint32_t            i;
    int                 rc;

    if (!bdom)
        goto done;

    zdom->backend_data = NULL;
    mutex_destroy(&bdom->node_mutex);
    for (i = 0; i < bdom->node_idx; i++) {
        uue = bdom->nodes[i].uue;
        if (uue) {
            rc = uuid_free(&uue->uuid);
            ret = (ret >= 0 ? rc : ret);
        }
    }
    free(bdom->nodes);
    free(bdom);

 done:
    return ret;
}

static int zhpe_domain(struct zhpeq_dom *zdom)
{
    int                 ret = -ENOMEM;
    struct zdom_data    *bdom;

    bdom = zdom->backend_data = calloc(1, sizeof(*bdom));
    if (!bdom)
        goto done;
    mutex_init(&bdom->node_mutex, NULL);
    ret = 0;

 done:

    return ret;
}

static int zhpe_qalloc(struct zhpeq *zq, int wqlen, int cqlen,
                       int traffic_class, int priority, int slice_mask)
{
    int                 ret;
    union zhpe_op       op;
    union zhpe_req      *req = &op.req;
    union zhpe_rsp      *rsp = &op.rsp;

    req->hdr.opcode = ZHPE_OP_XQALLOC;
    req->xqalloc.cmdq_ent = wqlen;
    req->xqalloc.cmplq_ent = wqlen;
    req->xqalloc.traffic_class = traffic_class;
    req->xqalloc.priority = priority;
    req->xqalloc.slice_mask = slice_mask;
    ret = driver_cmd(&op, sizeof(req->xqalloc), sizeof(rsp->xqalloc));
    if (ret < 0)
        goto done;
    zq->fd = dev_fd;
    zq->xqinfo = rsp->xqalloc.info;

 done:

    return ret;
}

static int zhpe_exchange(struct zhpeq *zq, int sock_fd, void *sa,
                         size_t *sa_len)
{
    int                 ret = 0;
    struct sockaddr_zhpe *sz = sa;

    sz->sz_family = AF_ZHPE;
    memcpy(sz->sz_uuid, zhpeq_uuid, sizeof(sz->sz_uuid));
    sz->sz_queue = ~0U;
    *sa_len = sizeof(*sz);

    if (sock_fd == -1)
        goto done;

    ret = sock_send_blob(sock_fd, sz, sizeof(*sz));
    if (ret < 0)
        goto done;
    ret = sock_recv_fixed_blob(sock_fd, sz, sizeof(*sz));
    if (ret < 0)
        goto done;
 done:
    return ret;
}

static int zhpe_open(struct zhpeq *zq, void *sa)
{
    int                 ret = 0;
    struct zdom_data    *bdom = zq->zdom->backend_data;
    struct sockaddr_zhpe *sz = sa;
    union zhpe_op       op;
    union zhpe_req      *req = &op.req;
    union zhpe_rsp      *rsp = &op.rsp;
    struct zdom_node    *node;
    void                **tval;
    struct dev_uuid_tree_entry *uue;

    if (sz->sz_family != AF_ZHPE)
        return -EINVAL;

    mutex_lock(&dev_mutex);
    tval = tsearch(&sz->sz_uuid, &dev_uuid_tree, compare_uuid);
    if (tval) {
        uue = *tval;
        if (uue != (void *)&sz->sz_uuid)
            uue->use_count++;
        else {
            uue = malloc(sizeof(*uue));
            if (!uue)
                ret = -ENOMEM;
            if (ret >= 0) {
                *tval = uue;
                memcpy(uue->uuid, sz->sz_uuid, sizeof(uue->uuid));
                uue->use_count = 1;
                uue->fam = ((sz->sz_queue & ZHPE_SA_TYPE_MASK) ==
                            ZHPE_SA_TYPE_FAM);

                req->hdr.opcode = ZHPE_OP_UUID_IMPORT;
                memcpy(req->uuid_import.uuid, sz->sz_uuid,
                       sizeof(req->uuid_import.uuid));
                if (uue->fam) {
                    memcpy(req->uuid_import.mgr_uuid, sz[1].sz_uuid,
                           sizeof(req->uuid_import.mgr_uuid));
                    req->uuid_import.uu_flags = UUID_IS_FAM;
                } else {
                    memset(req->uuid_import.mgr_uuid, 0,
                           sizeof(req->uuid_import.mgr_uuid));
                    req->uuid_import.uu_flags = 0;
                }
                ret = __driver_cmd(&op, sizeof(req->uuid_import),
                                   sizeof(rsp->uuid_import));
            }
            if (ret < 0) {
                (void)tdelete(&sz->sz_uuid, &dev_uuid_tree, compare_uuid);
                free(uue);
            }
        }
    } else {
        ret = -ENOMEM;
        print_func_err(__func__, __LINE__, "tsearch", "", ret);
    }
    mutex_unlock(&dev_mutex);
    if (ret < 0)
        goto done;

    mutex_lock(&bdom->node_mutex);
    if ((bdom->node_idx % NODE_CHUNKS) == 0) {
        bdom->nodes = realloc(
            bdom->nodes, (bdom->node_idx + NODE_CHUNKS) * sizeof(*bdom->nodes));
        if (!bdom->nodes)
            ret = -ENOMEM;
    }
    if (ret >= 0) {
        if (bdom->node_idx < INT32_MAX) {
            ret = bdom->node_idx++;
            node = &bdom->nodes[ret];
            node->uue = uue;
            node->sz_queue = sz->sz_queue;
        } else
            ret = -ENOSPC;
    } else
        (void)uuid_free(&sz->sz_uuid);
    mutex_unlock(&bdom->node_mutex);

 done:
    return ret;
}

static int zhpe_close(struct zhpeq *zq, int open_idx)
{
    int                 ret = -EINVAL;
    struct zdom_data    *bdom = zq->zdom->backend_data;
    struct zdom_node    *node;
    struct dev_uuid_tree_entry *uue;

    if (open_idx < 0 || open_idx >= bdom->node_idx)
        goto done;

    mutex_lock(&bdom->node_mutex);
    node = &bdom->nodes[open_idx];
    uue = node->uue;
    node->uue = NULL;
    mutex_unlock(&bdom->node_mutex);
    ret = (uue ? uuid_free(&uue->uuid) : -ENOENT);

 done:
    return ret;
}

static int zhpe_qfree(struct zhpeq *zq)
{
    int                 ret;
    union zhpe_op       op;
    union zhpe_req      *req = &op.req;
    union zhpe_rsp      *rsp = &op.rsp;

    req->hdr.opcode = ZHPE_OP_XQFREE;
    req->xqfree.info = zq->xqinfo;
    ret = driver_cmd(&op, sizeof(req->xqfree), sizeof(rsp->xqfree));

    return ret;
}

static int zhpe_wq_signal(struct zhpeq *zq)
{
    return 0;
}

static int compare_qkdata(const void *key1, const void *key2)
{
    int                 ret;
    const struct zhpeq_key_data *qk1 = *(const struct zhpeq_key_data **)key1;
    const struct zhpeq_key_data *qk2 = *(const struct zhpeq_key_data **)key2;

    ret = arithcmp(qk1->z.vaddr, qk2->z.vaddr);
    if (ret)
        return ret;
    ret = arithcmp(qk1->z.len, qk2->z.len);
    if (ret)
        return ret;
    ret = arithcmp(qk1->z.access,  qk2->z.access);

    return ret;
}

static int zhpe_mr_reg(struct zhpeq_dom *zdom,
                       const void *buf, size_t len,
                       uint32_t access, struct zhpeq_key_data **qkdata_out)
{
    int                 ret = -ENOMEM;
    struct zhpeq_mr_desc_v1 *desc = NULL;
    union zhpe_op       op;
    union zhpe_req      *req = &op.req;
    union zhpe_rsp      *rsp = &op.rsp;
    void                **tval;
    struct dev_mr_tree_entry *mre;

    desc = malloc(sizeof(*desc));
    if (!desc)
        goto done;
    /* Zero access is expected to work. */
    if (!access)
        access = ZHPEQ_MR_PUT;
    desc->access_plus = access | ZHPE_MR_INDIVIDUAL;
    desc->hdr.magic = ZHPE_MAGIC;
    desc->hdr.version = ZHPEQ_MR_V1;
    desc->qkdata.z.vaddr = (uintptr_t)buf;
    desc->qkdata.laddr = (uintptr_t)buf;
    desc->qkdata.z.len = len;
    desc->qkdata.z.access = access;
    *qkdata_out = &desc->qkdata;

    mutex_lock(&dev_mutex);
    tval = tsearch(qkdata_out, &dev_mr_tree, compare_qkdata);
    if (tval) {
        mre = *tval;
        if (mre != (void *)qkdata_out) {
            *qkdata_out = mre->qkdata;
            mre->use_count++;
            free(desc);
            ret = 0;
        } else {
            mre = malloc(sizeof(*mre));
            if (mre) {
                *tval = mre;
                req->hdr.opcode = ZHPE_OP_MR_REG;
                req->mr_reg.vaddr = (uintptr_t)buf;
                req->mr_reg.len = len;
                req->mr_reg.access = desc->access_plus;
                ret = __driver_cmd(&op, sizeof(req->mr_reg),
                                   sizeof(rsp->mr_reg));
            }
            if (ret >= 0) {
                desc->qkdata.z.zaddr = rsp->mr_reg.rsp_zaddr;
                mre->qkdata = *qkdata_out;
                mre->use_count = 1;
            } else {
                (void)tdelete(qkdata_out, &dev_mr_tree, compare_qkdata);
                free(mre);
            }
        }
    } else
        print_func_err(__func__, __LINE__, "tsearch", "", ret);
    mutex_unlock(&dev_mutex);

 done:
    if (ret < 0) {
        free(desc);
        *qkdata_out = NULL;
    }

    return ret;
}

static int zhpe_mr_free(struct zhpeq_dom *zdom, struct zhpeq_key_data *qkdata)
{
    int                 ret = -EINVAL;
    struct zhpeq_mr_desc_v1 *desc = container_of(qkdata,
                                                 struct zhpeq_mr_desc_v1,
                                                 qkdata);
    union zhpe_op       op;
    union zhpe_req      *req = &op.req;
    union zhpe_rsp      *rsp = &op.rsp;
    void                **tval;
    struct dev_mr_tree_entry *mre;


    if (desc->hdr.magic != ZHPE_MAGIC || desc->hdr.version != ZHPEQ_MR_V1)
        goto done;

    mutex_lock(&dev_mutex);
    tval = tfind(&qkdata, &dev_mr_tree, compare_qkdata);
    if (tval) {
        ret = 0;
        mre = *tval;
        if (!--(mre->use_count)) {
            (void)tdelete(&qkdata, &dev_mr_tree, compare_qkdata);
            req->hdr.opcode = ZHPE_OP_MR_FREE;
            req->mr_free.vaddr = desc->qkdata.z.vaddr;
            req->mr_free.len = desc->qkdata.z.len;
            req->mr_free.access = desc->access_plus;
            req->mr_free.rsp_zaddr = desc->qkdata.z.zaddr;
            ret = __driver_cmd(&op, sizeof(req->mr_free), sizeof(rsp->mr_free));
            free(desc);
            free(mre);
        }
    } else
        ret = -ENOENT;
    mutex_unlock(&dev_mutex);

 done:
    return ret;
}

static int zhpe_zmmu_import(struct zhpeq_dom *zdom, int open_idx,
                            const void *blob, size_t blob_len, bool cpu_visible,
                            struct zhpeq_key_data **qkdata_out)
{
    int                 ret = -EINVAL;
    struct zdom_data    *bdom = zdom->backend_data;
    const struct key_data_packed *pdata = blob;
    struct zhpeq_mr_desc_v1 *desc = NULL;
    union zhpe_op       op;
    union zhpe_req      *req = &op.req;
    union zhpe_rsp      *rsp = &op.rsp;
    struct zdom_node    *node;

    if (blob_len != sizeof(*pdata) || cpu_visible ||
        open_idx < 0 || open_idx >= bdom->node_idx)
        goto done;
    node = &bdom->nodes[open_idx];
    if (!node->uue || node->uue->fam)
        goto done;

    ret = -ENOMEM;
    desc = malloc(sizeof(*desc));
    if (!desc)
        goto done;

    desc->hdr.magic = ZHPE_MAGIC;
    desc->hdr.version = ZHPEQ_MR_V1 | ZHPEQ_MR_REMOTE;
    unpack_kdata(pdata, &desc->qkdata);
    desc->qkdata.rsp_zaddr = desc->qkdata.z.zaddr;
    desc->access_plus = desc->qkdata.z.access | ZHPE_MR_INDIVIDUAL;
    desc->uuid_idx = open_idx;

    req->hdr.opcode = ZHPE_OP_RMR_IMPORT;
    memcpy(req->rmr_import.uuid, node->uue->uuid, sizeof(req->rmr_import.uuid));
    req->rmr_import.rsp_zaddr = desc->qkdata.rsp_zaddr;
    req->rmr_import.len = desc->qkdata.z.len;
    req->rmr_import.access = desc->access_plus;
    ret = driver_cmd(&op, sizeof(req->rmr_import), sizeof(rsp->rmr_import));
    if (ret < 0)
        goto done;
    desc->qkdata.z.zaddr = rsp->rmr_import.req_addr;
    *qkdata_out = &desc->qkdata;

 done:
    if (ret < 0)
        free(desc);

    return ret;
}

static int zhpe_zmmu_fam_import(struct zhpeq_dom *zdom, int open_idx,
                                bool cpu_visible,
                                struct zhpeq_key_data **qkdata_out)
{
    int                 ret = -EINVAL;
    struct zdom_data    *bdom = zdom->backend_data;
    struct zhpeq_mr_desc_v1 *desc = NULL;
    union zhpe_op       op;
    union zhpe_req      *req = &op.req;
    union zhpe_rsp      *rsp = &op.rsp;
    struct zdom_node    *node;

    if (cpu_visible || open_idx < 0 || open_idx >= bdom->node_idx)
        goto done;
    node = &bdom->nodes[open_idx];
    if (!node->uue || !node->uue->fam)
        goto done;

    ret = -ENOMEM;
    desc = malloc(sizeof(*desc));
    if (!desc)
        goto done;

    desc->hdr.magic = ZHPE_MAGIC;
    desc->hdr.version = ZHPEQ_MR_V1 | ZHPEQ_MR_REMOTE;
    desc->qkdata.z.vaddr = 0;
    desc->qkdata.rsp_zaddr = 0;
    /* FAM size in GiB in XID portion of sz_queue. */
    desc->qkdata.z.len = node->sz_queue & ZHPE_SA_XID_MASK;
    desc->qkdata.z.len *= (1ULL << 30);
    desc->qkdata.z.access = (ZHPEQ_MR_GET_REMOTE | ZHPEQ_MR_PUT_REMOTE |
                             ZHPEQ_MR_KEY_ZERO_OFF);
    desc->access_plus = desc->qkdata.z.access | ZHPE_MR_INDIVIDUAL;
    desc->uuid_idx = open_idx;

    req->hdr.opcode = ZHPE_OP_RMR_IMPORT;
    memcpy(req->rmr_import.uuid, node->uue->uuid, sizeof(req->rmr_import.uuid));
    req->rmr_import.rsp_zaddr = desc->qkdata.rsp_zaddr;
    req->rmr_import.len = desc->qkdata.z.len;
    req->rmr_import.access = desc->access_plus;
    ret = driver_cmd(&op, sizeof(req->rmr_import), sizeof(rsp->rmr_import));
    if (ret < 0)
        goto done;
    desc->qkdata.z.zaddr = rsp->rmr_import.req_addr;
    *qkdata_out = &desc->qkdata;

 done:
    if (ret < 0)
        free(desc);

    return ret;
}

static int zhpe_zmmu_free(struct zhpeq_dom *zdom, struct zhpeq_key_data *qkdata)
{
    int                 ret = -EINVAL;
    struct zdom_data    *bdom = zdom->backend_data;
    struct zhpeq_mr_desc_v1 *desc = container_of(qkdata,
                                                 struct zhpeq_mr_desc_v1,
                                                 qkdata);
    union zhpe_op       op;
    union zhpe_req      *req = &op.req;
    union zhpe_rsp      *rsp = &op.rsp;

    if (desc->hdr.magic != ZHPE_MAGIC ||
        desc->hdr.version != (ZHPEQ_MR_V1 | ZHPEQ_MR_REMOTE))
        goto done;

    req->hdr.opcode = ZHPE_OP_RMR_FREE;
    memcpy(req->rmr_free.uuid, bdom->nodes[desc->uuid_idx].uue->uuid,
           sizeof(req->rmr_free.uuid));
    req->rmr_free.req_addr = qkdata->z.zaddr;
    req->rmr_free.len = qkdata->z.len;
    req->rmr_free.access = desc->access_plus;
    req->rmr_free.rsp_zaddr = qkdata->rsp_zaddr;
    ret = driver_cmd(&op, sizeof(req->rmr_free), sizeof(rsp->rmr_free));
    free(desc);

 done:
    return ret;
}

static int zhpe_zmmu_export(struct zhpeq_dom *zdom,
                            const struct zhpeq_key_data *qkdata,
                            void *blob, size_t *blob_len)
{
    int                 ret = -EOVERFLOW;

    if (*blob_len < sizeof(struct key_data_packed))
        goto done;

    pack_kdata(qkdata, blob, qkdata->z.zaddr);
    ret = 0;

 done:
    *blob_len = sizeof(struct key_data_packed);

    return ret;
}

static void zhpe_print_info(struct zhpeq *zq)
{
    print_info("GenZ ASIC backend\n");
}

static int zhpe_getaddr(struct zhpeq *zq, void *sa, size_t *sa_len)
{
    int                 ret = -EOVERFLOW;
    struct sockaddr_zhpe *sz = sa;

    if (*sa_len < sizeof(*sz))
        goto done;

    sz->sz_family = AF_ZHPE;
    memcpy(sz->sz_uuid, &zhpeq_uuid, sizeof(sz->sz_uuid));
    sz->sz_queue = ZHPE_QUEUEINVAL;
    ret = 0;

 done:
    *sa_len = sizeof(*sz);

    return ret;
}

char *zhpe_qkdata_id_str(struct zhpeq_dom *zdom,
                         const struct zhpeq_key_data *qkdata)
{
    char                *ret = NULL;
    struct zdom_data    *bdom = zdom->backend_data;
    struct zhpeq_mr_desc_v1 *desc = container_of(qkdata,
                                                 struct zhpeq_mr_desc_v1,
                                                 qkdata);
    char                uuid_str[37];

    if (!(desc->hdr.version & ZHPEQ_MR_REMOTE))
        goto done;

    uuid_unparse_upper(bdom->nodes[desc->uuid_idx].uue->uuid, uuid_str);
    if (zhpeu_asprintf(&ret, "%d %s", desc->uuid_idx, uuid_str) == -1)
        ret = NULL;
 done:

    return ret;
}

struct backend_ops ops = {
    .lib_init           = zhpe_lib_init,
    .domain             = zhpe_domain,
    .domain_free        = zhpe_domain_free,
    .qalloc             = zhpe_qalloc,
    .qfree              = zhpe_qfree,
    .exchange           = zhpe_exchange,
    .open               = zhpe_open,
    .close              = zhpe_close,
    .wq_signal          = zhpe_wq_signal,
    .mr_reg             = zhpe_mr_reg,
    .mr_free            = zhpe_mr_free,
    .zmmu_import        = zhpe_zmmu_import,
    .zmmu_fam_import    = zhpe_zmmu_fam_import,
    .zmmu_free          = zhpe_zmmu_free,
    .zmmu_export        = zhpe_zmmu_export,
    .print_info         = zhpe_print_info,
    .getaddr            = zhpe_getaddr,
    .qkdata_id_str      = zhpe_qkdata_id_str,
};

void zhpeq_backend_zhpe_init(int fd)
{
    if (fd == -1)
        return;

    zhpeq_register_backend(ZHPE_BACKEND_ZHPE, &ops);
}
