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
    }

    node::ptr_t node::create() {
        ptr_t ret = std::make_shared<node>();
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

        self_.init(watcher_, id, conf_.children_mask);
        node_father_.init(watcher_, 0, 0);

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
        return EN_ATBUS_ERR_SUCCESS;
    }

    int node::proc(time_t sec, time_t usec) {
        int ret = 0;
        // TODO 以后可以优化成event_fd通知，这样就不需要轮询了
        // 点对点IO流通道
        for (std::list<no_stream_channel_t>::iterator iter = basic_channels.begin(); iter != basic_channels.end(); ++ iter) {
            ret += iter->proc_fn(*this, &(*iter), sec, usec);
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
        channel::channel_address_t addr;
        if(false == channel::make_address(addr_str, addr)) {
            return EN_ATBUS_ERR_CHANNEL_ADDR_INVALID;
        }

        if(0 == ATBUS_FUNC_STRNCASE_CMP("mem", addr.scheme.c_str(), 3)) {
            channel::mem_channel* mem_chann;
            intptr_t ad;
            detail::str2int(ad, addr.host.c_str());
            int res = channel::mem_attach(reinterpret_cast<void*>(ad), conf_.recv_buffer_size, &mem_chann, NULL);
            if (res < 0) {
                res = channel::mem_init(reinterpret_cast<void*>(ad), conf_.recv_buffer_size, &mem_chann, NULL);
            }

            if (res < 0) {
                return res;
            }

            // 加入轮询队列
            no_stream_channel_t channel;
            channel.channel = mem_chann;
            channel.key = static_cast<key_t>(ad);
            channel.proc_fn = mem_proc_fn;
            channel.free_fn = mem_free_fn;
            basic_channels.push_back(channel);
            return res;
        } else if (0 == ATBUS_FUNC_STRNCASE_CMP("shm", addr.scheme.c_str(), 3)) {
            channel::shm_channel* shm_chann;
            key_t shm_key;
            detail::str2int(shm_key, addr.host.c_str());
            int res = channel::shm_attach(shm_key, conf_.recv_buffer_size, &shm_chann, NULL);
            if (res < 0) {
                res = channel::shm_init(shm_key, conf_.recv_buffer_size, &shm_chann, NULL);
            }

            if (res < 0) {
                return res;
            }

            // 加入轮询队列
            no_stream_channel_t channel;
            channel.channel = shm_chann;
            channel.key = shm_key;
            channel.proc_fn = shm_proc_fn;
            channel.free_fn = shm_free_fn;
            basic_channels.push_back(channel);
            return res;
        } else {
            return channel::io_stream_listen(get_iostream_channel(), addr, NULL);
        }

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
        return self_.is_child_node(id);
    }

    bool node::is_brother_node(bus_id_t id) const {
        return self_.is_brother_node(id, node_father_.get_id(), node_father_.get_children_mask());
    }

    bool node::is_parent_node(bus_id_t id) const {
        return self_.is_parent_node(id, node_father_.get_id(), node_father_.get_children_mask());
    }

    int node::get_pid() {
        return getpid();
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

    bool node::remove_proc_connection(connection::ptr_t conn) {
        if (!conn) {
            return false;
        }

        detail::auto_select_map<std::string, connection::ptr_t>::type::iterator iter = proc_connections_.find(conn->get_address().address);
        if (iter == proc_connections_.end()) {
            return false;
        }

        proc_connections_.erase(iter);
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
