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

#include <stdarg.h>

static void node_reg_test_on_debug(const char* file_path, size_t line, 
    const atbus::node& n, const atbus::endpoint* ep, const atbus::connection* conn, 
    const atbus::protocol::msg* m,
    const char* fmt, ...) {
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

#ifdef _MSC_VER

    static char* APPVEYOR = getenv("APPVEYOR");
    static char* CI = getenv("CI");
    
    // appveyor ci open msg content
    if (APPVEYOR && APPVEYOR[0] && CI && CI[0] && NULL != m) {
        std::cout << *m << std::endl;
    }
#endif
}

struct node_reg_test_recv_msg_record_t {
    const atbus::node* n;
    const atbus::endpoint* ep;
    const atbus::connection* conn;
    std::string data;
    int status;
    int count;
    int add_endpoint_count;
    int remove_endpoint_count;

    node_reg_test_recv_msg_record_t(): n(NULL), ep(NULL), conn(NULL), status(0), count(0),
        add_endpoint_count(0), remove_endpoint_count(0) {}
};

static node_reg_test_recv_msg_record_t recv_msg_history;

static int node_reg_test_recv_msg_test_record_fn(const atbus::node& n, const atbus::endpoint* ep, const atbus::connection* conn, 
    int status, const void* buffer, size_t len) {
    recv_msg_history.n = &n;
    recv_msg_history.ep = ep;
    recv_msg_history.conn = conn;
    recv_msg_history.status = status;
    ++recv_msg_history.count;

    if (NULL != buffer && len > 0) {
        recv_msg_history.data.assign(reinterpret_cast<const char*>(buffer), len);
    } else {
        recv_msg_history.data.clear();
    }

    return 0;
}

static int node_reg_test_add_endpoint_fn(const atbus::node& n, atbus::endpoint* ep, int) {
    ++recv_msg_history.add_endpoint_count;

    CASE_EXPECT_NE(NULL, ep);
    CASE_EXPECT_NE(n.get_self_endpoint(), ep);
    return 0;
}

static int node_reg_test_remove_endpoint_fn(const atbus::node& n, atbus::endpoint* ep, int) {
    ++recv_msg_history.remove_endpoint_count;

    CASE_EXPECT_NE(NULL, ep);
    CASE_EXPECT_NE(n.get_self_endpoint(), ep);
    return 0;
}

static void node_reg_test_setup_exit(uv_loop_t* ev) {
    size_t left_tick = 128 * 30; // 30s
    while (left_tick > 0 && UV_EBUSY == uv_loop_close(ev)) {
        uv_run(ev, UV_RUN_NOWAIT);
        CASE_THREAD_SLEEP_MS(8);
        
        -- left_tick;
    }
    
    CASE_EXPECT_NE(left_tick, 0);
}

