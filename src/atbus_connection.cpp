#include <cstdio>
#include <assert.h>
#include <ctime>
#include <stdint.h>
#include <cstddef>
#include <cstring>
#include <cstdlib>

#include "common/string_oprs.h"

#include "detail/buffer.h"

#include "atbus_node.h"
#include "atbus_connection.h"

#include "detail/libatbus_protocol.h"

namespace atbus {
    namespace detail {
        struct connection_async_data {
            node::ptr_t owner_node;
            connection::ptr_t conn;
        };
    }

    connection::connection():state_(state_t::DISCONNECTED), owner_(NULL), binding_(NULL){
        flags_.reset();
        memset(&conn_data_, 0, sizeof(conn_data_));
    }

    connection::ptr_t connection::create(node* owner) {
        if (!owner) {
            return connection::ptr_t();
        }

        connection::ptr_t ret(new connection());
        if (!ret) {
            return ret;
        }

        ret->owner_ = owner;
        ret->watcher_ = ret;
        return ret;
    }

    connection::~connection() {
        reset();
    }

    void connection::reset() {
        // 这个函数可能会在析构时被调用，这时候不能使用watcher_.lock()
        if(flags_.test(flag_t::RESETTING)) {
            return;
        }
        flags_.set(flag_t::RESETTING, true);

        disconnect();

        if (NULL != binding_) {
            binding_->remove_connection(this);
        }

        flags_.reset();
        // 只能由上层设置binding_所属的节点
        // binding_ = NULL;

        // 只要connection存在，则它一定存在于owner_的某个位置。
        // 并且这个值只能在创建时指定，所以不能重置这个值
        // owner_ = NULL;
    }

    int connection::proc(node& n, time_t sec, time_t usec) {
        if (state_t::CONNECTED != state_) {
            return 0;
        }

        if (NULL != conn_data_.proc_fn) {
            return conn_data_.proc_fn(n, *this, sec, usec);
        }

        return 0;
    }

    int connection::listen(const char* addr_str) {
        if (state_t::DISCONNECTED != state_) {
            return EN_ATBUS_ERR_ALREADY_INITED;
        }

        if (NULL == owner_) {
            return EN_ATBUS_ERR_NOT_INITED;
        }
        const node::conf_t& conf = owner_->get_conf();

        if (false == channel::make_address(addr_str, address_)) {
            return EN_ATBUS_ERR_CHANNEL_ADDR_INVALID;
        }

        if (0 == UTIL_STRFUNC_STRNCASE_CMP("mem", address_.scheme.c_str(), 3)) {
            channel::mem_channel* mem_chann = NULL;
            intptr_t ad;
            util::string::str2int(ad, address_.host.c_str());
            int res = channel::mem_attach(reinterpret_cast<void*>(ad), conf.recv_buffer_size, &mem_chann, NULL);
            if (res < 0) {
                res = channel::mem_init(reinterpret_cast<void*>(ad), conf.recv_buffer_size, &mem_chann, NULL);
            }

            if (res < 0) {
                return res;
            }
            
            conn_data_.proc_fn = mem_proc_fn;
            conn_data_.free_fn = mem_free_fn;

            // 加入轮询队列
            conn_data_.shared.mem.channel = mem_chann;
            conn_data_.shared.mem.buffer = reinterpret_cast<void*>(ad);
            conn_data_.shared.mem.len = conf.recv_buffer_size;
            owner_->add_proc_connection(watcher_.lock());
            flags_.set(flag_t::REG_PROC, true);
            flags_.set(flag_t::ACCESS_SHARE_ADDR, true);
            flags_.set(flag_t::ACCESS_SHARE_HOST, true);
            state_ = state_t::CONNECTED;

            return res;
        } else if (0 == UTIL_STRFUNC_STRNCASE_CMP("shm", address_.scheme.c_str(), 3)) {
            channel::shm_channel* shm_chann = NULL;
            key_t shm_key;
            util::string::str2int(shm_key, address_.host.c_str());
            int res = channel::shm_attach(shm_key, conf.recv_buffer_size, &shm_chann, NULL);
            if (res < 0) {
                res = channel::shm_init(shm_key, conf.recv_buffer_size, &shm_chann, NULL);
            }

            if (res < 0) {
                return res;
            }

            conn_data_.proc_fn = shm_proc_fn;
            conn_data_.free_fn = shm_free_fn;

            // 加入轮询队列
            conn_data_.shared.shm.channel = shm_chann;
            conn_data_.shared.shm.shm_key = shm_key;
            conn_data_.shared.shm.len = conf.recv_buffer_size;
            owner_->add_proc_connection(watcher_.lock());
            flags_.set(flag_t::REG_PROC, true);
            flags_.set(flag_t::ACCESS_SHARE_HOST, true);
            state_ = state_t::CONNECTED;

            return res;
        } else {
            detail::connection_async_data* async_data = new detail::connection_async_data();
            if (NULL == async_data) {
                return EN_ATBUS_ERR_MALLOC;
            }
            connection::ptr_t self = watcher_.lock();
            async_data->conn = self;
            async_data->owner_node = owner_->get_watcher();

            state_ = state_t::CONNECTING;
            int res = channel::io_stream_listen(owner_->get_iostream_channel(), address_, iostream_on_listen_cb, async_data, 0);
            if (res < 0) {
                delete async_data;
                return res;
            }

            // 可能会进入异步流程
            if (state_t::CONNECTING == state_) {
                owner_->add_connection_timer(self);
            }
        }

        return EN_ATBUS_ERR_SUCCESS;
    }

