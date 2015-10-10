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

static int g_check_flag = 0;
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


    ++g_check_flag;
}

CASE_TEST(channel, io_stream_tcp_basic)
{
    return;
    atbus::channel::io_stream_channel svr;
    atbus::channel::io_stream_init(&svr, NULL, NULL);

    atbus::channel::channel_address_t svr_addr;
    atbus::channel::make_address("ipv4://127.0.0.1:16387", svr_addr);
    g_check_flag = 0;
    atbus::channel::io_stream_listen(&svr, svr_addr, listen_callback_test_fn);
    atbus::channel::io_stream_run(&svr);
    CASE_EXPECT_EQ(1, g_check_flag);

    CASE_EXPECT_NE(NULL, svr.ev_loop);

    atbus::channel::io_stream_close(&svr);
    CASE_EXPECT_EQ(0, uv_loop_alive(svr.ev_loop));
}