// 主动reset流程测试
// 正常首发数据测试
CASE_TEST(atbus_node_reg, reset_and_send)
{
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;
    uv_loop_t ev_loop;
    uv_loop_init(&ev_loop);

    conf.ev_loop = &ev_loop;

    {
        atbus::node::ptr_t node1 = atbus::node::create();
        atbus::node::ptr_t node2 = atbus::node::create();
        node1->on_debug = node_reg_test_on_debug;
        node2->on_debug = node_reg_test_on_debug;

        node1->init(0x12345678, &conf);
        node2->init(0x12356789, &conf);

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node1->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node2->listen("ipv4://127.0.0.1:16388"));

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node1->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node2->start());

        time_t proc_t = time(NULL);
        node1->proc(proc_t + 1, 0);
        node2->proc(proc_t + 1, 0);

        // 连接兄弟节点回调测试
        int check_ep_count = recv_msg_history.add_endpoint_count;
        node1->set_on_add_endpoint_handle(node_reg_test_add_endpoint_fn);
        node1->set_on_remove_endpoint_handle(node_reg_test_remove_endpoint_fn);
        node2->set_on_add_endpoint_handle(node_reg_test_add_endpoint_fn);
        node2->set_on_remove_endpoint_handle(node_reg_test_remove_endpoint_fn);

        node1->connect("ipv4://127.0.0.1:16388");

        for (int i = 0; i < 512; ++i) {
            atbus::endpoint* ep1 = node2->get_endpoint(node1->get_id());
            atbus::endpoint* ep2 = node1->get_endpoint(node2->get_id());

            if (NULL != ep1 && NULL != ep2 && NULL != ep1->get_data_connection(ep2) && NULL != ep2->get_data_connection(ep1)) {
                break;
            }

            uv_run(conf.ev_loop, UV_RUN_ONCE);
        }
        CASE_EXPECT_EQ(check_ep_count + 2, recv_msg_history.add_endpoint_count);

        // 兄弟节点消息转发测试
        std::string send_data;
        send_data.assign("abcdefg\0hello world!\n", sizeof("abcdefg\0hello world!\n") - 1);

        node1->proc(proc_t + 1000, 0);
        node2->proc(proc_t + 1000, 0);

        int count = recv_msg_history.count;
        node2->set_on_recv_handle(node_reg_test_recv_msg_test_record_fn);
        node1->send_data(node2->get_id(), 0, send_data.data(), send_data.size());

        for (int i = 0; i < 256; ++i) {
            uv_run(conf.ev_loop, UV_RUN_ONCE);
            CASE_THREAD_SLEEP_MS(16);
            if (count != recv_msg_history.count) {
                break;
            }
        }

        // check add endpoint callback
        CASE_EXPECT_EQ(send_data, recv_msg_history.data);

        check_ep_count = recv_msg_history.remove_endpoint_count;

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

        // check remove endpoint callback
        CASE_EXPECT_EQ(check_ep_count + 2, recv_msg_history.remove_endpoint_count);

        CASE_EXPECT_EQ(NULL, node2->get_endpoint(node1->get_id()));
        CASE_EXPECT_EQ(NULL, node1->get_endpoint(node2->get_id()));

    }

    node_reg_test_setup_exit(&ev_loop);
}

// 被动析构流程测试
CASE_TEST(atbus_node_reg, destruct)
{
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;
    uv_loop_t ev_loop;
    uv_loop_init(&ev_loop);

    conf.ev_loop = &ev_loop;

    {
        atbus::node::ptr_t node1 = atbus::node::create();
        atbus::node::ptr_t node2 = atbus::node::create();
        node1->on_debug = node_reg_test_on_debug;
        node2->on_debug = node_reg_test_on_debug;

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

        for (int i = 0; i < 512; ++i) {
            atbus::endpoint* ep1 = node2->get_endpoint(node1->get_id());
            atbus::endpoint* ep2 = node1->get_endpoint(node2->get_id());

            if (NULL != ep1 && NULL != ep2 && NULL != ep1->get_data_connection(ep2) && NULL != ep2->get_data_connection(ep1)) {
                break;
            }

            uv_run(conf.ev_loop, UV_RUN_ONCE);
        }

        for (int i = 0; i < 16; ++i) {
            uv_run(conf.ev_loop, UV_RUN_NOWAIT);
            CASE_THREAD_SLEEP_MS(4);
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

    node_reg_test_setup_exit(&ev_loop);
}

// 注册成功流程测试
CASE_TEST(atbus_node_reg, reg_success)
{
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;
    uv_loop_t ev_loop;
    uv_loop_init(&ev_loop);

    conf.ev_loop = &ev_loop;

    int check_ep_rm = recv_msg_history.remove_endpoint_count;
    {
        atbus::node::ptr_t node_parent = atbus::node::create();
        atbus::node::ptr_t node_child = atbus::node::create();
        node_parent->on_debug = node_reg_test_on_debug;
        node_child->on_debug = node_reg_test_on_debug;

        node_parent->init(0x12345678, &conf);
        
        conf.children_mask = 8;
        conf.father_address = "ipv4://127.0.0.1:16387";
        node_child->init(0x12346789, &conf);

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child->listen("ipv4://127.0.0.1:16388"));

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child->start());

        // 父子节点注册回调测试
        int check_ep_count = recv_msg_history.add_endpoint_count;
        node_parent->set_on_add_endpoint_handle(node_reg_test_add_endpoint_fn);
        node_parent->set_on_remove_endpoint_handle(node_reg_test_remove_endpoint_fn);
        node_child->set_on_add_endpoint_handle(node_reg_test_add_endpoint_fn);
        node_child->set_on_remove_endpoint_handle(node_reg_test_remove_endpoint_fn);

        time_t proc_t = time(NULL);
        node_parent->proc(proc_t + 1, 0);
        node_child->proc(proc_t + 1, 0);

        // 注册成功自动会有可用的端点
        for (int i = 0; i < 512; ++i) {
            atbus::endpoint* ep1 = node_child->get_endpoint(node_parent->get_id());
            atbus::endpoint* ep2 = node_parent->get_endpoint(node_child->get_id());

            if (NULL != ep1 && NULL != ep2 && NULL != ep1->get_data_connection(ep2) && NULL != ep2->get_data_connection(ep1)) {
                break;
            }

            uv_run(conf.ev_loop, UV_RUN_ONCE);
        }
        CASE_EXPECT_EQ(check_ep_count + 2, recv_msg_history.add_endpoint_count);
    }

    node_reg_test_setup_exit(&ev_loop);

    CASE_EXPECT_EQ(check_ep_rm + 2, recv_msg_history.remove_endpoint_count);
}

