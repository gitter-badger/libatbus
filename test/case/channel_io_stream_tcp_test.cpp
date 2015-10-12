#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <atomic>
#include <memory>
#include <thread>
#include <chrono>
#include <functional>

#include <detail/libatbus_error.h>
#include "detail/libatbus_channel_export.h"
#include "frame/test_macros.h"

static const size_t MAX_TEST_BUFFER_LEN = 1024 * 32;
static int g_check_flag = 0;

static void accepted_callback_test_fn(
    atbus::channel::io_stream_channel* channel,         // 事件触发的channel
    atbus::channel::io_stream_connection* connection,   // 事件触发的连接
    int status,                         // libuv传入的转态码
    void*,                              // 额外参数(不同事件不同含义)
    size_t s                            // 额外参数长度
    ) {
    CASE_EXPECT_NE(NULL, channel);
    CASE_EXPECT_NE(NULL, connection);
    CASE_EXPECT_EQ(0, status);

    if (0 != status) {
        CASE_ERROR() << uv_err_name(status) << ":" << uv_strerror(status) << std::endl;
    } else {
        CASE_INFO() << "accept connection: " << connection->addr.address<< std::endl;
    }

    ++g_check_flag;
}

static void listen_callback_test_fn(
    atbus::channel::io_stream_channel* channel,         // 事件触发的channel
    atbus::channel::io_stream_connection* connection,   // 事件触发的连接
    int status,                         // libuv传入的转态码
    void*,                              // 额外参数(不同事件不同含义)
    size_t s                            // 额外参数长度
    ) {
    CASE_EXPECT_NE(NULL, channel);
    CASE_EXPECT_NE(NULL, connection);
    CASE_EXPECT_EQ(0, status);

    // listen accepted event
    connection->evt.callbacks[atbus::channel::io_stream_callback_evt_t::EN_FN_ACCEPTED] = accepted_callback_test_fn;

    ++g_check_flag;
}

static void connected_callback_test_fn(
    atbus::channel::io_stream_channel* channel,         // 事件触发的channel
    atbus::channel::io_stream_connection* connection,   // 事件触发的连接
    int status,                         // libuv传入的转态码
    void*,                              // 额外参数(不同事件不同含义)
    size_t s                            // 额外参数长度
    ) {
    CASE_EXPECT_NE(NULL, channel);
    CASE_EXPECT_NE(NULL, connection);
    CASE_EXPECT_EQ(0, status);

    if (0 != status) {
        CASE_ERROR() << uv_err_name(status) << ":"<< uv_strerror(status) << std::endl;
    } else {
        CASE_INFO() << "connect to " << connection->addr.address<< " success" << std::endl;
    }

    ++g_check_flag;
}

static void setup_channel(atbus::channel::io_stream_channel& channel, const char* listen, const char* conn) {
    atbus::channel::channel_address_t addr;

    int res = 0;
    if (NULL != listen) {
        atbus::channel::make_address(listen, addr);
        res = atbus::channel::io_stream_listen(&channel, addr, listen_callback_test_fn);
    } else {
        atbus::channel::make_address(conn, addr);
        res = atbus::channel::io_stream_connect(&channel, addr, connected_callback_test_fn);
    }

    if (0 != res) {
        CASE_ERROR() << uv_err_name(channel.error_code) << ":" << uv_strerror(channel.error_code) << std::endl;
    }
}


/*static char* get_test_buffer() {
    static char ret[MAX_TEST_BUFFER_LEN] = {0};
    if (0 != ret[0]) {
        return ret;
    }

    for (size_t i = 0; i < MAX_TEST_BUFFER_LEN; ++ i) {
        ret[i] = 'A' + rand() % 26;
    }

    return ret;
}*/

CASE_TEST(channel, io_stream_tcp_basic)
{
    atbus::channel::io_stream_channel svr, cli;
    atbus::channel::io_stream_init(&svr, NULL, NULL);
    atbus::channel::io_stream_init(&cli, NULL, NULL);

    g_check_flag = 0;

    setup_channel(svr, "ipv6://:::16387", NULL);
    CASE_EXPECT_EQ(1, g_check_flag);
    CASE_EXPECT_NE(NULL, svr.ev_loop);

    setup_channel(cli, NULL, "ipv4://127.0.0.1:16387");
    setup_channel(cli, NULL, "dns://localhost:16387");
    setup_channel(cli, NULL, "ipv6://::1:16387");

    int check_flag = g_check_flag;
    while (g_check_flag - check_flag < 6) {
        atbus::channel::io_stream_run(&svr, atbus::adapter::RUN_NOWAIT);
        atbus::channel::io_stream_run(&cli, atbus::adapter::RUN_NOWAIT);
        CASE_THREAD_SLEEP_MS(64);
    }

    atbus::channel::io_stream_close(&svr);
    atbus::channel::io_stream_close(&cli);
    //CASE_EXPECT_EQ(0, uv_loop_alive(svr.ev_loop));
    //CASE_EXPECT_EQ(0, uv_loop_alive(cli.ev_loop));
}
