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

static void node_msg_test_on_debug(const char* file_path, size_t line, const atbus::node& n, const atbus::endpoint* ep, const atbus::connection* conn, const char* fmt, ...) {
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

struct node_msg_test_recv_msg_record_t {
    const atbus::node* n;
    const atbus::endpoint* ep;
    const atbus::connection* conn;
    std::string data;
    int status;
    int count;

    node_msg_test_recv_msg_record_t(): n(NULL), ep(NULL), conn(NULL), status(0), count(0) {}
};

static node_msg_test_recv_msg_record_t recv_msg_history;

static int node_msg_test_recv_msg_test_record_fn(const atbus::node& n, const atbus::endpoint& ep, const atbus::connection& conn, 
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
