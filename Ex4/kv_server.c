#include "kv_shared.h"


int g_argc;
char **g_argv;

/* server data structs for use */

typedef struct {
    struct ibv_mr * rndv_mr;
    char rndv_buffer[MAX_TEST_SIZE];
    struct MEMORY_INFO * next_mem;
} MEMORY_INFO;

typedef struct {
    struct KV_ENTRY  * prev_entry;
    struct KV_ENTRY * next_entry;
    unsigned int key_len;
    unsigned int val_len;
    MEMORY_INFO * large_val_mem_info;
    char * key_and_value[0];
} KV_ENTRY;

KV_ENTRY * entries_head = NULL;
KV_ENTRY * entries_tail = NULL;
int entries_cnt = 0;

MEMORY_INFO * mem_pool_head = NULL;
MEMORY_INFO * mem_pool_tail = NULL;
int pool_size = 0;


//struct KV_NODE {
//    struct KV_NODE * next;
//    struct KV_NODE * prev;
//    unsigned int val_len;
//    char key_and_value[0];
//};
//
//struct RNDV_MEMORY_INFO {
//    struct ibv_mr * rndv_mr;
//    char rndv_buffer[MAX_TEST_SIZE];
//    struct RNDV_MEMORY_INFO * next;
//};
//
//struct RNDV_NODE {
//    struct RNDV_NODE * next;
//    struct RNDV_MEMORY_INFO * mem_info;
//    unsigned int key_len;
//    char key[0];
//};

//struct RNDV_CACHE_NODE {//todo check if needed
//    struct RNDV_CACHE_NODE * next;
//    uint64_t srv_addr;
//    unsigned int srv_rkey;
//    unsigned int val_len;
//    unsigned int key_len;
//    char key[0];
//};

//struct KV_NODE * kv_head = NULL;
//struct KV_NODE * kv_tail = NULL;
//int kv_nodes_counter = 0;

///* server data structs for use */
//struct RNDV_NODE * rndv_head = NULL;
//struct RNDV_NODE * rndv_tail = NULL;
//struct RNDV_MEMORY_INFO * rndv_pool_head = NULL;
//struct RNDV_MEMORY_INFO * rndv_pool_tail = NULL;
//int rndv_pool_nodes_counter = 0;

bool close_server = false;
bool use_rndv_protocol = false;


