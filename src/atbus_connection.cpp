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
#include "atbus_connection.h"

namespace atbus {
    connection::connection():state_(state_t::DISCONNECTED) {
        flags_.reset();
        memset(&conn_data_, 0, sizeof(conn_data_));
    }

    connection::ptr_t connection::create(std::weak_ptr<node> owner) {
        if (!owner.lock()) {
            return connection::ptr_t();
        }

        connection::ptr_t ret = std::make_shared<connection>();
        ret->owner_ = owner;
        return ret;
    }

    connection::~connection() {
        reset();
    }

    void connection::reset() {
        disconnect();
        // TODO 移除proc队列
        // TODO 关闭fd

        binding_.reset();
        owner_.reset();
    }

    int connection::listen(const char* addr_str) {
        if (state_t::DISCONNECTED != state_) {
            return EN_ATBUS_ERR_ALREADY_INITED;
        }

        if (false == channel::make_address(addr_str, address_)) {
            return EN_ATBUS_ERR_CHANNEL_ADDR_INVALID;
        }

        if (0 == ATBUS_FUNC_STRNCASE_CMP("mem", address_.scheme.c_str(), 3)) {
            channel::mem_channel* mem_chann;
            intptr_t ad;
            detail::str2int(ad, address_.host.c_str());
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
            detail::str2int(shm_key, address_.host.c_str());
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

    bool connection::is_connected() const {
        return state_t::CONNECTED == state_;
    }

    endpoint* connection::get_binding() {
        return binding_.lock().get();
    }

    const endpoint* connection::get_binding() const {
        return binding_.lock().get();
    }

    void connection::iostream_on_recv_cb(channel::io_stream_channel* channel, channel::io_stream_connection* conn_ios, int status, void* buffer, size_t s) {

        assert(channel && channel->data);
        node* _this = reinterpret_cast<node*>(channel->data);
        connection* conn = reinterpret_cast<connection*>(conn_ios->data);

        if (status < 0 || NULL == buffer || s <= 0) {
            _this->on_recv(conn, NULL, status, channel->error_code);
            return;
        }

        // TODO 要特别注意处理一下地址对齐问题对flatbuffer有无影响
        // 看flatbuffer代码的话，这部分没有设置对齐，应该会有影响
        const protocol::msg* m = protocol::Getmsg(buffer);
        _this->on_recv(conn, m, status, channel->error_code);
    }

    void connection::iostream_on_accepted(channel::io_stream_channel* channel, channel::io_stream_connection* conn_ios, int status, void* buffer, size_t s) {
        // TODO 连接成功加入点对点传输池
        // TODO 加入超时检测
        channel->data = NULL;
    }

    void connection::iostream_on_connected(channel::io_stream_channel* channel, channel::io_stream_connection* conn_ios, int status, void* buffer, size_t s) {
        // TODO 连接成功加入点对点传输池
        // TODO 发送注册协议
        // TODO 加入超时检测
        channel->data = NULL;
    }

    void connection::iostream_on_disconnected(channel::io_stream_channel* channel, channel::io_stream_connection* conn_ios, int status, void* buffer, size_t s) {
        // TODO 移除相关的连接
        // TODO 移除相关的node信息
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
                conn.conn_data_.shm.channel,
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
                const protocol::msg* m = protocol::Getmsg(static_buffer->data());
                n.on_recv(&conn, m, res, res);
                ++ret;
            }
        }

        return ret;
    }

    int connection::shm_free_fn(node& n, connection& conn) {
        return channel::shm_close(conn.conn_data_.shm.shm_key);
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
                conn.conn_data_.mem.channel,
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
                const protocol::msg* m = protocol::Getmsg(static_buffer->data());
                n.on_recv(&conn, m, res, res);
                ++ret;
            }
        }

        return ret;
    }

    int connection::mem_free_fn(node& n, connection& conn) {
        // 什么都不用干，反正内存也不是这里分配的
        return 0;
    }
}
