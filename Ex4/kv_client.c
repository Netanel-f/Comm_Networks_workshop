#include "kv_shared.h"

int g_argc;
char **g_argv;

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


static int pp_post_send(struct pingpong_context *ctx, enum ibv_wr_opcode opcode, unsigned size,
        const char *local_ptr, void *remote_ptr, uint32_t remote_key) {
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
    printf("  %s <kv_indexer-host> <kv_server#1-host> ... ... <kv_server#N-host>     "
           "connect to indexer at <indexer-host>  and #n servers\n", argv0);
    printf("     and use the default port set in kv_shared header file.\n");
    printf("  %s <kv_indexer-host:port> <kv_server#1-host:port> ... ... <kv_server#N-host:port>     "
           "connect to indexer at <indexer-host>  and #n servers\n", argv0);
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
    if (g_argc < 3) {
        usage(g_argv[0]);
        exit(1);
    }
    struct kv_server_address * temp_indexer = (struct kv_server_address *) malloc(sizeof(struct kv_server_address));
    struct kv_server_address * temp_servers = (struct kv_server_address *) malloc((g_argc - 1) * sizeof(struct kv_server_address));

    // indexer
    temp_copy = strdup(g_argv[1]);
    temp_indexer->servername = strtok(temp_copy, ":");
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
    // servers
    for (int arg_idx = 2; arg_idx < g_argc; arg_idx++) {
        temp_copy = strdup(g_argv[arg_idx]);
        temp_servers[arg_idx-2].servername = strtok(temp_copy, ":");

        port_token = strtok(NULL, ":");
        if (port_token != NULL) {
            port = strtol(port_token, NULL, 0);
            if (port < 0 || port > 65535) {
                return 1;
            }
            temp_servers[arg_idx-2].port = port;
        } else {
            temp_servers[arg_idx-2].port = DEFAULT_SRV_PORT;
        }
    }

    temp_servers[g_argc-2].servername = NULL;
    temp_servers[g_argc-2].port = 0;
    *indexer = temp_indexer;
    *servers = temp_servers;
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
    return 0;
#endif
}


