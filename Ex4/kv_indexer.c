#include "kv_shared.h"


int g_argc;
char **g_argv;

////same as client
//int main(int argc, char **argv)
//{
//    void *kv_ctx; /* handle to internal KV-client context */
//
//    char send_buffer[MAX_TEST_SIZE] = {0};
//    char *recv_buffer;
//
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
//
//    g_argc = argc;
//    g_argv = argv;
//
//#ifdef EX4
//    assert(0 == my_open(servers, indexer, &kv_ctx));
//#else
//    assert(0 == my_open(&servers[0], &kv_ctx));
//#endif
//
//    /* Test small size */
//    assert(100 < MAX_TEST_SIZE);
//    memset(send_buffer, 'a', 100);
//    assert(0 == set(kv_ctx, "1", send_buffer));
//    assert(0 == get(kv_ctx, "1", &recv_buffer));
//    assert(0 == strcmp(send_buffer, recv_buffer));
//    release(recv_buffer);
//
//    /* Test logic */
//    assert(0 == get(kv_ctx, "1", &recv_buffer));
//    assert(0 == strcmp(send_buffer, recv_buffer));
//    release(recv_buffer);
//    memset(send_buffer, 'b', 100);
//    assert(0 == set(kv_ctx, "1", send_buffer));
//    memset(send_buffer, 'c', 100);
//    assert(0 == set(kv_ctx, "22", send_buffer));
//    memset(send_buffer, 'b', 100);
//    assert(0 == get(kv_ctx, "1", &recv_buffer));
//    assert(0 == strcmp(send_buffer, recv_buffer));
//    release(recv_buffer);
//
//    /* Test large size */
//    memset(send_buffer, 'a', MAX_TEST_SIZE - 1);
//    assert(0 == set(kv_ctx, "1", send_buffer));
//    assert(0 == set(kv_ctx, "333", send_buffer));
//    assert(0 == get(kv_ctx, "1", &recv_buffer));
//    assert(0 == strcmp(send_buffer, recv_buffer));
//    release(recv_buffer);
//
//
//#ifdef EX4
//    recursive_fill_kv(TEST_LOCATION, kv_ctx);
//#endif
//
//    my_close(kv_ctx);
//    return 0;
//}
