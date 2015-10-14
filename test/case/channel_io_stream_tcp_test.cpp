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
static std::list<std::pair<size_t, size_t> > g_check_buff_sequence;

static void disconnected_callback_test_fn(
    const atbus::channel::io_stream_channel* channel,         // 事件触发的channel
    const atbus::channel::io_stream_connection* connection,   // 事件触发的连接
    int status,                         // libuv传入的转态码
    void*,                              // 额外参数(不同事件不同含义)
    size_t s                            // 额外参数长度
    ) {
    CASE_EXPECT_NE(NULL, channel);
    CASE_EXPECT_NE(NULL, connection);
    CASE_EXPECT_EQ(0, status);

    if (0 != status) {
        CASE_MSG_INFO() << uv_err_name(status) << ":" << uv_strerror(status) << std::endl;
    } else {
        CASE_MSG_INFO() << "disconnect done: " << connection->addr.address << std::endl;
    }

    ++g_check_flag;
}

static void accepted_callback_test_fn(
    const atbus::channel::io_stream_channel* channel,         // 事件触发的channel
    const atbus::channel::io_stream_connection* connection,   // 事件触发的连接
    int status,                         // libuv传入的转态码
    void*,                              // 额外参数(不同事件不同含义)
    size_t s                            // 额外参数长度
    ) {
    CASE_EXPECT_NE(NULL, channel);
    CASE_EXPECT_NE(NULL, connection);
    CASE_EXPECT_EQ(0, status);

    if (0 != status) {
        CASE_MSG_INFO() << uv_err_name(status) << ":" << uv_strerror(status) << std::endl;
    } else {
        CASE_MSG_INFO() << "accept connection: " << connection->addr.address<< std::endl;
    }

    ++g_check_flag;
}

static void listen_callback_test_fn(
    const atbus::channel::io_stream_channel* channel,         // 事件触发的channel
    const atbus::channel::io_stream_connection* connection,   // 事件触发的连接
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
    const atbus::channel::io_stream_channel* channel,         // 事件触发的channel
    const atbus::channel::io_stream_connection* connection,   // 事件触发的连接
    int status,                         // libuv传入的转态码
    void*,                              // 额外参数(不同事件不同含义)
    size_t s                            // 额外参数长度
    ) {
    CASE_EXPECT_NE(NULL, channel);
    CASE_EXPECT_NE(NULL, connection);
    CASE_EXPECT_EQ(0, status);

    if (0 != status) {
        CASE_MSG_INFO() << uv_err_name(status) << ":"<< uv_strerror(status) << std::endl;
    } else {
        CASE_MSG_INFO() << "connect to " << connection->addr.address<< " success" << std::endl;
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
        CASE_MSG_INFO() << uv_err_name(channel.error_code) << ":" << uv_strerror(channel.error_code) << std::endl;
    }
}

static char* get_test_buffer() {
    static char ret[MAX_TEST_BUFFER_LEN] = {0};
    if (0 != ret[0]) {
        return ret;
    }

    for (size_t i = 0; i < MAX_TEST_BUFFER_LEN - 1; ++ i) {
        ret[i] = 'A' + rand() % 26;
    }

    return ret;
}

static void recv_callback_check_fn(
    const atbus::channel::io_stream_channel* channel,         // 事件触发的channel
    const atbus::channel::io_stream_connection* connection,   // 事件触发的连接
    int status,                         // libuv传入的转态码
    void* input,                        // 额外参数(不同事件不同含义)
    size_t s                            // 额外参数长度
    ) {
    CASE_EXPECT_NE(NULL, channel);
    CASE_EXPECT_NE(NULL, connection);
    CASE_EXPECT_NE(NULL, input);
    CASE_EXPECT_EQ(0, status);

    CASE_EXPECT_FALSE(g_check_buff_sequence.empty());
    if (g_check_buff_sequence.empty()) {
        return;
    }

    CASE_EXPECT_EQ(s, g_check_buff_sequence.front().second);
    char* buff = get_test_buffer();
    char* input_buff = reinterpret_cast<char*>(input);
    for (size_t i = 0; i < g_check_buff_sequence.front().second; ++ i) {
        CASE_EXPECT_EQ(buff[i + g_check_buff_sequence.front().first], input_buff[i]);
        if (buff[i + g_check_buff_sequence.front().first] != input_buff[i]) {
            break;
        }
    }
    g_check_buff_sequence.pop_front();

    ++g_check_flag;
}

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


    svr.evt.callbacks[atbus::channel::io_stream_callback_evt_t::EN_FN_RECVED] = recv_callback_check_fn;
    cli.evt.callbacks[atbus::channel::io_stream_callback_evt_t::EN_FN_RECVED] = recv_callback_check_fn;
    char* buf = get_test_buffer();

    check_flag = g_check_flag;
    // small buffer
    atbus::channel::io_stream_send(cli.conn_pool.begin()->second.get(), buf, 13);
    g_check_buff_sequence.push_back(std::make_pair(0, 13));
    atbus::channel::io_stream_send(cli.conn_pool.begin()->second.get(), buf + 13, 28);
    g_check_buff_sequence.push_back(std::make_pair(13, 28));
    atbus::channel::io_stream_send(cli.conn_pool.begin()->second.get(), buf + 13 + 28, 100);
    g_check_buff_sequence.push_back(std::make_pair(13 + 28, 100));

    // big buffer
    atbus::channel::io_stream_send(cli.conn_pool.begin()->second.get(), buf + 1024, 56 * 1024 + 3);
    g_check_buff_sequence.push_back(std::make_pair(1024, 56 * 1024 + 3));

    while (g_check_flag - check_flag < 4) {
        atbus::channel::io_stream_run(&svr, atbus::adapter::RUN_NOWAIT);
        atbus::channel::io_stream_run(&cli, atbus::adapter::RUN_NOWAIT);
        CASE_THREAD_SLEEP_MS(64);
    }

    // many big buffer
    {
        check_flag = g_check_flag;
        atbus::channel::io_stream_channel::conn_pool_t::iterator it = svr.conn_pool.begin();
        // 跳过listen的socket
        if (it->second->addr.address == "ipv6://:::16387") {
            ++it;
        }
        for (int i = 0; i < 153; ++ i) {
            size_t s = static_cast<size_t>(rand() % 2048);
            size_t l = static_cast<size_t>(rand() % 10240) + 20 * 1024;
            atbus::channel::io_stream_send(it->second.get(), buf + s, l);
            g_check_buff_sequence.push_back(std::make_pair(s, l));
        }

        while (g_check_flag - check_flag < 153) {
            atbus::channel::io_stream_run(&svr, atbus::adapter::RUN_NOWAIT);
            atbus::channel::io_stream_run(&cli, atbus::adapter::RUN_NOWAIT);
            CASE_THREAD_SLEEP_MS(64);
        }
    }

    atbus::channel::io_stream_close(&svr);
    atbus::channel::io_stream_close(&cli);
    CASE_EXPECT_EQ(0, svr.conn_pool.size());
    CASE_EXPECT_EQ(0, cli.conn_pool.size());
}


