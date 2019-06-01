#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/time.h>
#include <stdlib.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>

#include <infiniband/verbs.h>

#include <stdbool.h>
#define BYTES_TO_BITS 8


#define ONE_BYTE 1
#define MEGABYTE_IN_KILOBYTES 1024
#define MEGABYTE_IN_BYTES 1048576


#define KILOBIT_IN_BITS 1000
#define MEGABIT_IN_BITS 1000000
#define GIGABIT_IN_BITS 1000000000

#define RESULTS_FORMAT "%ld\t%s\t%u\t%d\t%f\t%s\t%.3f\t%s\t%Lf\t%s\n"
#define CSV_RESULTS_FORMAT "%ld %s,%u,%d,%f %s,%.3f %s,%Lf %s,\n"
#define SAVE_RESULTS_TO_CSV true //todo set true when submit

#define IB_PACKET_PER_CYCLE 100
#define IB_NUM_OF_CYCLE 1000
#define IB_MAX_QPS 10
#define WC_BATCH (10)

enum {
    PINGPONG_RECV_WRID = 1,
    PINGPONG_SEND_WRID = 2,
};

static int page_size;


struct pingpong_context {
    struct ibv_context		*context;
    struct ibv_comp_channel	*channel;
    struct ibv_pd		*pd;
    struct ibv_mr		*mr;
    struct ibv_cq		*cq;
    struct ibv_qp		*qp;
    void			*buf;
    int				size;
    int				rx_depth;
    int				routs;
    struct ibv_port_attr	portinfo;
    int created_qps;
    int active_qps;
    struct ibv_cq cq_array[IB_MAX_QPS];
    struct ibv_qp qp_array[IB_MAX_QPS];
    struct ibv_port_attr	portinfo_array[IB_MAX_QPS];
    int routs_array[IB_MAX_QPS];
    int rx_depths_array[IB_MAX_QPS];
};

struct pingpong_dest {
    int lid;
    int qpn;
    int psn;
    union ibv_gid gid;
};

enum ibv_mtu pp_mtu_to_enum(int mtu)
{
    switch (mtu) {
        case 256:  return IBV_MTU_256;
        case 512:  return IBV_MTU_512;
        case 1024: return IBV_MTU_1024;
        case 2048: return IBV_MTU_2048;
        case 4096: return IBV_MTU_4096;
        default:   return -1;
    }
}

uint16_t pp_get_local_lid(struct ibv_context *context, int port)
{
    struct ibv_port_attr attr;

    if (ibv_query_port(context, port, &attr))
        return 0;

    return attr.lid;
}

int pp_get_port_info(struct ibv_context *context, int port,
                     struct ibv_port_attr *attr)
{
    return ibv_query_port(context, port, attr);
}

void wire_gid_to_gid(const char *wgid, union ibv_gid *gid)
{
    char tmp[9];
    uint32_t v32;
    int i;

    for (tmp[8] = 0, i = 0; i < 4; ++i) {
        memcpy(tmp, wgid + i * 8, 8);
        sscanf(tmp, "%x", &v32);
        *(uint32_t *)(&gid->raw[i * 4]) = ntohl(v32);
    }
}

void gid_to_wire_gid(const union ibv_gid *gid, char wgid[])
{
    int i;

    for (i = 0; i < 4; ++i)
        sprintf(&wgid[i * 8], "%08x", htonl(*(uint32_t *)(gid->raw + i * 4)));
}

static int pp_connect_ctx(struct pingpong_context *ctx, int port, int my_psn,
                          enum ibv_mtu mtu, int sl,
                          struct pingpong_dest *dest, int sgid_idx)
{
    struct ibv_qp_attr attr = {
            .qp_state		= IBV_QPS_RTR,
            .path_mtu		= mtu,
            .dest_qp_num		= dest->qpn,
            .rq_psn			= dest->psn,
            .max_dest_rd_atomic	= 1,
            .min_rnr_timer		= 12,
            .ah_attr		= {
                    .is_global	= 0,
                    .dlid		= dest->lid,
                    .sl		= sl,
                    .src_path_bits	= 0,
                    .port_num	= port
            }
    };

