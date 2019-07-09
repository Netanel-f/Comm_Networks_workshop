#include "kv_shared.h"

int g_argc;
char **g_argv;

/* server data structs for use */


/* key-value database */
/* a struct to maintain key-value node of database */
typedef struct KV_ENTRY{
    struct KV_ENTRY * prev_entry;
    struct KV_ENTRY * next_entry;
    unsigned int key_len;
//    unsigned int val_len;//todo
    MEMORY_INFO * large_val_mem_info; // for values of size > 4KB - use large memory.
    char * key;
    char * value; // NULL for values > 4KB.
} KV_ENTRY;

/* important pointer of data base.*/
KV_ENTRY * entries_head = NULL;
KV_ENTRY * entries_tail = NULL;
int entries_counter = 0;

MEMORY_INFO * mem_pool_head = NULL;
MEMORY_INFO * mem_pool_tail = NULL;
MEMORY_INFO * tainted_mem_pool_head = NULL;
MEMORY_INFO * tainted_mem_pool_tail = NULL;
int pool_size = 0;

bool close_server = false;

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
    printf("Usage:\n");
    printf("  %s            start a server and wait for connection\n", argv0);
    printf("  %s <host>     connect to server at <host>\n", argv0);
    printf("\n");
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

    while (1) {
        int c;

        static struct option long_options[] = {
                { .name = "port",     .has_arg = 1, .val = 'p' },
                { .name = "ib-dev",   .has_arg = 1, .val = 'd' },
                { .name = "ib-port",  .has_arg = 1, .val = 'i' },
                { .name = "size",     .has_arg = 1, .val = 's' },
                { .name = "mtu",      .has_arg = 1, .val = 'm' },
                { .name = "rx-depth", .has_arg = 1, .val = 'r' },
                { .name = "iters",    .has_arg = 1, .val = 'n' },
                { .name = "sl",       .has_arg = 1, .val = 'l' },
                { .name = "events",   .has_arg = 0, .val = 'e' },
                { .name = "gid-idx",  .has_arg = 1, .val = 'g' },
                { 0 }
        };

        c = getopt_long(argc, argv, "p:d:i:s:m:r:n:l:eg:", long_options, NULL);
        if (c == -1)
            break;

        switch (c) {
            case 'p':
                port = strtol(optarg, NULL, 0);
                if (port < 0 || port > 65535) {
                    usage(argv[0]);
                    return 1;
                }
                break;

            case 'd':
                ib_devname = strdup(optarg);
                break;

            case 'i':
                ib_port = strtol(optarg, NULL, 0);
                if (ib_port < 0) {
                    usage(argv[0]);
                    return 1;
                }
                break;

            case 's':
                size = strtol(optarg, NULL, 0);
                break;

            case 'm':
                mtu = pp_mtu_to_enum(strtol(optarg, NULL, 0));
                if (mtu < 0) {
                    usage(argv[0]);
                    return 1;
                }
                break;

            case 'r':
                rx_depth = strtol(optarg, NULL, 0);
                break;

            case 'n':
                iters = strtol(optarg, NULL, 0);
                break;

            case 'l':
                sl = strtol(optarg, NULL, 0);
                break;

            case 'e':
                ++use_event;
                break;

            case 'g':
                gidx = strtol(optarg, NULL, 0);
                break;

            default:
                usage(argv[0]);
                return 1;
        }
    }

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


unsigned long hash_key(unsigned char *str) {
    // djb2-this algorithm (k=33) was first reported by dan bernstein many years ago in comp.lang.c.
    // another version of this algorithm (now favored by bernstein)
    // uses xor: hash(i) = hash(i - 1) * 33 ^ str[i];
    // the magic of number 33 (why it works better than many other constants, prime or not)
    // has never been adequately explained.
    unsigned long hash = 5381;
    int c;

    while (c = *str++)
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;
}


