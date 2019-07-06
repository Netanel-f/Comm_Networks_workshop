#include "kv_shared.h"


int g_argc;
char **g_argv;

/* client cache of last accessed key */
char last_rndv_accessed_key[EAGER_PROTOCOL_LIMIT] = "";
unsigned int last_accesed_key_val_length = 0;
uint64_t last_accessed_server_addr = 0;
uint32_t last_accessed_server_rkey = 0;

/* client memory pool */
MEMORY_INFO * mem_pool_head = NULL;
MEMORY_INFO * mem_pool_tail = NULL;
int mem_pool_size = 0;

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

    if (packet_size < EAGER_PROTOCOL_LIMIT) {
        if (strcmp(key, last_rndv_accessed_key) == 0) {
            memset(last_rndv_accessed_key, '\0', key_length);
            last_accesed_key_val_length = 0;
        }
        /* Eager protocol - exercise part 1 */
        set_packet->type = EAGER_SET_REQUEST;
        memcpy(set_packet->eager_set_request.key_and_value, key, key_length + 1);
        memcpy(&set_packet->eager_set_request.key_and_value[key_length + 1], value, value_length + 1);

        /* Sends the packet to the server */
        pp_post_send(ctx, IBV_WR_SEND, packet_size, NULL, NULL, 0);

        /* await EAGER_SET_REQUEST completion */
        return pp_wait_completions(ctx, 1);
    }

    /* Otherwise, use RENDEZVOUS - exercise part 2 */
    set_packet->type = RENDEZVOUS_SET_REQUEST;
    /* maybe already cached*/
    if (strcmp(key, last_rndv_accessed_key) == 0) {
        last_accesed_key_val_length = value_length;
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
    set_packet->rndv_set_request.value_length = value_length + 1;
    memcpy(set_packet->rndv_set_request.key, key, key_length + 1);

    /* Posts a receive-buffer for RENDEZVOUS_SET_RESPONSE */
    pp_post_recv(ctx, 1);
    /* Sends the packet to the server */
    pp_post_send(ctx, IBV_WR_SEND, packet_size, NULL, NULL, 0);

    // set temp mr
    struct ibv_mr * orig_mr = ctx->mr;
    struct ibv_mr * temp_mr = ibv_reg_mr(ctx->pd, (void*)value, value_length + 1, IBV_ACCESS_LOCAL_WRITE |IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
    ctx->mr = temp_mr;

    /* wait for both to complete */
    assert(pp_wait_completions(ctx, 2));
    assert(set_packet->type == RENDEZVOUS_SET_RESPONSE);

    /* update cache */
    strcpy(last_rndv_accessed_key, key);
    last_accesed_key_val_length = value_length;
    last_accessed_server_addr = set_packet->rndv_set_response.server_ptr;
    last_accessed_server_rkey = set_packet->rndv_set_response.server_key;

    pp_post_send(ctx, IBV_WR_RDMA_WRITE, value_length + 1, value, (void *)set_packet->rndv_set_response.server_ptr, set_packet->rndv_set_response.server_key);
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
        printf("ERROR");//todo
        return -1;
    }

    if (strcmp(key, last_rndv_accessed_key) != 0) {
        get_packet->type = EAGER_GET_REQUEST;
        memcpy(get_packet->eager_get_request.key, key, key_length + 1);

        /* Sends the packet to the server */
        pp_post_send(ctx, IBV_WR_SEND, packet_size, NULL, NULL, 0);
        /* await EAGER_GET_REQUEST completion */
        pp_wait_completions(ctx, 1);
        /* await EAGER_GET_RESPONSE / RNDV_GET_RESPONSE completion */
        pp_wait_completions(ctx, 1);

        struct packet *response_packet = (struct packet *) ctx->buf;
        unsigned int value_len;
        switch (response_packet->type) {

            case EAGER_GET_RESPONSE:
                value_len = response_packet->eager_get_response.value_length;
                *value = (char *) malloc(value_len + 1);
                memcpy(*value, response_packet->eager_get_response.value, value_len);
                memset(&((*value)[value_len]), '\0', 1);
                break;

            case RENDEZVOUS_GET_RESPONSE:
                value_len = response_packet->rndv_get_response.value_length;
                *value = calloc(value_len + 1, 1);

                /* update cache */
                strcpy(last_rndv_accessed_key, key);
                last_accesed_key_val_length = value_len;
                last_accessed_server_addr = response_packet->rndv_set_response.server_ptr;
                last_accessed_server_rkey = response_packet->rndv_set_response.server_key;

                // set temp mr
                struct ibv_mr *orig_mr = ctx->mr;
                struct ibv_mr *temp_mr = ibv_reg_mr(ctx->pd, (void *) value, value_len + 1,
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
        *value = calloc(last_accesed_key_val_length + 1, 1);

        // set temp mr
        struct ibv_mr *orig_mr = ctx->mr;
        struct ibv_mr *temp_mr = ibv_reg_mr(ctx->pd, (void *) value, last_accesed_key_val_length + 1,
                                            IBV_ACCESS_LOCAL_WRITE |
                                            IBV_ACCESS_REMOTE_WRITE |
                                            IBV_ACCESS_REMOTE_READ);
        ctx->mr = temp_mr;

        pp_post_send(ctx, IBV_WR_RDMA_READ, last_accesed_key_val_length + 1, *value,
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

    pp_post_send(ctx, IBV_WR_SEND, packet_size, NULL, NULL, 0); /* Sends the packet to the server */

    pp_wait_completions(ctx, 1); /* await EAGER_GET_REQUEST completion */

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

#define my_open    dkv_open
#define set(a,b,c) dkv_set(a,b,c,strlen(c))
#define get(a,b,c) dkv_get(a,b,c,&g_argc)
#define release    dkv_release
#define my_close   dkv_close
#endif /* EX4 */



//void print_results_to_file(FILE * results_file, ssize_t value_size, double throughput) {
//    char * value_size_unit;
//    char * rate_unit;
//
//    if (throughput >= GIGABIT_IN_BITS) {
//        throughput /= GIGABIT_IN_BITS;
//        rate_unit = "Gbps";
//
//    } else if (throughput >= MEGABIT_IN_BITS) {
//        throughput /= MEGABIT_IN_BITS;
//        rate_unit = "Mbps";
//
//    } else if (throughput >= KILOBIT_IN_BITS) {
//        throughput /= KILOBIT_IN_BITS;
//        rate_unit = "Kbps";
//
//    } else {
//        rate_unit = "bps";
//    }
//
//    if (value_size >= MEGABYTE_IN_BYTES) {
//        value_size /= MEGABIT_IN_BITS;
//        value_size_unit = "MBytes";
//
//    } else if (value_size >= KILOBYTE_IN_BYTES) {
//        value_size /= KILOBIT_IN_BITS;
//        value_size_unit = "KBytes";
//
//    } else {
//        value_size_unit = "Bytes";
//    }
//
//    fprintf(results_file, "Value size: %ld\t%s,\tThroughput: %.3f\t%s\n",
//            value_size, value_size_unit, throughput, rate_unit);
//}
//
//
//void run_throuput_tests(void *kv_ctx, FILE * results_file, bool rndv_mode) {
//    char send_buffer[MAX_TEST_SIZE] = {0};
//    char *recv_buffer;
//
//    use_rndv_protocol = rndv_mode;
//    if (rndv_mode) {
//        printf("Testing Rendezvous protocol...\n");
//        fprintf(results_file, "\nRendezvous protocol:\n\n");
//    } else {
//        printf("Testing Eager protocol...\n");
//        fprintf(results_file, "\nEager protocol:\n\n");
//    }
//    unsigned packet_struct_size = sizeof(struct packet);
//    ssize_t maximal_test_size = (use_rndv_protocol ? MAX_TEST_SIZE : EAGER_PROTOCOL_LIMIT);
//
//
//    for (ssize_t value_size = 1; value_size < maximal_test_size; value_size = value_size<< 1) {
//        struct timeval start, end;
//        double total_time_usec = 0.0;
//        int total_bytes = 0;
//        int total_attempts = 50;
//        memset(send_buffer, 'a', value_size);
//
//        if (gettimeofday(&start, NULL)) {
//            perror("gettimeofday");
//            break;
//        }
//
//        char key[10];
//        for (int attempt = 0; attempt < total_attempts; attempt++) {
//            sprintf(key, "%ld-%d", value_size, attempt);
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
//        fflush(stdout);
//    }
//}


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

#ifdef EX4
        assert(0 == my_open(servers, indexer, &kv_ctx));
#else
    assert(0 == my_open(&servers[0], &kv_ctx));
#endif

    /* Test small size */
    assert(100 < MAX_TEST_SIZE);
    memset(send_buffer, 'a', 100);
    assert(0 == set(kv_ctx, "1", send_buffer));
    assert(0 == get(kv_ctx, "1", &recv_buffer));
    assert(0 == strcmp(send_buffer, recv_buffer));
    release(recv_buffer);

    /* Test logic */
    assert(0 == get(kv_ctx, "1", &recv_buffer));
    assert(0 == strcmp(send_buffer, recv_buffer));
    release(recv_buffer);
    memset(send_buffer, 'b', 100);
    assert(0 == set(kv_ctx, "1", send_buffer));
    memset(send_buffer, 'c', 100);
    assert(0 == set(kv_ctx, "22", send_buffer));
    memset(send_buffer, 'b', 100);
    assert(0 == get(kv_ctx, "1", &recv_buffer));
    assert(0 == strcmp(send_buffer, recv_buffer));
    release(recv_buffer);

    /* Test large size */
    memset(send_buffer, 'a', MAX_TEST_SIZE - 1);
    assert(0 == set(kv_ctx, "1", send_buffer));
    assert(0 == set(kv_ctx, "333", send_buffer));
    assert(0 == get(kv_ctx, "1", &recv_buffer));
    assert(0 == strcmp(send_buffer, recv_buffer));
    release(recv_buffer);


#ifdef EX4
//    recursive_fill_kv(TEST_LOCATION, kv_ctx);
#endif

    my_close(kv_ctx);
    return 0;
}