    if (dest->gid.global.interface_id) {
        attr.ah_attr.is_global = 1;
        attr.ah_attr.grh.hop_limit = 1;
        attr.ah_attr.grh.dgid = dest->gid;
        attr.ah_attr.grh.sgid_index = sgid_idx;
    }
    if (ibv_modify_qp(ctx->qp, &attr,
                      IBV_QP_STATE              |
                      IBV_QP_AV                 |
                      IBV_QP_PATH_MTU           |
                      IBV_QP_DEST_QPN           |
                      IBV_QP_RQ_PSN             |
                      IBV_QP_MAX_DEST_RD_ATOMIC |
                      IBV_QP_MIN_RNR_TIMER)) {
        fprintf(stderr, "Failed to modify QP to RTR\n");
        return 1;
    }

    attr.qp_state	    = IBV_QPS_RTS;
    attr.timeout	    = 14;
    attr.retry_cnt	    = 7;
    attr.rnr_retry	    = 7;
    attr.sq_psn	    = my_psn;
    attr.max_rd_atomic  = 1;
    if (ibv_modify_qp(ctx->qp, &attr,
                      IBV_QP_STATE              |
                      IBV_QP_TIMEOUT            |
                      IBV_QP_RETRY_CNT          |
                      IBV_QP_RNR_RETRY          |
                      IBV_QP_SQ_PSN             |
                      IBV_QP_MAX_QP_RD_ATOMIC)) {
        fprintf(stderr, "Failed to modify QP to RTS\n");
        return 1;
    }

    return 0;
}

static struct pingpong_dest *pp_client_exch_dest(const char *servername, int port,
                                                 const struct pingpong_dest *my_dest)
{
    struct addrinfo *res, *t;
    struct addrinfo hints = {
            .ai_family   = AF_INET,
            .ai_socktype = SOCK_STREAM
    };
    char *service;
    char msg[sizeof "0000:000000:000000:00000000000000000000000000000000"];
    int n;
    int sockfd = -1;
    struct pingpong_dest *rem_dest = NULL;
    char gid[33];

    if (asprintf(&service, "%d", port) < 0)
        return NULL;

    n = getaddrinfo(servername, service, &hints, &res);

    if (n < 0) {
        fprintf(stderr, "%s for %s:%d\n", gai_strerror(n), servername, port);
        free(service);
        return NULL;
    }

    for (t = res; t; t = t->ai_next) {
        sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
        if (sockfd >= 0) {
            if (!connect(sockfd, t->ai_addr, t->ai_addrlen))
                break;
            close(sockfd);
            sockfd = -1;
        }
    }

    freeaddrinfo(res);
    free(service);

    if (sockfd < 0) {
        fprintf(stderr, "Couldn't connect to %s:%d\n", servername, port);
        return NULL;
    }

    gid_to_wire_gid(&my_dest->gid, gid);
    sprintf(msg, "%04x:%06x:%06x:%s", my_dest->lid, my_dest->qpn, my_dest->psn, gid);
    if (write(sockfd, msg, sizeof msg) != sizeof msg) {
        fprintf(stderr, "Couldn't send local address\n");
        goto out;
    }

    if (read(sockfd, msg, sizeof msg) != sizeof msg) {
        perror("client read");
        fprintf(stderr, "Couldn't read remote address\n");
        goto out;
    }

    write(sockfd, "done", sizeof "done");

    rem_dest = malloc(sizeof *rem_dest);
    if (!rem_dest)
        goto out;

    sscanf(msg, "%x:%x:%x:%s", &rem_dest->lid, &rem_dest->qpn, &rem_dest->psn, gid);
    wire_gid_to_gid(gid, &rem_dest->gid);

    out:
    close(sockfd);
    return rem_dest;
}

