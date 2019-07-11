#include "kv_shared.h"
#include "limits.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define VERSION 23
#define BUFSIZE 8096
#define ERROR      42
#define LOG        44
#define FORBIDDEN 403
#define NOTFOUND  404


//// kv_client source code copied for part 2
int g_argc;
char **g_argv;
void * kv_ctx;
char top_dir[PATH_MAX] = "";

/* client cache of last accessed key */
char last_rndv_accessed_key[EAGER_PROTOCOL_LIMIT] = "";
unsigned int last_accessed_key_val_length = 0;
uint64_t last_accessed_server_addr = 0;
uint32_t last_accessed_server_rkey = 0;

/* client memory pool */
MEMORY_INFO * mem_pool_head = NULL;
MEMORY_INFO * mem_pool_tail = NULL;
int mem_pool_size = 0;

static int pp_connect_ctx(struct pingpong_context *ctx, int port, int my_psn,
                          enum ibv_mtu mtu, int sl,
                          struct pingpong_dest *dest, int sgid_idx) {
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
                                                 const struct pingpong_dest *my_dest) {
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
                                                 int sgid_idx) {
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

static struct pingpong_context *pp_init_ctx(struct ibv_device *ib_dev, int size,
                                            int rx_depth, int port,
                                            int use_event, int is_server) {
    struct pingpong_context *ctx;

    ctx = calloc(1, sizeof *ctx);
    if (!ctx)
        return NULL;

    ctx->size     = size;
    ctx->rx_depth = rx_depth;
    ctx->routs    = rx_depth;

    ctx->buf = malloc(roundup(size, page_size));
    if (!ctx->buf) {
        fprintf(stderr, "Couldn't allocate work buf.\n");
        return NULL;
    }

    memset(ctx->buf, 0x7b + is_server, size);

    ctx->context = ibv_open_device(ib_dev);
    if (!ctx->context) {
        if(DEBUG) { printf("ib_dev: %s, port:%d\n", ib_dev->dev_name, port); } //todo delete
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

    ctx->cq = ibv_create_cq(ctx->context, rx_depth + 1, NULL,
                            ctx->channel, 0);
    if (!ctx->cq) {
        fprintf(stderr, "Couldn't create CQ\n");
        return NULL;
    }

    {
        struct ibv_qp_init_attr attr = {
                .send_cq = ctx->cq,
                .recv_cq = ctx->cq,
                .cap     = {
                        .max_send_wr  = 1,
                        .max_recv_wr  = rx_depth,
                        .max_send_sge = 1,
                        .max_recv_sge = 1
                },
                .qp_type = IBV_QPT_RC
        };

        ctx->qp = ibv_create_qp(ctx->pd, &attr);
        if (!ctx->qp)  {
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

        if (ibv_modify_qp(ctx->qp, &attr,
                          IBV_QP_STATE              |
                          IBV_QP_PKEY_INDEX         |
                          IBV_QP_PORT               |
                          IBV_QP_ACCESS_FLAGS)) {
            fprintf(stderr, "Failed to modify QP to INIT\n");
            return NULL;
        }
    }

    if (is_server) {
        ctx->server = true;
    } else {
        ctx->server = false;
    }

    return ctx;
}


int pp_close_ctx(struct pingpong_context *ctx) {
    if (ibv_destroy_qp(ctx->qp)) {
        fprintf(stderr, "Couldn't destroy QP\n");
        return 1;
    }

    if (ibv_destroy_cq(ctx->cq)) {
        fprintf(stderr, "Couldn't destroy CQ\n");
        return 1;
    }

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

static int pp_post_recv(struct pingpong_context *ctx, int n) {
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
        if (ibv_post_recv(ctx->qp, &wr, &bad_wr))
            break;

    return i;
}

static int pp_post_send(struct pingpong_context *ctx, enum ibv_wr_opcode opcode, unsigned size, const char *local_ptr, void *remote_ptr, uint32_t remote_key) {
    struct ibv_sge list = {
            .addr	= (uintptr_t) (local_ptr ? local_ptr : ctx->buf),
            .length = size,
            .lkey	= ctx->mr->lkey
    };

    struct ibv_send_wr wr = {
            .wr_id	    = PINGPONG_SEND_WRID,
            .sg_list    = &list,
            .num_sge    = 1,
            .opcode     = opcode,
            .send_flags = IBV_SEND_SIGNALED,
            .next       = NULL
    };
    struct ibv_send_wr *bad_wr;

    if (remote_ptr) {
        wr.wr.rdma.remote_addr = (uintptr_t) remote_ptr;
        wr.wr.rdma.rkey = remote_key;
    }

    return ibv_post_send(ctx->qp, &wr, &bad_wr);
}


static void usage(const char *argv0) {
#ifdef EX4
    printf("Usage:\n");
    printf("  %s <kv_indexer-host> <kv_server#1-host> ... ... <kv_server#N-host>     connect to indexer at <indexer-host>  and #n servers\n", argv0);
    printf("     and use the default port set in kv_shared header file.\n");
    printf("  %s <kv_indexer-host:port> <kv_server#1-host:port> ... ... <kv_server#N-host:port>     connect to indexer at <indexer-host>  and #n servers\n", argv0);
    printf("     and override the default port set in kv_shared header file.\n");
    printf("\n");
#else
    printf("Usage:\n");
    printf("  %s            start a server and wait for connection\n", argv0);
    printf("  %s <host>     connect to server at <host>\n", argv0);
    printf("Options:\n");
    printf("  -p, --port=<port>      listen on/connect to port <port> (default 18515)\n");
    printf("  -d, --ib-dev=<dev>     use IB device <dev> (default first device found)\n");
    printf("  -i, --ib-port=<port>   use port <port> of IB device (default 1)\n");
    printf("  -s, --size=<size>      size of message to exchange (default 4096)\n");
    printf("  -m, --mtu=<size>       path MTU (default 1024)\n");
    printf("  -r, --rx-depth=<dep>   number of receives to post at a time (default 500)\n");
    printf("  -n, --iters=<iters>    number of exchanges (default 1000)\n");
    printf("  -l, --sl=<sl>          service level value\n");
    printf("  -e, --events           sleep on CQ events (default poll)\n");
    printf("  -g, --gid-idx=<gid index> local port gid index\n");
#endif
}


int parse_args(struct kv_server_address ** indexer, struct kv_server_address ** servers) {
    char * temp_copy;
    char * port_token;
    int port;
#ifdef EX4
    // indexer-hostname<:port> server-hostname<:port>, port is optional
    if (g_argc < 2) {
        usage("nweb");
        return 1;
    }
    if(DEBUG) { printf("parse args before malloc\n"); }
    struct kv_server_address * temp_indexer = (struct kv_server_address *) malloc(sizeof(struct kv_server_address));
    struct kv_server_address * temp_servers = (struct kv_server_address *) malloc((g_argc - 1) * sizeof(struct kv_server_address));
    if(DEBUG) { printf("parse args after malloc\n"); }

    // indexer
    temp_copy = strdup(g_argv[0]);
    if(DEBUG) { printf("parse args - after strdup\n"); }
    temp_indexer->servername = strtok(temp_copy, ":");
    if(DEBUG) { printf("parse args - after strtok. indexer: %s\n", temp_indexer->servername); }
    port_token = strtok(NULL, ":");
    if (port_token != NULL) {
        port = atoi(port_token);
        if (port < 0 || port > 65535) {
            return 1;
        }
        temp_indexer->port = port;
    } else {
        temp_indexer->port = DEFAULT_IDX_PORT;
    }
    if(DEBUG) { printf("parse args - after indexer\n"); }
    // servers
    for (int arg_idx = 1; arg_idx < g_argc; arg_idx++) {
        temp_copy = strdup(g_argv[arg_idx]);
        if(DEBUG) { printf("parse args - after strdup argidx%d\n", arg_idx); }
        temp_servers[arg_idx-1].servername = strtok(temp_copy, ":");
        if(DEBUG) { printf("parse args - after strtok servername %s\n", temp_servers[arg_idx-1].servername); }

        port_token = strtok(NULL, ":");
        if (port_token != NULL) {
            port = strtol(port_token, NULL, 0);
            if (port < 0 || port > 65535) {
                return 1;
            }
            temp_servers[arg_idx-1].port = port;
        } else {
            temp_servers[arg_idx-1].port = DEFAULT_SRV_PORT;
        }
    }

    if(DEBUG) { printf("parse args - before last line\n"); }
    temp_servers[g_argc-1].servername = NULL;
    temp_servers[g_argc-1].port = 0;
    *indexer = temp_indexer;
    *servers = temp_servers;
    if(DEBUG) { printf("parse args - after last line\n"); }
    return 0;
#else
    // server-hostname<:port>, port is optional
    if (g_argc < 2) {
        usage(g_argv[0]);
        return 1;
    }
    struct kv_server_address * temp_servers = malloc(2*sizeof(struct kv_server_address));
    temp_copy = strdup(g_argv[1]);

    temp_servers[0].servername = strtok(temp_copy, ":");
    port_token = strtok(NULL, ":");

    if (port_token != NULL) {
        port = strtol(port_token, NULL, 0);
        if (port < 0 || port > 65535) {
            return 1;
        }
        temp_servers[0].port = port;
    } else {
        temp_servers[0].port = DEFAULT_SRV_PORT;
    }

    temp_servers[1].servername = NULL;
    temp_servers[1].port = 0;

    *servers = temp_servers;
//    free(temp_copy);
    return 0;
#endif
}


int orig_main(struct kv_server_address *server, unsigned size, int argc, char *argv[], struct pingpong_context **result_ctx) {
    struct ibv_device      **dev_list;
    struct ibv_device	     *ib_dev;
    struct pingpong_context *ctx;
    struct pingpong_dest     my_dest;
    struct pingpong_dest    *rem_dest;
    struct timeval           start, end;
    char                    *ib_devname = NULL;
    char                    *servername = server->servername;
    int                      port = server->port;
    int                      ib_port = 1;
    enum ibv_mtu		      mtu = IBV_MTU_1024;
    int                      rx_depth = 1;
    int                      iters = 1000;
    int                      use_event = 0;
    int                      routs;
    int                      rcnt, scnt;
    int                      num_cq_events = 0;
    int                      sl = 0;
    int			             gidx = -1;
    char			         gid[33];

    srand48(getpid() * time(NULL));

//    while (1) {
//        int c;
//
//        static struct option long_options[] = {
//                { .name = "port",     .has_arg = 1, .val = 'p' },
//                { .name = "ib-dev",   .has_arg = 1, .val = 'd' },
//                { .name = "ib-port",  .has_arg = 1, .val = 'i' },
//                { .name = "size",     .has_arg = 1, .val = 's' },
//                { .name = "mtu",      .has_arg = 1, .val = 'm' },
//                { .name = "rx-depth", .has_arg = 1, .val = 'r' },
//                { .name = "iters",    .has_arg = 1, .val = 'n' },
//                { .name = "sl",       .has_arg = 1, .val = 'l' },
//                { .name = "events",   .has_arg = 0, .val = 'e' },
//                { .name = "gid-idx",  .has_arg = 1, .val = 'g' },
//                { 0 }
//        };
//
//        c = getopt_long(argc, argv, "p:d:i:s:m:r:n:l:eg:", long_options, NULL);
//        if (c == -1)
//            break;
//
//        switch (c) {
//            case 'p':
//                port = strtol(optarg, NULL, 0);
//                if (port < 0 || port > 65535) {
//                    usage(argv[0]);
//                    return 1;
//                }
//                break;
//
//            case 'd':
//                ib_devname = strdup(optarg);
//                break;
//
//            case 'i':
//                ib_port = strtol(optarg, NULL, 0);
//                if (ib_port < 0) {
//                    usage(argv[0]);
//                    return 1;
//                }
//                break;
//
//            case 's':
//                size = strtol(optarg, NULL, 0);
//                break;
//
//            case 'm':
//                mtu = pp_mtu_to_enum(strtol(optarg, NULL, 0));
//                if (mtu < 0) {
//                    usage(argv[0]);
//                    return 1;
//                }
//                break;
//
//            case 'r':
//                rx_depth = strtol(optarg, NULL, 0);
//                break;
//
//            case 'n':
//                iters = strtol(optarg, NULL, 0);
//                break;
//
//            case 'l':
//                sl = strtol(optarg, NULL, 0);
//                break;
//
//            case 'e':
//                ++use_event;
//                break;
//
//            case 'g':
//                gidx = strtol(optarg, NULL, 0);
//                break;
//
//            default:
//                usage(argv[0]);
//                return 1;
//        }
//    }
//
//    if (optind == argc - 1)
//        servername = strdup(argv[optind]);
//    else if (optind < argc) {
//        usage(argv[0]);
//        return 1;
//    }

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
    if(DEBUG) { printf("srvname %s ||| ib_dev: %s, port:%d\n", servername, ib_dev->dev_name, port); } //todo delete
    ctx = pp_init_ctx(ib_dev, size, rx_depth, ib_port, use_event, !servername);
    if (!ctx)
        return 1;

    routs = pp_post_recv(ctx, ctx->rx_depth);
    if (routs < ctx->rx_depth) {
        fprintf(stderr, "Couldn't post receive (%d)\n", routs);
        return 1;
    }

    if (use_event)
        if (ibv_req_notify_cq(ctx->cq, 0)) {
            fprintf(stderr, "Couldn't request CQ notification\n");
            return 1;
        }


    if (pp_get_port_info(ctx->context, ib_port, &ctx->portinfo)) {
        fprintf(stderr, "Couldn't get port info\n");
        return 1;
    }

    my_dest.lid = ctx->portinfo.lid;
    if (ctx->portinfo.link_layer == IBV_LINK_LAYER_INFINIBAND && !my_dest.lid) {
        fprintf(stderr, "Couldn't get local LID\n");
        return 1;
    }

    if (gidx >= 0) {
        if (ibv_query_gid(ctx->context, ib_port, gidx, &my_dest.gid)) {
            fprintf(stderr, "Could not get local gid for gid index %d\n", gidx);
            return 1;
        }
    } else
        memset(&my_dest.gid, 0, sizeof my_dest.gid);

    my_dest.qpn = ctx->qp->qp_num;
    my_dest.psn = lrand48() & 0xffffff;
    inet_ntop(AF_INET6, &my_dest.gid, gid, sizeof gid);
    printf("  local address:  LID 0x%04x, QPN 0x%06x, PSN 0x%06x, GID %s\n",
           my_dest.lid, my_dest.qpn, my_dest.psn, gid);

    if (servername)
        rem_dest = pp_client_exch_dest(servername, port, &my_dest);
    else
        rem_dest = pp_server_exch_dest(ctx, ib_port, mtu, port, sl, &my_dest, gidx);

    if (!rem_dest)
        return 1;

    inet_ntop(AF_INET6, &rem_dest->gid, gid, sizeof gid);
    printf("  remote address: LID 0x%04x, QPN 0x%06x, PSN 0x%06x, GID %s\n",
           rem_dest->lid, rem_dest->qpn, rem_dest->psn, gid);

    if (servername)
        if (pp_connect_ctx(ctx, ib_port, my_dest.psn, mtu, sl, rem_dest, gidx))
            return 1;

    ibv_free_device_list(dev_list);
    free(rem_dest);
    *result_ctx = ctx;
    return 0;
}

int pp_wait_completions(struct pingpong_context *ctx, int iters) {
    int rcnt, scnt, num_cq_events, use_event = 0;
    rcnt = scnt = 0;
    while (rcnt + scnt < iters) {
        struct ibv_wc wc[2];
        int ne, i;

        do {
            ne = ibv_poll_cq(ctx->cq, 2, wc);
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
                    ++rcnt;
                    pp_post_recv(ctx, 1);
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


int kv_open(struct kv_server_address *server, void **kv_handle) {
    return orig_main(server, EAGER_PROTOCOL_LIMIT, g_argc, g_argv, (struct pingpong_context **)kv_handle);
}


int kv_set(void *kv_handle, const char *key, const char *value, unsigned length) {
    struct pingpong_context *ctx = kv_handle;
    struct packet *set_packet = (struct packet*)ctx->buf;

    unsigned key_length = strlen(key);
    unsigned value_length = length;
    unsigned packet_size = key_length + 1 + value_length + sizeof(struct packet);

    if (DEBUG) { printf("kv_set key %s value %s length %d\n", key, value, length); }

    if (packet_size < EAGER_PROTOCOL_LIMIT) {
        if (DEBUG) { printf("EAGER_SET_REQUEST\n"); }
        if (strcmp(key, last_rndv_accessed_key) == 0) {

            memset(last_rndv_accessed_key, '\0', key_length);
            last_accessed_key_val_length = 0;
        }
        /* Eager protocol - exercise part 1 */
        set_packet->type = EAGER_SET_REQUEST;
        memcpy(set_packet->eager_set_request.key_and_value, key, key_length + 1);
        memcpy(&set_packet->eager_set_request.key_and_value[key_length + 1], value, value_length);
        set_packet->eager_set_request.value_length = value_length;
        /* Sends the packet to the server */
        pp_post_send(ctx, IBV_WR_SEND, packet_size, NULL, NULL, 0);

        /* await EAGER_SET_REQUEST completion */
        return pp_wait_completions(ctx, 1);
    }

    /* Otherwise, use RENDEZVOUS - exercise part 2 */
    set_packet->type = RENDEZVOUS_SET_REQUEST;
    if (DEBUG) { printf("RENDEZVOUS_SET_REQUEST\n"); }
    /* maybe already cached*/
    if ((last_rndv_accessed_key != NULL) && (strcmp(key, last_rndv_accessed_key) == 0)) {
        if (DEBUG) { printf("kv_set key %s == last rndv: %s\n", key, last_rndv_accessed_key); }
        last_accessed_key_val_length = value_length;
        // set temp mr
        struct ibv_mr *orig_mr = ctx->mr;
        struct ibv_mr *temp_mr = ibv_reg_mr(ctx->pd, (void *) value, value_length + 1,
                                            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
                                            IBV_ACCESS_REMOTE_READ);
        ctx->mr = temp_mr;
        pp_post_send(ctx, IBV_WR_RDMA_WRITE, value_length + 1, value,
                     (void *) last_accessed_server_addr,
                     last_accessed_server_rkey);

        int ret_value = pp_wait_completions(ctx, 1);
        ctx->mr = orig_mr;
        ibv_dereg_mr(temp_mr);
        return ret_value;
    }

    packet_size = key_length + 1 + sizeof(struct packet);
    set_packet->rndv_set_request.value_length = value_length;
    memcpy(set_packet->rndv_set_request.key, key, key_length + 1);
    if (DEBUG) { printf("REND set key request: %s, value %s  length %d\n", set_packet->rndv_set_request.key, value, set_packet->rndv_set_request.value_length); }
    /* Posts a receive-buffer for RENDEZVOUS_SET_RESPONSE */
    pp_post_recv(ctx, 1);
    /* Sends the packet to the server */
    pp_post_send(ctx, IBV_WR_SEND, packet_size, NULL, NULL, 0);


    /* wait for both to complete */
    assert(pp_wait_completions(ctx, 2) == 0);
    assert(set_packet->type == RENDEZVOUS_SET_RESPONSE);
    // set temp mr
    struct ibv_mr * orig_mr = ctx->mr;
    struct ibv_mr * temp_mr = ibv_reg_mr(ctx->pd, (void*)value, value_length, IBV_ACCESS_LOCAL_WRITE |IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
    ctx->mr = temp_mr;

    /* update cache */
    strcpy(last_rndv_accessed_key, key);
    last_accessed_key_val_length = value_length;
    last_accessed_server_addr = set_packet->rndv_set_response.server_ptr;
    last_accessed_server_rkey = set_packet->rndv_set_response.server_key;

    if (DEBUG) { printf("1REND set key response from server: %s, server_addr %ld, server_rkey %d\n", last_rndv_accessed_key, last_accessed_server_addr, last_accessed_server_rkey); }

    pp_post_send(ctx, IBV_WR_RDMA_WRITE, value_length, value, (void *)set_packet->rndv_set_response.server_ptr, set_packet->rndv_set_response.server_key);
    if (DEBUG) { printf("2REND set key response from server: %s, server_addr %ld, server_rkey %d\n", last_rndv_accessed_key, last_accessed_server_addr, last_accessed_server_rkey); }
    int ret_value = pp_wait_completions(ctx, 1);
    if (DEBUG) { printf("3REND set key response from server: %s, server_addr %ld, server_rkey %d\n", last_rndv_accessed_key, last_accessed_server_addr, last_accessed_server_rkey); }
    ctx->mr = orig_mr;
    ibv_dereg_mr(temp_mr);
    if (DEBUG) { printf("4REND set key response from server: %s, server_addr %ld, server_rkey %d\n", last_rndv_accessed_key, last_accessed_server_addr, last_accessed_server_rkey); }
    return ret_value;
}


int kv_get(void *kv_handle, const char *key, char **value, unsigned *length) {
    struct pingpong_context *ctx = kv_handle;
    unsigned int key_length = strlen(key);
    struct packet * get_packet = (struct packet*)ctx->buf;

    //// Key length assumption: key length < 4KB
    unsigned packet_size = key_length + 1 + sizeof(struct packet);

    if (packet_size > EAGER_PROTOCOL_LIMIT) {
        printf("ERROR");//todo
        return -1;
    }

    if (strcmp(key, last_rndv_accessed_key) != 0) {
        get_packet->type = EAGER_GET_REQUEST;
        memcpy(get_packet->eager_get_request.key, key, key_length + 1);

        /* Posts a receive-buffer for EAGER_GET_RESPONSE / RNDV_GET_RESPONSE */
        pp_post_recv(ctx, 1);
        /* Sends the packet to the server */
        pp_post_send(ctx, IBV_WR_SEND, packet_size, NULL, NULL, 0);
        /* await EAGER_GET_REQUEST completion */
        pp_wait_completions(ctx, 2);
//        pp_wait_completions(ctx, 1);//todo delete

        struct packet *response_packet = (struct packet *) ctx->buf;
        unsigned int value_len;
        switch (response_packet->type) {

            case EAGER_GET_RESPONSE:
                if (DEBUG) { printf("kv_get key %s, EAGER_GET_RESPONSE\n", key); }
                value_len = response_packet->eager_get_response.value_length;
                if (DEBUG) {
                    if (length == NULL)
                        printf("kv_get  NULL *length %d\n", *length);
                    else
                        printf("kv_get *length %p\n", length);
                }

                *length = value_len;
                if (DEBUG) { printf("kv_get after assign *length %d\n", *length); }
                *value = (char *) malloc(value_len + 1);
                if (DEBUG) { printf("kv_get before memcpy \n"); }
                memcpy(*value, response_packet->eager_get_response.value, value_len);
                if (DEBUG) { printf("kv_get before memset \n"); }
                memset(&((*value)[value_len]), '\0', 1);
                if (DEBUG) { printf("kv_get before break \n"); }
                break;

            case RENDEZVOUS_GET_RESPONSE:
                if (DEBUG) { printf("kv_get key %s, RENDEZVOUS_GET_RESPONSE\n", key); }
                value_len = response_packet->rndv_get_response.value_length;
                *length = value_len;
                *value = calloc(value_len + 1, 1);

                /* update cache */
                memcpy(last_rndv_accessed_key, key, key_length + 1);
                last_accessed_key_val_length = value_len;
                last_accessed_server_addr = response_packet->rndv_get_response.server_ptr;
                last_accessed_server_rkey = response_packet->rndv_get_response.server_key;

                // set temp mr
                struct ibv_mr *orig_mr = ctx->mr;
                struct ibv_mr *temp_mr = ibv_reg_mr(ctx->pd, (void *) *value, value_len + 1,
                                                    IBV_ACCESS_LOCAL_WRITE |
                                                    IBV_ACCESS_REMOTE_WRITE |
                                                    IBV_ACCESS_REMOTE_READ);
                ctx->mr = temp_mr;

                pp_post_send(ctx, IBV_WR_RDMA_READ, value_len + 1, *value,
                             (void *) response_packet->rndv_get_response.server_ptr,
                             response_packet->rndv_get_response.server_key);
                int ret_value = pp_wait_completions(ctx, 1);
                ctx->mr = orig_mr;
                ibv_dereg_mr(temp_mr);
                break;

            default:
                printf("ERROR");//todo
                return -1;
        }

    } else {
        if (DEBUG) { printf("kv_get key %s == last rndv: %s\n", key, last_rndv_accessed_key); }
        if (DEBUG) { printf("last_accessed_key_val_length %d last_accessed_server_addr %ld, last_accessed_server_rkey %d\n", last_accessed_key_val_length, last_accessed_server_addr, last_accessed_server_rkey); }
        *value = calloc(last_accessed_key_val_length + 1, 1);

        // set temp mr
        struct ibv_mr *orig_mr = ctx->mr;
        struct ibv_mr *temp_mr = ibv_reg_mr(ctx->pd, (void *) *value, last_accessed_key_val_length + 1,
                                            IBV_ACCESS_LOCAL_WRITE |
                                            IBV_ACCESS_REMOTE_WRITE |
                                            IBV_ACCESS_REMOTE_READ);
        ctx->mr = temp_mr;

        pp_post_send(ctx, IBV_WR_RDMA_READ, last_accessed_key_val_length, *value,
                     (void *) last_accessed_server_addr,
                     last_accessed_server_rkey);

        pp_wait_completions(ctx, 1);
        ctx->mr = orig_mr;
        ibv_dereg_mr(temp_mr);
        if (DEBUG) { printf("kv_get value read: %s\n", *value); }
    }

    return 0;
}


void kv_release(char *value) {
    free(value);
}


int kv_close(void *kv_handle) {

    struct pingpong_context *ctx = kv_handle;
    struct packet *close_packet = (struct packet*)ctx->buf;

    unsigned packet_size = sizeof(struct packet);

    close_packet->type = CLOSE_CONNECTION;
    close_packet->close_connection.to_close = 1;

    pp_post_send(ctx, IBV_WR_SEND, packet_size, NULL, NULL, 0); /* Sends the packet to the server */

    pp_wait_completions(ctx, 1); /* await EAGER_GET_REQUEST completion */

    return pp_close_ctx((struct pingpong_context*)kv_handle);
}


#ifdef EX4
struct mkv_ctx {
    unsigned num_servers;
    struct pingpong_context *kv_ctxs[0];
};


int mkv_open(struct kv_server_address *servers, void **mkv_h) {
    struct mkv_ctx *ctx;
    unsigned count = 0;
    while (servers[count++].servername); /* count servers */


    ctx = malloc(sizeof(*ctx) + count * sizeof(void*));
    if (!ctx) {
        return 1;
    }
    ctx->num_servers = count;
    ctx->num_servers = count-1;
    for (count = 0; count < ctx->num_servers; count++) {
        if (DEBUG) { printf("connecting to \'%s\' port %d\n", servers[count].servername, servers[count].port); }
        if (orig_main(&servers[count], EAGER_PROTOCOL_LIMIT, g_argc, g_argv, &ctx->kv_ctxs[count])) {
            return 1;
        }
    }

    *mkv_h = ctx;
    return 0;
}


int mkv_set(void *mkv_h, unsigned kv_id, const char *key, const char *value, unsigned length) {
    struct mkv_ctx *ctx = mkv_h;
    return kv_set(ctx->kv_ctxs[kv_id], key, value, length);
}


int mkv_get(void *mkv_h, unsigned kv_id, const char *key, char **value, unsigned *length) {
    struct mkv_ctx *ctx = mkv_h;
    return kv_get(ctx->kv_ctxs[kv_id], key, value, length);
}


void mkv_release(char *value) {
    kv_release(value);
}


void mkv_close(void *mkv_h) {
    unsigned count;
    struct mkv_ctx *ctx = mkv_h;
    for (count = 0; count < ctx->num_servers; count++) {
//		pp_close_ctx((struct pingpong_context*)ctx->kv_ctxs[count]);
        kv_close(ctx->kv_ctxs[count]);
    }
    free(ctx);
}


struct dkv_ctx {
    struct mkv_ctx *mkv;
    struct pingpong_context *indexer;
};


int dkv_open(struct kv_server_address *servers, /* array of servers */
             struct kv_server_address *indexer, /* single indexer */
             void **dkv_h) {
    struct dkv_ctx *ctx = malloc(sizeof(*ctx));
    if(DEBUG) { printf("dkv_open indexer: %s : %d\n", indexer->servername, indexer->port); }
    if (orig_main(indexer, EAGER_PROTOCOL_LIMIT, g_argc, g_argv, &ctx->indexer)) {
        if(DEBUG) { printf("ERRdkv_open indexer: %s : %d\n", indexer->servername, indexer->port); }
        return 1;
    }
    if (mkv_open(servers, (void**)&ctx->mkv)) {
        return 1;
    }
    *dkv_h = ctx;
    return 0;//todo verify
}


int dkv_set(void *dkv_h, const char *key, const char *value, unsigned length) {
    struct dkv_ctx *ctx = dkv_h;
    struct packet *set_packet = (struct packet*)ctx->indexer->buf;
    unsigned packet_size = strlen(key) + sizeof(struct packet);

    /* Step #1: The client sends the Index server FIND(key, #kv-servers) */
    set_packet->type = FIND;
    set_packet->find.num_of_servers = ctx->mkv->num_servers;
    memcpy(set_packet->find.key, key, strlen(key));

    pp_post_recv(ctx->indexer, 1); /* Posts a receive-buffer for LOCATION */
    pp_post_send(ctx->indexer, IBV_WR_SEND, packet_size, NULL, NULL, 0); /* Sends the packet to the server */
    assert(pp_wait_completions(ctx->indexer, 2) == 0); /* wait for both to complete */

    /* Step #2: The Index server responds with LOCATION(#kv-server-id) */
    if (DEBUG) {printf("packet type: %d\n", set_packet->type); }
    assert(set_packet->type == LOCATION);

    /* Step #3: The client contacts KV-server with the ID returned in LOCATION, using SET/GET messages. */
//    return mkv_set(ctx->mkv, set_packet->location.selected_server, key, value);
    return mkv_set(ctx->mkv, set_packet->location.selected_server, key, value, length);
    /* TODO (10LOC): Add this value length parameter to all the relevant functions... including kv_set()/kv_get() */
}


//int dkv_get(void *dkv_h, const char *key, char **value, unsigned *length) {
int dkv_get(void *dkv_h, const char *key, char **value, unsigned *length) {
    /* TODO (20LOC): implement similarly to dkv_get() */
    struct dkv_ctx *ctx = dkv_h;
    struct packet *set_packet = (struct packet*)ctx->indexer->buf;
    unsigned packet_size = strlen(key) + sizeof(struct packet);

    /* Step #1: The client sends the Index server FIND(key, #kv-servers) */
    set_packet->type = FIND;
    set_packet->find.num_of_servers = ctx->mkv->num_servers;
    memcpy(set_packet->find.key, key, strlen(key));

    pp_post_recv(ctx->indexer, 1); /* Posts a receive-buffer for LOCATION */
    pp_post_send(ctx->indexer, IBV_WR_SEND, packet_size, NULL, NULL, 0); /* Sends the packet to the server */
    assert(pp_wait_completions(ctx->indexer, 2) == 0); /* wait for both to complete */

    /* Step #2: The Index server responds with LOCATION(#kv-server-id) */
    assert(set_packet->type == LOCATION);

    /* Step #3: The client contacts KV-server with the ID returned in LOCATION, using SET/GET messages. */
//    return mkv_get(ctx->mkv, set_packet->location.selected_server, key, value);
    return mkv_get(ctx->mkv, set_packet->location.selected_server, key, value, length);
}


void dkv_release(char *value) {
    mkv_release(value);
}


int dkv_close(void *dkv_h) {
    struct dkv_ctx *ctx = dkv_h;
    kv_close(ctx->indexer);
//	pp_close_ctx(ctx->indexer);
    mkv_close(ctx->mkv);
    free(ctx);
    return 0;//todo verify
}


void recursive_fill_kv(char const* dirname, void *dkv_h) {
    if (DEBUG) { printf("recursive_fill_kv dir: %s\n", dirname); }
    struct dirent *curr_ent;
    DIR* dirp = opendir(dirname);
    if (dirp == NULL) {
        printf("recursive_fill_kv couldn't open dir: %s\nZ", dirname);
        return;
    }

    while ((curr_ent = readdir(dirp)) != NULL) {
        if (!((strcmp(curr_ent->d_name, ".") == 0) ||
              (strcmp(curr_ent->d_name, "..") == 0))) {
            char* path = malloc(strlen(dirname) + strlen(curr_ent->d_name) + 2);
            strcpy(path, dirname);
            strcat(path, "/");
            strcat(path, curr_ent->d_name);

            if (curr_ent->d_type == DT_DIR) {
                recursive_fill_kv(path, dkv_h);

            } else if (curr_ent->d_type == DT_REG) {
                int fd = open(path, O_RDONLY);
                size_t fsize = lseek(fd, (size_t)0, SEEK_END);
//                void *p = mmap(0, fsize, PROT_READ, MAP_PRIVATE, fd, 0);
                void *p = mmap(0, fsize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE, fd, 0);
                /* TODO (1LOC): Add a print here to see you found the full paths... */
                printf("dkv_set path: %s, file size: %ld\n", path, fsize);
                dkv_set(dkv_h, path, p, fsize);
                munmap(p, fsize);
                close(fd);
            }

            free(path);
        }
    }
    closedir(dirp);
}

#define my_open    dkv_open
#define set(a,b,c) dkv_set(a,b,c,strlen(c))
#define get(a,b,c) dkv_get(a,b,c,&g_argc)
#define release    dkv_release
#define my_close   dkv_close
#endif /* EX4 */

//// end of kv_client copy
struct {
    char *ext;
    char *filetype;
} extensions [] = {
        {"gif", "image/gif" },
        {"jpg", "image/jpg" },
        {"jpeg","image/jpeg"},
        {"png", "image/png" },
        {"ico", "image/ico" },
        {"zip", "image/zip" },
        {"gz",  "image/gz"  },
        {"tar", "image/tar" },
        {"htm", "text/html" },
        {"html","text/html" },
        {0,0} };

void logger(int type, char *s1, char *s2, int socket_fd)
{
    int fd ;
    char logbuffer[BUFSIZE*2];

    switch (type) {
        case ERROR: (void)sprintf(logbuffer,"ERROR: %s:%s Errno=%d exiting pid=%d",s1, s2, errno,getpid());
            break;
        case FORBIDDEN:
            (void)write(socket_fd, "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type or operation is not allowed on this simple static file webserver.\n</body></html>\n",271);
            (void)sprintf(logbuffer,"FORBIDDEN: %s:%s",s1, s2);
            break;
        case NOTFOUND:
            (void)write(socket_fd, "HTTP/1.1 404 Not Found\nContent-Length: 136\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>\n",224);
            (void)sprintf(logbuffer,"NOT FOUND: %s:%s",s1, s2);
            break;
        case LOG: (void)sprintf(logbuffer," INFO: %s:%s:%d",s1, s2,socket_fd); break;
    }
    /* No checks here, nothing can be done with a failure anyway */
    if((fd = open("nweb.log", O_CREAT| O_WRONLY | O_APPEND,0644)) >= 0) {
        (void)write(fd,logbuffer,strlen(logbuffer));
        (void)write(fd,"\n",1);
        (void)close(fd);
    }
    if(type == ERROR || type == NOTFOUND || type == FORBIDDEN) exit(3);
}

/* this is a child web server process, so we can exit on errors */
//void web(int fd, int hit, char * dir_path)
void web(int fd, int hit)
{
    int j, file_fd, buflen;
    long i, ret, len;
    char * fstr;
    static char buffer[BUFSIZE+1]; /* static so zero filled */

    ret =read(fd,buffer,BUFSIZE); 	/* read Web request in one go */
    if(ret == 0 || ret == -1) {	/* read failure stop now */
        logger(FORBIDDEN,"failed to read browser request","",fd);
    }
    if(ret > 0 && ret < BUFSIZE)	/* return code is valid chars */
        buffer[ret]=0;		/* terminate the buffer */
    else buffer[0]=0;
    for(i=0;i<ret;i++)	/* remove CF and LF characters */
        if(buffer[i] == '\r' || buffer[i] == '\n')
            buffer[i]='*';
    logger(LOG,"request",buffer,hit);
    if( strncmp(buffer,"GET ",4) && strncmp(buffer,"get ",4) ) {
        logger(FORBIDDEN,"Only simple GET operation supported",buffer,fd);
    }
    for(i=4;i<BUFSIZE;i++) { /* null terminate after the second space to ignore extra stuff */
        if(buffer[i] == ' ') { /* string is "GET URL " +lots of other stuff */
            buffer[i] = 0;
            break;
        }
    }
    for(j=0;j<i-1;j++) 	/* check for illegal parent directory use .. */
        if(buffer[j] == '.' && buffer[j+1] == '.') {
            logger(FORBIDDEN,"Parent directory (..) path names not supported",buffer,fd);
        }
    if( !strncmp(&buffer[0],"GET /\0",6) || !strncmp(&buffer[0],"get /\0",6) ) /* convert no filename to index file */
        (void)strcpy(buffer,"GET /index.html");

    /* work out the file type and check we support it */
    buflen=strlen(buffer);
    fstr = (char *)0;
    for(i=0;extensions[i].ext != 0;i++) {
        len = strlen(extensions[i].ext);
        if( !strncmp(&buffer[buflen-len], extensions[i].ext, len)) {
            fstr =extensions[i].filetype;
            break;
        }
    }
    if(fstr == 0) logger(FORBIDDEN,"file extension type not supported",buffer,fd);

//	if(( file_fd = open(&buffer[5],O_RDONLY)) == -1) {  /* open the file for reading */
//		logger(NOTFOUND, "failed to open file",&buffer[5],fd);
//	}
//	logger(LOG,"SEND",&buffer[5],hit);
//	len = (long)lseek(file_fd, (off_t)0, SEEK_END); /* lseek to the file end to find the length */
//	      (void)lseek(file_fd, (off_t)0, SEEK_SET); /* lseek back to the file start ready for reading */
//          (void)sprintf(buffer,"HTTP/1.1 200 OK\nServer: nweb/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: %s\n\n", VERSION, len, fstr); /* Header + a blank line */
//	logger(LOG,"Header",buffer,hit);
//    (void)write(fd,buffer,strlen(buffer));
//
//    /* send file in 8KB block - last block may be smaller */
//    while (	(ret = read(file_fd, buffer, BUFSIZE)) > 0 ) {
//        (void)write(fd,buffer,ret);
//    }
//    sleep(1);	/* allow socket to drain before signalling the socket is closed */
//    close(fd);
//    exit(1);


    ////TODO DKV_GET code
    unsigned int file_length;
    size_t dir_path_len = strlen(top_dir);
    size_t buffer_len = strlen(&buffer[4]);
    char * key = (char *) calloc((strlen(&buffer[4]) + dir_path_len + 1), 1);
    memcpy(key, top_dir, dir_path_len);
//    key[dir_path_len] = '/';
    memcpy(&key[dir_path_len], &buffer[4], buffer_len+1);
//    memset(&key[dir_path_len], '/', 1);
//////
//// size_t dir_path_len = strlen(dir_path);
//    size_t buffer_len = strlen(&buffer[5]);
//    char * key = (char *) calloc((strlen(&buffer[5]) + dir_path_len + 2), 1);
//    memcpy(key, dir_path, dir_path_len);
////    key[dir_path_len] = '/';
//    memcpy(&key[dir_path_len+1], &buffer[5], buffer_len+1);
////    memset(&key[dir_path_len], '/', 1);
//////
    if (DEBUG) {
        printf("buffer: %s\n", buffer);
        printf("key: %s\n", key);
        printf("dir_path: %s\n", top_dir);
    }

    char * value = (char *) malloc(MAX_TEST_SIZE);
    if (dkv_get(kv_ctx, key, &value, &file_length) != 0) {
        logger(LOG, "DKV_GET", buffer, hit);
    }

    len = file_length;
    free(key);

    (void)sprintf(buffer,"HTTP/1.1 200 OK\nServer: nweb/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: %s\n\n", VERSION, len, fstr); /* Header + a blank line */
    logger(LOG,"Header",buffer,hit);


    (void)write(fd, buffer, strlen(buffer));
    (void)write(fd, value ,file_length);
    free(value);
//	/* send file in 8KB block - last block may be smaller */
//	while (	(ret = read(file_fd, buffer, BUFSIZE)) > 0 ) {
//		(void)write(fd,buffer,ret);
//	}
	sleep(1);	/* allow socket to drain before signalling the socket is closed */
    close(fd);
//    exit(1);
}


/**
 * This will init a dkvs client
 * @param argc
 * @param argv
 * @return 0 if initialized client successfully, 1 otherwise.
 */
void * init_dkvs_client(int argc, char **argv) {
    if (DEBUG) { printf("init_dkvs_client \n"); }
    void * kv_ctx; /* handle to internal KV-client context */
//    g_argc = argc - 3;
//    g_argv = &argv[3];
    g_argc = argc - 2;
    g_argv = &argv[2];

    struct kv_server_address * servers = NULL;
    struct kv_server_address * indexer = NULL;
    assert (parse_args(&indexer, &servers) == 0);
    assert(0 == my_open(servers, indexer, &kv_ctx));
    return kv_ctx;//todo check if needed
}


int main(int argc, char **argv)
{
    int i, port, pid, listenfd, socketfd, hit;
    socklen_t length;
    static struct sockaddr_in cli_addr; /* static = initialised to zeros */
    static struct sockaddr_in serv_addr; /* static = initialised to zeros */

//	if( argc < 3  || argc > 3 || !strcmp(argv[1], "-?") ) {
//		(void)printf("hint: nweb Port-Number Top-Directory\t\tversion %d\n\n"
    if( argc < 4 || !strcmp(argv[1], "-?")) {//todo
//		(void)printf("hint: nweb Port-Number Top-Directory <indexer> <server>...<server>\t\tversion %d\n\n"
        (void)printf("hint: nweb Port-Number <indexer> <server>...<server>\t\tversion %d\n\n"
                     "\tnweb is a small and very safe mini web server\n"
                     "\tnweb only servers out file/web pages with extensions named below\n"
                     "\t and only from the named directory or its sub-directories.\n"
                     "\tThere is no fancy features = safe and secure.\n\n"
                     "\tExample: nweb 8181 /home/nwebdir &\n\n"
                     "\tOnly Supports:", VERSION);
        for(i=0;extensions[i].ext != 0;i++)
            (void)printf(" %s",extensions[i].ext);

        (void)printf("\n\tNot Supported: URLs including \"..\", Java, Javascript, CGI\n"
                     "\tNot Supported: directories / /etc /bin /lib /tmp /usr /dev /sbin \n"
                     "\tNo warranty given or implied\n\tNigel Griffiths nag@uk.ibm.com\n"  );
        exit(0);
    }

    if (TEST_LOCATION[0] == '~') {
        strcpy(top_dir, getenv("HOME"));
        size_t home_len = strlen(top_dir);
//	    top_dir[home_len] = '/';
        strcpy(&top_dir[home_len], &TEST_LOCATION[1]);
        if (DEBUG) { printf("top_dir: %s\n", top_dir); }
    }
    ////todo
    if( !strncmp(top_dir,"/"   ,2 ) || !strncmp(top_dir,"/etc", 5 ) ||
        !strncmp(top_dir,"/bin",5 ) || !strncmp(top_dir,"/lib", 5 ) ||
        !strncmp(top_dir,"/tmp",5 ) || !strncmp(top_dir,"/usr", 5 ) ||
        !strncmp(top_dir,"/dev",5 ) || !strncmp(top_dir,"/sbin",6) ){
        (void)printf("ERROR: Bad top directory %s, see nweb -?\n",top_dir);
        exit(3);
    }
    if(chdir(top_dir) == -1){
        (void)printf("ERROR: Can't Change to directory %s\n",top_dir);
        exit(4);
    }
    ////end todo

//	if( !strncmp(argv[2],"/"   ,2 ) || !strncmp(argv[2],"/etc", 5 ) ||
//	    !strncmp(argv[2],"/bin",5 ) || !strncmp(argv[2],"/lib", 5 ) ||
//	    !strncmp(argv[2],"/tmp",5 ) || !strncmp(argv[2],"/usr", 5 ) ||
//	    !strncmp(argv[2],"/dev",5 ) || !strncmp(argv[2],"/sbin",6) ){
//		(void)printf("ERROR: Bad top directory %s, see nweb -?\n",argv[2]);
//		exit(3);
//	}
//	if(chdir(argv[2]) == -1){
//		(void)printf("ERROR: Can't Change to directory %s\n",argv[2]);
//		exit(4);
//	}

    //// init dkvs client & fill_kv
//    void * kv_ctx = init_dkvs_client(argc, argv);
    kv_ctx = init_dkvs_client(argc, argv);
    assert(kv_ctx != NULL);

    recursive_fill_kv(top_dir, kv_ctx);
//    recursive_fill_kv(argv[2], kv_ctx);//todo
    //// end of init dkvs client & fill_kv

//	/* Become deamon + unstopable and no zombies children (= no wait()) */
//	if(fork() != 0)
//		return 0; /* parent returns OK to shell */
//	(void)signal(SIGCLD, SIG_IGN); /* ignore child death */
//	(void)signal(SIGHUP, SIG_IGN); /* ignore terminal hangups */
//	for(i=0;i<32;i++)
//		(void)close(i);		/* close open files */
//	(void)setpgrp();		/* break away from process group */
    logger(LOG,"nweb starting",argv[1],getpid());
    /* setup the network socket */
    if((listenfd = socket(AF_INET, SOCK_STREAM,0)) <0)
        logger(ERROR, "system call","socket",0);
    port = atoi(argv[1]);
    if(port < 0 || port >60000)
        logger(ERROR,"Invalid port number (try 1->60000)",argv[1],0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);
    if(bind(listenfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) <0)
        logger(ERROR,"system call","bind",0);
    if( listen(listenfd,64) <0)
        logger(ERROR,"system call","listen",0);
    for(hit=1; ;hit++) {
        length = sizeof(cli_addr);
        if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
            logger(ERROR,"system call","accept",0);
        web(socketfd,hit); /* never returns *///todo
        close(socketfd);

//		if((pid = fork()) < 0) {
//			logger(ERROR,"system call","fork",0);
//		}
//		else {
//			if(pid == 0) { 	/* child */
//				(void)close(listenfd);
////				web(socketfd,hit); /* never returns */
//				web(socketfd,hit, argv[2]); /* never returns *///todo
//			} else { 	/* parent */
//				(void)close(socketfd);
//			}
//		}
    }
    close(listenfd);
}