    int connection::connect(const char* addr_str) {
        if (state_t::DISCONNECTED != state_) {
            return EN_ATBUS_ERR_ALREADY_INITED;
        }

        if (NULL == owner_) {
            return EN_ATBUS_ERR_NOT_INITED;
        }
        const node::conf_t& conf = owner_->get_conf();

        if (false == channel::make_address(addr_str, address_)) {
            return EN_ATBUS_ERR_CHANNEL_ADDR_INVALID;
        }

        if (0 == UTIL_STRFUNC_STRNCASE_CMP("mem", address_.scheme.c_str(), 3)) {
            channel::mem_channel* mem_chann = NULL;
            intptr_t ad;
            util::string::str2int(ad, address_.host.c_str());
            int res = channel::mem_attach(reinterpret_cast<void*>(ad), conf.recv_buffer_size, &mem_chann, NULL);
            if (res < 0) {
                res = channel::mem_init(reinterpret_cast<void*>(ad), conf.recv_buffer_size, &mem_chann, NULL);
            }

            if (res < 0) {
                return res;
            }

            conn_data_.proc_fn = mem_proc_fn;
            conn_data_.free_fn = mem_free_fn;
            conn_data_.push_fn = mem_push_fn;

            // 连接信息
            conn_data_.shared.mem.channel = mem_chann;
            conn_data_.shared.mem.buffer = reinterpret_cast<void*>(ad);
            conn_data_.shared.mem.len = conf.recv_buffer_size;
            flags_.set(flag_t::REG_PROC, true);
            state_ = state_t::CONNECTED;

            return res;
        } else if (0 == UTIL_STRFUNC_STRNCASE_CMP("shm", address_.scheme.c_str(), 3)) {
            channel::shm_channel* shm_chann = NULL;
            key_t shm_key;
            util::string::str2int(shm_key, address_.host.c_str());
            int res = channel::shm_attach(shm_key, conf.recv_buffer_size, &shm_chann, NULL);
            if (res < 0) {
                res = channel::shm_init(shm_key, conf.recv_buffer_size, &shm_chann, NULL);
            }

            if (res < 0) {
                return res;
            }

            conn_data_.proc_fn = shm_proc_fn;
            conn_data_.free_fn = shm_free_fn;
            conn_data_.push_fn = shm_push_fn;

            // 连接信息
            conn_data_.shared.shm.channel = shm_chann;
            conn_data_.shared.shm.shm_key = shm_key;
            conn_data_.shared.shm.len = conf.recv_buffer_size;

            flags_.set(flag_t::REG_PROC, true);
            state_ = state_t::CONNECTED;

            return res;
        } else {
            detail::connection_async_data* async_data = new detail::connection_async_data();
            if (NULL == async_data) {
                return EN_ATBUS_ERR_MALLOC;
            }
            connection::ptr_t self = watcher_.lock();
            async_data->conn = self;
            async_data->owner_node = owner_->get_watcher();

            state_ = state_t::CONNECTING;
            int res = channel::io_stream_connect(owner_->get_iostream_channel(), address_, iostream_on_connected_cb, async_data, 0);
            if (res < 0) {
                delete async_data;
                return res;
            }

            // 可能会进入异步流程
            if (state_t::CONNECTING == state_) {
                owner_->add_connection_timer(self);
            }
        }

        return EN_ATBUS_ERR_SUCCESS;
    }