static struct pingpong_dest *pp_server_exch_dest(struct pingpong_context *ctx,
                                                 int ib_port, enum ibv_mtu mtu,
                                                 int port, int sl,
                                                 const struct pingpong_dest *my_dest,
                                                 int sgid_idx)
{
    struct addrinfo *res, *t;
    struct addrinfo hints = {
            .ai_flags    = AI_PASSIVE,
            .ai_family   = AF_INET,
            .ai_socktype = SOCK_STREAM
    };
    char *service;
    char msg[sizeof "0000:000000:000000:00000000000000000000000000000000"];
    int n;
    int sockfd = -1, connfd;
    struct pingpong_dest *rem_dest = NULL;
    char gid[33];

    if (asprintf(&service, "%d", port) < 0)
        return NULL;

    n = getaddrinfo(NULL, service, &hints, &res);

    if (n < 0) {
        fprintf(stderr, "%s for port %d\n", gai_strerror(n), port);
        free(service);
        return NULL;
    }

    for (t = res; t; t = t->ai_next) {
        sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
        if (sockfd >= 0) {
            n = 1;

            setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof n);

            if (!bind(sockfd, t->ai_addr, t->ai_addrlen))
                break;
            close(sockfd);
            sockfd = -1;
        }
    }

    freeaddrinfo(res);
    free(service);

    if (sockfd < 0) {
        fprintf(stderr, "Couldn't listen to port %d\n", port);
        return NULL;
    }

    listen(sockfd, 1);
    connfd = accept(sockfd, NULL, 0);
    close(sockfd);
    if (connfd < 0) {
        fprintf(stderr, "accept() failed\n");
        return NULL;
    }

    n = read(connfd, msg, sizeof msg);
    if (n != sizeof msg) {
        perror("server read");
        fprintf(stderr, "%d/%d: Couldn't read remote address\n", n, (int) sizeof msg);
        goto out;
    }

    rem_dest = malloc(sizeof *rem_dest);
    if (!rem_dest)
        goto out;

    sscanf(msg, "%x:%x:%x:%s", &rem_dest->lid, &rem_dest->qpn, &rem_dest->psn, gid);
    wire_gid_to_gid(gid, &rem_dest->gid);

    if (pp_connect_ctx(ctx, ib_port, my_dest->psn, mtu, sl, rem_dest, sgid_idx)) {
        fprintf(stderr, "Couldn't connect to remote QP\n");
        free(rem_dest);
        rem_dest = NULL;
        goto out;
    }


    gid_to_wire_gid(&my_dest->gid, gid);
    sprintf(msg, "%04x:%06x:%06x:%s", my_dest->lid, my_dest->qpn, my_dest->psn, gid);
    if (write(connfd, msg, sizeof msg) != sizeof msg) {
        fprintf(stderr, "Couldn't send local address\n");
        free(rem_dest);
        rem_dest = NULL;
        goto out;
    }

    read(connfd, msg, sizeof msg);

    out:
    close(connfd);
    return rem_dest;
}

#include <sys/param.h>

