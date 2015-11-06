/**
 * @brief 所有channel文件的模式均为 c + channel<br />
 *        使用c的模式是为了简单、结构清晰并且避免异常<br />
 *        附带c++的部分是为了避免命名空间污染并且c++的跨平台适配更加简单
 */

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

        /**
         * @brief 字符串转整数
         * @param out 输出的整数
         * @param str 被转换的字符串
         * @note 性能肯定比sscanf系，和iostream系高。strtol系就不知道了
         */
        template<typename T> 
        void str2int(T& out, const char* str) {
            out = static_cast<T>(0);
            if (NULL == str || !(*str)) {
                return;
            }
            
            if ('0' == str[0] && 'x' == str[1]) { // hex
                for (size_t i = 2; str[i]; ++ i) {
                    char c = static_cast<char>(::tolower(str[i]));
                    if (str[i] >= '0' && str[i] <= '9') {
                        out <<= 4;
                        out += str[i] - '0';
                    } else if (str[i] >= 'a' && str[i] <= 'f') {
                        out <<= 4;
                        out += str[i] - 'a' + 10;
                    } else {
                        break;
                    }
                }
            } else if ('\\' == str[0]) { // oct
                for (size_t i = 0; str[i] >= '0' && str[i] < '8'; ++i) {
                    out <<= 3;
                    out += str[i] - '0';
                }
            } else { // dec
                for (size_t i = 0; str[i] >= '0' && str[i] <= '9'; ++i) {
                    out *= 10;
                    out += str[i] - '0';
                }
            }
        }
    }

    node::node(): ev_loop_(NULL), static_buffer_(NULL) {
        self_.id = 0;
        self_.children_mask = 0;

        node_father_ = self_;
    }

    node::~node() {
        reset();
    }

    void node::default_conf(conf_t* conf) {
        conf->ev_loop = NULL;
        conf->children_mask = 0;
        conf->loop_times = 128;

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

    int node::init(bus_id_t id, const conf_t* conf) {
        reset();

        self_.id = id;
        if (NULL == conf) {
            default_conf(&conf_);
        } else {
            conf_ = *conf;
        }
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
        
        node_father_.id = 0;
        node_father_.children_mask = 0;

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

        // TODO 超时点对点通道强行关闭
        return ret;
    }

    int node::listen(const char* addr_str, bool is_caddr) {
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

    void node::iostream_on_recv_cb(channel::io_stream_channel* channel, channel::io_stream_connection* connection, int status, void* buffer, size_t s) {

        assert(channel && channel->data);
        node* _this = reinterpret_cast<node*>(channel->data);

        if (status < 0 || NULL == buffer || s <= 0) {
            _this->on_recv(NULL, status, channel->error_code);
            return;
        }

        // TODO 要特别注意处理一下地址对齐问题对flatbuffer有无影响
        // 看flatbuffer代码的话，这部分没有设置对齐，应该会有影响
        const protocol::msg* m = protocol::Getmsg(buffer);
        _this->on_recv(m, status, channel->error_code);
    }

    void node::iostream_on_accepted(channel::io_stream_channel* channel, channel::io_stream_connection* connection, int status, void* buffer, size_t s) {
        // TODO 连接成功加入点对点传输池
        // TODO 加入超时检测
        channel->data = NULL;
    }

    void node::iostream_on_connected(channel::io_stream_channel* channel, channel::io_stream_connection* connection, int status, void* buffer, size_t s) {
        // TODO 连接成功加入点对点传输池
        // TODO 发送注册协议
        // TODO 加入超时检测
        channel->data = NULL;
    }

    void node::iostream_on_disconnected(channel::io_stream_channel* channel, channel::io_stream_connection* connection, int status, void* buffer, size_t s) {  
        // TODO 移除相关的连接
        // TODO 移除相关的node信息
    }

    void node::on_recv(const protocol::msg* m, int status, int errcode) {
        // TODO 内部协议处理
        // m->head()->cmd();
        // TODO 消息传递
    }

    int node::shm_proc_fn(node& n, no_stream_channel_t* c, time_t sec, time_t usec) {
        int ret = 0;
        size_t left_times = n.conf_.loop_times;
        while(left_times -- > 0) {
            size_t recv_len;
            int res = channel::shm_recv(
                reinterpret_cast<channel::shm_channel*>(c->channel), 
                n.static_buffer_->data(), 
                n.static_buffer_->size(), 
                &recv_len
            );

            if (EN_ATBUS_ERR_NO_DATA == res) {
                break;
            }

            // 回调收到数据事件
            if(res < 0) {
                ret = res;
                n.on_recv(NULL, res, res);
                break;
            } else {
                const protocol::msg* m = protocol::Getmsg(n.static_buffer_->data());
                n.on_recv(m, res, res);
                ++ret;
            }
        }

        return ret;
    }

    int node::shm_free_fn(node& n, no_stream_channel_t* c) {
        return channel::shm_close(c->key);
    }

    int node::mem_proc_fn(node& n, no_stream_channel_t* c, time_t sec, time_t usec) {
        int ret = 0;
        size_t left_times = n.conf_.loop_times;
        while (left_times-- > 0) {
            size_t recv_len;
            int res = channel::mem_recv(
                reinterpret_cast<channel::mem_channel*>(c->channel),
                n.static_buffer_->data(),
                n.static_buffer_->size(),
                &recv_len
            );

            if (EN_ATBUS_ERR_NO_DATA == res) {
                break;
            }

            // 回调收到数据事件
            if (res < 0) {
                ret = res;
                n.on_recv(NULL, res, res);
                break;
            } else {
                const protocol::msg* m = protocol::Getmsg(n.static_buffer_->data());
                n.on_recv(m, res, res);
                ++ret;
            }
        }

        return ret;
    }

    int node::mem_free_fn(node& n, no_stream_channel_t* c) {
        // 什么都不用干，反正内存也不是这里分配的
        return 0;
    }

    bool node::is_child_node(bus_id_t id) {
        // 目前id是整数，直接位运算即可
        bus_id_t mask = ~((1 << conf_.children_mask) - 1);
        return (id & mask) == (self_.id & mask);
    }

    bool node::is_brother_node(bus_id_t id) {
        // 目前id是整数，直接位运算即可
        bus_id_t c_mask = ~((1 << conf_.children_mask) - 1);
        bus_id_t f_mask = ~((1 << node_father_.children_mask) - 1);
        // 同一父节点下，且子节点域不同
        return (id & c_mask) != (self_.id & c_mask) && (id & f_mask) == (self_.id & f_mask);
    }

    bool node::is_parent_node(bus_id_t id) {
        return id == node_father_.id;
    }

    channel::io_stream_channel* node::get_iostream_channel() {
        if(iostream_channel_) {
            return iostream_channel_.get();
        }
        iostream_channel_ = std::shared_ptr<channel::io_stream_channel>(new channel::io_stream_channel(), detail::io_stream_channel_del());

        channel::io_stream_init(iostream_channel_.get(), get_evloop(), get_iostream_conf());
        iostream_channel_->data = this;

        // callbacks
        iostream_channel_->evt.callbacks[channel::io_stream_callback_evt_t::EN_FN_ACCEPTED] = iostream_on_accepted;
        iostream_channel_->evt.callbacks[channel::io_stream_callback_evt_t::EN_FN_CONNECTED] = iostream_on_connected;
        iostream_channel_->evt.callbacks[channel::io_stream_callback_evt_t::EN_FN_DISCONNECTED] = iostream_on_disconnected;
        iostream_channel_->evt.callbacks[channel::io_stream_callback_evt_t::EN_FN_RECVED] = iostream_on_recv_cb;

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