    int connection::disconnect() {
        if (state_t::DISCONNECTED == state_) {
            return EN_ATBUS_ERR_NOT_INITED;
        }

        state_ = state_t::DISCONNECTING;
        if (NULL != conn_data_.free_fn) {
            if (NULL != owner_) {
                conn_data_.free_fn(*owner_, *this);
            }
        }

        if (NULL != owner_) {
            owner_->on_disconnect(this);
        }

        // 移除proc队列
        if (flags_.test(flag_t::REG_PROC)) {
            if (NULL != owner_) {
                owner_->remove_proc_connection(address_.address);
            }
            flags_.set(flag_t::REG_PROC, false);
        }

        memset(&conn_data_, 0, sizeof(conn_data_));
        state_ = state_t::DISCONNECTED;
        return 0;
    }

    int connection::push(const void* buffer, size_t s) {
        if (state_t::CONNECTED != state_) {
            return EN_ATBUS_ERR_NOT_INITED;
        }

        if (NULL == conn_data_.push_fn) {
            return EN_ATBUS_ERR_ACCESS_DENY;
        }

        return conn_data_.push_fn(*this, buffer, s);
    }

    bool connection::is_connected() const {
        return state_t::CONNECTED == state_;
    }

    endpoint* connection::get_binding() {
        return binding_;
    }

    const endpoint* connection::get_binding() const {
        return binding_;
    }

    void connection::iostream_on_listen_cb(channel::io_stream_channel* channel, channel::io_stream_connection* connection, int status, void* buffer, size_t s) {
        detail::connection_async_data* async_data = reinterpret_cast<detail::connection_async_data*>(buffer);
        assert(NULL != async_data);
        if(NULL == async_data) {
            return;
        }

        if (status < 0) {
            async_data->owner_node->on_error(async_data->conn->binding_, async_data->conn.get(), status, channel->error_code);
            async_data->conn->state_ = state_t::DISCONNECTED;

        } else {
            async_data->conn->flags_.set(flag_t::REG_FD, true);
            async_data->conn->state_ = state_t::CONNECTED;

            async_data->conn->conn_data_.shared.ios_fd.channel = channel;
            async_data->conn->conn_data_.shared.ios_fd.conn = connection;
            async_data->conn->conn_data_.free_fn = ios_free_fn;
            connection->data = async_data->conn.get();
        }

        delete async_data;
    }

    void connection::iostream_on_connected_cb(channel::io_stream_channel* channel, channel::io_stream_connection* connection, int status, void* buffer, size_t s) {
        detail::connection_async_data* async_data = reinterpret_cast<detail::connection_async_data*>(buffer);
        assert(NULL != async_data);
        if (NULL == async_data) {
            return;
        }

        if (status < 0) {
            async_data->owner_node->on_error(async_data->conn->binding_, async_data->conn.get(), status, channel->error_code);
            async_data->conn->state_ = state_t::DISCONNECTED;

        } else {
            async_data->conn->flags_.set(flag_t::REG_FD, true);
            async_data->conn->state_ = state_t::HANDSHAKING;

            async_data->conn->conn_data_.shared.ios_fd.channel = channel;
            async_data->conn->conn_data_.shared.ios_fd.conn = connection;

            async_data->conn->conn_data_.free_fn = ios_free_fn;
            async_data->conn->conn_data_.push_fn = ios_push_fn;
            connection->data = async_data->conn.get();

            async_data->owner_node->on_new_connection(async_data->conn.get());
        }

        delete async_data;
    }

    void connection::iostream_on_recv_cb(channel::io_stream_channel* channel, channel::io_stream_connection* conn_ios, int status, void* buffer, size_t s) {

        assert(channel && channel->data);
        node* _this = reinterpret_cast<node*>(channel->data);
        connection* conn = reinterpret_cast<connection*>(conn_ios->data);

        if (status < 0 || NULL == buffer || s <= 0) {
            _this->on_recv(conn, NULL, status, channel->error_code);
            return;
        }

        if (NULL == conn) {
            _this->on_error(conn->binding_, conn, EN_ATBUS_ERR_UNPACK, EN_ATBUS_ERR_PARAMS);
            return;
        }

        // unpack
        protocol::msg m;
        if (false == unpack(*conn, m, buffer, s)) {
            return;
        }
        _this->on_recv(conn, &m, status, channel->error_code);
    }

    void connection::iostream_on_accepted(channel::io_stream_channel* channel, channel::io_stream_connection* conn_ios, int status, void* buffer, size_t s) {
        // 连接成功加入点对点传输池
        // 加入超时检测
        node* n = reinterpret_cast<node*>(channel->data);
        assert(NULL != n);
        if (NULL == n) {
            channel::io_stream_disconnect(channel, conn_ios, NULL);
            return;
        }

        ptr_t conn = create(n);
        conn->state_ = state_t::HANDSHAKING;
        conn->flags_.set(flag_t::REG_FD, true);

        conn->conn_data_.free_fn = ios_free_fn;
        conn->conn_data_.push_fn = ios_push_fn;

        conn->conn_data_.shared.ios_fd.channel = channel;
        conn->conn_data_.shared.ios_fd.conn = conn_ios;
        conn_ios->data = conn.get();

        n->on_new_connection(conn.get());
    }