static struct pingpong_context *pp_init_ctx(struct ibv_device *ib_dev, int size,
                                            int rx_depth, int tx_depth, int port,
                                            int use_event, int is_server, bool multistream)
{
    struct pingpong_context *ctx;

    ctx = calloc(1, sizeof *ctx);
    if (!ctx)
        return NULL;

    if (multistream) {
        ctx->created_qps = IB_MAX_QPS;
    } else {
        ctx->created_qps = 1;
    }
    ctx->active_qps = 1;
    ctx->size     = size;

    for (int idx =0; idx < ctx->created_qps; idx++) {
        ctx->rx_depths_array[idx] = rx_depth;
        ctx->routs_array[idx] = rx_depth;
    }
//    ctx->rx_depth = rx_depth;
//    ctx->routs    = rx_depth;

    ctx->buf = malloc(roundup(size, page_size));
    if (!ctx->buf) {
        fprintf(stderr, "Couldn't allocate work buf.\n");
        return NULL;
    }

    memset(ctx->buf, 0x7b + is_server, size);

    ctx->context = ibv_open_device(ib_dev);
    if (!ctx->context) {
        fprintf(stderr, "Couldn't get context for %s\n",
                ibv_get_device_name(ib_dev));
        return NULL;
    }

    if (use_event) {
        ctx->channel = ibv_create_comp_channel(ctx->context);
        if (!ctx->channel) {
            fprintf(stderr, "Couldn't create completion channel\n");
            return NULL;
        }
    } else
        ctx->channel = NULL;

    ctx->pd = ibv_alloc_pd(ctx->context);
    if (!ctx->pd) {
        fprintf(stderr, "Couldn't allocate PD\n");
        return NULL;
    }

    ctx->mr = ibv_reg_mr(ctx->pd, ctx->buf, size, IBV_ACCESS_LOCAL_WRITE);
    if (!ctx->mr) {
        fprintf(stderr, "Couldn't register MR\n");
        return NULL;
    }

    for (int i=0; i < ctx->created_qps; i++) {
        ctx->cq_array[i] = &ibv_create_cq(ctx->context, rx_depth + tx_depth, NULL, ctx->channel, 0);
        if (!(ctx->cq_array[i])) {
            fprintf(stderr, "Couldn't create CQ\n");
            return NULL;
        }

        {
            struct ibv_qp_init_attr attr = {
                    .send_cq = ctx->cq_array[i],
                    .recv_cq = ctx->cq_array[i],
                    .cap     = {
                            .max_send_wr  = tx_depth,
                            .max_recv_wr  = rx_depth,
                            .max_send_sge = 1,
                            .max_recv_sge = 1
                    },
                    .qp_type = IBV_QPT_RC
            };

            ctx->qp_array[i] = ibv_create_qp(ctx->pd, &attr);

            if (!ctx->qp_array[i])  {
                fprintf(stderr, "Couldn't create QP\n");
                return NULL;
            }
        }

        {
            struct ibv_qp_attr attr = {
                    .qp_state        = IBV_QPS_INIT,
                    .pkey_index      = 0,
                    .port_num        = port,
                    .qp_access_flags = IBV_ACCESS_REMOTE_READ |
                                       IBV_ACCESS_REMOTE_WRITE
            };

            if (ibv_modify_qp(ctx->qp_array[i], &attr,
                              IBV_QP_STATE              |
                              IBV_QP_PKEY_INDEX         |
                              IBV_QP_PORT               |
                              IBV_QP_ACCESS_FLAGS)) {
                fprintf(stderr, "Failed to modify QP to INIT\n");
                return NULL;
            }
        }

    }


//    ctx->cq = ibv_create_cq(ctx->context, rx_depth + tx_depth, NULL,
//                            ctx->channel, 0);
//    if (!ctx->cq) {
//        fprintf(stderr, "Couldn't create CQ\n");
//        return NULL;
//    }
//
//    {
//        struct ibv_qp_init_attr attr = {
//                .send_cq = ctx->cq,
//                .recv_cq = ctx->cq,
//                .cap     = {
//                        .max_send_wr  = tx_depth,
//                        .max_recv_wr  = rx_depth,
//                        .max_send_sge = 1,
//                        .max_recv_sge = 1
//                },
//                .qp_type = IBV_QPT_RC
//        };
//
//        ctx->qp = ibv_create_qp(ctx->pd, &attr);
//        if (!ctx->qp)  {
//            fprintf(stderr, "Couldn't create QP\n");
//            return NULL;
//        }
//    }
//
//    {
//        struct ibv_qp_attr attr = {
//                .qp_state        = IBV_QPS_INIT,
//                .pkey_index      = 0,
//                .port_num        = port,
//                .qp_access_flags = IBV_ACCESS_REMOTE_READ |
//                                   IBV_ACCESS_REMOTE_WRITE
//        };
//
//        if (ibv_modify_qp(ctx->qp, &attr,
//                          IBV_QP_STATE              |
//                          IBV_QP_PKEY_INDEX         |
//                          IBV_QP_PORT               |
//                          IBV_QP_ACCESS_FLAGS)) {
//            fprintf(stderr, "Failed to modify QP to INIT\n");
//            return NULL;
//        }
//    }

    return ctx;
}

int pp_close_ctx(struct pingpong_context *ctx)
{
    for (int idx = 0; idx < ctx->created_qps; idx++) {
        if (ibv_destroy_qp(ctx->qp_array[idx])) {
            fprintf(stderr, "Couldn't destroy QP\n");
            return 1;
        }

        if (ibv_destroy_cq(ctx->cq_array[idx])) {
            fprintf(stderr, "Couldn't destroy CQ\n");
            return 1;
        }

    }
//    if (ibv_destroy_qp(ctx->qp)) {
//        fprintf(stderr, "Couldn't destroy QP\n");
//        return 1;
//    }
//
//    if (ibv_destroy_cq(ctx->cq)) {
//        fprintf(stderr, "Couldn't destroy CQ\n");
//        return 1;
//    }

    if (ibv_dereg_mr(ctx->mr)) {
        fprintf(stderr, "Couldn't deregister MR\n");
        return 1;
    }

    if (ibv_dealloc_pd(ctx->pd)) {
        fprintf(stderr, "Couldn't deallocate PD\n");
        return 1;
    }

    if (ctx->channel) {
        if (ibv_destroy_comp_channel(ctx->channel)) {
            fprintf(stderr, "Couldn't destroy completion channel\n");
            return 1;
        }
    }

    if (ibv_close_device(ctx->context)) {
        fprintf(stderr, "Couldn't release context\n");
        return 1;
    }

    free(ctx->buf);
    free(ctx);

    return 0;
}