// reset by peer(client)
CASE_TEST(channel, io_stream_tcp_reset_by_client)
{
    atbus::channel::io_stream_channel svr, cli;
    atbus::channel::io_stream_init(&svr, NULL, NULL);
    atbus::channel::io_stream_init(&cli, NULL, NULL);

    svr.evt.callbacks[atbus::channel::io_stream_callback_evt_t::EN_FN_DISCONNECTED] = disconnected_callback_test_fn;

    int check_flag = g_check_flag = 0;

    setup_channel(svr, "ipv6://:::16387", NULL);
    CASE_EXPECT_EQ(1, g_check_flag);
    CASE_EXPECT_NE(NULL, svr.ev_loop);

    setup_channel(cli, NULL, "ipv4://127.0.0.1:16387");
    setup_channel(cli, NULL, "dns://localhost:16387");
    setup_channel(cli, NULL, "ipv6://::1:16387");

    while (g_check_flag - check_flag < 7) {
        atbus::channel::io_stream_run(&svr, atbus::adapter::RUN_NOWAIT);
        atbus::channel::io_stream_run(&cli, atbus::adapter::RUN_NOWAIT);
        CASE_THREAD_SLEEP_MS(64);
    }

    check_flag = g_check_flag;
    atbus::channel::io_stream_close(&cli);
    CASE_EXPECT_EQ(0, cli.conn_pool.size());

    while (g_check_flag - check_flag < 3) {
        atbus::channel::io_stream_run(&svr, atbus::adapter::RUN_NOWAIT);
        CASE_THREAD_SLEEP_MS(64);
    }
    CASE_EXPECT_EQ(1, svr.conn_pool.size());

    atbus::channel::io_stream_close(&svr);
    CASE_EXPECT_EQ(0, svr.conn_pool.size());
}

// reset by peer(server)
CASE_TEST(channel, io_stream_tcp_reset_by_server)
{
    atbus::channel::io_stream_channel svr, cli;
    atbus::channel::io_stream_init(&svr, NULL, NULL);
    atbus::channel::io_stream_init(&cli, NULL, NULL);

    cli.evt.callbacks[atbus::channel::io_stream_callback_evt_t::EN_FN_DISCONNECTED] = disconnected_callback_test_fn;

    int check_flag = g_check_flag = 0;

    setup_channel(svr, "ipv6://:::16387", NULL);
    CASE_EXPECT_EQ(1, g_check_flag);
    CASE_EXPECT_NE(NULL, svr.ev_loop);

    setup_channel(cli, NULL, "ipv4://127.0.0.1:16387");
    setup_channel(cli, NULL, "dns://localhost:16387");
    setup_channel(cli, NULL, "ipv6://::1:16387");

    while (g_check_flag - check_flag < 7) {
        atbus::channel::io_stream_run(&svr, atbus::adapter::RUN_NOWAIT);
        atbus::channel::io_stream_run(&cli, atbus::adapter::RUN_NOWAIT);
        CASE_THREAD_SLEEP_MS(64);
    }

    check_flag = g_check_flag;
    atbus::channel::io_stream_close(&svr);
    CASE_EXPECT_EQ(0, svr.conn_pool.size());

    while (g_check_flag - check_flag < 3) {
        atbus::channel::io_stream_run(&cli, atbus::adapter::RUN_NOWAIT);
        CASE_THREAD_SLEEP_MS(64);
    }
    CASE_EXPECT_EQ(0, cli.conn_pool.size());

    atbus::channel::io_stream_close(&cli);
}

// buffer recv size limit

// buffer send size limit

// connect failed