void handle_server_packets_only(struct pingpong_context *ctx, struct packet *packet) {
    unsigned response_size = 0;
    struct KV_NODE * cur_node = kv_head;
    int key_length;

    switch (packet->type) {

        /* Only handle packets relevant to the server here - client will handle inside get/set() calls */
        case EAGER_GET_REQUEST:
            key_length = strlen(packet->eager_get_request.key);

            while (cur_node != NULL) {
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
                }
                cur_node = cur_node->next;
            }

            /* No nodes or match wasn't found */
            if (cur_node == NULL) {
                /* key is not exists on server, respond "" */
                struct packet *response_packet = ctx->buf;
                response_packet->type = EAGER_GET_RESPONSE;
                memset(response_packet->eager_get_response.value, '\0', 1);
                response_packet->eager_get_response.value_length = 1;
                response_size = sizeof(response_packet);
            }
            break;

        case EAGER_SET_REQUEST:
            key_length = strlen(packet->eager_set_request.key_and_value);
            unsigned int vlength = packet->eager_set_request.value_length;

            while (cur_node != NULL) {
                if (strcmp(cur_node->key_and_value, packet->eager_set_request.key_and_value) == 0) {
                    /* found match */
                    struct KV_NODE * prev_node = cur_node->prev;
                    struct KV_NODE * next_node = cur_node->next;
                    free(cur_node);
                    cur_node = (struct KV_NODE * ) malloc(sizeof(struct KV_NODE) + key_length + 2 + vlength);
                    memcpy(cur_node->key_and_value, packet->eager_set_request.key_and_value, key_length + 1 + vlength);
                    memset(&(kv_head->key_and_value[key_length + 1 + vlength]), '\0', 1);

                    if (prev_node != NULL) {
                        prev_node->next = cur_node;
                    }
                    cur_node->prev = prev_node;
                    cur_node->next = next_node;
                    if (next_node != NULL) {
                        next_node->prev = cur_node;
                    }
                    break;
                }
                cur_node = cur_node->next;
            }

            if (cur_node == NULL) {
                /* key is not exists on server, appending it */
                struct KV_NODE * temp_node = (struct KV_NODE *) malloc(sizeof(struct KV_NODE) + key_length + 2 + vlength);
                memcpy(temp_node->key_and_value, packet->eager_set_request.key_and_value, key_length + 1 + vlength);
                temp_node->next = NULL;
                temp_node->val_len = vlength;

                if (kv_tail == NULL) {
                    temp_node->prev = NULL;
                    kv_head = temp_node;
                    kv_tail = temp_node;
                } else {
                    temp_node->prev = kv_tail;
                    kv_tail->next = temp_node;
                    kv_tail = temp_node;
                }
                kv_nodes_counter++;
            }
            break;

        case RENDEZVOUS_GET_REQUEST:

            key_length = packet->rndv_get_request.key_len;

            /* couldn't find existing key  - create a (empty) node for it */

            struct RNDV_NODE * rndv_tempGET = (struct RNDV_NODE *) malloc(sizeof(struct RNDV_NODE) + key_length + 1);
            rndv_tempGET->next = NULL;
            rndv_tempGET->mem_info = rndv_pool_head;
            rndv_pool_head = rndv_pool_head->next;
            rndv_pool_nodes_counter--;
            rndv_tempGET->mem_info->next = NULL;
            rndv_tempGET->key_len = key_length;
            strncpy(rndv_tempGET->key, packet->rndv_get_request.key, key_length);
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
            break;

        case RENDEZVOUS_SET_REQUEST:
            key_length = packet->rndv_set_request.key_len;

            /* RENDEZVOUS_SET_REQUEST in case client does NOT have the remote details.
            add a rndv node to store value */

            struct RNDV_NODE * rndv_temp = (struct RNDV_NODE *) malloc(sizeof(struct RNDV_NODE) + key_length + 1);
            rndv_temp->next = NULL;
            rndv_temp->mem_info = rndv_pool_head;
            rndv_pool_head = rndv_pool_head->next;
            rndv_pool_nodes_counter--;
            rndv_temp->mem_info->next = NULL;
            strncpy(rndv_temp->mem_info->rndv_buffer, packet->rndv_set_request.key, key_length);
            if (rndv_tail != NULL) {
                rndv_tail->next = rndv_temp;
                rndv_tail = rndv_temp;
            } else {
                rndv_head = rndv_temp;
                rndv_tail = rndv_temp;
            }
            struct packet * response_packet = (struct packet*) ctx->buf;
            response_size = sizeof(response_packet) + sizeof(uint64_t) + sizeof(unsigned int);
            response_packet->type = RENDEZVOUS_SET_RESPONSE;
            response_packet->rndv_set_response.server_ptr = (uint64_t) rndv_tail->mem_info->rndv_mr->addr;
            response_packet->rndv_set_response.server_key = rndv_tail->mem_info->rndv_mr->rkey;
            break;
        case CLOSE_CONNECTION:
            close_server = true;
            break;
#ifdef EX4
            case FIND: /* TODO (2LOC): use some hash function */
#endif
        default:
            break;
    }

    if (response_size) {
        pp_post_send(ctx, IBV_WR_SEND, response_size, NULL, NULL, 0);
    }
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
            while (rndv_pool_nodes_counter < MIN_POOL_NODES) {
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
                rndv_pool_nodes_counter++;
            }
        }
    }
    return 0;
}


void run_server() {
    struct pingpong_context *ctx;
    struct kv_server_address server = {0};
    server.port = 12345;
    assert(0 == orig_main(&server, EAGER_PROTOCOL_LIMIT, g_argc, g_argv, &ctx));
    while (0 <= pp_wait_completions(ctx, 1));
    pp_close_ctx(ctx);
}


int main(int argc, char **argv) {
    void *kv_ctx; /* handle to internal KV-client context */
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
