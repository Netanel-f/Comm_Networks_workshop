/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2006 Cisco Systems.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

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
#include <stdbool.h>

#include <infiniband/verbs.h>

#define EX3 //todo
#define DEBUG 1//todo
/* todo */
#define MIN_POOL_NODES 10
#define KILOBIT_IN_BITS 1000
#define MEGABIT_IN_BITS 1000000
#define GIGABIT_IN_BITS 1000000000
#define KILOBYTE_IN_BYTES 1024
#define MEGABYTE_IN_BYTES 1048576

struct KV_NODE {
    struct KV_NODE * next;
    struct KV_NODE * prev;
    unsigned int val_len;
    char key_and_value[0];
};



struct KV_NODE * kv_head = NULL;
int kv_nodes_counter = 0;

/* server data structs for use */
struct RNDV_NODE * rndv_head = NULL;
struct RNDV_NODE * rndv_tail = NULL;
struct RNDV_MEMORY_INFO * rndv_pool_head = NULL;
struct RNDV_MEMORY_INFO * rndv_pool_tail = NULL;
int rndv_nodes_counter = 0;

/* client cache */
struct RNDV_CACHE_NODE * cache_node_head = NULL;
struct RNDV_CACHE_NODE * cache_node_tail = NULL;
bool close_server = false;


#ifdef EX4
#include <fcntl.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/stat.h>
#endif

int g_argc;
char **g_argv;

#define EAGER_PROTOCOL_LIMIT (1 << 12) /* 4KB limit */
#define MAX_TEST_SIZE (10 * EAGER_PROTOCOL_LIMIT)
#define TEST_LOCATION "~/www/"

struct RNDV_MEMORY_INFO {
    struct ibv_mr * rndv_mr;
    char rndv_buffer[MAX_TEST_SIZE];
    struct RNDV_MEMORY_INFO * next;
};

struct RNDV_NODE {
    struct RNDV_NODE * next;
//    struct RNDV_NODE * prev;
  	  struct RNDV_MEMORY_INFO * mem_info;
    unsigned int key_len;
    char key[0];
};

struct RNDV_CACHE_NODE {
    struct RNDV_CACHE_NODE * next;
    uint64_t srv_addr;
    unsigned int srv_rkey;
    unsigned int val_len;
    unsigned int key_len;
    char key[0];
};

enum packet_type {
    EAGER_GET_REQUEST,
    EAGER_GET_RESPONSE,
    EAGER_SET_REQUEST,
    // EAGER_SET_RESPONSE - not needed!

    RENDEZVOUS_GET_REQUEST,
    RENDEZVOUS_GET_RESPONSE,
    RENDEZVOUS_SET_REQUEST,
    RENDEZVOUS_SET_RESPONSE,

    CLOSE_CONNECTION,

#ifdef EX4
    FIND,
    LOCATION,
#endif
};

struct packet {
    enum packet_type type; /* What kind of packet/protocol is this */
    union {
        /* The actual packet type will determine which struct will be used: */

        struct {
            char key[0];
        } eager_get_request;
		
        struct {
            unsigned int value_length;
            char value[0];
        } eager_get_response;

        /* EAGER PROTOCOL PACKETS */
        struct {
#ifdef EX4
            unsigned value_length; /* value is binary, so needs to have length! */
#endif
            unsigned int value_length;
            char key_and_value[0]; /* null terminator between key and value */
        } eager_set_request;

        struct {
            // previously mentioned: EAGER_SET_RESPONSE - not needed!
        } eager_set_response;

        /* RENDEZVOUS PROTOCOL PACKETS */
        struct {
            /* TODO */
            unsigned int key_len;
            char key[0];
        } rndv_get_request;

        struct {
            /* TODO */
            unsigned int value_length;
//            char value_empty[1];
            uint64_t server_ptr;
            uint32_t server_key;
        } rndv_get_response;

        struct {
            /* TODO */
//            unsigned int val_len;
            unsigned int key_len;
            char key[0];
        } rndv_set_request;

        struct {
            /* TODO */
            uint64_t server_ptr;
            unsigned int server_key;
        } rndv_set_response;

		/* TODO - maybe there are more packet types? */

		struct {
		    unsigned int to_close;
		} close_connection;
					
#ifdef EX4
        struct {
            unsigned num_of_servers;
            char key[0];
        } find;

        struct {
            unsigned selected_server;
        } location;
#endif
    };
};

struct kv_server_address {
    char *servername; /* In the last item of an array this is NULL */
    short port; /* This is useful for multiple servers on a host */
};

enum {
	PINGPONG_RECV_WRID = 1,
	PINGPONG_SEND_WRID = 2,
};

static int page_size;