    void connection::iostream_on_connected(channel::io_stream_channel* channel, channel::io_stream_connection* conn_ios, int status, void* buffer, size_t s) {
    }

    void connection::iostream_on_disconnected(channel::io_stream_channel* channel, channel::io_stream_connection* conn_ios, int status, void* buffer, size_t s) {
        connection* conn = reinterpret_cast<connection*>(conn_ios->data);
        assert(NULL != conn);
        if (NULL == conn) {
            return;
        }

        conn->disconnect();
    }

    int connection::shm_proc_fn(node& n, connection& conn, time_t sec, time_t usec) {
        int ret = 0;
        size_t left_times = n.get_conf().loop_times;
        detail::buffer_block* static_buffer = n.get_temp_static_buffer();
        if (NULL == static_buffer) {
            return n.on_error(NULL, &conn, EN_ATBUS_ERR_NOT_INITED, 0);
        }

        while (left_times-- > 0) {
            size_t recv_len;
            int res = channel::shm_recv(
                conn.conn_data_.shared.shm.channel,
                static_buffer->data(),
                static_buffer->size(),
                &recv_len
            );

            if (EN_ATBUS_ERR_NO_DATA == res) {
                break;
            }

            // 回调收到数据事件
            if (res < 0) {
                ret = res;
                n.on_recv(&conn, NULL, res, res);
                break;
            } else {
                // unpack
                protocol::msg m;
                if (false == unpack(conn, m, static_buffer->data(), recv_len)) {
                    continue;
                }

                n.on_recv(&conn, &m, res, res);
                ++ret;
            }
        }

        return ret;
    }

    int connection::shm_free_fn(node& n, connection& conn) {
        return channel::shm_close(conn.conn_data_.shared.shm.shm_key);
    }

    int connection::shm_push_fn(connection& conn, const void* buffer, size_t s) {
        return channel::shm_send(conn.conn_data_.shared.shm.channel, buffer, s);
    }

    int connection::mem_proc_fn(node& n, connection& conn, time_t sec, time_t usec) {
        int ret = 0;
        size_t left_times = n.get_conf().loop_times;
        detail::buffer_block* static_buffer = n.get_temp_static_buffer();
        if (NULL == static_buffer) {
            return n.on_error(NULL, &conn, EN_ATBUS_ERR_NOT_INITED, 0);
        }

        while (left_times-- > 0) {
            size_t recv_len;
            int res = channel::mem_recv(
                conn.conn_data_.shared.mem.channel,
                static_buffer->data(),
                static_buffer->size(),
                &recv_len
            );

            if (EN_ATBUS_ERR_NO_DATA == res) {
                break;
            }

            // 回调收到数据事件
            if (res < 0) {
                ret = res;
                n.on_recv(&conn, NULL, res, res);
                break;
            } else {
                // unpack
                protocol::msg m;
                if (false == unpack(conn, m, static_buffer->data(), recv_len)) {
                    continue;
                }

                n.on_recv(&conn, &m, res, res);
                ++ret;
            }
        }

        return ret;
    }

    int connection::mem_free_fn(node& n, connection& conn) {
        return 0;
    }

    int connection::mem_push_fn(connection& conn, const void* buffer, size_t s) {
        return channel::mem_send(conn.conn_data_.shared.mem.channel, buffer, s);
    }

    int connection::ios_free_fn(node& n, connection& conn) {
        return channel::io_stream_disconnect(conn.conn_data_.shared.ios_fd.channel, conn.conn_data_.shared.ios_fd.conn, NULL);
    }

    int connection::ios_push_fn(connection& conn, const void* buffer, size_t s) {
        return channel::io_stream_send(conn.conn_data_.shared.ios_fd.conn, buffer, s);
    }

    bool connection::unpack(connection& conn, atbus::protocol::msg& m, void* buffer, size_t s) {
        msgpack::unpacked result;
        msgpack::unpack(result, reinterpret_cast<const char*>(buffer), s);
        msgpack::object obj = result.get();
        if (obj.is_nil()) {
            conn.owner_->on_error(conn.binding_, &conn, EN_ATBUS_ERR_UNPACK, EN_ATBUS_ERR_UNPACK);
            return false;
        }

        obj.convert(m);
        return true;
    }
}