static int pp_post_recv(struct pingpong_context *ctx, int qp_idx, int n)
{
    struct ibv_sge list = {
            .addr	= (uintptr_t) ctx->buf,
            .length = ctx->size,
            .lkey	= ctx->mr->lkey
    };
    struct ibv_recv_wr wr = {
            .wr_id	    = PINGPONG_RECV_WRID,
            .sg_list    = &list,
            .num_sge    = 1,
            .next       = NULL
    };
    struct ibv_recv_wr *bad_wr;
    int i;


    for (i = 0; i < n; ++i)
        if (ibv_post_recv(ctx->qp_array[qp_idx], &wr, &bad_wr))
            break;

    return i;
}

static int pp_post_send(struct pingpong_context *ctx, int qp_idx)
{
    struct ibv_sge list = {
            .addr	= (uint64_t)ctx->buf,
            .length = ctx->size,
            .lkey	= ctx->mr->lkey
    };

    struct ibv_send_wr *bad_wr, wr = {
            .wr_id	    = PINGPONG_SEND_WRID,
            .sg_list    = &list,
            .num_sge    = 1,
            .opcode     = IBV_WR_SEND,
            .send_flags = IBV_SEND_SIGNALED,
            .next       = NULL
    };

//    return ibv_post_send(ctx->qp, &wr, &bad_wr);
    return ibv_post_send(ctx->qp_array[qp_idx], &wr, &bad_wr);
}

int pp_wait_completions(struct pingpong_context *ctx, int iters)
{
    int rcnt = 0, scnt = 0;
    while (rcnt + scnt < iters) {
        struct ibv_wc wc[WC_BATCH];
        int ne, i;

        do {
            ne = ibv_poll_cq(ctx->cq, WC_BATCH, wc);
            if (ne < 0) {
                fprintf(stderr, "poll CQ failed %d\n", ne);
                return 1;
            }

        } while (ne < 1);

        for (i = 0; i < ne; ++i) {
            if (wc[i].status != IBV_WC_SUCCESS) {
                fprintf(stderr, "Failed status %s (%d) for wr_id %d\n",
                        ibv_wc_status_str(wc[i].status),
                        wc[i].status, (int) wc[i].wr_id);
                return 1;
            }

            switch ((int) wc[i].wr_id) {
                case PINGPONG_SEND_WRID:
                    ++scnt;
                    break;

                case PINGPONG_RECV_WRID:
                    if (--ctx->routs <= 10) {
                        ctx->routs += pp_post_recv(ctx, ctx->rx_depth - ctx->routs);
                        if (ctx->routs < ctx->rx_depth) {
                            fprintf(stderr,
                                    "Couldn't post receive (%d)\n",
                                    ctx->routs);
                            return 1;
                        }
                    }
                    ++rcnt;
                    break;

                default:
                    fprintf(stderr, "Completion for unknown wr_id %d\n",
                            (int) wc[i].wr_id);
                    return 1;
            }
        }

    }
    return 0;
}

static void usage(const char *argv0)
{
    printf("Usage:\n");
    printf("  %s            start a server and wait for connection\n", argv0);
    printf("  %s <host>     connect to server at <host>\n", argv0);
    printf("\n");
    printf("Options:\n");
    printf("  -i=<y>, or -i=<n>   use incremental message sizes to measure performance. (default no)\n");
    printf("  -s=<y>, or -s=<n>   use multiple streams to measure performance. (default single stream)\n");
    printf("  -t=<y>, or -t=<n>   use multiple threads to measure performance. (default single thread)\n");
}

