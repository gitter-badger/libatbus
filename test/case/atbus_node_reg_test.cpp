#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <iomanip>

#include "common/string_oprs.h"

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

#include <atbus_node.h>

#include "detail/libatbus_protocol.h"

#include "frame/test_macros.h"

#include <varargs.h>

static void node_test_on_debug(const char* file_path, size_t line, const atbus::node& n, const atbus::endpoint* ep, const atbus::connection* conn, const char* fmt, ...) {
    size_t offset = 0;
    for (size_t i = 0; file_path[i]; ++i) {
        if ('/' == file_path[i] || '\\' == file_path[i]) {
            offset = i + 1;
        }
    }
    file_path += offset;

    std::streamsize w = std::cout.width();
    CASE_MSG_INFO() << "[Log Debug][" << std::setw(24) << file_path << ":" << std::setw(4) << line << "] node=0x" << 
        std::setfill('0')<< std::hex<< std::setw(8)<< n.get_id() <<
        ", endpoint=0x" << std::setw(8)<< (NULL == ep ? 0 : ep->get_id()) <<
        ", connection=" << conn<< std::setfill(' ')<< std::setw(w)<<  std::dec<<
        "\t";

    va_list ap;
    va_start(ap, fmt);

    vprintf(fmt, ap);

    va_end(ap);

    puts("");
}

struct recv_msg_record_t {
    const atbus::node* n;
    const atbus::endpoint* ep;
    const atbus::connection* conn;
    std::string data;
    int status;
    int count;

    recv_msg_record_t(): n(NULL), ep(NULL), conn(NULL), status(0), count(0) {}
};

static recv_msg_record_t recv_msg_history;

static int recv_msg_test_record_fn(const atbus::node& n, const atbus::endpoint& ep, const atbus::connection& conn, 
    int status, const void* buffer, size_t len) {
    recv_msg_history.n = &n;
    recv_msg_history.ep = &ep;
    recv_msg_history.conn = &conn;
    recv_msg_history.status = status;
    ++recv_msg_history.count;

    if (NULL != buffer && len > 0) {
        recv_msg_history.data.assign(reinterpret_cast<const char*>(buffer), len);
    } else {
        recv_msg_history.data.clear();
    }

    return 0;
}

// 主动reset流程测试
// 正常首发数据测试
CASE_TEST(atbus_node_reg, reset_and_send)
{
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;
    conf.ev_loop = uv_loop_new();

    {
        atbus::node::ptr_t node1 = atbus::node::create();
        atbus::node::ptr_t node2 = atbus::node::create();
        node1->on_debug = node_test_on_debug;
        node2->on_debug = node_test_on_debug;

        node1->init(0x12345678, &conf);
        node2->init(0x12356789, &conf);

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node1->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node2->listen("ipv4://127.0.0.1:16388"));

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node1->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node2->start());

        time_t proc_t = time(NULL);
        node1->proc(proc_t + 1, 0);
        node2->proc(proc_t + 1, 0);
        node1->connect("ipv4://127.0.0.1:16388");

        for (int i = 0; i < 256; ++i) {
            uv_run(conf.ev_loop, UV_RUN_ONCE);
            CASE_THREAD_SLEEP_MS(16);

            atbus::endpoint* ep1 = node2->get_endpoint(node1->get_id());
            atbus::endpoint* ep2 = node1->get_endpoint(node2->get_id());

            if (NULL != ep1 && NULL != ep2 && NULL != ep1->get_data_connection(ep2) && NULL != ep2->get_data_connection(ep1)) {
                break;
            }
        }

        std::string send_data;
        send_data.assign("abcdefg\0hello world!\n", sizeof("abcdefg\0hello world!\n") - 1);

        node1->proc(proc_t + 1000, 0);
        node2->proc(proc_t + 1000, 0);

        int count = recv_msg_history.count;
        node2->set_on_recv_handle(recv_msg_test_record_fn);
        node1->send_data(node2->get_id(), 0, send_data.data(), send_data.size());

        for (int i = 0; i < 256; ++i) {
            uv_run(conf.ev_loop, UV_RUN_ONCE);
            CASE_THREAD_SLEEP_MS(16);
            if (count != recv_msg_history.count) {
                break;
            }
        }

        CASE_EXPECT_EQ(send_data, recv_msg_history.data);

        // reset
        node1->reset();

        for (int i = 0; i < 256; ++i) {
            uv_run(conf.ev_loop, UV_RUN_NOWAIT);
            CASE_THREAD_SLEEP_MS(16);

            node1->proc(proc_t + 1000 + i, 0);
            node2->proc(proc_t + 1000 + i, 0);

            atbus::endpoint* ep1 = node2->get_endpoint(node1->get_id());
            atbus::endpoint* ep2 = node1->get_endpoint(node2->get_id());
            if (NULL == ep1 && NULL == ep2) {
                break;
            }
        }

        CASE_EXPECT_EQ(NULL, node2->get_endpoint(node1->get_id()));
        CASE_EXPECT_EQ(NULL, node1->get_endpoint(node2->get_id()));

    }
    uv_loop_delete(conf.ev_loop);
}