static int g_node_test_on_shutdown_check_reason = 0;
static int node_test_on_shutdown(const atbus::node& n, int reason) {
    if (0 == g_node_test_on_shutdown_check_reason) {
        ++ g_node_test_on_shutdown_check_reason;
    } else {
        CASE_EXPECT_EQ(reason, g_node_test_on_shutdown_check_reason);
        g_node_test_on_shutdown_check_reason = 0;
    }

    return 0;
}

// 注册到父节点失败导致下线的流程测试
// 注册到子节点失败不会导致下线的流程测试
CASE_TEST(atbus_node_reg, conflict)
{
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;
    uv_loop_t ev_loop;
    uv_loop_init(&ev_loop);

    conf.ev_loop = &ev_loop;
    
    // 只有发生冲突才会注册不成功，否则会无限重试注册父节点，直到其上线
    {
        atbus::node::ptr_t node_parent = atbus::node::create();
        atbus::node::ptr_t node_child = atbus::node::create();
        atbus::node::ptr_t node_child_fail = atbus::node::create();
        node_parent->on_debug = node_reg_test_on_debug;
        node_child->on_debug = node_reg_test_on_debug;
        node_child_fail->on_debug = node_reg_test_on_debug;

        node_parent->init(0x12345678, &conf);
        
        conf.children_mask = 8;
        conf.father_address = "ipv4://127.0.0.1:16387";
        node_child->init(0x12346789, &conf);
        // 子域冲突，注册失败
        node_child_fail->init(0x12346780, &conf);

        node_child->set_on_shutdown_handle(node_test_on_shutdown);
        node_child_fail->set_on_shutdown_handle(node_test_on_shutdown);
        g_node_test_on_shutdown_check_reason = EN_ATBUS_ERR_ATNODE_INVALID_ID;
        
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child->listen("ipv4://127.0.0.1:16388"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child_fail->listen("ipv4://127.0.0.1:16389"));

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child_fail->start());

        time_t proc_t = time(NULL) + 1;
        // 必然有一个失败的
        while (atbus::node::state_t::CREATED != node_child->get_state() && atbus::node::state_t::CREATED != node_child_fail->get_state()) {
            node_parent->proc(proc_t, 0);
            node_child->proc(proc_t, 0);
            node_child_fail->proc(proc_t, 0);
            
            CASE_THREAD_SLEEP_MS(8);
            uv_run(&ev_loop, UV_RUN_NOWAIT);
            proc_t += conf.retry_interval;
        }

        for (int i = 0; i < 64; ++i) {
            CASE_THREAD_SLEEP_MS(8);
            uv_run(&ev_loop, UV_RUN_NOWAIT);
        }
        
        // 注册到子节点失败不会导致下线的流程测试
        CASE_EXPECT_TRUE(atbus::node::state_t::RUNNING == node_child->get_state() || atbus::node::state_t::RUNNING == node_child_fail->get_state());
        CASE_EXPECT_EQ(atbus::node::state_t::RUNNING, node_parent->get_state());
    }

    node_reg_test_setup_exit(&ev_loop);
}