void handle_server_packets_only(struct pingpong_context *ctx, struct packet *packet) {
    struct packet * response_packet = ctx->buf;
    unsigned response_size = 0;
    KV_ENTRY * current_node = entries_head;
    unsigned int key_length;
    unsigned int value_length;
    unsigned long hash_value;
    if (DEBUG) { printf("handle server type: %d\n", packet->type); }
    printf("~~handle server type: %d\n", packet->type);
    switch (packet->type) {
        /* Only handle packets relevant to the server here - client will handle inside get/set() calls */
        case EAGER_GET_REQUEST:

            while (current_node != NULL) {
                if (strcmp(current_node->key, packet->eager_get_request.key) == 0) {
                    /* found match */
//                    struct packet * response_packet = ctx->buf;

                    if (current_node->value != NULL) {
                        /* small value */
                        response_packet->type = EAGER_GET_RESPONSE;
//                        response_packet->eager_get_response.value_length = current_node->val_len;
                        response_packet->eager_get_response.value_length = strlen(current_node->value);
//                        memcpy(response_packet->eager_get_response.value, current_node->value, current_node->val_len + 1);
                        memcpy(response_packet->eager_get_response.value, current_node->value, strlen(current_node->value) + 1);
                        response_size = sizeof(struct packet) + strlen(current_node->value);
//                        response_size = sizeof(struct packet) + current_node->val_len;

                    } else {
                        ///* need to response with RNDV */
                        response_packet->type = RENDEZVOUS_GET_RESPONSE;
//                        response_packet->rndv_get_response.value_length = current_node->val_len;
                        response_packet->rndv_get_response.value_length = strlen(current_node->large_val_mem_info->rndv_buffer);
                        response_packet->rndv_get_response.server_ptr = (uint64_t) current_node->large_val_mem_info->rndv_mr->addr;
                        response_packet->rndv_get_response.server_key = current_node->large_val_mem_info->rndv_mr->rkey;
                        response_size = sizeof(struct packet);
                    }
                    break;
                }
                current_node = current_node->next_entry;
            }

            if (current_node == NULL) {
                /* key is not exists on server, respond "" */
                struct packet *response_packet = ctx->buf;
                response_packet->type = EAGER_GET_RESPONSE;
                memset(response_packet->eager_get_response.value, '\0', 1);
                response_packet->eager_get_response.value_length = 1;
                response_size = sizeof(response_packet) + 1;
            }
            break;

        case EAGER_SET_REQUEST:
            key_length = strlen(packet->eager_set_request.key_and_value);
            value_length = strlen(&packet->eager_set_request.key_and_value[key_length + 1]);

            while (current_node != NULL) {
                /* looking if key already exists */
                if (strcmp(current_node->key, packet->eager_set_request.key_and_value) == 0) {
                    /* found match */
                    // todo what if node is RNDV?
                    if (current_node->value == NULL) {
                        /* current node is RNDV */
                        if (tainted_mem_pool_head == NULL) {
                            tainted_mem_pool_head = current_node->large_val_mem_info;
                            tainted_mem_pool_tail = current_node->large_val_mem_info;

                        } else {
                            tainted_mem_pool_tail->next_mem = current_node->large_val_mem_info;
                            tainted_mem_pool_tail = current_node->large_val_mem_info;
                        }
                        current_node->large_val_mem_info = NULL;
//                        current_node->val_len = value_length;
                        current_node->value = (char *) calloc(value_length, 1);

//                    } else if (current_node->val_len != value_length) {
                    } else if (strlen(current_node->value) != value_length) {
                        free(current_node->value);
                        current_node->value = (char *) calloc(value_length, 1);
//                        current_node->val_len = value_length;
                    }

                    memcpy(current_node->value, &packet->eager_set_request.key_and_value[key_length+1], value_length);
                    break;

                } else {
                    current_node = current_node->next_entry;
                }
            }

            if (current_node == NULL) {
                /* key wasn't found in DB so we need to create it. */
                KV_ENTRY * temp_node = (KV_ENTRY *) malloc(sizeof(KV_ENTRY));
                temp_node->key = calloc(key_length, 1);
                temp_node->value = calloc(value_length, 1);

                strcpy(temp_node->key, packet->eager_set_request.key_and_value);
                strcpy(temp_node->value, &packet->eager_set_request.key_and_value[key_length + 1]);

                /* fix pointers */
                temp_node->next_entry = NULL;
                temp_node->key_len = key_length;
//                temp_node->val_len = value_length;
                temp_node->large_val_mem_info = NULL;
                if (entries_tail == NULL) {
                    temp_node->prev_entry = NULL;
                    entries_head = temp_node;
                    entries_tail = temp_node;
                } else {
                    temp_node->prev_entry = entries_tail;
                    entries_tail->next_entry = temp_node;
                    entries_tail = temp_node;
                }
                entries_counter++;
            }
            break;

        case RENDEZVOUS_GET_REQUEST:
            break;

        case RENDEZVOUS_SET_REQUEST:
            key_length = strlen(packet->rndv_set_request.key);
            value_length = packet->rndv_set_request.value_length;
//            struct packet * response_packet = ctx->buf;
            while (current_node != NULL) {
                /* looking if key already exists */
                if (strcmp(current_node->key, packet->rndv_set_request.key) == 0) {
                    /* found match */
                    if (current_node->large_val_mem_info == NULL) {
                        /* need to assign large memory */
//                        current_node->val_len = value_length;
                        current_node->large_val_mem_info = mem_pool_head;
                        mem_pool_head = mem_pool_head->next_mem;
                        current_node->large_val_mem_info->next_mem = NULL;
                        pool_size--;

                        /* clear value field */
                        free(current_node->value);
                        current_node->value = NULL;

                    } else {
                        /* a large mem already exists */
//                        current_node->val_len = value_length;
                    }

                    response_packet->type = RENDEZVOUS_SET_RESPONSE;
                    response_packet->rndv_set_response.server_ptr = (uint64_t) current_node->large_val_mem_info->rndv_mr->addr;
                    response_packet->rndv_set_response.server_key = current_node->large_val_mem_info->rndv_mr->rkey;
                    response_size = sizeof(struct packet);
                    if (DEBUG) { printf("REND response key: %s, server_addr %ld, server_rkey %d, value length %d\n", packet->rndv_set_request.key, packet->rndv_set_response.server_ptr, packet->rndv_set_response.server_key, value_length); }
                    break;

                } else {
                    current_node = current_node->next_entry;
                }
            }

            if (current_node == NULL) {
                if (DEBUG) { printf("current node null\n"); }
                /* key wasn't found in DB so we need to create it. */
                KV_ENTRY * temp_node = (KV_ENTRY *) malloc(sizeof(KV_ENTRY));
                temp_node->key = calloc(key_length, 1);
                temp_node->value = NULL;


                strcpy(temp_node->key, packet->rndv_set_request.key);


                /* fix pointers */
                temp_node->next_entry = NULL;
                temp_node->key_len = key_length;
//                temp_node->val_len = value_length;
                if (entries_tail == NULL) {
                    temp_node->prev_entry = NULL;
                    entries_head = temp_node;
                    entries_tail = temp_node;

                } else {
                    temp_node->prev_entry = entries_tail;
                    entries_tail->next_entry = temp_node;
                    entries_tail = temp_node;
                }

                entries_counter++;

                /* need to assign large memory */
                if (DEBUG) { printf("need to assign large mem\n"); }
                temp_node->large_val_mem_info = mem_pool_head;
                mem_pool_head = mem_pool_head->next_mem;
                temp_node->large_val_mem_info->next_mem = NULL;
                pool_size--;

                response_packet->type = RENDEZVOUS_SET_RESPONSE;
                response_packet->rndv_set_response.server_ptr = (uint64_t) temp_node->large_val_mem_info->rndv_mr->addr;
                response_packet->rndv_set_response.server_key = temp_node->large_val_mem_info->rndv_mr->rkey;
                response_size = sizeof(struct packet);
                if (DEBUG) { printf("REND response key: %s, server_addr %ld, server_rkey %d, value length %d\n", temp_node->key, packet->rndv_set_response.server_ptr, packet->rndv_set_response.server_key, value_length); }
            }

            break;

        case CLOSE_CONNECTION:
            if (DEBUG) { printf("received CLOSE_CONNECTION packet\n"); }
            close_server = true;
            break;
#ifdef EX4
        case FIND: /* TODO (2LOC): use some hash function */
            hash_value = hash_key((unsigned char *)packet->find.key);
            hash_value = hash_value % (packet->find.num_of_servers);
            response_packet->type = LOCATION;
            response_packet->location.selected_server = hash_value;
            response_size = sizeof(struct packet);
            break;

#endif
        default:
            break;
    }

    if (response_size) {
        if (DEBUG) { printf("response size %d type %d\n", response_size, packet->type); }
        pp_post_send(ctx, IBV_WR_SEND, response_size, NULL, NULL, 0);
    }
}