struct pingpong_context {
	struct ibv_context	*context;
	struct ibv_comp_channel *channel;
	struct ibv_pd		*pd;
	struct ibv_mr		*mr;
	struct ibv_mr		*remote_mr;
	struct ibv_cq		*cq;
	struct ibv_qp		*qp;
	void			*buf;
	void            *remote_buf;
	int			 size;
	int			 rx_depth;
    int          routs;
	int			 pending;
	bool         server;
	struct ibv_port_attr     portinfo;
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
#include <stdbool.h>

static struct pingpong_context *pp_init_ctx(struct ibv_device *ib_dev, int size,
					    int rx_depth, int port,
					    int use_event, int is_server)
{
	struct pingpong_context *ctx;
	printf("initctx\n");

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

	if (!is_server) {
        ctx->remote_buf = malloc(roundup(MAX_TEST_SIZE, page_size));
        if (!ctx->remote_buf) {
            fprintf(stderr, "Couldn't allocate work buf.\n");
            return NULL;
        }
        memset(ctx->remote_buf, 0x7b + is_server, MAX_TEST_SIZE);
    } else {
	    ctx->remote_buf = NULL;
	}

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

	/* init pool for server */
	if (is_server) {
        printf("init pool head\n");
        rndv_pool_head = (struct RNDV_MEMORY_INFO *) malloc(sizeof(struct RNDV_MEMORY_INFO));
        rndv_pool_head->next = NULL;
        rndv_pool_head->rndv_mr = NULL;
        printf("init pool head buffer\n");
        memset(rndv_pool_head->rndv_buffer, '\0', MAX_TEST_SIZE);
        printf("init pool head mr\n");
        rndv_pool_head->rndv_mr = ibv_reg_mr(ctx->pd, &(rndv_pool_head->rndv_buffer), MAX_TEST_SIZE,
                                             IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
                                             IBV_ACCESS_REMOTE_READ);
        if (!rndv_pool_head->rndv_mr) {
            fprintf(stderr, "Couldn't register MR\n");
            return NULL;
        }
        rndv_nodes_counter++;
        printf("after pool head\n");

        struct RNDV_MEMORY_INFO *current_pool_node = rndv_pool_head;

        while (rndv_nodes_counter < MIN_POOL_NODES) {
            current_pool_node->next = (struct RNDV_MEMORY_INFO *) malloc(
                    sizeof(struct RNDV_MEMORY_INFO));
            current_pool_node = current_pool_node->next;
            memset(current_pool_node->rndv_buffer, '\0', MAX_TEST_SIZE);
            current_pool_node->rndv_mr = ibv_reg_mr(ctx->pd, current_pool_node->rndv_buffer,
                                                    MAX_TEST_SIZE, IBV_ACCESS_LOCAL_WRITE |
                                                                   IBV_ACCESS_REMOTE_WRITE |
                                                                   IBV_ACCESS_REMOTE_READ);
            if (!current_pool_node->rndv_mr) {
                fprintf(stderr, "Couldn't register MR\n");
                return NULL;
            }
            rndv_nodes_counter++;
        }
        rndv_pool_tail = current_pool_node;
    }

    if (!is_server) {
        ctx->remote_mr = ibv_reg_mr(ctx->pd, ctx->remote_buf, MAX_TEST_SIZE,
                                    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
                                    IBV_ACCESS_REMOTE_READ);
        if (!ctx->remote_mr) {
            fprintf(stderr, "Couldn't register MR\n");
            return NULL;
        }
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

int pp_close_ctx(struct pingpong_context *ctx)
{
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

	// dereg mr of structs
	while (rndv_pool_head != NULL) {
	    if (ibv_dereg_mr(rndv_pool_head->rndv_mr)) {
            fprintf(stderr, "Couldn't deregister MR\n");
            return 1;
	    }
	    struct RNDV_MEMORY_INFO * temp = rndv_pool_head->next;
	    free(rndv_pool_head);
	    rndv_pool_head = temp;
	}

	if (ctx->remote_buf != NULL) {
        if (ibv_dereg_mr(ctx->remote_mr)) {
            fprintf(stderr, "Couldn't deregister MR\n");
            return 1;
        }
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

static int pp_post_recv(struct pingpong_context *ctx, int n)
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
		if (ibv_post_recv(ctx->qp, &wr, &bad_wr))
			break;

	return i;
}

static int pp_post_send(struct pingpong_context *ctx, enum ibv_wr_opcode opcode, unsigned size, const char *local_ptr, void *remote_ptr, uint32_t remote_key)
{
    struct ibv_sge list;
    if (ctx->server) {
        list = (struct ibv_sge) {
                .addr	= (uintptr_t) (local_ptr ? local_ptr : ctx->buf),
                .length = size,
                .lkey	= ctx->mr->lkey
        };
    } else {
        list = (struct ibv_sge) {
                .addr	= (uintptr_t) (local_ptr ? local_ptr : ctx->buf),
                .length = size,
                .lkey	= (local_ptr == ctx->remote_buf ? ctx->remote_mr->lkey : ctx->mr->lkey)//todo
        };
    }

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

static void usage(const char *argv0)
{
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

void handle_server_packets_only(struct pingpong_context *ctx, struct packet *packet)
{
//    if (DEBUG) { printf("handle server packets\n"); }
    printf("handle server packets\n");

    unsigned response_size = 0;

    struct KV_NODE * cur_node = kv_head;
    bool keep_search = true;
    int key_length;

    switch (packet->type) {

        /* Only handle packets relevant to the server here - client will handle inside get/set() calls */
        case EAGER_GET_REQUEST: /* TODO (10LOC): handle a short GET() on the server */
            key_length = strlen(packet->eager_get_request.key);

            if (kv_nodes_counter == 0) {
                struct packet * response_packet = (struct packet*) ctx->buf;
                response_packet->type = EAGER_GET_RESPONSE;
                memset(response_packet->eager_get_response.value, '\0', 1);
                response_packet->eager_get_response.value_length = 1;
                response_size = sizeof(response_packet);
            } else {
                while (keep_search) {
                    if (strcmp(packet->eager_get_request.key, cur_node->key_and_value) == 0) {
                        /* found match */
                        struct packet *response_packet = ctx->buf;
                        response_packet->type = EAGER_GET_RESPONSE;
                        unsigned int val_len = cur_node->val_len;
                        memcpy(response_packet->eager_get_response.value, &(cur_node->key_and_value[key_length + 1]), val_len);
                        memset(&(response_packet->eager_get_response.value[val_len]), '\0', 1);

                        response_packet->eager_get_response.value_length = val_len;
                        response_size = sizeof(struct packet) + val_len;
                        break;
                    } else if (cur_node->next != NULL) {
                        /* no match, yet */
                        cur_node = cur_node->next;
                    } else {
                        /* key is not exists on server, respond "" */
                        struct packet *response_packet = ctx->buf;
                        response_packet->type = EAGER_GET_RESPONSE;
                        memset(response_packet->eager_get_response.value, '\0', 1);
                        response_packet->eager_get_response.value_length = 1;
                        response_size = sizeof(response_packet);
                        break;
                    }
                }
            }
            break;

        case EAGER_SET_REQUEST: /* TODO (10LOC): handle a short SET() on the server */
            key_length = strlen(packet->eager_set_request.key_and_value);
            unsigned int vlength = packet->eager_set_request.value_length;

            if (kv_nodes_counter == 0) {
                kv_head = (struct KV_NODE * ) malloc(sizeof(struct KV_NODE) + key_length + 2 + vlength);
                kv_head->val_len = vlength;

                memcpy(kv_head->key_and_value, packet->eager_set_request.key_and_value, key_length + 1 + vlength);
                memset(&(kv_head->key_and_value[key_length + 1 + vlength]), '\0', 2);
                kv_head->next = NULL;
                kv_head->prev = NULL;
                kv_nodes_counter++;
                break;
            }

            while (keep_search) {
                if (strcmp(cur_node->key_and_value, packet->eager_set_request.key_and_value) == 0) {
                    /* found match */
                    struct KV_NODE * prev_node = cur_node->prev;
                    struct KV_NODE * next_node = cur_node->next;
                    free(cur_node);
                    cur_node = (struct KV_NODE * ) malloc(sizeof(struct KV_NODE) + key_length + 2 + vlength);
                    memcpy(cur_node->key_and_value, packet->eager_set_request.key_and_value, key_length + 1 + vlength);
                    memset(&(kv_head->key_and_value[key_length + 1 + vlength]), '\0', 2);

                    if (prev_node != NULL) {
                        prev_node->next = cur_node;
                    }
                    cur_node->prev = prev_node;
                    cur_node->next = next_node;
                    if (next_node != NULL) {
                        next_node->prev = cur_node;
                    }
                    break;

                } else if (cur_node->next != NULL) {
                    /* no match, yet */
                     cur_node = cur_node->next;

                } else {
                    /* key is not exists on server, appending it */
                    cur_node->next = (struct KV_NODE * ) malloc(sizeof(struct KV_NODE) + key_length + 2 + vlength);
                    cur_node->next->val_len = vlength;

                    memcpy(cur_node->next->key_and_value, packet->eager_set_request.key_and_value, key_length + 1 + vlength);

                    cur_node->next->next = NULL;
                    cur_node->next->prev = cur_node;
                    kv_nodes_counter++;
                    break;
                }
            }
            break;

        case RENDEZVOUS_GET_REQUEST: /* TODO (10LOC): handle a long GET() on the server */
            printf("handle server RENDEZVOUS_GET_REQUEST\n");
            printf("handle server key: %s\n", packet->rndv_set_request.key);

			key_length = packet->rndv_get_request.key_len;
//todo 18.6
//            /* a get request will received only if client doesnt have it cached for remote access */
//            struct RNDV_NODE * current_node = rndv_head;
//            while (current_node != NULL) {
//            	if ((strncmp(current_node->key, packet->rndv_get_request.key, key_length) == 0)) {
//					/* found match */
//					struct packet *response_packet = ctx->buf;
//					response_packet->type = RENDEZVOUS_GET_RESPONSE;
//					response_packet->rndv_get_response.server_ptr = (uint64_t) current_node->mem_info->rndv_mr->addr;
//					response_packet->rndv_get_response.server_key = ctx->mr->lkey;
//					response_size = sizeof(struct packet);
//					break;
//            	}
//            }

            /* couldn't find existing key  - create a (empty) node for it */

			struct RNDV_NODE * rndv_tempGET = (struct RNDV_NODE *) malloc(sizeof(struct RNDV_NODE) + key_length + 1);
			rndv_tempGET->next = NULL;
			rndv_tempGET->mem_info = rndv_pool_head;
			rndv_pool_head = rndv_pool_head->next;
            rndv_nodes_counter--;
			rndv_tempGET->mem_info->next = NULL;
			strncpy(rndv_tempGET->mem_info->rndv_buffer, packet->rndv_get_request.key, key_length);
			if (rndv_tail != NULL) {
				rndv_tail->next = rndv_tempGET;
				rndv_tail = rndv_tempGET;
			} else {
				rndv_head = rndv_tempGET;
				rndv_tail = rndv_tempGET;
			}

			struct packet * response_packetGET = (struct packet*) ctx->buf;
			response_packetGET->type = RENDEZVOUS_GET_RESPONSE;
			response_packetGET->rndv_get_response.value_length = 1;
			response_packetGET->rndv_get_response.server_ptr = (uint64_t) rndv_tail->mem_info->rndv_mr->addr;
			response_packetGET->rndv_get_response.server_key = rndv_tail->mem_info->rndv_mr->rkey;


//			// check if key exists
//            if (kv_nodes_counter == 0) {
//                struct packet * response_packet = (struct packet*) ctx->buf;
//                response_packet->type = RENDEZVOUS_GET_RESPONSE;
//                memset(response_packet->rndv_get_response.value_empty, '\0', 1);
//                response_packet->rndv_get_response.value_length = 1;
//                response_packet->rndv_get_response.server_ptr = NULL;
//                response_packet->rndv_get_response.server_key = 0;
//                response_size = sizeof(response_packet);
//                break;
//            }
//
//            while (keep_search) {
//                if (strcmp(packet->eager_get_request.key, cur_node->key_and_value) == 0) {
//                    /* found match */
//                    struct packet *response_packet = ctx->buf;
//                    response_packet->type = RENDEZVOUS_GET_RESPONSE;
//                    unsigned int val_len = cur_node->val_len;
//                    response_packet->eager_get_response.value_length = val_len;
//                    response_packet->rndv_get_response.server_ptr = &(cur_node->key_and_value[key_length + 1]);
//                    response_packet->rndv_get_response.server_key = ctx->mr->lkey;
//                    response_size = sizeof(struct packet);
//                    break;
//
//                } else if (cur_node->next != NULL) {
//                    /* no match, yet */
//                    cur_node = cur_node->next;
//
//                } else {
//                    /* key is not exists on server, respond "" */
//                    struct packet * response_packet = (struct packet*) ctx->buf;
//                    response_packet->type = RENDEZVOUS_GET_RESPONSE;
//                    memset(response_packet->rndv_get_response.value_empty, '\0', 1);
//                    response_packet->rndv_get_response.value_length = 1;
//                    response_packet->rndv_get_response.server_ptr = NULL;
//                    response_packet->rndv_get_response.server_key = 0;
//                    response_size = sizeof(response_packet);
//                    break;
//                }
//            }
            break;

        case RENDEZVOUS_SET_REQUEST: /* TODO (20LOC): handle a long SET() on the server */
            printf("handle server RENDEZVOUS_SET_REQUEST\n");
            printf("handle server key: %s\n", packet->rndv_set_request.key);
            key_length = packet->rndv_set_request.key_len;
//            vlength = packet->rndv_set_request.value_length;

            // RENDEZVOUS_SET_REQUEST in case client does NOT have the remote details.
            // add a rndv node to store value

			struct RNDV_NODE * rndv_temp = (struct RNDV_NODE *) malloc(sizeof(struct RNDV_NODE) + key_length + 1);
			rndv_temp->next = NULL;
			rndv_temp->mem_info = rndv_pool_head;
			rndv_pool_head = rndv_pool_head->next;
			rndv_nodes_counter--;
			rndv_temp->mem_info->next = NULL;
			strncpy(rndv_temp->mem_info->rndv_buffer, packet->rndv_set_request.key, key_length);
			if (rndv_tail != NULL) {
				rndv_tail->next = rndv_temp;
				rndv_tail = rndv_temp;
            } else {
				rndv_head = rndv_temp;
				rndv_tail = rndv_temp;
			}

			printf("handle server creating RENDEZVOUS_SET_RESPONSE\n");
			struct packet * response_packet = (struct packet*) ctx->buf;
			response_size = sizeof(response_packet) + sizeof(uint64_t) + sizeof(unsigned int);
			response_packet->type = RENDEZVOUS_SET_RESPONSE;
			response_packet->rndv_set_response.server_ptr = (uint64_t) rndv_tail->mem_info->rndv_mr->addr;
			response_packet->rndv_set_response.server_key = rndv_tail->mem_info->rndv_mr->rkey;
//            pp_post_recv(ctx, 1);

//            // check if key exists
//            if (kv_nodes_counter == 0) {
//                kv_head = (struct KV_NODE * ) malloc(sizeof(struct KV_NODE) + key_length + 2 + vlength);
//                kv_head->val_len = vlength;
//
//
//                memcpy(kv_head->key_and_value, packet->rndv_set_request.key, key_length + 1);
//                memset(&(kv_head->key_and_value[key_length]), '\0', 1);
//
//                kv_head->next = NULL;
//                kv_head->prev = NULL;
//                kv_nodes_counter++;
//
//                void * local_ptr = &(head->key_and_value[key_length + 1]);
////                printf("value len:%d\n", vlength);
//
//                struct packet * response_packet = (struct packet*) ctx->buf;
//                response_packet->type = RENDEZVOUS_SET_RESPONSE;
//                response_packet->rndv_set_response.server_ptr = (uint64_t) ctx->remote_mr->addr;
//                response_packet->rndv_set_response.server_key = ctx->remote_mr->rkey;
//
                printf("server key: %u\n", response_packet->rndv_set_response.server_key);
                printf("server ptr: %lu\n", response_packet->rndv_set_response.server_ptr);
//                response_size = sizeof(response_packet) + sizeof(uint64_t) + sizeof(unsigned int);
//                break;
//            }
//
//            while (keep_search) {
//                if (strcmp(cur_node->key_and_value, packet->rndv_set_request.key) == 0) {
//                    /* found match */
//                    struct KV_NODE * prev_node = cur_node->prev;
//                    struct KV_NODE * next_node = cur_node->next;
//                    free(cur_node);
//                    cur_node = (struct KV_NODE * ) malloc(sizeof(struct KV_NODE) + key_length + 2 + vlength);
//                    memcpy(cur_node->key_and_value, packet->rndv_set_request.key, key_length + 1);
//                    memset(&(kv_head->key_and_value[key_length + 1]), '\0', 1);
//                    if (prev_node != NULL) {
//                        prev_node->next = cur_node;
//                    }
//                    cur_node->prev = prev_node;
//                    cur_node->next = next_node;
//                    if (next_node != NULL) {
//                        next_node->prev = cur_node;
//                    }
//                    cur_node->val_len = vlength;
//                    void * local_ptr = &(cur_node->key_and_value[key_length + 2]);
//                    struct packet * response_packet = (struct packet*) ctx->buf;
//
//                    response_packet->type = RENDEZVOUS_SET_RESPONSE;
//                    response_packet->rndv_set_response.server_ptr = (uint64_t) ctx->remote_mr->addr;
//                    response_packet->rndv_set_response.server_key = ctx->remote_mr->lkey;
//                    response_size = sizeof(response_packet);
//                    break;
//
//                } else if (cur_node->next != NULL) {
//                    /* no match, yet */
//                    cur_node = cur_node->next;
//                } else {
//                    /* key is not exists on server, appending it */
//                    cur_node->next = (struct KV_NODE * ) malloc(sizeof(struct KV_NODE) + key_length + 2 + vlength);
//                    cur_node->next->val_len = vlength;
//
//                    memcpy(cur_node->next->key_and_value, packet->rndv_set_request.key, key_length + 1);
//                    memset(&(kv_head->key_and_value[key_length + 1]), '\0', 1);
//
//                    cur_node->next->next = NULL;
//                    cur_node->next->prev = cur_node;
//                    kv_nodes_counter++;
//
//                    void * local_ptr = &(cur_node->key_and_value[key_length + 2]);
//                    struct packet * response_packet = (struct packet*) ctx->buf;
//                    response_packet->type = RENDEZVOUS_SET_RESPONSE;
////                    response_packet->rndv_set_response.server_ptr = local_ptr;
////                    response_packet->rndv_set_response.server_key = ctx->mr->lkey;
//                    response_size = sizeof(response_packet);
//
//                    break;
//                }
//            }
            break;

        case EAGER_GET_RESPONSE:
//            if (DEBUG) { printf("wait: EAGER GET RESP\n"); }
            return;
        case RENDEZVOUS_SET_RESPONSE:
            printf("RENDEZVOUS_SET_RESPONSE\n");
            break;
		case RENDEZVOUS_GET_RESPONSE:
			printf("RENDEZVOUS_GET_RESPONSE\n");
			break;
        case CLOSE_CONNECTION:
            close_server = true;
            break;
    #ifdef EX4
        case FIND: /* TODO (2LOC): use some hash function */
    #endif
        default:
            printf("handle server default\n");
            break;
    }

    printf("in handle_server response_size %d\n", response_size);
	if (response_size) {
		pp_post_send(ctx, IBV_WR_SEND, response_size, NULL, NULL, 0);
    }
}

int orig_main(struct kv_server_address *server, unsigned size, int argc, char *argv[], struct pingpong_context **result_ctx)
{
	struct ibv_device      **dev_list;
	struct ibv_device	*ib_dev;
	struct pingpong_context *ctx;
	struct pingpong_dest     my_dest;
	struct pingpong_dest    *rem_dest;
	struct timeval           start, end;
	char                    *ib_devname = NULL;
	char                    *servername = server->servername;
	int                      port = server->port;
	int                      ib_port = 1;
	enum ibv_mtu		 mtu = IBV_MTU_1024;
	int                      rx_depth = 1;
	int                      iters = 1000;
	int                      use_event = 0;
	int                      routs;
	int                      rcnt, scnt;
	int                      num_cq_events = 0;
	int                      sl = 0;
	int			 gidx = -1;
	char			 gid[33];

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

int pp_wait_completions(struct pingpong_context *ctx, int iters)
{
    if (DEBUG) { printf("wait complete\n"); }

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

//			if (DEBUG) { printf("waitcomp%d\n",(int) wc[i].wr_id); }
			printf("waitcomp%d\n",(int) wc[i].wr_id);
			switch ((int) wc[i].wr_id) {
			case PINGPONG_SEND_WRID:
			    ++scnt;
				break;

			case PINGPONG_RECV_WRID:
			    ++rcnt;
				handle_server_packets_only(ctx, (struct packet*)ctx->buf);
				if (close_server) {
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
		if (ctx->server) {
            while (rndv_nodes_counter < MIN_POOL_NODES) {
                printf("rndv_nodes_counter = %d\n", rndv_nodes_counter);
                rndv_pool_tail->next = (struct RNDV_MEMORY_INFO *) malloc(
                        sizeof(struct RNDV_MEMORY_INFO));
                rndv_pool_tail = rndv_pool_tail->next;
                rndv_pool_tail->next = NULL;
                memset(rndv_pool_tail->rndv_buffer, '\0', MAX_TEST_SIZE);
                rndv_pool_tail->rndv_mr = ibv_reg_mr(ctx->pd, &(rndv_pool_tail->rndv_buffer),
                                                     MAX_TEST_SIZE, IBV_ACCESS_LOCAL_WRITE |
                                                                    IBV_ACCESS_REMOTE_WRITE |
                                                                    IBV_ACCESS_REMOTE_READ);
                if (!rndv_pool_tail->rndv_mr) {
                    fprintf(stderr, "Couldn't register MR\n");
                    return 1;
                }
                rndv_nodes_counter++;
            }
        }
	}
//    printf("wait: return 0\n");
	return 0;
}

int kv_open(struct kv_server_address *server, void **kv_handle)
{
    return orig_main(server, EAGER_PROTOCOL_LIMIT, g_argc, g_argv, (struct pingpong_context **)kv_handle);
}

int kv_set(void *kv_handle, const char *key, const char *value)
{
    struct pingpong_context *ctx = kv_handle;
    struct packet *set_packet = (struct packet*)ctx->buf;

    unsigned packet_size = strlen(key) + 1 + strlen(value) + sizeof(struct packet);

    if (packet_size < (EAGER_PROTOCOL_LIMIT)) {
        /* Eager protocol - exercise part 1 */
        set_packet->type = EAGER_SET_REQUEST;
        /* TODO (4LOC): fill in the rest of the set_packet */
        set_packet->eager_set_request.value_length = strlen(value);
        strncpy(set_packet->eager_set_request.key_and_value, key, strlen(key));
        memset(&(set_packet->eager_set_request.key_and_value[strlen(key)]), '\0', 1);
        strncpy(&set_packet->eager_set_request.key_and_value[strlen(key) + 1], value, strlen(value));
        memset(&(set_packet->eager_set_request.key_and_value[strlen(key) + 1 + strlen(value)]), '\0', 1);
        pp_post_send(ctx, IBV_WR_SEND, packet_size, NULL, NULL, 0); /* Sends the packet to the server */
        return pp_wait_completions(ctx, 1); /* await EAGER_SET_REQUEST completion */
    }

    printf("kv_set RENDEZVOUS_SET_REQUEST\n");
    /* Otherwise, use RENDEZVOUS - exercise part 2 */
    set_packet->type = RENDEZVOUS_SET_REQUEST;
    /* TODO (4LOC): fill in the rest of the set_packet - request peer address & remote key */
    struct RNDV_CACHE_NODE * current_cache_node = cache_node_head;
    if (current_cache_node == NULL) {
		printf("current cache node is null\n");
    }
    while (current_cache_node != NULL) {
        printf("current cache node key %s\n", current_cache_node->key);
        if (strcmp(current_cache_node->key, key) == 0) {
            // we can write directly
            memcpy(ctx->remote_buf, value, strlen(value) + 1);
//            pp_post_recv(ctx, 1);
            pp_post_send(ctx, IBV_WR_RDMA_WRITE, strlen(value) + 1, ctx->remote_buf, (void *)current_cache_node->srv_addr, current_cache_node->srv_rkey);
            return pp_wait_completions(ctx, 1);
//            return pp_wait_completions(ctx, 2);
        }
        current_cache_node = current_cache_node->next;
    }
    printf("cneed to get remote access info from server\n");
    // need to get remote access info from server
    unsigned int key_length = strlen(key);
    packet_size = key_length + 1 + sizeof(struct packet);
    size_t value_length = strlen(value);
//    set_packet->rndv_set_request.val_len = strlen(value) + 1;
    set_packet->rndv_set_request.key_len = key_length;
    strncpy(set_packet->rndv_set_request.key, key, key_length);
    memset(&(set_packet->rndv_set_request.key[key_length]), '\0', 1);

    pp_post_recv(ctx, 1); /* Posts a receive-buffer for RENDEZVOUS_SET_RESPONSE */
    pp_post_send(ctx, IBV_WR_SEND, packet_size, NULL, NULL, 0); /* Sends the packet to the server */

    assert(pp_wait_completions(ctx, 2) == 0); /* wait for both to complete */

    assert(set_packet->type == RENDEZVOUS_SET_RESPONSE);
	printf("kv_set RENDEZVOUS_SET_RESPONSE\n");
//    set_packet = (struct packet*)ctx->buf;
//    uint64_t s_ptr = set_packet->rndv_set_response.server_ptr;
//    uint32_t s_key = set_packet->rndv_set_response.server_key;

    struct RNDV_CACHE_NODE * temp = (struct RNDV_CACHE_NODE *) malloc(sizeof(struct RNDV_CACHE_NODE) + strlen(key) + 1);
    temp->next = NULL;
    temp->srv_addr = set_packet->rndv_set_response.server_ptr;
    temp->srv_rkey = set_packet->rndv_set_response.server_key;
    temp->val_len = value_length;
    temp->key_len = key_length;
    strncpy(temp->key, key, key_length);
    memset(&(temp->key[key_length]), '\0', 1);
    if (cache_node_tail != NULL) {
        cache_node_tail->next = temp;
    } else {
        cache_node_head = temp;
    }
    cache_node_tail = temp;
// todo 17.6
//    struct RNDV_MEMORY_INFO * temp_memory = rndv_pool_head;
//    rndv_pool_head = rndv_pool_head->next;
//    rndv_nodes_counter--;
//    memcpy(temp_memory->rndv_buffer, value, value_length);
//    memset(&(temp_memory->rndv_buffer[value_length]), '\0', 1);
    memcpy(ctx->remote_buf, value, value_length);
    memset(&(ctx->remote_buf[value_length]), '\0', 1);

    printf("server key: %u\n", set_packet->rndv_set_response.server_key);
    printf("server key: %lu\n", set_packet->rndv_set_response.server_ptr);

	printf("ctx buf lkey: %u\n", ctx->mr->lkey);
	printf("temp mem lkey: %u\n", ctx->remote_mr->lkey);

//    pp_post_send(ctx, IBV_WR_RDMA_WRITE, packet_size, value, NULL, 0/* TODO (1LOC): replace with remote info for RDMA_WRITE from packet */);
//    pp_post_send(ctx, IBV_WR_RDMA_WRITE, value_length, (ctx->remote_buf), (void *)s_ptr, s_key);
//    pp_post_send(ctx, IBV_WR_RDMA_WRITE, value_length, NULL, (void *)cache_node_tail->srv_addr, cache_node_tail->srv_rkey);
//    pp_post_recv(ctx, 1);
    pp_post_send(ctx, IBV_WR_RDMA_WRITE, value_length, ctx->remote_buf, (void *)cache_node_tail->srv_addr, cache_node_tail->srv_rkey);
    printf("after  pp_post_send WRITE\n");
    return pp_wait_completions(ctx, 1); /* wait for both to complete */
//    return pp_wait_completions(ctx, 2); /* wait for both to complete */
}

int kv_get(void *kv_handle, const char *key, char **value)
{
//    if (DEBUG) { printf("in kv_get\n"); }
	unsigned int key_length = strlen(key);
    struct pingpong_context *ctx = kv_handle;
    struct packet *get_packet = (struct packet*)ctx->buf;

    unsigned packet_size = key_length + 1+ sizeof(struct packet);
//    if (DEBUG) { printf("in kv_get packet_size %d\n", packet_size); }

    if (packet_size < (EAGER_PROTOCOL_LIMIT)) {
        /* Eager protocol - exercise part 1 */
        get_packet->type = EAGER_GET_REQUEST;
        memcpy(get_packet->eager_get_request.key, key, key_length);
        memset(&(get_packet->eager_get_request.key[key_length]), '\0', 1);

        pp_post_send(ctx, IBV_WR_SEND, packet_size, NULL, NULL, 0); /* Sends the packet to the server */

        pp_wait_completions(ctx, 1); /* await EAGER_GET_REQUEST completion */
        pp_wait_completions(ctx, 1); /* await EAGER_SET_REQUEST completion */

        struct pingpong_context * kv_handle_temp = (struct pingpong_context *) kv_handle;

        struct packet* resp = (struct packet *) kv_handle_temp->buf;
        unsigned int value_len = resp->eager_get_response.value_length;
        *value = (char *) malloc(value_len + 1);
        memcpy(*value, resp->eager_get_response.value, value_len);
        memset(&((*value)[value_len]), '\0', 1);
        return 0;
    }

    /* Otherwise, use RENDEZVOUS - exercise part 2 */
    get_packet->type = RENDEZVOUS_GET_REQUEST;

    //todo delete? 17/6
//    struct RNDV_MEMORY_INFO * temp_get_memory = rndv_pool_head;
//    rndv_pool_head = rndv_pool_head->next;

    struct RNDV_CACHE_NODE * current_cache_node = cache_node_head;
    while (current_cache_node != NULL) {
        if (strcmp(current_cache_node->key, key) == 0) {
            // we can read directly
//            pp_post_send(ctx, IBV_WR_RDMA_READ, current_cache_node->val_len + 1, NULL, (void *)current_cache_node->srv_addr, current_cache_node->srv_rkey);
            pp_post_recv(ctx, 1);
            pp_post_send(ctx, IBV_WR_RDMA_READ, current_cache_node->val_len + 1, ctx->remote_buf, (void *)current_cache_node->srv_addr, current_cache_node->srv_rkey);
            pp_wait_completions(ctx, 2);
            strncpy(*value, ctx->remote_buf, current_cache_node->val_len);
            return 0;
        }
    }
    // need to get info from server;

    packet_size = sizeof(struct packet) + key_length + 1;
    strncpy(get_packet->rndv_get_request.key, key, key_length+1);

    pp_post_recv(ctx, 1); /* Posts a receive-buffer for RENDEZVOUS_GET_RESPONSE */
    pp_post_send(ctx, IBV_WR_SEND, packet_size, NULL, NULL, 0); /* Sends the packet to the server */
    assert(pp_wait_completions(ctx, 2)); /* wait for both to complete */


    assert(get_packet->type == RENDEZVOUS_GET_RESPONSE);

	struct RNDV_CACHE_NODE * temp = (struct RNDV_CACHE_NODE *) malloc(sizeof(struct RNDV_CACHE_NODE) + strlen(key) + 1);
	temp->next = NULL;
	temp->srv_addr = get_packet->rndv_get_response.server_ptr;
	temp->srv_rkey = get_packet->rndv_get_response.server_key;
    temp->val_len = get_packet->rndv_get_response.value_length;
	temp->key_len = key_length;
	strncpy(temp->key, key, key_length);
	memset(&(temp->key[key_length]), '\0', 1);
	if (cache_node_tail != NULL) {
		cache_node_tail->next = temp;
	} else {
		cache_node_head = temp;
	}
	cache_node_tail = temp;

    //todo 17.6
//	/* no key on server */
//    if (get_packet->rndv_get_response.server_ptr == NULL) {
//        *value = "";
//        return 0;
//    }


    unsigned int value_length = get_packet->rndv_get_response.value_length;
    *value = (char *) malloc(value_length + 1);


//    pp_post_send(ctx, IBV_WR_RDMA_READ, packet_size, *value, cache_node_tail->srv_addr, cache_node_tail->srv_rkey);
    pp_post_recv(ctx, 1);
    pp_post_send(ctx, IBV_WR_RDMA_READ, value_length, ctx->remote_buf, (void*)cache_node_tail->srv_addr, cache_node_tail->srv_rkey);
//    return pp_wait_completions(ctx, 1); /* wait for both to complete */
    pp_wait_completions(ctx, 2); /* wait for both to complete */
    strcpy(*value, ctx->remote_buf);
    return 0;
}

void kv_release(char *value)
{
    free(value);
    //todo maybe need to delete from cache?
    /* TODO (2LOC): free value */
}

int kv_close(void *kv_handle)
{

    /* todo Eager protocol - exercise part 1 */
    struct pingpong_context *ctx = kv_handle;
    struct packet *close_packet = (struct packet*)ctx->buf;

    unsigned packet_size = sizeof(struct packet);

    close_packet->type = CLOSE_CONNECTION;
    close_packet->close_connection.to_close = 1;

    pp_post_send(ctx, IBV_WR_SEND, packet_size, NULL, NULL, 0); /* Sends the packet to the server */

    pp_wait_completions(ctx, 1); /* await EAGER_GET_REQUEST completion */
    /* todo Eager protocol - exercise part 1 */

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

int mkv_open(struct kv_server_address *servers, void **mkv_h)
{
	struct mkv_ctx *ctx;
	unsigned count = 0;
	while (servers[count++].servername); /* count servers */
	ctx = malloc(sizeof(*ctx) + count * sizeof(void*));
	if (!ctx) {
		return 1;
	}
	
	ctx->num_servers = count;
	for (count = 0; count < ctx->num_servers; count++) {
		if (orig_main(&servers[count], EAGER_PROTOCOL_LIMIT, g_argc, g_argv, &ctx->kv_ctxs[count])) {
			return 1;
		}
	}
	
	*mkv_h = ctx;
	return 0;
}

int mkv_set(void *mkv_h, unsigned kv_id, const char *key, const char *value)
{
	struct mkv_ctx *ctx = mkv_h;
	return kv_set(ctx->kv_ctxs[kv_id], key, value);
}

int mkv_get(void *mkv_h, unsigned kv_id, const char *key, char **value)
{
	struct mkv_ctx *ctx = mkv_h;
	return kv_get(ctx->kv_ctxs[kv_id], key, value);
}

void mkv_release(char *value)
{
	kv_release(value);
}

void mkv_close(void *mkv_h)
{
	unsigned count;
	struct mkv_ctx *ctx = mkv_h;
	for (count = 0; count < ctx->num_servers; count++) {
		pp_close_ctx((struct pingpong_context*)ctx->kv_ctxs[count]);
	}
	free(ctx);
}









struct dkv_ctx {
	struct mkv_ctx *mkv;
	struct pingpong_context *indexer;
};

int dkv_open(struct kv_server_address *servers, /* array of servers */
             struct kv_server_address *indexer, /* single indexer */
             void **dkv_h)
{
	struct dkv_ctx *ctx = malloc(sizeof(*ctx));
	if (orig_main(indexer, EAGER_PROTOCOL_LIMIT, g_argc, g_argv, &ctx->indexer)) {
		return 1;
	}
	if (mkv_open(servers, (void**)&ctx->mkv)) {
		return 1;
	}
	*dkv_h = ctx;
}

int dkv_set(void *dkv_h, const char *key, const char *value, unsigned length)
{
	struct dkv_ctx *ctx = dkv_h;
    struct packet *set_packet = (struct packet*)&ctx->indexer->buf;
    unsigned packet_size = strlen(key) + sizeof(struct packet);

    /* Step #1: The client sends the Index server FIND(key, #kv-servers) */
    set_packet->type = FIND;
	set_packet->find.num_of_servers = ctx->mkv->num_servers;
	strcpy(set_packet->find.key, key);

    pp_post_recv(ctx->indexer, 1); /* Posts a receive-buffer for LOCATION */
    pp_post_send(ctx->indexer, IBV_WR_SEND, packet_size, NULL, NULL, 0); /* Sends the packet to the server */
    assert(pp_wait_completions(ctx->indexer, 2)); /* wait for both to complete */

    /* Step #2: The Index server responds with LOCATION(#kv-server-id) */
    assert(set_packet->type == LOCATION);

    /* Step #3: The client contacts KV-server with the ID returned in LOCATION, using SET/GET messages. */
	return mkv_set(ctx->mkv, set_packet->location.selected_server, key, value);
		//length); /* TODO (10LOC): Add this value length parameter to all the relevant functions... including kv_set()/kv_get() */
}

int dkv_get(void *dkv_h, const char *key, char **value, unsigned *length)
{
	/* TODO (20LOC): implement similarly to dkv_get() */
}

void dkv_release(char *value)
{
	mkv_release(value);
}

int dkv_close(void *dkv_h)
{
	struct dkv_ctx *ctx = dkv_h;
	pp_close_ctx(ctx->indexer);
	mkv_close(ctx->mkv);
	free(ctx);
}

void recursive_fill_kv(char const* dirname, void *dkv_h) {
	struct dirent *curr_ent;
	DIR* dirp = opendir(dirname);
	if (dirp == NULL) {
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
				void *p = mmap(0, fsize, PROT_READ, MAP_PRIVATE, fd, 0);
				/* TODO (1LOC): Add a print here to see you found the full paths... */
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









void run_server() {
    struct pingpong_context *ctx;
    struct kv_server_address server = {0};
    server.port = 12345;
    printf("before orig_main\n");
    assert(0 == orig_main(&server, EAGER_PROTOCOL_LIMIT, g_argc, g_argv, &ctx));
	printf("after orig_main\n");
    while (0 <= pp_wait_completions(ctx, 1));
    pp_close_ctx(ctx);
}

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

    fprintf(results_file, "Value size: %ld %s, Throughput: %.3f %s\n",
            value_size, value_size_unit, throughput, rate_unit);

}

int main(int argc, char **argv)
{
    void *kv_ctx; /* handle to internal KV-client context */

    char send_buffer[MAX_TEST_SIZE] = {0};
    char *recv_buffer;

    struct kv_server_address servers[2] = {
            {
                    .servername = "localhost",
                    .port = 12345
            },
            {0}
    };

#ifdef EX4
    struct kv_server_address indexer[2] = {
            {
                    .servername = "localhost",
                    .port = 12346
            },
            {0}
    };
#endif

    g_argc = argc;
    g_argv = argv;
//    if (argc > 1) {
    if (argc == 1) {//todo
        run_server();
    }

#ifdef EX4
    assert(0 == my_open(servers, indexer, &kv_ctx));
#else
    assert(0 == my_open(&servers[0], &kv_ctx));
#endif

//    /* Test small size */
//    assert(100 < MAX_TEST_SIZE);
//    memset(send_buffer, 'a', 100);
//    if (DEBUG) { printf("main: before set\n"); }
//    assert(0 == set(kv_ctx, "1", send_buffer));
//    if (DEBUG) { printf("main: before get\n"); }
//    assert(0 == get(kv_ctx, "1", &recv_buffer));
//    if (DEBUG) { printf("send: %s\n", send_buffer); }
//    if (DEBUG) { printf("recv: %s\n", recv_buffer); }
//    assert(0 == strcmp(send_buffer, recv_buffer));
//    if (DEBUG) { printf("main: before release\n"); }
//    release(recv_buffer);
//
//    /* Test logic */
//    if (DEBUG) { printf("main: before get 1\n"); }
//    assert(0 == get(kv_ctx, "1", &recv_buffer));
//    if (DEBUG) { printf("main: sendbuf %s\n", send_buffer); }
//    if (DEBUG) { printf("main: recvbuf %s\n", recv_buffer); }
//    assert(0 == strcmp(send_buffer, recv_buffer));
//    release(recv_buffer);
//    memset(send_buffer, 'b', 100);
//    if (DEBUG) { printf("main: before set 1\n"); }
//    assert(0 == set(kv_ctx, "1", send_buffer));
//    memset(send_buffer, 'c', 100);
//    if (DEBUG) { printf("main: before set 2\n"); }
//    assert(0 == set(kv_ctx, "22", send_buffer));
//    memset(send_buffer, 'b', 100);
//    if (DEBUG) { printf("main: before get 1\n"); }
//    assert(0 == get(kv_ctx, "1", &recv_buffer));
//    if (DEBUG) { printf("main: sendbuf %s\n", send_buffer); }
//    if (DEBUG) { printf("main: recvbuf %s\n", recv_buffer); }
//    assert(0 == strcmp(send_buffer, recv_buffer));
//    if (DEBUG) { printf("main: before release\n"); }
//    release(recv_buffer);
//
    /* Test large size */
    if (DEBUG) { printf("main: TEST LARGE SIZE\n"); }

    memset(send_buffer, 'a', MAX_TEST_SIZE - 1);
    if (DEBUG) { printf("main: before set 1 a\n"); }
    assert(0 == set(kv_ctx, "1", send_buffer));
    if (DEBUG) { printf("main: before set 333 a\n"); }
    assert(0 == set(kv_ctx, "333", send_buffer));
//    if (DEBUG) { printf("main: before get 1\n"); }
//    assert(0 == get(kv_ctx, "1", &recv_buffer));
//    if (DEBUG) { printf("main: sendbuf %s\n", send_buffer); }
//    if (DEBUG) { printf("main: recvbuf %s\n", recv_buffer); }
//    assert(0 == strcmp(send_buffer, recv_buffer));
//    if (DEBUG) { printf("main: before release\n"); }
//    release(recv_buffer);

//    /* Test throughput */
//    FILE * results_file;
//    results_file = fopen("RESULTS.txt", "w+");
//    unsigned packet_struct_size = sizeof(struct packet);
//    for (ssize_t value_size = 1; value_size < EAGER_PROTOCOL_LIMIT; value_size = value_size<< 1) {
//        struct timeval start, end;
//        double total_time_usec = 0.0;
//        int total_bytes = 0;
//        int total_attempts = 20;
//        memset(send_buffer, 'a', value_size);
//
//
//        if (gettimeofday(&start, NULL)) {
//            perror("gettimeofday");
//            break;
//        }
//
//        char key[10];
//        for (int attempt = 0; attempt < total_attempts; attempt++) {
//            sprintf(key, "%ld-%d", value_size,attempt);
//
//            set(kv_ctx, key, send_buffer);
//            get(kv_ctx, key, &recv_buffer);
//            assert(0 == strcmp(send_buffer, recv_buffer));
//
//            total_bytes = total_bytes + 2 * (strlen(key) + 1 + value_size + packet_struct_size);
//            release(recv_buffer);
//        }
//
//        if (gettimeofday(&end, NULL)) {
//            perror("gettimeofday");
//            break;
//        }
//
//        total_time_usec = ((end.tv_sec - start.tv_sec) * 1000000) + (end.tv_usec - start.tv_usec);
//        long total_bits_trans = total_bytes * 8;
//        double total_time_sec = total_time_usec / 1000000;
//        double throughput = total_bits_trans / total_time_sec;
//        print_results_to_file(results_file, value_size, throughput);
////        fprintf(results_file, "Value size: %ld, Throughput: %.3f bps\n", value_size, throughput);
//        fflush(stdout);
//    }
#ifdef EX4
	recursive_fill_kv(TEST_LOCATION, kv_ctx);
#endif

    my_close(kv_ctx);
    return 0;
}
