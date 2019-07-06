//
// Created by netanelf on 7/3/19.
//

#ifndef COMM_NETS_KV_SHARED_H
#define COMM_NETS_KV_SHARED_H

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
#include <sys/param.h>

#include <infiniband/verbs.h>

#define EX3
//#define EX4

#ifdef EX4
#include <fcntl.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/stat.h>
#endif

//#define _GNU_SOURCE


#define EAGER_PROTOCOL_LIMIT (1 << 12) /* 4KB limit */
#define MAX_TEST_SIZE (10 * EAGER_PROTOCOL_LIMIT)
#define TEST_LOCATION "~/www/"

#define MIN_POOL_NODES 10
#define KILOBIT_IN_BITS 1000
#define MEGABIT_IN_BITS 1000000
#define GIGABIT_IN_BITS 1000000000
#define KILOBYTE_IN_BYTES 1024
#define MEGABYTE_IN_BYTES 1048576


typedef struct MEMORY_INFO {
    struct ibv_mr * rndv_mr;
    char rndv_buffer[MAX_TEST_SIZE];
    struct MEMORY_INFO * next_mem;
} MEMORY_INFO;


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
//            unsigned int value_length;
            char key_and_value[0]; /* null terminator between key and value */
        } eager_set_request;

        struct {
            // previously mentioned: EAGER_SET_RESPONSE - not needed!
        } eager_set_response;

        /* RENDEZVOUS PROTOCOL PACKETS */
        struct {
            unsigned int key_len;
            char key[0];
        } rndv_get_request;

        struct {
            unsigned int value_length;
            uint64_t server_ptr;
            uint32_t server_key;
        } rndv_get_response;

        struct {
//            unsigned int key_len;//todo add value len, and multi size bufs
            unsigned int value_length;
            char key[0];
        } rndv_set_request;

        struct {
            uint64_t server_ptr;
            unsigned int server_key;
        } rndv_set_response;

//        struct {
//            unsigned int to_close;
//        } close_connection;

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

enum ibv_mtu pp_mtu_to_enum(int mtu);

uint16_t pp_get_local_lid(struct ibv_context *context, int port);

int pp_get_port_info(struct ibv_context *context, int port, struct ibv_port_attr *attr);

void wire_gid_to_gid(const char *wgid, union ibv_gid *gid);

void gid_to_wire_gid(const union ibv_gid *gid, char wgid[]);

static int pp_connect_ctx(struct pingpong_context *ctx, int port, int my_psn,
                          enum ibv_mtu mtu, int sl,
                          struct pingpong_dest *dest, int sgid_idx);

static struct pingpong_dest *pp_client_exch_dest(const char *servername, int port,
                                                 const struct pingpong_dest *my_dest);

static struct pingpong_dest *pp_server_exch_dest(struct pingpong_context *ctx,
                                                 int ib_port, enum ibv_mtu mtu,
                                                 int port, int sl,
                                                 const struct pingpong_dest *my_dest,
                                                 int sgid_idx);

static struct pingpong_context *pp_init_ctx(struct ibv_device *ib_dev, int size,
                                            int rx_depth, int port,
                                            int use_event, int is_server);

int pp_close_ctx(struct pingpong_context *ctx);

static int pp_post_recv(struct pingpong_context *ctx, int n);

static int pp_post_send(struct pingpong_context *ctx, enum ibv_wr_opcode opcode, unsigned size, const char *local_ptr, void *remote_ptr, uint32_t remote_key);

static void usage(const char *argv0);

int orig_main(struct kv_server_address *server, unsigned size, int argc, char *argv[], struct pingpong_context **result_ctx);

//int pp_wait_completions(struct pingpong_context *ctx, int iters);


#endif //COMM_NETS_KV_SHARED_H