// 对父节点重连失败不会导致下线的流程测试
// 对父节点断线重连的流程测试
CASE_TEST(atbus_node_reg, reconnect_father_failed)
{
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;
    uv_loop_t ev_loop;
    uv_loop_init(&ev_loop);

    conf.ev_loop = &ev_loop;
    
    // 只有发生冲突才会注册不成功，否则会无限重试注册父节点，直到其上线
    {
        atbus::node::ptr_t node_parent = atbus::node::create();
        atbus::node::ptr_t node_child = atbus::node::create();
        node_parent->on_debug = node_reg_test_on_debug;
        node_child->on_debug = node_reg_test_on_debug;

        node_parent->init(0x12345678, &conf);
        
        conf.children_mask = 8;
        conf.father_address = "ipv4://127.0.0.1:16387";
        node_child->init(0x12346789, &conf);

        node_child->set_on_shutdown_handle(node_test_on_shutdown);
        g_node_test_on_shutdown_check_reason = EN_ATBUS_ERR_ATNODE_INVALID_ID;
        
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child->listen("ipv4://127.0.0.1:16388"));

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child->start());

        time_t proc_t = time(NULL) + 1;
        // 先等连接成功
        while (atbus::node::state_t::RUNNING != node_child->get_state()) {
            node_parent->proc(proc_t, 0);
            node_child->proc(proc_t, 0);
            
            CASE_THREAD_SLEEP_MS(8);
            uv_run(&ev_loop, UV_RUN_NOWAIT);
            ++ proc_t;
        }
        
        // 关闭父节点
        node_parent->reset();
        
        // 重连父节点，但是连接不成功也不会导致下线
        // 连接过程中的转态变化
        size_t retry_times = 0;
        while (atbus::node::state_t::RUNNING == node_child->get_state() || retry_times < 16) {
            proc_t += conf.retry_interval + 1;
            // node_parent->proc(proc_t, 0);
            node_child->proc(proc_t, 0);
            
            if (atbus::node::state_t::RUNNING != node_child->get_state()) {
                ++ retry_times;
                CASE_EXPECT_TRUE(atbus::node::state_t::LOST_PARENT == node_child->get_state() || atbus::node::state_t::CONNECTING_PARENT == node_child->get_state());
                CASE_EXPECT_NE(atbus::node::state_t::CREATED, node_child->get_state());
                CASE_EXPECT_NE(atbus::node::state_t::INITED, node_child->get_state());
            }
            
            CASE_THREAD_SLEEP_MS(8);
            uv_run(&ev_loop, UV_RUN_NOWAIT);
            uv_run(&ev_loop, UV_RUN_NOWAIT);
            uv_run(&ev_loop, UV_RUN_NOWAIT);
        }
        
        // 父节点断线重连测试
        // 子节点断线后重新注册测试
        conf.children_mask = 16;
        conf.father_address = "";
        node_parent->init(0x12345678, &conf);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->start());
        
        while (atbus::node::state_t::RUNNING != node_child->get_state()) {
            proc_t += conf.retry_interval;
            node_parent->proc(proc_t, 0);
            node_child->proc(proc_t, 0);
            
            CASE_THREAD_SLEEP_MS(8);
            uv_run(&ev_loop, UV_RUN_NOWAIT);
        }
        
        {
            atbus::endpoint* ep1 = node_child->get_endpoint(node_parent->get_id());
            atbus::endpoint* ep2 = node_parent->get_endpoint(node_child->get_id());
            
            CASE_EXPECT_NE(NULL, ep1);
            CASE_EXPECT_NE(NULL, ep2);
            CASE_EXPECT_EQ(atbus::node::state_t::RUNNING, node_child->get_state());
        }
        
        // 注册到子节点失败不会导致下线的流程测试
        CASE_EXPECT_EQ(atbus::node::state_t::RUNNING, node_parent->get_state());
    }

    while (UV_EBUSY == uv_loop_close(&ev_loop)) {
        uv_run(&ev_loop, UV_RUN_ONCE);
    }
}