int maintain_pool(struct pingpong_context *ctx) {
    while (tainted_mem_pool_head != NULL) {
        memset(tainted_mem_pool_head->rndv_buffer, '\0', MAX_TEST_SIZE);
        if (mem_pool_tail != NULL) {
            mem_pool_tail->next_mem = tainted_mem_pool_head;
        } else {
            mem_pool_head = tainted_mem_pool_head;
            mem_pool_tail = tainted_mem_pool_head;
        }
        pool_size++;
        tainted_mem_pool_head = tainted_mem_pool_head->next_mem;
    }

    while (pool_size < MIN_POOL_NODES) {
        if (pool_size == 0) {
            mem_pool_head = (struct MEMORY_INFO *) malloc(sizeof(MEMORY_INFO));
            mem_pool_tail = mem_pool_head;
        } else {
            mem_pool_tail->next_mem = (struct MEMORY_INFO *) malloc(sizeof(MEMORY_INFO));
            mem_pool_tail = mem_pool_tail->next_mem;
        }

        mem_pool_tail->next_mem = NULL;
        memset(mem_pool_tail->rndv_buffer, '\0', MAX_TEST_SIZE);
        mem_pool_tail->rndv_mr = ibv_reg_mr(ctx->pd, &(mem_pool_tail->rndv_buffer),
                                            MAX_TEST_SIZE, IBV_ACCESS_LOCAL_WRITE |
                                                           IBV_ACCESS_REMOTE_WRITE |
                                                           IBV_ACCESS_REMOTE_READ);
        if (!mem_pool_tail->rndv_mr) {
            fprintf(stderr, "Couldn't register MR\n");
            return 1;
        }
        pool_size++;
    }

    return 0;
}