// 被动析构流程测试
CASE_TEST(atbus_node_reg, destruct)
{
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;
    conf.ev_loop = uv_loop_new();

    {
        atbus::node::ptr_t node1 = atbus::node::create();
        atbus::node::ptr_t node2 = atbus::node::create();
        node1->on_debug = node_test_on_debug;
        node2->on_debug = node_test_on_debug;

        node1->init(0x12345678, &conf);
        node2->init(0x12356789, &conf);

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node1->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node2->listen("ipv4://127.0.0.1:16388"));

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node1->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node2->start());

        time_t proc_t = time(NULL);
        node1->proc(proc_t + 1, 0);
        node2->proc(proc_t + 1, 0);
        node1->connect("ipv4://127.0.0.1:16388");

        for (int i = 0; i < 256; ++i) {
            uv_run(conf.ev_loop, UV_RUN_ONCE);
            CASE_THREAD_SLEEP_MS(16);

            atbus::endpoint* ep1 = node2->get_endpoint(node1->get_id());
            atbus::endpoint* ep2 = node1->get_endpoint(node2->get_id());

            if (NULL != ep1 && NULL != ep2 && NULL != ep1->get_data_connection(ep2) && NULL != ep2->get_data_connection(ep1)) {
                break;
            }
        }

        // reset shared_ptr and delete it 
        node1.reset();

        for (int i = 0; i < 256; ++i) {
            uv_run(conf.ev_loop, UV_RUN_NOWAIT);
            CASE_THREAD_SLEEP_MS(16);

            node2->proc(proc_t + 2 + i, 0);

            atbus::endpoint* ep1 = node2->get_endpoint(0x12345678);
            if (NULL == ep1) {
                break;
            }
        }

        CASE_EXPECT_EQ(NULL, node2->get_endpoint(0x12345678));
    }
    uv_loop_delete(conf.ev_loop);
}

// TODO 注册成功流程测试
// TODO 注册到父节点失败导致下线的流程测试
// TODO 注册到子节点失败不会导致下线的流程测试
// TODO 对父节点重连失败不会导致下线的流程测试

// TODO 连接未握手超时下线测试

// TODO 父节点断线重连测试
// TODO 兄弟节点断线重连测试
// TODO 子节点断线后重新注册测试
// TODO 连接过程中的转态检查

// TODO 定时Ping Pong协议测试
// TODO 自定义命令协议测试

// TODO 父子节点消息转发测试
// TODO 兄弟节点消息转发测试
// TODO 兄弟节点通过父节点转发消息并建立直连测试（测试路由）
// TODO 兄弟节点通过多层父节点转发消息并不会建立直连测试
// TODO 直连节点发送失败测试
// TODO 发送给子节点转发失败的回复通知测试
// TODO 发送给父节点转发失败的回复通知测试
// TODO 发送给已下线兄弟节点并失败的回复通知测试（网络失败）


// TODO 全量表第一次拉取测试
// TODO 全量表通知给父节点和子节点测试