int main(int argc, char *argv[])
{
    struct ibv_device      **dev_list;
    struct ibv_device       *ib_dev;
    struct pingpong_context *ctx;
    struct pingpong_dest     my_dest;
    struct pingpong_dest    *rem_dest;
    char                    *ib_devname = NULL;
    char                    *servername = NULL;
    int                      port = 12345;
    int                      ib_port = 1;
    enum ibv_mtu             mtu = IBV_MTU_2048;
    int                      rx_depth = IB_PACKET_PER_CYCLE;
    int                      tx_depth = IB_PACKET_PER_CYCLE;
    int                      iters = IB_NUM_OF_CYCLE;
    int                      use_event = 0;
    int                      size = 1;
    int                      sl = 0;
    int                      gidx = -1;
    char                     gid[33];

    double max_throughput_result = 0.0;
    long double packet_rate_result = 0.0;
    double latency_result = 0.0;
    bool inc_msgs_size = false;
    bool multi_streams = false;
    bool multi_threads = false;
    struct pingpong_dest     my_dest_array[IB_MAX_QPS];
    struct pingpong_dest    *rem_dest_array[IB_MAX_QPS];


    FILE * csv_fp;
    srand48(getpid() * time(NULL));

    while (1) {
        int c;

        static struct option long_options[] = {
                { .name = "inc-msg-size",    .has_arg = 1, .val = 'i' },
                { .name = "multi-streams",   .has_arg = 1, .val = 's' },
                { .name = "multi-threads",   .has_arg = 1, .val = 't' },
                { 0 }
        };

        c = getopt_long(argc, argv, "i:s:t:", long_options, NULL);
        if (c == -1)
            break;

        switch (c) {
            case 'i':
                // incremental msgs size
                if (strcmp(optarg, "y") == 0) {
                    multi_streams = true;
                } else if (strcmp(optarg, "n") == 0) {
                    multi_streams = false;
                } else {
                    usage(argv[0]);
                    return 1;
                }
                break;

            case 's':
                // multi streams
                if (strcmp(optarg, "y") == 0) {
                    inc_msgs_size = true;
                } else if (strcmp(optarg, "n") == 0) {
                    inc_msgs_size = false;
                } else {
                    usage(argv[0]);
                    return 1;
                }
                break;

            case 't'://todo
                // multi threads
                if (strcmp(optarg, "y") == 0) {
                    multi_threads = true;
                } else if (strcmp(optarg, "n") == 0) {
                    multi_threads = false;
                } else {
                    usage(argv[0]);
                    return 1;
                }
                break;

            default:
                usage(argv[0]);
                return 1;
        }
    }

    // get server name
    if (optind == argc - 1)
        servername = strdup(argv[optind]);
    else if (optind < argc) {
        usage(argv[0]);
        return 1;
    }

    page_size = sysconf(_SC_PAGESIZE);

    dev_list = ibv_get_device_list(NULL);
    if (!dev_list) {
        perror("Failed to get IB devices list");
        return 1;
    }

    if (!ib_devname) {
        ib_dev = *dev_list;
        if (!ib_dev) {
            fprintf(stderr, "No IB devices found\n");
            return 1;
        }
    } else {
        int i;
        for (i = 0; dev_list[i]; ++i)
            if (!strcmp(ibv_get_device_name(dev_list[i]), ib_devname))
                break;
        ib_dev = dev_list[i];
        if (!ib_dev) {
            fprintf(stderr, "IB device %s not found\n", ib_devname);
            return 1;
        }
    }

    ctx = pp_init_ctx(ib_dev, size, rx_depth, tx_depth, ib_port, use_event, !servername, multi_streams);
    if (!ctx)
        return 1;

    for (int idx = 0; idx < ctx->created_qps; idx++) {
        ctx->routs_array[idx] = pp_post_recv(ctx, idx, ctx->rx_depths_array[idx]);
        if (ctx->routs_array[idx] < ctx->rx_depths_array[idx]) {
            fprintf(stderr, "Couldn't post receive (%d)\n", ctx->routs);
            return 1;
        }



    }
//    ctx->routs = pp_post_recv(ctx, ctx->rx_depth);
//    if (ctx->routs < ctx->rx_depth) {
//        fprintf(stderr, "Couldn't post receive (%d)\n", ctx->routs);
//        return 1;
//    }

    if (use_event)
        if (ibv_req_notify_cq(ctx->cq, 0)) {
            fprintf(stderr, "Couldn't request CQ notification\n");
            return 1;
        }


    if (pp_get_port_info(ctx->context, ib_port, &ctx->portinfo)) {
        fprintf(stderr, "Couldn't get port info\n");
        return 1;
    }

    for (int idx = 0; idx < ctx->created_qps; idx++) {
        my_dest_array[idx].lid = ctx->portinfo.lid;

        if (ctx->portinfo.link_layer == IBV_LINK_LAYER_INFINIBAND && !my_dest_array[idx].lid) {
            fprintf(stderr, "Couldn't get local LID\n");
            return 1;
        }

        if (gidx >= 0) {
            if (ibv_query_gid(ctx->context, ib_port, gidx, &my_dest_array[idx].gid)) {
                fprintf(stderr, "Could not get local gid for gid index %d\n", gidx);
                return 1;
            }
        } else
            memset(&my_dest_array[idx].gid, 0, sizeof my_dest_array[idx].gid);

        my_dest_array[idx].qpn = ctx->qp_array[idx]->qp_num;
        my_dest_array[idx].psn = lrand48() & 0xffffff;
        inet_ntop(AF_INET6, &my_dest_array[idx].gid, gid, sizeof gid);

    }
//    my_dest.lid = ctx->portinfo.lid;
//    if (ctx->portinfo.link_layer == IBV_LINK_LAYER_INFINIBAND && !my_dest.lid) {
//        fprintf(stderr, "Couldn't get local LID\n");
//        return 1;
//    }

//    if (gidx >= 0) {
//        if (ibv_query_gid(ctx->context, ib_port, gidx, &my_dest.gid)) {
//            fprintf(stderr, "Could not get local gid for gid index %d\n", gidx);
//            return 1;
//        }
//    } else
//        memset(&my_dest.gid, 0, sizeof my_dest.gid);
//
//    my_dest.qpn = ctx->qp->qp_num;
//    my_dest.psn = lrand48() & 0xffffff;
//    inet_ntop(AF_INET6, &my_dest.gid, gid, sizeof gid);
//    printf("  local address:  LID 0x%04x, QPN 0x%06x, PSN 0x%06x, GID %s\n",
//           my_dest.lid, my_dest.qpn, my_dest.psn, gid);


    if (servername) {
        if (SAVE_RESULTS_TO_CSV) {
            csv_fp = fopen("infiniband.csv", "w+");
            fprintf(csv_fp,
                    "Message size,#sockets,#threads,Total latency,Total throughput,Total packet rate,\n");
        }
        for (int idx=0; idx<ctx->created_qps; idx++) {
            rem_dest_array[idx] = pp_client_exch_dest(servername, port, &my_dest_array[idx]);
            if (!rem_dest_array[idx])
                return 1;

            inet_ntop(AF_INET6, &rem_dest_array[idx]->gid, gid, sizeof gid);
        }
//        rem_dest = pp_client_exch_dest(servername, port, &my_dest);
    } else {
        for (int idx=0; idx<ctx->created_qps; idx++) {
            rem_dest_array[idx] = pp_server_exch_dest(ctx, ib_port, mtu, port, sl, &my_dest_array[idx], gidx);
            if (!rem_dest_array[idx])
                return 1;

            inet_ntop(AF_INET6, &rem_dest_array[idx]->gid, gid, sizeof gid);
        }
//        rem_dest = pp_server_exch_dest(ctx, ib_port, mtu, port, sl, &my_dest, gidx);
    }

//    if (!rem_dest)
//        return 1;

//    inet_ntop(AF_INET6, &rem_dest->gid, gid, sizeof gid);
//    printf("  remote address: LID 0x%04x, QPN 0x%06x, PSN 0x%06x, GID %s\n",
//           rem_dest->lid, rem_dest->qpn, rem_dest->psn, gid);

    if (servername)
        for (int idx=0; idx<ctx->created_qps; idx++) {
            if (pp_connect_ctx(ctx, ib_port, my_dest_array[idx].psn, mtu, sl, rem_dest_array[idx], gidx))
                return 1;

        }
//        if (pp_connect_ctx(ctx, ib_port, my_dest.psn, mtu, sl, rem_dest, gidx))
//            return 1;

    if (servername) {

        while(ctx->active_qps <= ctx->created_qps) {
            /* Set chrono clocks*/
            struct timeval cycle_start, cycle_end;
            struct timeval lat_start, lat_end;
            max_throughput_result = 0.0;

            /* init calculations */
            long long cycle_bytes_transferred =
                    2 * IB_PACKET_PER_CYCLE * (long long) size * ctx->active_qps;
            long bits_transferred_per_cycle = cycle_bytes_transferred * BYTES_TO_BITS;

            /* Measure throughput for pre defined # of cycle */
            int i;
            for (i = 0; i < iters; i++) {
                if (gettimeofday(&cycle_start, NULL)) {
                    perror("gettimeofday");
                    return 1;
                }

                /* Sending continuously pre defined # of packets */
                if ((i != 0) && (i % tx_depth == 0)) {
                    pp_wait_completions(ctx, tx_depth * ctx->active_qps);
                }

                for (int qp_idx = 0; qp_idx < ctx->active_qps; qp_idx++) {
                    if (pp_post_send(ctx, qp_idx)) {
                        fprintf(stderr, "Client couldn't post send\n");
                        return 1;
                    }
                }
//                if (pp_post_send(ctx)) {
//                    fprintf(stderr, "Client couldn't post send\n");
//                    return 1;
//                }

                if (gettimeofday(&cycle_end, NULL)) {
                    perror("gettimeofday");
                    return 1;
                }

                double cycle_time_usec = (cycle_end.tv_sec - cycle_start.tv_sec) * 1000000 +
                                         (cycle_end.tv_usec - cycle_start.tv_usec);

                double cycle_time_sec = (cycle_end.tv_sec - cycle_start.tv_sec) +
                                        ((cycle_end.tv_usec - cycle_start.tv_usec) * 1000000);

                double cycle_throughput = bits_transferred_per_cycle / cycle_time_sec;

                if (cycle_throughput > max_throughput_result) {
                    max_throughput_result = cycle_throughput;
                }
            }

            /* packet rate */
            packet_rate_result = max_throughput_result / size;

            /* latency */
            if (gettimeofday(&lat_start, NULL)) {
                perror("gettimeofday");
                return 1;
            }
            /* Send 1 packet with (size_t) packet_size */

            if (pp_post_send(ctx, 0)) {
                fprintf(stderr, "Client couldn't post send\n");
                return 1;
            }

            /* Send 1 packet with (size_t) packet_size */
//            if (pp_post_send(ctx)) {
//                fprintf(stderr, "Client couldn't post send\n");
//                return 1;
//            }
            /* Receive 1 packet with (size_t) packet_size */
            pp_wait_completions(ctx, 1);

            if (gettimeofday(&lat_end, NULL)) {
                perror("gettimeofday");
                return 1;
            }

            double rtt_ms = ((lat_end.tv_sec - lat_start.tv_sec) * 1000) +
                            ((lat_end.tv_usec - lat_start.tv_usec) / 100);
            latency_result = rtt_ms / 2;


            char *packet_unit = "Bytes";
            char *rate_unit;
            if (max_throughput_result >= GIGABIT_IN_BITS) {
                max_throughput_result /= GIGABIT_IN_BITS;
                rate_unit = "Gbps";

            } else if (max_throughput_result >= MEGABIT_IN_BITS) {
                max_throughput_result /= MEGABIT_IN_BITS;
                rate_unit = "Mbps";

            } else if (max_throughput_result >= KILOBIT_IN_BITS) {
                max_throughput_result /= KILOBIT_IN_BITS;
                rate_unit = "Kbps";

            } else {
                rate_unit = "bps";
            }

            if (SAVE_RESULTS_TO_CSV) {
                fprintf(csv_fp, CSV_RESULTS_FORMAT, (long) size, packet_unit, ctx->active_qps, 1, latency_result,
                        "milliseconds",
                        max_throughput_result, rate_unit, packet_rate_result, "packets/second");
            } else {
                printf(RESULTS_FORMAT, (long) size, packet_unit, 1, 1, latency_result,
                       "milliseconds",
                       max_throughput_result, rate_unit, packet_rate_result, "packets/second");
            }
            ctx->active_qps++;
        }

        if (SAVE_RESULTS_TO_CSV) {
            fclose(csv_fp);
        }
        printf("Client Done.\n");

    } else {
        while (ctx->active_qps <= ctx->created_qps) {
            for (int qp_idx = 0; qp_idx < ctx->active_qps; qp_idx++) {
                if (pp_post_send(ctx, qp_idx)) {
                    fprintf(stderr, "Server couldn't post send\n");
                    return 1;
                }
                pp_wait_completions(ctx, iters * ctx->active_qps);
                pp_wait_completions(ctx, 1);
            }
        }
//        if (pp_post_send(ctx)) {
//            fprintf(stderr, "Server couldn't post send\n");
//            return 1;
//        }
//        pp_wait_completions(ctx, iters);
//        pp_wait_completions(ctx, 1);
        printf("Server Done.\n");
    }

    ibv_free_device_list(dev_list);
    free(rem_dest);
    return 0;
}