int clear_server_data() {
    /* deregister mr of structs */
    while (mem_pool_head != NULL) {
        if (ibv_dereg_mr(mem_pool_head->rndv_mr)) {
            fprintf(stderr, "Couldn't deregister MR\n");
            return 1;
        }
        struct MEMORY_INFO * temp = mem_pool_head->next_mem;
        free(mem_pool_head);
        mem_pool_head = temp;
    }
    if (DEBUG) {printf("after mem_pool clean\n"); }

    while (tainted_mem_pool_head != NULL) {
        if (ibv_dereg_mr(tainted_mem_pool_head->rndv_mr)) {
            fprintf(stderr, "Couldn't deregister MR\n");
            return 1;
        }
        struct MEMORY_INFO * temp = tainted_mem_pool_head->next_mem;
        free(tainted_mem_pool_head);
        tainted_mem_pool_head = temp;
    }
    if (DEBUG) {printf("after tainted_mem_pool clean\n"); }

    while (entries_head != NULL) {
        if (entries_head->large_val_mem_info != NULL) {
            if (ibv_dereg_mr(entries_head->large_val_mem_info->rndv_mr)) {
                fprintf(stderr, "Couldn't deregister MR\n");
                return 1;
            }
            free(entries_head->large_val_mem_info);
        }
        free(entries_head->value);
        free(entries_head->key);
        KV_ENTRY * temp = entries_head->next_entry;
        free(entries_head);
        entries_head = temp;
    }
    if (DEBUG) {printf("after entries clean\n"); }

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
                    handle_server_packets_only(ctx, (struct packet*)ctx->buf);
                    if (close_server) {
                        if (DEBUG) {printf("close_server = true\n"); }
                        if (clear_server_data() == 1) {
                            fprintf(stderr, "Error while closing server\n");
                        }
                        return -1;
                    }
                    pp_post_recv(ctx, 1);
                    break;

                default:
                    fprintf(stderr, "Completion for unknown wr_id %d\n",
                            (int) wc[i].wr_id);
                    return 1;
            }
        }

        if (maintain_pool(ctx) == 1) {
            fprintf(stderr, "Error while closing server\n");
            return -1;
        }
    }
    return 0;
}


void run_server() {
    struct pingpong_context *ctx;
    struct kv_server_address server = {0};
    server.port = 12345;
    assert(0 == orig_main(&server, EAGER_PROTOCOL_LIMIT, g_argc, g_argv, &ctx));
    printf("AFTER origmain assert\n");
    if (maintain_pool(ctx) == 1) {
        fprintf(stderr, "Error while closing server\n");
        return;
    }
    printf("AFTER RUNSRV-ORIG MAIN\n");
    while (0 <= pp_wait_completions(ctx, 1));
    printf("AFTER RUNSRV-WAITCOMP\n");
    pp_close_ctx(ctx);
}


int main(int argc, char **argv) {
//    void *kv_ctx; /* handle to internal KV-client context */
//todo
//    char send_buffer[MAX_TEST_SIZE] = {0};
//    char *recv_buffer;

//    struct kv_server_address servers[2] = {
//            {
//                    .servername = "localhost",
//                    .port = 12345
//            },
//            {0}
//    };
//
//#ifdef EX4
//    struct kv_server_address indexer[2] = {
//            {
//                    .servername = "localhost",
//                    .port = 12346
//            },
//            {0}
//    };
//#endif

    g_argc = argc;
    g_argv = argv;

    run_server();

    return 0;
}
