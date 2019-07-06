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
//    unsigned int val_len;
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


void handle_server_packets_only(struct pingpong_context *ctx, struct packet *packet) {
    unsigned response_size = 0;
    KV_ENTRY * current_node = entries_head;
    unsigned int key_length;
    unsigned int value_length;

    switch (packet->type) {

        /* Only handle packets relevant to the server here - client will handle inside get/set() calls */
        case EAGER_GET_REQUEST:

            while (current_node != NULL) {
                if (strcmp(current_node->key, packet->eager_get_request.key) == 0) {
                    /* found match */
                    struct packet * response_packet = ctx->buf;

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
//            key_length = packet->rndv_get_request.key_len;
//
//            /* couldn't find existing key  - create a (empty) node for it */
//
//            struct RNDV_NODE * rndv_tempGET = (struct RNDV_NODE *) malloc(sizeof(struct RNDV_NODE) + key_length + 1);
//            rndv_tempGET->next = NULL;
//            rndv_tempGET->mem_info = rndv_pool_head;
//            rndv_pool_head = rndv_pool_head->next;
//            rndv_pool_nodes_counter--;
//            rndv_tempGET->mem_info->next = NULL;
//            rndv_tempGET->key_len = key_length;
//            strncpy(rndv_tempGET->key, packet->rndv_get_request.key, key_length);
//            if (rndv_tail != NULL) {
//                rndv_tail->next = rndv_tempGET;
//                rndv_tail = rndv_tempGET;
//            } else {
//                rndv_head = rndv_tempGET;
//                rndv_tail = rndv_tempGET;
//            }
//
//            struct packet * response_packetGET = (struct packet*) ctx->buf;
//            response_packetGET->type = RENDEZVOUS_GET_RESPONSE;
//            response_packetGET->rndv_get_response.value_length = 1;
//            response_packetGET->rndv_get_response.server_ptr = (uint64_t) rndv_tail->mem_info->rndv_mr->addr;
//            response_packetGET->rndv_get_response.server_key = rndv_tail->mem_info->rndv_mr->rkey;
            break;

        case RENDEZVOUS_SET_REQUEST:
            key_length = strlen(packet->rndv_set_request.key);
            value_length = packet->rndv_set_request.value_length;

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

                    packet->type = RENDEZVOUS_SET_RESPONSE;
                    packet->rndv_set_response.server_ptr = (uint64_t) current_node->large_val_mem_info->rndv_mr->addr;
                    packet->rndv_set_response.server_key = current_node->large_val_mem_info->rndv_mr->rkey;
                    response_size = sizeof(struct packet);
                    break;

                } else {
                    current_node = current_node->next_entry;
                }
            }

            if (current_node == NULL) {
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
                temp_node->large_val_mem_info = mem_pool_head;
                mem_pool_head = mem_pool_head->next_mem;
                temp_node->large_val_mem_info->next_mem = NULL;
                pool_size--;

                packet->type = RENDEZVOUS_SET_RESPONSE;
                packet->rndv_set_response.server_ptr = (uint64_t) temp_node->large_val_mem_info->rndv_mr->addr;
                packet->rndv_set_response.server_key = temp_node->large_val_mem_info->rndv_mr->rkey;
                response_size = sizeof(struct packet);
            }

            break;
//        case CLOSE_CONNECTION:
//            close_server = true;
//            break;
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
//        if (ctx->server) {
//            while (rndv_pool_nodes_counter < MIN_POOL_NODES) {
//                rndv_pool_tail->next = (struct RNDV_MEMORY_INFO *) malloc(
//                        sizeof(struct RNDV_MEMORY_INFO));
//                rndv_pool_tail = rndv_pool_tail->next;
//                rndv_pool_tail->next = NULL;
//                memset(rndv_pool_tail->rndv_buffer, '\0', MAX_TEST_SIZE);
//                rndv_pool_tail->rndv_mr = ibv_reg_mr(ctx->pd, &(rndv_pool_tail->rndv_buffer),
//                                                     MAX_TEST_SIZE, IBV_ACCESS_LOCAL_WRITE |
//                                                                    IBV_ACCESS_REMOTE_WRITE |
//                                                                    IBV_ACCESS_REMOTE_READ);
//                if (!rndv_pool_tail->rndv_mr) {
//                    fprintf(stderr, "Couldn't register MR\n");
//                    return 1;
//                }
//                rndv_pool_nodes_counter++;
//            }
//        }
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