int orig_main(struct kv_server_address *server, unsigned size, int argc, char *argv[],
        struct pingpong_context **result_ctx) {
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


int kv_set(void *kv_handle, const char *key, const char *value) {
    struct pingpong_context *ctx = kv_handle;
    struct packet *set_packet = (struct packet*)ctx->buf;

    unsigned key_length = strlen(key);
    unsigned value_length = strlen(value);
    unsigned packet_size = key_length + value_length + sizeof(struct packet) + 2;

    if (DEBUG) { printf("kv_set key %s value %s\n", key, value); }

    if (packet_size < EAGER_PROTOCOL_LIMIT) {
        if (strcmp(key, last_rndv_accessed_key) == 0) {

            memset(last_rndv_accessed_key, '\0', key_length);
            last_accessed_key_val_length = 0;
        }

        /* Eager protocol - exercise part 1 */
        set_packet->type = EAGER_SET_REQUEST;
        set_packet->eager_set_request.value_length = value_length;
        memcpy(set_packet->eager_set_request.key_and_value, key, key_length + 1);
        memcpy(&set_packet->eager_set_request.key_and_value[key_length + 1], value, value_length + 1);

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
//        if (DEBUG) { printf("kv_set key %s == last rndv: %s\n", key, last_rndv_accessed_key); }
        last_accessed_key_val_length = value_length;
        // set temp mr
        struct ibv_mr *orig_mr = ctx->mr;
        struct ibv_mr *temp_mr = ibv_reg_mr(ctx->pd, (void *) value, value_length + 1,
                IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);

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
    set_packet->rndv_set_request.value_length = value_length + 1;
    memcpy(set_packet->rndv_set_request.key, key, key_length + 1);

    /* Posts a receive-buffer for RENDEZVOUS_SET_RESPONSE */
    pp_post_recv(ctx, 1);
    /* Sends the packet to the server */
    pp_post_send(ctx, IBV_WR_SEND, packet_size, NULL, NULL, 0);


    /* wait for both to complete */
    assert(pp_wait_completions(ctx, 2) == 0);
    assert(set_packet->type == RENDEZVOUS_SET_RESPONSE);
    // set temp mr
    struct ibv_mr * orig_mr = ctx->mr;
    struct ibv_mr * temp_mr = ibv_reg_mr(ctx->pd, (void*)value, value_length + 1,
            IBV_ACCESS_LOCAL_WRITE |IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);

    ctx->mr = temp_mr;

    /* update cache */
    strcpy(last_rndv_accessed_key, key);
    last_accessed_key_val_length = value_length;
    last_accessed_server_addr = set_packet->rndv_set_response.server_ptr;
    last_accessed_server_rkey = set_packet->rndv_set_response.server_key;

    pp_post_send(ctx, IBV_WR_RDMA_WRITE, value_length + 1, value,
            (void *)set_packet->rndv_set_response.server_ptr, set_packet->rndv_set_response.server_key);

    int ret_value = pp_wait_completions(ctx, 1);
    ctx->mr = orig_mr;
    ibv_dereg_mr(temp_mr);
    return ret_value;
}


int kv_get(void *kv_handle, const char *key, char **value) {
    struct pingpong_context *ctx = kv_handle;
    unsigned int key_length = strlen(key);
    struct packet * get_packet = (struct packet*)ctx->buf;

    //// Key length assumption: key length < 4KB
    unsigned packet_size = key_length + 1 + sizeof(struct packet);

    if (packet_size > EAGER_PROTOCOL_LIMIT) {
        printf("ERROR");
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

        struct packet *response_packet = (struct packet *) ctx->buf;
        unsigned int value_len;
        switch (response_packet->type) {

            case EAGER_GET_RESPONSE:
                if (DEBUG) { printf("kv_get key %s, EAGER_GET_RESPONSE\n", key); }
                value_len = response_packet->eager_get_response.value_length;
                *value = (char *) malloc(value_len + 1);
                memcpy(*value, response_packet->eager_get_response.value, value_len);
                memset(&((*value)[value_len]), '\0', 1);
                break;

            case RENDEZVOUS_GET_RESPONSE:
                if (DEBUG) { printf("kv_get key %s, RENDEZVOUS_GET_RESPONSE\n", key); }
                value_len = response_packet->rndv_get_response.value_length;
                *value = calloc(value_len + 1, 1);

                /* update cache */
                memcpy(last_rndv_accessed_key, key, key_length + 1);
                last_accessed_key_val_length = value_len;
                last_accessed_server_addr = response_packet->rndv_get_response.server_ptr;
                last_accessed_server_rkey = response_packet->rndv_get_response.server_key;

                // set temp mr
                struct ibv_mr *orig_mr = ctx->mr;
                struct ibv_mr *temp_mr = ibv_reg_mr(ctx->pd, (void *) *value, value_len + 1,
                        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);

                ctx->mr = temp_mr;

                pp_post_send(ctx, IBV_WR_RDMA_READ, value_len + 1, *value,
                             (void *) response_packet->rndv_get_response.server_ptr,
                             response_packet->rndv_get_response.server_key);
                pp_wait_completions(ctx, 1);
                ctx->mr = orig_mr;
                ibv_dereg_mr(temp_mr);
                break;

            default:
                printf("ERROR");
                return -1;
        }

    } else {
        *value = calloc(last_accessed_key_val_length + 1, 1);

        // set temp mr
        struct ibv_mr *orig_mr = ctx->mr;
        struct ibv_mr *temp_mr = ibv_reg_mr(ctx->pd, (void *) *value, last_accessed_key_val_length + 1,
                IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);

        ctx->mr = temp_mr;

        pp_post_send(ctx, IBV_WR_RDMA_READ, last_accessed_key_val_length, *value,
                     (void *) last_accessed_server_addr,
                     last_accessed_server_rkey);

        pp_wait_completions(ctx, 1);
        ctx->mr = orig_mr;
        ibv_dereg_mr(temp_mr);
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

    /* Sends the packet to the server */
    pp_post_send(ctx, IBV_WR_SEND, packet_size, NULL, NULL, 0);

    /* await EAGER_GET_REQUEST completion */
    pp_wait_completions(ctx, 1);

    return pp_close_ctx((struct pingpong_context*)kv_handle);
}


#ifdef EX3
#define my_open  kv_open
#define set      kv_set
#define get      kv_get
#define release  kv_release
#define my_close kv_close
#endif /* EX3 */



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
	ctx->num_servers = count - 1;
	for (count = 0; count < ctx->num_servers; count++) {
	    if (DEBUG) { printf("connecting to \'%s\' port %d\n", servers[count].servername, servers[count].port); }
		if (orig_main(&servers[count], EAGER_PROTOCOL_LIMIT, g_argc, g_argv, &ctx->kv_ctxs[count])) {
			return 1;
		}
	}

	*mkv_h = ctx;
	return 0;
}

int mkv_set(void *mkv_h, unsigned kv_id, const char *key, const char *value) {
	struct mkv_ctx *ctx = mkv_h;
	return kv_set(ctx->kv_ctxs[kv_id], key, value);
}

int mkv_get(void *mkv_h, unsigned kv_id, const char *key, char **value) {
	struct mkv_ctx *ctx = mkv_h;
	return kv_get(ctx->kv_ctxs[kv_id], key, value);
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
	if (orig_main(indexer, EAGER_PROTOCOL_LIMIT, g_argc, g_argv, &ctx->indexer)) {
		return 1;
	}
	if (mkv_open(servers, (void**)&ctx->mkv)) {
		return 1;
	}
	*dkv_h = ctx;
	return 0;
}


int dkv_set(void *dkv_h, const char *key, const char *value, unsigned length) {
	struct dkv_ctx *ctx = dkv_h;
    struct packet *set_packet = (struct packet*)ctx->indexer->buf;
    unsigned packet_size = strlen(key) + sizeof(struct packet);

    /* Step #1: The client sends the Index server FIND(key, #kv-servers) */
    set_packet->type = FIND;
	set_packet->find.num_of_servers = ctx->mkv->num_servers;
	strcpy(set_packet->find.key, key);

    /* Posts a receive-buffer for LOCATION */
    pp_post_recv(ctx->indexer, 1);
    /* Sends the packet to the server */
    pp_post_send(ctx->indexer, IBV_WR_SEND, packet_size, NULL, NULL, 0);
    /* wait for both to complete */
    assert(pp_wait_completions(ctx->indexer, 2) == 0);

    /* Step #2: The Index server responds with LOCATION(#kv-server-id) */
    if (DEBUG) {printf("packet type: %d\n", set_packet->type); }
    assert(set_packet->type == LOCATION);

    /* Step #3: The client contacts KV-server with the ID returned in LOCATION, using SET/GET messages. */
	return mkv_set(ctx->mkv, set_packet->location.selected_server, key, value);
}

int dkv_get(void *dkv_h, const char *key, char **value, unsigned * length) {
    struct dkv_ctx *ctx = dkv_h;
    struct packet *set_packet = (struct packet*)ctx->indexer->buf;
    unsigned packet_size = strlen(key) + sizeof(struct packet);

    /* Step #1: The client sends the Index server FIND(key, #kv-servers) */
    set_packet->type = FIND;
    set_packet->find.num_of_servers = ctx->mkv->num_servers;
    strcpy(set_packet->find.key, key);

    /* Posts a receive-buffer for LOCATION */
    pp_post_recv(ctx->indexer, 1);
    /* Sends the packet to the server */
    pp_post_send(ctx->indexer, IBV_WR_SEND, packet_size, NULL, NULL, 0);
    /* wait for both to complete */
    assert(pp_wait_completions(ctx->indexer, 2) == 0);

    /* Step #2: The Index server responds with LOCATION(#kv-server-id) */
    assert(set_packet->type == LOCATION);

    /* Step #3: The client contacts KV-server with the ID returned in LOCATION, using SET/GET messages. */
    return mkv_get(ctx->mkv, set_packet->location.selected_server, key, value);
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
    return 0;
}

#define my_open    dkv_open
#define set(a,b,c) dkv_set(a,b,c,strlen(c))
#define get(a,b,c) dkv_get(a,b,c,&g_argc)
#define release    dkv_release
#define my_close   dkv_close
#endif /* EX4 */



void print_results_to_file(FILE * results_file, ssize_t value_size, double throughput) {
    char * value_size_unit;
    char * rate_unit;

    if (throughput >= GIGABIT_IN_BITS) {
        throughput /= GIGABIT_IN_BITS;
        rate_unit = "Gbps";

    } else if (throughput >= MEGABIT_IN_BITS) {
        throughput /= MEGABIT_IN_BITS;
        rate_unit = "Mbps";

    } else if (throughput >= KILOBIT_IN_BITS) {
        throughput /= KILOBIT_IN_BITS;
        rate_unit = "Kbps";

    } else {
        rate_unit = "bps";
    }

    if (value_size >= MEGABYTE_IN_BYTES) {
        value_size /= MEGABIT_IN_BITS;
        value_size_unit = "MBytes";

    } else if (value_size >= KILOBYTE_IN_BYTES) {
        value_size /= KILOBIT_IN_BITS;
        value_size_unit = "KBytes";

    } else {
        value_size_unit = "Bytes";
    }

    fprintf(results_file, "Value size: %ld\t%s,\tThroughput: %.3f\t%s\n",
            value_size, value_size_unit, throughput, rate_unit);
}


void run_throughput_tests(void *kv_ctx, FILE * results_file) {
    char send_buffer[MAX_TEST_SIZE] = {0};
    char *recv_buffer;

    unsigned packet_struct_size = sizeof(struct packet);


    for (ssize_t value_size = 1; value_size < MAX_TEST_SIZE; value_size = value_size<< 1) {
        struct timeval start, end;
        double total_time_usec = 0.0;
//        int total_bytes = 0;
        ssize_t total_bytes = 0;
        int total_attempts = THROUGHPUT_ATTEMPTS;
        memset(send_buffer, 'a', value_size);

        if (gettimeofday(&start, NULL)) {
            perror("gettimeofday");
            break;
        }

        char key[10];
        for (int attempt = 0; attempt < total_attempts; attempt++) {
            sprintf(key, "%ld-%d", value_size, attempt);
            set(kv_ctx, key, send_buffer);
            get(kv_ctx, key, &recv_buffer);
//            assert(0 == strcmp(send_buffer, recv_buffer));
//            total_bytes = total_bytes + 2 * (strlen(key) + 1 + value_size + packet_struct_size);
            release(recv_buffer);
        }

        if (gettimeofday(&end, NULL)) {
            perror("gettimeofday");
            break;
        }

        if (value_size < EAGER_PROTOCOL_LIMIT) {
            total_bytes = total_attempts * 2 * (strlen(key) + 1 + value_size + 1) + 3 * packet_struct_size;
        } else {
            total_bytes = total_attempts * (strlen(key) + 1 + value_size + 2*packet_struct_size);
        }

        total_time_usec = ((end.tv_sec - start.tv_sec) * 1000000) + (end.tv_usec - start.tv_usec);
        long total_bits_trans = total_bytes * 8;
        double total_time_sec = total_time_usec / 1000000;
        double throughput = total_bits_trans / total_time_sec;
        print_results_to_file(results_file, value_size, throughput);
        fflush(stdout);
    }
}


int main(int argc, char **argv)
{
    void *kv_ctx; /* handle to internal KV-client context */

    char send_buffer[MAX_TEST_SIZE] = {0};
    char *recv_buffer;


    g_argc = argc;
    g_argv = argv;
    struct kv_server_address * servers = NULL;
    struct kv_server_address * indexer = NULL;

#ifdef EX4
    if(DEBUG) { printf("before parse args\n"); }
    assert (parse_args(&indexer, &servers) == 0);
    if(DEBUG) { printf("before my_open %s-%d\t%s-%d\t%s-%d\n",
            indexer->servername, indexer->port,
            servers[0].servername, servers[0].port,
            servers[1].servername, servers[1].port); }
    assert(0 == my_open(servers, indexer, &kv_ctx));
#else
    assert (parse_args(NULL, &servers) == 0);
    assert(0 == my_open(&servers[0], &kv_ctx));
#endif

    /* Test throughput */
    FILE * results_file;
    results_file = fopen("RESULTS.txt", "w+");
    run_throughput_tests(kv_ctx, results_file);
    fclose(results_file);

    // Read contents from file
    results_file = fopen("RESULTS.txt", "r");
    char c = fgetc(results_file);
    while (c != EOF)
    {
        printf ("%c", c);
        c = fgetc(results_file);
    }
    fclose(results_file);

//    /* Test small size */
//    assert(100 < MAX_TEST_SIZE);
//    memset(send_buffer, 'a', 100);
//    if (DEBUG) { printf("before set 1 aX100\n"); }
//    assert(0 == set(kv_ctx, "1", send_buffer));
//    if (DEBUG) { printf("before get 1\n"); }
//    assert(0 == get(kv_ctx, "1", &recv_buffer));
//    if (DEBUG) { printf("before strcmp 1\n"); }
//    assert(0 == strcmp(send_buffer, recv_buffer));
//    if (DEBUG) { printf("before release 1\n"); }
//    release(recv_buffer);
//    if (DEBUG) { printf("after release 1\n"); }
//
//    /* Test logic */
//    if (DEBUG) { printf("before get 1 aX100\n"); }
//    assert(0 == get(kv_ctx, "1", &recv_buffer));
//    if (DEBUG) { printf("before strcmp 2\n"); }
//    assert(0 == strcmp(send_buffer, recv_buffer));
//    if (DEBUG) { printf("before release 2\n"); }
//    release(recv_buffer);
//    if (DEBUG) { printf("after release 2\n"); }
//    memset(send_buffer, 'b', 100);
//    if (DEBUG) { printf("before set 1 bX100\n"); }
//    assert(0 == set(kv_ctx, "1", send_buffer));
//    memset(send_buffer, 'c', 100);
//    if (DEBUG) { printf("before set 22 cX100\n"); }
//    assert(0 == set(kv_ctx, "22", send_buffer));
//    memset(send_buffer, 'b', 100);
//    if (DEBUG) { printf("before get 1 bx100\n"); }
//    assert(0 == get(kv_ctx, "1", &recv_buffer));
//    if (DEBUG) { printf("before strcmp 3\n"); }
//    assert(0 == strcmp(send_buffer, recv_buffer));
//    if (DEBUG) { printf("before release 3\n"); }
//    release(recv_buffer);
//    if (DEBUG) { printf("after release 3\n"); }
//
//    /* Test large size */
//    memset(send_buffer, 'a', MAX_TEST_SIZE - 1);
//    if (DEBUG) { printf("before set 1 aXBIG\n"); }
//    assert(0 == set(kv_ctx, "1", send_buffer));
//    if (DEBUG) { printf("before set 333 aXBIG\n"); }
//    assert(0 == set(kv_ctx, "333", send_buffer));
//    if (DEBUG) { printf("before get 1 eeee\n"); }
//    assert(0 == get(kv_ctx, "1", &recv_buffer));
//    if (DEBUG) { printf("before strcmp 4\n"); }
//    assert(0 == strcmp(send_buffer, recv_buffer));
//    if (DEBUG) { printf("before release 4\n"); }
//    release(recv_buffer);
//    if (DEBUG) { printf("after release 4\n"); }


    my_close(kv_ctx);
    return 0;
}
