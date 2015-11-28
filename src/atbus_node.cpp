/**
 * @brief 所有channel文件的模式均为 c + channel<br />
 *        使用c的模式是为了简单、结构清晰并且避免异常<br />
 *        附带c++的部分是为了避免命名空间污染并且c++的跨平台适配更加简单
 */

#ifndef _MSC_VER

#include <sys/types.h>
#include <unistd.h>

#else
#pragma comment(lib, "Ws2_32.lib") 
#endif

#include <cstdio>
#include <assert.h>
#include <ctime>
#include <stdint.h>
#include <cstddef>
#include <cstring>
#include <cstdlib>

#include "atbus_proto_generated.h"

#include "detail/buffer.h"

#include "atbus_node.h"

namespace atbus {
    namespace detail {
        struct io_stream_channel_del {
            void operator()(channel::io_stream_channel* p) const {
                channel::io_stream_close(p);
                delete p;
            }
        };
    }

    node::node(): state_(state_t::CREATED), ev_loop_(NULL), static_buffer_(NULL) {
    }

    node::~node() {
        reset();
    }

    void node::default_conf(conf_t* conf) {
        conf->ev_loop = NULL;
        conf->children_mask = 0;
        conf->loop_times = 128;
        conf->ttl = 16; // 默认最长8次跳转

        conf->first_idle_timeout = ATBUS_MACRO_CONNECTION_CONFIRM_TIMEOUT;
        conf->ping_interval = 60;
        conf->retry_interval = 3;
        conf->fault_tolerant = 3;
        conf->backlog = ATBUS_MACRO_CONNECTION_BACKLOG;

        conf->msg_size = ATBUS_MACRO_MSG_LIMIT;
        conf->recv_buffer_size = ATBUS_MACRO_MSG_LIMIT * 32; // default for 3 times of ATBUS_MACRO_MSG_LIMIT = 2MB
        conf->send_buffer_size = ATBUS_MACRO_MSG_LIMIT;
        conf->send_buffer_number = 0;

        conf->flags.reset();
    }

    node::ptr_t node::create() {
        ptr_t ret(new node());
        if (!ret) {
            return ret;
        }

        ret->watcher_ = ret;
        return ret;
    }

    int node::init(bus_id_t id, const conf_t* conf) {
        reset();

        if (NULL == conf) {
            default_conf(&conf_);
        } else {
            conf_ = *conf;
        }

        self_ = endpoint::create(this, id, conf_.children_mask);

        static_buffer_ = detail::buffer_block::malloc(conf_.msg_size + detail::buffer_block::head_size(conf_.msg_size) + 16); // 预留crc32长度和vint长度);

        ev_loop_ = NULL;

        return EN_ATBUS_ERR_SUCCESS;
    }

    int node::start() {
        // TODO 连接父节点
        if (!conf_.father_address.empty()) {
        }

        return 0;
    }

    int node::reset() {
        // 这个函数可能会在析构时被调用，这时候不能使用watcher_.lock()
        if (conf_.flags.test(flag_t::RESETTING)) {
            return EN_ATBUS_ERR_SUCCESS;
        }
        conf_.flags.set(flag_t::RESETTING, true);

        // TODO 所有连接断开

        // TODO 销毁endpoint

        // 基础数据
        iostream_channel_.reset();
        iostream_conf_.reset();

        if (NULL != ev_loop_) {
            ev_loop_ = NULL;
        }

        if (NULL != static_buffer_) {
            detail::buffer_block::free(static_buffer_);
            static_buffer_ = NULL;
        }
        
        node_father_.reset();
        conf_.flags.reset();
        return EN_ATBUS_ERR_SUCCESS;
    }

    int node::proc(time_t sec, time_t usec) {
        int ret = 0;
        // TODO 以后可以优化成event_fd通知，这样就不需要轮询了
        // 点对点IO流通道
        for (detail::auto_select_map<std::string, connection::ptr_t>::type::iterator iter = proc_connections_.begin(); iter != proc_connections_.end(); ++ iter) {
            ret += iter->second->proc(*this, sec, usec);
        }

        // 点对点IO流通道
        int loop_left = conf_.loop_times;
        while (iostream_channel_ && loop_left > 0 &&
            EN_ATBUS_ERR_EV_RUN == channel::io_stream_run(get_iostream_channel(), adapter::RUN_NOWAIT)) {
            --loop_left;
        }

        ret += conf_.loop_times - loop_left;

        // TODO connection超时下线
        // TODO 父节点重连
        // TODO Ping包
        // TODO 同步协议
        return ret;
    }

    int node::listen(const char* addr_str) {
        connection::ptr_t conn = connection::create(this);
        if(!conn) {
            return EN_ATBUS_ERR_MALLOC;
        }

        int ret = conn->listen(addr_str);
        if (ret < 0) {
            return ret;
        }

        // TODO 添加到self_里
        return EN_ATBUS_ERR_SUCCESS;
    }

    adapter::loop_t* node::get_evloop() {
        if (NULL != ev_loop_) {
            return ev_loop_;
        }

        ev_loop_ = uv_default_loop();
        return ev_loop_;
    }

    bool node::is_child_node(bus_id_t id) const {
        return self_->is_child_node(id);
    }

    bool node::is_brother_node(bus_id_t id) const {
        if (node_father_) {
            return self_->is_brother_node(id, node_father_->get_id(), node_father_->get_children_mask());
        } else {
            return self_->is_brother_node(id, 0, 0);
        }
    }

    bool node::is_parent_node(bus_id_t id) const {
        if (node_father_) {
            return self_->is_parent_node(id, node_father_->get_id(), node_father_->get_children_mask());
        }

        return false;
    }

    int node::get_pid() {
#ifdef _MSC_VER
        return _getpid();
#else
        return getpid();
#endif
    }

    static std::string& host_name_buffer() {
        static std::string server_addr;
        return server_addr;
    }

    const std::string& node::get_hostname() {
        std::string& hn = host_name_buffer();
        if (!hn.empty()) {
            return hn;
        }

        // @see man gethostname
        char buffer[256] = {0};
        if(0 == gethostname(buffer, sizeof(buffer))) {
            hn = buffer;
        }
#ifdef _MSC_VER
        else {
            if (WSANOTINITIALISED == WSAGetLastError()) {
                WSADATA wsaData;
                WORD version = MAKEWORD(2, 0);
                if(0 == WSAStartup(version, &wsaData) && 0 == gethostname(buffer, sizeof(buffer))) {
                    hn = buffer;
                }
            }
        }
#endif

        return hn;
    }

    bool node::set_hostname(const std::string& hn) {
        std::string& h = host_name_buffer();
        if (h.empty()) {
            h = hn;
            return true;
        }

        return false;
    }

    bool node::add_proc_connection(connection::ptr_t conn) {
        if(!conn || conn->get_address().address.empty() || proc_connections_.end() != proc_connections_.find(conn->get_address().address)) {
            return false;
        }

        proc_connections_[conn->get_address().address] = conn;
        return true;
    }

    bool node::remove_proc_connection(const std::string& conn_key) {
        detail::auto_select_map<std::string, connection::ptr_t>::type::iterator iter = proc_connections_.find(conn_key);
        if (iter == proc_connections_.end()) {
            return false;
        }

        proc_connections_.erase(iter);
        return true;
    }

    bool node::add_connection_timer(connection::ptr_t conn) {
        // TODO 是否是正在连接状态
        // TODO 是否在正在连接池中？
        return true;
    }

    void node::on_recv(connection* conn, const protocol::msg* m, int status, int errcode) {
        if (status < 0 || errcode < 0) {
            on_error(NULL, conn, status, errcode);
        }

        // TODO 内部协议处理
        // m->head()->cmd();
        // TODO 消息传递
    }

    int node::on_error(const endpoint* ep, const connection* conn, int status, int errcode) {
        if (NULL == ep && NULL != conn) {
            ep = conn->get_binding();
        }

        if (events_.on_error) {
            events_.on_error(*this, ep, conn, status, errcode);
        }

        return status;
    }

    int node::on_disconnect(const connection* conn) {
        if (NULL == conn) {
            return EN_ATBUS_ERR_PARAMS;
        }
        
        // TODO 断线逻辑

        return EN_ATBUS_ERR_SUCCESS;
    }

    int node::on_new_connection(connection* conn) {
        if (NULL == conn) {
            return EN_ATBUS_ERR_PARAMS;
        }

        // TODO 如果处于握手阶段，发送节点关系逻辑并加入握手连接池并加入超时判定池

        return EN_ATBUS_ERR_SUCCESS;
    }

    channel::io_stream_channel* node::get_iostream_channel() {
        if(iostream_channel_) {
            return iostream_channel_.get();
        }
        iostream_channel_ = std::shared_ptr<channel::io_stream_channel>(new channel::io_stream_channel(), detail::io_stream_channel_del());

        channel::io_stream_init(iostream_channel_.get(), get_evloop(), get_iostream_conf());
        iostream_channel_->data = this;

        // callbacks
        iostream_channel_->evt.callbacks[channel::io_stream_callback_evt_t::EN_FN_ACCEPTED] = connection::iostream_on_accepted;
        iostream_channel_->evt.callbacks[channel::io_stream_callback_evt_t::EN_FN_CONNECTED] = connection::iostream_on_connected;
        iostream_channel_->evt.callbacks[channel::io_stream_callback_evt_t::EN_FN_DISCONNECTED] = connection::iostream_on_disconnected;
        iostream_channel_->evt.callbacks[channel::io_stream_callback_evt_t::EN_FN_RECVED] = connection::iostream_on_recv_cb;

        return iostream_channel_.get();
    }

    node::ptr_t node::get_watcher() {
        return watcher_.lock();
    }

    channel::io_stream_conf* node::get_iostream_conf() {
        if (iostream_conf_) {
            return iostream_conf_.get();
        }

        iostream_conf_.reset(new channel::io_stream_conf());
        channel::io_stream_init_configure(iostream_conf_.get());

        // 接收大小和msg size一致即可，可以只使用一块静态buffer
        iostream_conf_->recv_buffer_limit_size = conf_.msg_size;
        iostream_conf_->recv_buffer_max_size = conf_.msg_size;

        iostream_conf_->send_buffer_static = conf_.send_buffer_number;
        iostream_conf_->send_buffer_max_size = conf_.send_buffer_size;
        iostream_conf_->send_buffer_limit_size = conf_.msg_size;
        iostream_conf_->confirm_timeout = conf_.first_idle_timeout;
        iostream_conf_->backlog = conf_.backlog;

        return iostream_conf_.get();
    }
}
