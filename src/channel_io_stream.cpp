﻿/**
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
#include <vector>

#ifndef _MSC_VER
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

#include "std/smart_ptr.h"

#include "detail/libatbus_error.h"
#include "detail/libatbus_channel_export.h"
#include "detail/buffer.h"
#include "detail/crc32.h"

#ifdef ATBUS_MACRO_ENABLE_STATIC_ASSERT
#include <type_traits>
#include <detail/libatbus_channel_types.h>

#endif


#ifndef MAX_PATH
#ifdef _MAX_PATH
#define MAX_PATH _MAX_PATH
#elif defined(PATH_MAX)
#define MAX_PATH PATH_MAX
#else
// 默认取Windows内定义的值，因为Unix like系统一般定得都比较大
#define MAX_PATH 260
#endif
#endif

namespace atbus {
    namespace channel {

#ifdef ATBUS_MACRO_ENABLE_STATIC_ASSERT
        static_assert(std::is_pod<io_stream_conf>::value, "io_stream_conf should be a pod type");
#endif

        union io_stream_sockaddr_switcher {
            sockaddr     base;
            sockaddr_in  ipv4;
            sockaddr_in6 ipv6;
        };

        static inline void io_stream_channel_callback(
                io_stream_callback_evt_t::mem_fn_t fn, io_stream_channel* channel, io_stream_callback_t async_callback,
                io_stream_connection* connection, int status, int errcode,
                void* priv_data, size_t s
                ) {
            if (NULL != channel) {
                channel->error_code = status;
            }

            if (NULL != channel && NULL != channel->evt.callbacks[fn]) {
                channel->evt.callbacks[fn](channel, connection, errcode, priv_data, s);
            }

            if (NULL != async_callback) {
                async_callback(channel, connection, errcode, priv_data, s);
            }
        }

        static inline void io_stream_channel_callback(
            io_stream_callback_evt_t::mem_fn_t fn, io_stream_channel* channel, io_stream_connection* conn_evt,
            io_stream_connection* connection, int status, int errcode,
            void* priv_data, size_t s
            ) {
            io_stream_callback_t async_callback = (NULL != conn_evt && NULL != conn_evt->evt.callbacks[fn]) ? conn_evt->evt.callbacks[fn] : NULL;
            io_stream_channel_callback(
                fn, 
                channel,
                async_callback,
                connection,
                status,
                errcode,
                priv_data,
                s
            );
        }

        static inline void io_stream_channel_callback(
            io_stream_callback_evt_t::mem_fn_t fn, io_stream_channel* channel,
            io_stream_connection* connection, int status, int errcode,
            void* priv_data, size_t s
        ) {
            io_stream_channel_callback(fn, channel, connection, connection, status, errcode, priv_data, s);
        }

        void io_stream_init_configure(io_stream_conf* conf) {
            if (NULL == conf) {
                return;
            }

            conf->keepalive = 60;
            conf->is_noblock = true;
            conf->is_nodelay = true;
            conf->send_buffer_static = 0;
            conf->recv_buffer_static = 2; // 接收一般就一个正在处理的包，所以预留2个index足够了

            conf->send_buffer_max_size = 0;
            conf->send_buffer_limit_size = ATBUS_MACRO_MSG_LIMIT;

            conf->recv_buffer_max_size = ATBUS_MACRO_MSG_LIMIT * conf->recv_buffer_static;
            conf->recv_buffer_limit_size = ATBUS_MACRO_MSG_LIMIT;

            conf->backlog = ATBUS_MACRO_CONNECTION_BACKLOG;
        }

        static adapter::loop_t* io_stream_get_loop(io_stream_channel* channel) {
            if (NULL == channel) {
                return NULL;
            }

            if (NULL == channel->ev_loop) {
                channel->ev_loop = reinterpret_cast<adapter::loop_t*>(malloc(sizeof(adapter::loop_t)));
                if (NULL != channel->ev_loop) {
                    uv_loop_init(channel->ev_loop);
                    channel->is_loop_owner = true;
                }
            }

            return channel->ev_loop;
        }

        int io_stream_init(io_stream_channel* channel, adapter::loop_t* ev_loop, const io_stream_conf* conf) {
            if (NULL == channel) {
                return EN_ATBUS_ERR_PARAMS;
            }

            if (NULL == conf) {
                io_stream_conf default_conf;
                io_stream_init_configure(&default_conf);

                return io_stream_init(channel, ev_loop, &default_conf);
            }

            channel->conf = *conf;
            channel->ev_loop = ev_loop;
            channel->is_loop_owner = false;

            memset(channel->evt.callbacks, 0, sizeof(channel->evt.callbacks));

            channel->error_code = 0;
            return EN_ATBUS_ERR_SUCCESS;
        }

        int io_stream_close(io_stream_channel* channel) {
            if (NULL == channel) {
                return EN_ATBUS_ERR_PARAMS;
            }

            // 释放所有连接
            {
                std::vector<io_stream_connection*> pending_release;
                pending_release.reserve(channel->conn_pool.size());
                for (io_stream_channel::conn_pool_t::iterator iter = channel->conn_pool.begin();
                     iter != channel->conn_pool.end(); ++iter) {
                    pending_release.push_back(iter->second.get());
                }

                for (size_t i = 0; i < pending_release.size(); ++i) {
                    io_stream_disconnect(channel, pending_release[i], NULL);
                }
            }

            // 必须保证这个接口过后channel内的数据可以正常释放
            // 所以必须等待相关的回调全部完成
            // 当然也可以用另一种方法强行结束掉所有req，但是这样会造成丢失回调
            // 并且这会要求逻辑层设计相当完善，否则可能导致内存泄漏。所以为了简化逻辑层设计，还是block并销毁所有数据

            if (true == channel->is_loop_owner && NULL != channel->ev_loop) {
                // 先清理掉所有可以完成的事件
                while (uv_run(channel->ev_loop, UV_RUN_NOWAIT)) {
                    uv_run(channel->ev_loop, UV_RUN_ONCE);
                }

                uv_loop_close(channel->ev_loop);
                // 停止时阻塞操作，保证资源正常释放
                uv_run(channel->ev_loop, UV_RUN_DEFAULT);
                free(channel->ev_loop);
            } else {
                while(!channel->conn_pool.empty()) {
                    uv_run(channel->ev_loop, UV_RUN_ONCE);
                }
            }

            channel->ev_loop = NULL;
            return EN_ATBUS_ERR_SUCCESS;
        }

        int io_stream_run(io_stream_channel* channel, adapter::run_mode_t mode) {
            if (NULL == channel) {
                return EN_ATBUS_ERR_PARAMS;
            }

            channel->error_code = uv_run(io_stream_get_loop(channel), static_cast<uv_run_mode>(mode));
            if (0 != channel->error_code) {
                return EN_ATBUS_ERR_EV_RUN;
            }

            return EN_ATBUS_ERR_SUCCESS;
        }


        static void io_stream_on_recv_alloc_fn(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
            io_stream_connection* conn_raw_ptr = reinterpret_cast<io_stream_connection*>(handle->data);
            assert(conn_raw_ptr);
            assert(conn_raw_ptr->channel);

            void* data = NULL;
            size_t sread = 0, swrite = 0;
            conn_raw_ptr->read_buffers.back(data, sread, swrite);

            // 正在读取vint时，指定缓冲区为head内存块
            if (NULL == data || 0 == swrite) {
                buf->len = sizeof(conn_raw_ptr->read_head.buffer) - conn_raw_ptr->read_head.len;

                if (0 == buf->len) {
                    // 理论上这里不会走到，因为如果必然会先收取一次header的大小，这时候已经可以解出msg的大小
                    // 如果msg超过限制大小并低于缓冲区大小，则会发出大小错误回调并会减少header的占用量，
                    // 那么下一次这个回调函数调用时buf->len必然大于0
                    // 如果msg超过缓冲区大小，则会出错回调并立即断开连接,不会再有下一次调用
                    buf->base = NULL;
                } else {
                    buf->base = &conn_raw_ptr->read_head.buffer[conn_raw_ptr->read_head.len];
                }
                return;
            }

            // 否则指定为大内存块缓冲区
            buf->base = reinterpret_cast<char*>(data);
            buf->len = swrite;
        }

        static void io_stream_on_recv_read_fn(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf){
            io_stream_connection* conn_raw_ptr = reinterpret_cast<io_stream_connection*>(stream->data);
            assert(conn_raw_ptr);
            io_stream_channel* channel = conn_raw_ptr->channel;
            assert(channel);

            // 读取完或EAGAIN或signal中断，直接忽略即可
            if (0 == nread || UV_EAGAIN == nread || UV_EAI_AGAIN == nread || UV_EINTR == nread) {
                return;
            }

            // 网络错误
            if (nread < 0) {
                channel->error_code = static_cast<int>(nread);
                io_stream_channel_callback(
                    io_stream_callback_evt_t::EN_FN_RECVED,
                    channel,
                    conn_raw_ptr,
                    static_cast<int>(nread),
                    EN_ATBUS_ERR_READ_FAILED,
                    NULL,
                    0
                );

                // 任何非重试的错误则关闭
                // 注意libuv有个特殊的错误码 UV_ENOBUFS 表示缓冲区不足
                // 理论上除非配置错误，否则不应该会出现，并且可能会导致header数据无法缩减。所以也直接关闭连接
                io_stream_disconnect(channel, conn_raw_ptr, NULL);
                return;
            }

            void *data = NULL;
            size_t sread = 0, swrite = 0;
            conn_raw_ptr->read_buffers.back(data, sread, swrite);
            bool is_free = false;

            // head 阶段
            if (NULL == data || 0 == swrite) {
                assert(static_cast<size_t>(nread) <= sizeof(conn_raw_ptr->read_head.buffer) - conn_raw_ptr->read_head.len);
                conn_raw_ptr->read_head.len += static_cast<size_t>(nread); // 写数据计数

                // 尝试解出所有的head数据
                char* buff_start = conn_raw_ptr->read_head.buffer;
                size_t buff_left_len = conn_raw_ptr->read_head.len;

                // 可能包含多条消息
                while (buff_left_len > sizeof(uint32_t)) {
                    uint64_t msg_len = 0;
                    // 前4 字节为crc32
                    size_t vint_len = detail::fn::read_vint(msg_len, buff_start + sizeof(uint32_t), buff_left_len - sizeof(uint32_t));

                    // 剩余数据不足以解动态长度整数，直接中断退出
                    if (0 == vint_len) {
                        break;
                    }

                    // 如果读取vint成功，判定是否有小数据包。并对小数据包直接回调
                    if (buff_left_len >= sizeof(uint32_t) + vint_len + msg_len) {
                        channel->error_code = 0;
                        uint32_t check_crc = atbus::detail::crc32(0, reinterpret_cast<unsigned char*>(buff_start) + sizeof(uint32_t) + vint_len, msg_len);
                        uint32_t expect_crc;
                        memcpy(&expect_crc, buff_start, sizeof(uint32_t));
                        int errcode = EN_ATBUS_ERR_SUCCESS;
                        if (check_crc != expect_crc) {
                            errcode = EN_ATBUS_ERR_BAD_DATA;
                        } else if (channel->conf.recv_buffer_limit_size > 0 && msg_len > channel->conf.recv_buffer_limit_size) {
                            errcode = EN_ATBUS_ERR_INVALID_SIZE;
                        }

                        io_stream_channel_callback(
                            io_stream_callback_evt_t::EN_FN_RECVED,
                            channel,
                            conn_raw_ptr,
                            0,
                            errcode,
                            buff_start + sizeof(uint32_t) + vint_len,
                            // 这里的地址未对齐，所以buffer不能直接保存内存数据
                            msg_len
                        );

                        // crc32+vint+buffer
                        buff_start += sizeof(uint32_t) + vint_len + msg_len;
                        buff_left_len -= sizeof(uint32_t) + vint_len + msg_len;
                    } else {
                        // 大数据包，使用缓冲区，并且剩余数据一定是在一个包内
                        // CRC32 也暂存在这里
                        if (EN_ATBUS_ERR_SUCCESS == conn_raw_ptr->read_buffers.push_back(data, sizeof(uint32_t) + msg_len)) {
                            memcpy(data, buff_start, sizeof(uint32_t)); // CRC32
                            memcpy(reinterpret_cast<char*>(data) + sizeof(uint32_t), buff_start + sizeof(uint32_t) + vint_len, buff_left_len - sizeof(uint32_t) - vint_len);
                            conn_raw_ptr->read_buffers.pop_back(buff_left_len - vint_len, false); // vint_len不用保存

                            buff_start += buff_left_len;
                            buff_left_len = 0; // 循环退出
                        } else {
                            // 追加大缓冲区失败，可能是到达缓冲区限制
                            // 读缓冲区一般只有一个正在处理的数据包，如果发生创建失败则是数据错误或者这个包就是超出大小限制的
                            is_free = true;
                            buff_start += sizeof(uint32_t) + vint_len;
                            buff_left_len -= sizeof(uint32_t) + vint_len;
                            break;
                        }
                    }
                }

                // 后续数据前移
                if (buff_start != conn_raw_ptr->read_head.buffer && buff_left_len > 0) {
                    memmove(conn_raw_ptr->read_head.buffer, buff_start, buff_left_len);
                }
                conn_raw_ptr->read_head.len = buff_left_len;
            } else {
                size_t nread_s = static_cast<size_t>(nread);
                assert(nread_s <= swrite);

                // 写数据计数,但不释放缓冲区
                conn_raw_ptr->read_buffers.pop_back(nread_s, false);
            }

            // 如果在大内存块缓冲区，判定回调
            conn_raw_ptr->read_buffers.front(data, sread, swrite);
            if (NULL != data && 0 == swrite) {
                channel->error_code = 0;
                data = detail::fn::buffer_prev(data, sread);

                // CRC校验和
                uint32_t check_crc = atbus::detail::crc32(0, reinterpret_cast<unsigned char*>(data) + sizeof(uint32_t), sread - sizeof(uint32_t));
                uint32_t expect_crc;
                memcpy(&expect_crc, data, sizeof(uint32_t));
                size_t msg_len = sread - sizeof(uint32_t); // - crc32 header

                int errcode = EN_ATBUS_ERR_SUCCESS;
                if (check_crc != expect_crc) {
                    errcode = EN_ATBUS_ERR_BAD_DATA;
                } else if (channel->conf.recv_buffer_limit_size > 0 && msg_len > channel->conf.recv_buffer_limit_size) {
                    errcode = EN_ATBUS_ERR_INVALID_SIZE;
                }

                io_stream_channel_callback(
                    io_stream_callback_evt_t::EN_FN_RECVED,
                    channel,
                    conn_raw_ptr,
                    0,
                    errcode,
                    reinterpret_cast<char*>(data) + sizeof(uint32_t),   // + crc32 header
                    // 由于buffer_block内取出的数据已经保证了字节对齐，所以这里一定是4字节对齐
                    msg_len
                );

                // 回调并释放缓冲区
                conn_raw_ptr->read_buffers.pop_front(0, true);
            }

            if (is_free) {
                if (conn_raw_ptr->read_head.len > 0) {
                    io_stream_channel_callback(
                        io_stream_callback_evt_t::EN_FN_RECVED,
                        channel,
                        conn_raw_ptr,
                        0,
                        EN_ATBUS_ERR_INVALID_SIZE,
                        conn_raw_ptr->read_head.buffer,
                        // 由于buffer_block内取出的数据已经保证了字节对齐，所以这里一定是4字节对齐
                        conn_raw_ptr->read_head.len
                        );
                }

                // 强制中断
                io_stream_disconnect(channel, conn_raw_ptr, NULL);
            }
        }


        static void io_stream_stream_init(io_stream_channel* channel, io_stream_connection* conn, adapter::stream_t* handle) {
            if (NULL == channel || NULL == handle) {
                return;
            }

            handle->data = conn;
        }

        static void io_stream_tcp_init(io_stream_channel* channel, io_stream_connection* conn, adapter::tcp_t* handle) {
            if (NULL == channel || NULL == handle) {
                return;
            }

            io_stream_stream_init(channel, conn, reinterpret_cast<adapter::stream_t*>(handle));
        }

        static void io_stream_pipe_init(io_stream_channel* channel, io_stream_connection* conn, adapter::pipe_t* handle) {
            if (NULL == channel || NULL == handle) {
                return;
            }

            io_stream_stream_init(channel, conn, reinterpret_cast<adapter::stream_t*>(handle));
        }

        static void io_stream_stream_setup(io_stream_channel* channel, adapter::stream_t* handle) {
            if (NULL == channel || NULL == handle) {
                return;
            }

            uv_stream_set_blocking(handle, channel->conf.is_noblock? 0: 1);
        }

        static void io_stream_tcp_setup(io_stream_channel* channel, adapter::tcp_t* handle) {
            if (NULL == channel || NULL == handle) {
                return;
            }

            if (channel->conf.keepalive > 0) {
                uv_tcp_keepalive(handle, 1, static_cast<unsigned int>(channel->conf.keepalive));
            } else {
                uv_tcp_keepalive(handle, 0, 0);
            }

            uv_tcp_nodelay(handle, channel->conf.is_nodelay? 1: 0);
            io_stream_stream_setup(channel, reinterpret_cast<adapter::stream_t*>(handle));
        }

        static void io_stream_pipe_setup(io_stream_channel* channel, adapter::pipe_t* handle) {
            if (NULL == channel || NULL == handle) {
                return;
            }

            io_stream_stream_setup(channel, reinterpret_cast<adapter::stream_t*>(handle));
        }

        static void io_stream_connection_on_close(uv_handle_t* handle) {
            io_stream_connection* conn_raw_ptr = reinterpret_cast<io_stream_connection*>(handle->data);
            // 连接尚未初始化完毕，直接退出
            if (NULL == conn_raw_ptr) {
                return;
            }

            io_stream_channel* channel = conn_raw_ptr->channel;
            assert(channel);

            io_stream_channel::conn_pool_t::iterator iter = channel->conn_pool.end();
            iter = channel->conn_pool.find(conn_raw_ptr->fd);

            if (iter != channel->conn_pool.end()) {
                iter->second->status = io_stream_connection::EN_ST_DISCONNECTIED;
                io_stream_channel_callback(io_stream_callback_evt_t::EN_FN_DISCONNECTED, channel, iter->second.get(), 0, EN_ATBUS_ERR_SUCCESS, NULL, 0);

                if(NULL != conn_raw_ptr->act_disc_cbk) {
                    conn_raw_ptr->act_disc_cbk(channel, conn_raw_ptr, EN_ATBUS_ERR_SUCCESS, NULL, 0);
                }

                channel->conn_pool.erase(iter);
            }
        }

        static std::shared_ptr<io_stream_connection>
            io_stream_make_connection(io_stream_channel* channel, std::shared_ptr<adapter::stream_t> handle) {
            std::shared_ptr<io_stream_connection> ret;
            if (NULL == channel) {
                return ret;
            }

            ret = std::make_shared<io_stream_connection>();
            if(!ret) {
                return ret;
            }

            if (0 != uv_fileno(reinterpret_cast<const uv_handle_t*>(handle.get()), &ret->fd)) {
                ret.reset();
                return ret;
            }

            ret->handle = handle;
            ret->data = NULL;
            handle->data = ret.get();

            memset(ret->evt.callbacks, 0, sizeof(ret->evt.callbacks));
            ret->act_disc_cbk = NULL;
            ret->status = io_stream_connection::EN_ST_CREATED;


            ret->read_buffers.set_limit(channel->conf.recv_buffer_max_size, 0);
            if (channel->conf.recv_buffer_max_size > 0 && channel->conf.recv_buffer_static > 0) {
                ret->read_buffers.set_mode(channel->conf.recv_buffer_max_size, channel->conf.recv_buffer_static);
            }
            ret->read_head.len = 0;

            ret->write_buffers.set_limit(channel->conf.send_buffer_max_size, 0);
            if (channel->conf.send_buffer_max_size > 0 && channel->conf.send_buffer_static > 0) {
                ret->write_buffers.set_mode(channel->conf.send_buffer_max_size, channel->conf.send_buffer_static);
            }

            channel->conn_pool[ret->fd] = ret;
            ret->channel = channel;

            // 监听关闭事件，用于释放资源
            handle->close_cb = io_stream_connection_on_close;

            // 监听可读事件
            uv_read_start(handle.get(), io_stream_on_recv_alloc_fn, io_stream_on_recv_read_fn);

            return ret;
        }

        // ============ C Style转C++ Style内存管理 ============
        template<typename T>
        static void io_stream_delete_stream_fn(adapter::stream_t* handle) {
            T* real_conn = reinterpret_cast<T*>(handle);

            // 到这里必须已经释放handle了，否则删除hanlde会导致数据异常。
            assert(uv_is_closing(reinterpret_cast<adapter::handle_t*>(handle)));
//            if(0 == uv_is_closing(reinterpret_cast<adapter::handle_t*>(handle))) {
//                uv_close(reinterpret_cast<adapter::handle_t*>(handle), io_stream_connection_on_close);
//            }

            delete real_conn;
        }

        template<typename T>
        static T* io_stream_make_stream_ptr(std::shared_ptr<adapter::stream_t>& res) {
            T* real_conn = new T();
            adapter::stream_t* stream_conn = reinterpret_cast<adapter::stream_t*>(real_conn);
            res = std::shared_ptr<adapter::stream_t>(stream_conn, io_stream_delete_stream_fn<T>);
            stream_conn->data = NULL;
            return real_conn;
        }

        struct io_stream_connect_async_data {
            uv_connect_t req;
            channel_address_t addr;
            io_stream_channel* channel;
            io_stream_callback_t callback;
            std::shared_ptr<adapter::stream_t> stream;
            bool pipe;
        };

        static void io_stream_connect_on_failed_close(uv_handle_t* handle) {
            // 注意只有这里 handle->data 的数据指向io_stream_connect_async_data
            // 因为这时候没有建立io_stream_connection对象，其他情况下都是io_stream_connection
            io_stream_connect_async_data* async_data = reinterpret_cast<io_stream_connect_async_data*>(handle->data);
            // 连接尚未初始化完毕，直接退出
            if (NULL == async_data) {
                return;
            }

            delete async_data;
        }

        // tcp 收到连接通用逻辑
        static adapter::tcp_t* io_stream_tcp_connection_common(std::shared_ptr<io_stream_connection>& conn, uv_stream_t* req, int status) {
            io_stream_connection* conn_raw_ptr = reinterpret_cast<io_stream_connection*>(req->data);
            assert(conn_raw_ptr);
            io_stream_channel* channel = conn_raw_ptr->channel;
            assert(channel);

            if (0 != status) {
                return NULL;
            }

            std::shared_ptr<adapter::stream_t> recv_conn;
            adapter::tcp_t* tcp_conn = io_stream_make_stream_ptr<adapter::tcp_t>(recv_conn);
            if (NULL == tcp_conn) {
                return NULL;
            }

            uv_tcp_init(req->loop, tcp_conn);
            if (0 != (channel->error_code = uv_accept(req, recv_conn.get()))) {
                return NULL;
            }

            conn = io_stream_make_connection(
                channel,
                recv_conn
            );

            if (!conn) {
                // 拿一个东西来保存handle,否则会在close回调前释放handle
                io_stream_connect_async_data* async_data = new io_stream_connect_async_data();
                async_data->stream = recv_conn;
                recv_conn->data = async_data;

                uv_close(reinterpret_cast<adapter::handle_t*>(recv_conn.get()), io_stream_connect_on_failed_close);
                return NULL;
            }

            io_stream_tcp_setup(channel, tcp_conn);
            io_stream_tcp_init(channel, conn.get(), tcp_conn);
            return tcp_conn;
        }

        // tcp/ip 收到连接
        static void io_stream_tcp_connection_cb(uv_stream_t* req, int status) {
            io_stream_connection* conn_raw_ptr = reinterpret_cast<io_stream_connection*>(req->data);
            assert(conn_raw_ptr);
            io_stream_channel* channel = conn_raw_ptr->channel;
            assert(channel);
            channel->error_code = status;
            int res = EN_ATBUS_ERR_SUCCESS;

            std::shared_ptr<adapter::stream_t> recv_conn;
            std::shared_ptr<io_stream_connection> conn;

            do {
                adapter::tcp_t* tcp_conn = io_stream_tcp_connection_common(conn, req, status);
                if (NULL == tcp_conn || !conn) {
                    res = EN_ATBUS_ERR_SOCK_CONNECT_FAILED;
                    break;
                }

                conn->status = io_stream_connection::EN_ST_CONNECTED;

                union io_stream_sockaddr_switcher sock_addr;
                int name_len = sizeof(sock_addr);
                uv_tcp_getpeername(tcp_conn, &sock_addr.base, &name_len);

                char ip[40] = { 0 };
                if (sock_addr.base.sa_family == AF_INET6) {
                    uv_ip6_name(&sock_addr.ipv6, ip, sizeof(ip));
                    make_address("ipv6", ip, sock_addr.ipv6.sin6_port, conn->addr);
                } else {
                    uv_ip4_name(&sock_addr.ipv4, ip, sizeof(ip));
                    make_address("ipv4", ip, sock_addr.ipv4.sin_port, conn->addr);
                }
            } while (false);

            // 回调函数，如果发起连接接口调用成功一定要调用回调函数
            io_stream_channel_callback(io_stream_callback_evt_t::EN_FN_ACCEPTED, channel, conn_raw_ptr, conn.get(), channel->error_code, res, NULL, 0);
        }

        // pipe 收到连接
        static void io_stream_pipe_connection_cb(uv_stream_t* req, int status) {
            io_stream_connection* conn_raw_ptr = reinterpret_cast<io_stream_connection*>(req->data);
            assert(conn_raw_ptr);
            io_stream_channel* channel = conn_raw_ptr->channel;
            assert(channel);
            channel->error_code = status;
            int res = EN_ATBUS_ERR_SUCCESS;

            std::shared_ptr<io_stream_connection> conn;
            do {
                if (0 != status || NULL == channel) {
                    res = EN_ATBUS_ERR_PIPE_CONNECT_FAILED;
                    break;
                }

                std::shared_ptr<adapter::stream_t> recv_conn;
                adapter::pipe_t* pipe_conn = io_stream_make_stream_ptr<adapter::pipe_t>(recv_conn);
                if (NULL == pipe_conn) {
                    res = EN_ATBUS_ERR_PIPE_CONNECT_FAILED;
                    break;
                }

                uv_pipe_init(req->loop, pipe_conn, 1);
                if (0 != (channel->error_code = uv_accept(req, recv_conn.get()))) {
                    res = EN_ATBUS_ERR_PIPE_CONNECT_FAILED;
                    break;
                }

                conn = io_stream_make_connection(
                    channel,
                    recv_conn
                );

                if (!conn) {
                    // 拿一个东西来保存handle,否则会在close回调前释放handle
                    io_stream_connect_async_data* async_data = new io_stream_connect_async_data();
                    async_data->stream = recv_conn;
                    recv_conn->data = async_data;

                    uv_close(reinterpret_cast<adapter::handle_t*>(recv_conn.get()), io_stream_connect_on_failed_close);
                    res = EN_ATBUS_ERR_PIPE_CONNECT_FAILED;
                    break;
                }

                conn->status = io_stream_connection::EN_ST_CONNECTED;

                io_stream_pipe_setup(channel, pipe_conn);
                io_stream_pipe_init(channel, conn.get(), pipe_conn);

                char pipe_path[MAX_PATH] = { 0 };
                size_t path_len = sizeof(pipe_path);
                uv_pipe_getpeername(pipe_conn, pipe_path, &path_len);
                make_address("unix", pipe_path, 0, conn->addr);

            } while (false);

            // 回调函数，如果发起连接接口调用成功一定要调用回调函数
            io_stream_channel_callback(io_stream_callback_evt_t::EN_FN_ACCEPTED, channel, conn_raw_ptr, conn.get(), channel->error_code, res, NULL, 0);
        }

        // listen 接口传入域名时的回调异步数据
        struct io_stream_dns_async_data {
            io_stream_channel* channel;
            channel_address_t addr;
            io_stream_callback_t callback;
            uv_getaddrinfo_t req;
        };

        // listen 接口传入域名时的回调
        static void io_stream_dns_connection_cb(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
            io_stream_dns_async_data* async_data = NULL;
            int listen_res = status;
            do {
                async_data = reinterpret_cast<io_stream_dns_async_data*>(req->data);
                async_data->channel->error_code = status;
                assert(async_data);

                if (0 != status) {
                    break;
                }

                if (NULL == async_data) {
                    listen_res = -1;
                    break;
                }

                if (NULL == res) {
                    listen_res = -1;
                    break;
                }

                if(AF_INET == res->ai_family) {
                    sockaddr_in* res_c = reinterpret_cast<sockaddr_in*>(res->ai_addr);
                    char ip[17] = { 0 };
                    uv_ip4_name(res_c, ip, sizeof(ip));
                    make_address("ipv4", ip, async_data->addr.port, async_data->addr);
                    listen_res = io_stream_listen(async_data->channel, async_data->addr, async_data->callback);
                } else if (AF_INET6 == res->ai_family) {
                    sockaddr_in6* res_c = reinterpret_cast<sockaddr_in6*>(res->ai_addr);
                    char ip[40] = { 0 };
                    uv_ip6_name(res_c, ip, sizeof(ip));
                    make_address("ipv6", ip, async_data->addr.port, async_data->addr);
                    listen_res = io_stream_listen(async_data->channel, async_data->addr, async_data->callback);
                } else {
                    listen_res = -1;
                }
            } while (false);

            // 接口调用不成功则要调用回调函数
            if (0 != listen_res) {
                io_stream_channel_callback(io_stream_callback_evt_t::EN_FN_CONNECTED, async_data->channel, async_data->callback, NULL, listen_res, EN_ATBUS_ERR_DNS_GETADDR_FAILED, res, 0);
            }

            if (NULL != async_data) {
                delete async_data;
            }

            if (NULL != res) {
                uv_freeaddrinfo(res);
            }
        }

        int io_stream_listen(io_stream_channel* channel, const channel_address_t& addr, io_stream_callback_t callback) {
            if (NULL == channel) {
                return EN_ATBUS_ERR_PARAMS;
            }

            adapter::loop_t* ev_loop = io_stream_get_loop(channel);
            if (NULL == ev_loop) {
                return EN_ATBUS_ERR_MALLOC;
            }

            // socket 
            if (0 == ATBUS_FUNC_STRNCASE_CMP("ipv4", addr.scheme.c_str(), 4) || 
                0 == ATBUS_FUNC_STRNCASE_CMP("ipv6", addr.scheme.c_str(), 4)) {
                std::shared_ptr<adapter::stream_t> listen_conn;
                adapter::tcp_t* handle = io_stream_make_stream_ptr<adapter::tcp_t>(listen_conn);
                if (NULL == handle) {
                    return EN_ATBUS_ERR_MALLOC;
                }

                uv_tcp_init(ev_loop, handle);
                int ret = EN_ATBUS_ERR_SUCCESS;
                do {
                    io_stream_tcp_setup(channel, handle);
                    
                    if ('4' == addr.scheme[3]) {
                        sockaddr_in sock_addr;
                        uv_ip4_addr(addr.host.c_str(), addr.port, &sock_addr);
                        if (0 != (channel->error_code = uv_tcp_bind(handle, reinterpret_cast<const sockaddr*>(&sock_addr), 0))) {
                            ret = EN_ATBUS_ERR_SOCK_BIND_FAILED;
                            break;
                        }

                        if (0 != (channel->error_code = uv_listen(reinterpret_cast<adapter::stream_t*>(handle), channel->conf.backlog, io_stream_tcp_connection_cb))) {
                            ret = EN_ATBUS_ERR_SOCK_LISTEN_FAILED;
                            break;
                        }

                    } else {
                        sockaddr_in6 sock_addr;
                        uv_ip6_addr(addr.host.c_str(), addr.port, &sock_addr);
                        if (0 != (channel->error_code = uv_tcp_bind(handle, reinterpret_cast<const sockaddr*>(&sock_addr), 0))) {
                            ret = EN_ATBUS_ERR_SOCK_BIND_FAILED;
                            break;
                        }

                        if (0 != (channel->error_code = uv_listen(reinterpret_cast<adapter::stream_t*>(handle), channel->conf.backlog, io_stream_tcp_connection_cb))) {
                            ret = EN_ATBUS_ERR_SOCK_LISTEN_FAILED;
                            break;
                        }
                    }

                    std::shared_ptr<io_stream_connection> conn = io_stream_make_connection(channel, listen_conn);
                    if (!conn) {
                        ret = EN_ATBUS_ERR_MALLOC;
                        break;
                    }
                    conn->addr = addr;
                    conn->evt.callbacks[io_stream_callback_evt_t::EN_FN_ACCEPTED] = callback;
                    conn->status = io_stream_connection::EN_ST_CONNECTED;

                    io_stream_tcp_init(channel, conn.get(), handle);
                    io_stream_channel_callback(io_stream_callback_evt_t::EN_FN_CONNECTED, channel, callback, conn.get(), 0, ret, NULL, 0);
                    return ret;
                } while (false);

                uv_close(reinterpret_cast<uv_handle_t*>(handle), io_stream_connection_on_close);
                return ret;
            } else if (0 == ATBUS_FUNC_STRNCASE_CMP("unix", addr.scheme.c_str(), 4)) {
                std::shared_ptr<adapter::stream_t> listen_conn;
                adapter::pipe_t* handle = io_stream_make_stream_ptr<adapter::pipe_t>(listen_conn);
                uv_pipe_init(ev_loop, handle, 1);
                int ret = EN_ATBUS_ERR_SUCCESS;
                do {
                    if (0 != (channel->error_code = uv_pipe_bind(handle, addr.host.c_str()))) {
                        ret = EN_ATBUS_ERR_PIPE_BIND_FAILED;
                        break;
                    }

                    io_stream_pipe_setup(channel, handle);
                    if (0 != (channel->error_code = uv_listen(reinterpret_cast<adapter::stream_t*>(handle), channel->conf.backlog, io_stream_pipe_connection_cb))) {
                        ret = EN_ATBUS_ERR_PIPE_LISTEN_FAILED;
                        break;
                    }

                    std::shared_ptr<io_stream_connection> conn = io_stream_make_connection(channel, listen_conn);
                    if (!conn) {
                        ret = EN_ATBUS_ERR_MALLOC;
                        break;
                    }

                    conn->addr = addr;
                    conn->evt.callbacks[io_stream_callback_evt_t::EN_FN_ACCEPTED] = callback;
                    conn->status = io_stream_connection::EN_ST_CONNECTED;

                    io_stream_pipe_init(channel, conn.get(), handle);
                    io_stream_channel_callback(io_stream_callback_evt_t::EN_FN_CONNECTED, channel, callback, conn.get(), 0, ret, NULL, 0);
                    return ret;
                } while (false);

                uv_close(reinterpret_cast<uv_handle_t*>(handle), io_stream_connection_on_close);
                return ret;
            } else if (0 == ATBUS_FUNC_STRNCASE_CMP("dns", addr.scheme.c_str(), 3)) {
                io_stream_dns_async_data* async_data = new io_stream_dns_async_data();
                if (NULL == async_data) {
                    return EN_ATBUS_ERR_MALLOC;
                }
                async_data->channel = channel;
                async_data->addr = addr;
                async_data->callback = callback;
                async_data->req.data = async_data;

                
                if(0 != uv_getaddrinfo(ev_loop, &async_data->req, io_stream_dns_connection_cb, addr.host.c_str(), NULL, NULL)) {
                    delete async_data;
                    return EN_ATBUS_ERR_DNS_GETADDR_FAILED;
                }

                return EN_ATBUS_ERR_SUCCESS;
            }

            return EN_ATBUS_ERR_SCHEME;
        }

        static void io_stream_all_connected_cb(uv_connect_t* req, int status) {
            io_stream_connect_async_data* async_data = reinterpret_cast<io_stream_connect_async_data*>(req->data);
            assert(async_data);
            assert(async_data->channel);

            int errcode = EN_ATBUS_ERR_SUCCESS;
            async_data->channel->error_code = status;
            std::shared_ptr<io_stream_connection> conn;
            do {
                if (0 != status) {
                    if (async_data->pipe) {
                        errcode = EN_ATBUS_ERR_PIPE_CONNECT_FAILED;
                    } else {
                        errcode = EN_ATBUS_ERR_SOCK_CONNECT_FAILED;
                    }

                    break;
                }

                conn = io_stream_make_connection(async_data->channel, async_data->stream);
                if (!conn) {
                    errcode = EN_ATBUS_ERR_MALLOC;
                    break;
                }
                conn->addr = async_data->addr;

                if (async_data->pipe) {
                    io_stream_pipe_init(async_data->channel, conn.get(), reinterpret_cast<adapter::pipe_t*>(req->handle));
                } else {
                    io_stream_tcp_init(async_data->channel, conn.get(), reinterpret_cast<adapter::tcp_t*>(req->handle));
                }

                conn->status = io_stream_connection::EN_ST_CONNECTED;
            } while(false);

            io_stream_channel_callback(io_stream_callback_evt_t::EN_FN_CONNECTED, async_data->channel, async_data->callback, conn.get(), status, errcode, NULL, 0);

            // 如果连接成功，async_data->stream的生命周期由conn接管
            // 如果失败，需要关闭handle并在回调之后删除async_data。所以这时候不能直接
            // delete async_data;
            // 需要等关闭回调之后移除
            if(conn) {
                delete async_data;
            } else {
                // 只有这里走特殊的流程
                async_data->stream->data = async_data;
                uv_close(reinterpret_cast<uv_handle_t*>(async_data->stream.get()), io_stream_connect_on_failed_close);
            }
        }

        // listen 接口传入域名时的回调
        static void io_stream_dns_connect_cb(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
            io_stream_dns_async_data* async_data = NULL;
            int listen_res = status;
            do {
                async_data = reinterpret_cast<io_stream_dns_async_data*>(req->data);
                async_data->channel->error_code = status;
                assert(async_data);

                if (0 != status) {
                    break;
                }

                if (NULL == async_data) {
                    listen_res = -1;
                    break;
                }

                if (NULL == res) {
                    listen_res = -1;
                    break;
                }

                if (AF_INET == res->ai_family) {
                    sockaddr_in* res_c = reinterpret_cast<sockaddr_in*>(res->ai_addr);
                    char ip[17] = { 0 };
                    uv_ip4_name(res_c, ip, sizeof(ip));
                    make_address("ipv4", ip, async_data->addr.port, async_data->addr);
                    listen_res = io_stream_connect(async_data->channel, async_data->addr, async_data->callback);
                } else if (AF_INET6 == res->ai_family) {
                    sockaddr_in6* res_c = reinterpret_cast<sockaddr_in6*>(res->ai_addr);
                    char ip[40] = { 0 };
                    uv_ip6_name(res_c, ip, sizeof(ip));
                    make_address("ipv6", ip, async_data->addr.port, async_data->addr);
                    listen_res = io_stream_connect(async_data->channel, async_data->addr, async_data->callback);
                } else {
                    listen_res = -1;
                }
            } while (false);

            // 接口调用不成功则要调用回调函数
            if (0 != listen_res) {
                io_stream_channel_callback(io_stream_callback_evt_t::EN_FN_CONNECTED, async_data->channel, async_data->callback, NULL, listen_res, EN_ATBUS_ERR_DNS_GETADDR_FAILED, res, 0);
            }

            if (NULL != async_data) {
                delete async_data;
            }

            if (NULL != res) {
                uv_freeaddrinfo(res);
            }
        }

        int io_stream_connect(io_stream_channel* channel, const channel_address_t& addr, io_stream_callback_t callback) {
            if (NULL == channel) {
                return EN_ATBUS_ERR_PARAMS;
            }

            adapter::loop_t* ev_loop = io_stream_get_loop(channel);
            if (NULL == ev_loop) {
                return EN_ATBUS_ERR_MALLOC;
            }

            // socket 
            if (0 == ATBUS_FUNC_STRNCASE_CMP("ipv4", addr.scheme.c_str(), 4) ||
                0 == ATBUS_FUNC_STRNCASE_CMP("ipv6", addr.scheme.c_str(), 4)) {
                std::shared_ptr<adapter::stream_t> sock_conn;
                adapter::tcp_t* handle = io_stream_make_stream_ptr<adapter::tcp_t>(sock_conn);
                if (NULL == handle) {
                    return EN_ATBUS_ERR_MALLOC;
                }

                uv_tcp_init(ev_loop, handle);

                int ret = EN_ATBUS_ERR_SUCCESS;
                io_stream_connect_async_data* async_data = NULL;
                do {
                    async_data = new io_stream_connect_async_data();
                    if (NULL == async_data) {
                        ret = EN_ATBUS_ERR_MALLOC;
                        break;
                    }
                    
                    async_data->pipe = false;
                    async_data->addr = addr;
                    async_data->channel = channel;
                    async_data->callback = callback;
                    async_data->req.data = async_data;
                    async_data->stream = sock_conn;

                    io_stream_sockaddr_switcher sock_addr;
                    const sockaddr* sock_addr_ptr = NULL;

                    if ('4' == addr.scheme[3]) {
                        uv_ip4_addr(addr.host.c_str(), addr.port, &sock_addr.ipv4);
                        sock_addr_ptr = &sock_addr.base;
                    } else {
                        uv_ip6_addr(addr.host.c_str(), addr.port, &sock_addr.ipv6);
                        sock_addr_ptr = &sock_addr.base;
                    }

                    io_stream_tcp_setup(channel, handle);
                    if(0 != uv_tcp_connect(&async_data->req, handle, sock_addr_ptr, io_stream_all_connected_cb)) {
                        ret = EN_ATBUS_ERR_SOCK_CONNECT_FAILED;
                        break;
                    }

                    //conn_req = NULL; // 防止异常情况会调用回调时，任然释放对象
                    return ret;
                } while (false);

                // 回收
                if (NULL != async_data) {
                    delete async_data;
                }

                uv_close(reinterpret_cast<uv_handle_t*>(handle), io_stream_connection_on_close);
                return ret;
            } else if (0 == ATBUS_FUNC_STRNCASE_CMP("unix", addr.scheme.c_str(), 4)) {
                std::shared_ptr<adapter::stream_t> pipe_conn;
                adapter::pipe_t* handle = io_stream_make_stream_ptr<adapter::pipe_t>(pipe_conn);
                if (NULL == handle) {
                    return EN_ATBUS_ERR_MALLOC;
                }
                
                uv_pipe_init(ev_loop, handle, 1);
                
                int ret = EN_ATBUS_ERR_SUCCESS;
                io_stream_connect_async_data* async_data = NULL;
                do {
                    async_data = new io_stream_connect_async_data();
                    if (NULL == async_data) {
                        ret = EN_ATBUS_ERR_MALLOC;
                        break;
                    }
                    async_data->pipe = true;
                    async_data->addr = addr;
                    async_data->channel = channel;
                    async_data->callback = callback;
                    async_data->req.data = async_data;
                    async_data->stream = pipe_conn;

                    // 不会失败
                    io_stream_pipe_setup(channel, handle);
                    uv_pipe_connect(&async_data->req, handle, addr.host.c_str(), io_stream_all_connected_cb);
                    
                    return ret;
                } while (false);

                // 回收
                if (NULL != async_data) {
                    delete async_data;
                }
                
                uv_close(reinterpret_cast<uv_handle_t*>(handle), io_stream_connection_on_close);
                return ret;
                
            } else if (0 == ATBUS_FUNC_STRNCASE_CMP("dns", addr.scheme.c_str(), 3)) {
                io_stream_dns_async_data* async_data = new io_stream_dns_async_data();
                if (NULL == async_data) {
                    return EN_ATBUS_ERR_MALLOC;
                }
                async_data->channel = channel;
                async_data->addr = addr;
                async_data->callback = callback;
                async_data->req.data = async_data;

                if (0 != uv_getaddrinfo(ev_loop, &async_data->req, io_stream_dns_connect_cb, addr.host.c_str(), NULL, NULL)) {
                    delete async_data;
                    return EN_ATBUS_ERR_DNS_GETADDR_FAILED;
                }

                return EN_ATBUS_ERR_SUCCESS;
            }

            return EN_ATBUS_ERR_SUCCESS;
        }

        int io_stream_disconnect(io_stream_channel* channel, io_stream_connection* connection, io_stream_callback_t callback) {
            if (NULL == channel || NULL == connection) {
                return EN_ATBUS_ERR_PARAMS;
            }

            connection->act_disc_cbk = callback;

            if (0 == uv_is_closing(reinterpret_cast<uv_handle_t*>(connection->handle.get()))) {
                uv_close(reinterpret_cast<uv_handle_t*>(connection->handle.get()), io_stream_connection_on_close);
            }
            return EN_ATBUS_ERR_SUCCESS;
        }

        int io_stream_disconnect_fd(io_stream_channel* channel, adapter::fd_t fd, io_stream_callback_t callback) {
            if (NULL == channel) {
                return EN_ATBUS_ERR_PARAMS;
            }

            io_stream_channel::conn_pool_t::iterator iter = channel->conn_pool.find(fd);
            if(iter == channel->conn_pool.end()) {
                return EN_ATBUS_ERR_CONNECTION_NOT_FOUND;
            }

            return io_stream_disconnect(channel, iter->second.get(), callback);
        }

        static void io_stream_on_written_fn(uv_write_t* req, int status) {
            // 这里之后不会再调用req，req放在缓冲区内，可以正常释放了
            // 只要uv_write2返回0，这里都会回调。无论是否真的发送成功。所以这里必须释放内存块

            io_stream_connection* connection = reinterpret_cast<io_stream_connection*>(req->data);

            void* data = NULL;
            size_t nread, nwrite;

            // 弹出丢失的回调
            while(true) {
                connection->write_buffers.front(data, nread, nwrite);
                if (NULL == data) {
                    break;
                }

                if(0 == nwrite) {
                    connection->write_buffers.pop_front(0, true);
                }

                assert(0 == nread);
                assert(req == data);

                // nwrite = uv_write_t的大小+crc32+vint的大小+数据区长度
                char* buff_start = reinterpret_cast<char*>(data);
                buff_start += sizeof(uv_write_t) + sizeof(uint32_t);
                uint64_t out;
                size_t vint_len = detail::fn::read_vint(out, buff_start, nwrite - sizeof(uv_write_t) - sizeof(uint32_t));

                assert(out == nwrite - vint_len - sizeof(uint32_t) - sizeof(uv_write_t));

                io_stream_channel_callback(
                    io_stream_callback_evt_t::EN_FN_WRITEN,
                    connection->channel,
                    connection,
                    status,
                    req == data? EN_ATBUS_ERR_SUCCESS: EN_ATBUS_ERR_NODE_TIMEOUT,
                    buff_start + vint_len,
                    out
                );

                // 消除缓存
                connection->write_buffers.pop_front(nwrite, true);

                // 弹出结束
                if (req == data) {
                    break;
                }
            }
            // libuv内部维护了一个发送队列，所以不需要再启动发送流程
        }

        int io_stream_send(io_stream_connection* connection, const void* buf, size_t len) {
            if (NULL == connection) {
                return EN_ATBUS_ERR_PARAMS;
            }

            if (connection->channel->conf.send_buffer_limit_size > 0 && len > connection->channel->conf.send_buffer_limit_size) {
                return EN_ATBUS_ERR_INVALID_SIZE;
            }

            char vint[16];
            size_t vint_len = detail::fn::write_vint(len, vint, sizeof(vint));
            // 计算需要的内存块大小（uv_write_t的大小+crc32+vint的大小+len）
            size_t total_buffer_size = sizeof(uv_write_t) + sizeof(uint32_t) + vint_len + len;

            // 判定内存限制
            void* data;
            int res = connection->write_buffers.push_back(data, total_buffer_size);
            if (res < 0) {
                return res;
            }

            // 初始化req，填充vint，复制数据区
            uv_write_t* req = reinterpret_cast<uv_write_t*>(data);
            req->data = connection;
            char* buff_start = reinterpret_cast<char*>(data);

            // req
            buff_start += sizeof(uv_write_t);

            // crc32
            uint32_t crc32 = atbus::detail::crc32(0, reinterpret_cast<const unsigned char*>(buf), len);
            memcpy(buff_start, &crc32, sizeof(uint32_t));

            // vint
            memcpy(buff_start + sizeof(uint32_t), vint, vint_len);
            // buffer
            memcpy(buff_start + sizeof(uint32_t) + vint_len, buf, len);

            // 调用写出函数，bufs[]会在libuv内部复制
            uv_buf_t bufs[1] = { uv_buf_init(buff_start, static_cast<unsigned int>(total_buffer_size - sizeof(uv_write_t))) };
            res = uv_write(req, connection->handle.get(), bufs, 1, io_stream_on_written_fn);
            if (0 != res) {
                connection->channel->error_code = res;
                connection->write_buffers.pop_back(total_buffer_size, true);
                return EN_ATBUS_ERR_WRITE_FAILED;
            }

            // libuv调用失败时，直接返回底层错误。因为libuv内部也维护了一个发送队列，所以不会受到TCP发送窗口的限制
            return EN_ATBUS_ERR_SUCCESS;
        }

        void io_stream_show_channel(io_stream_channel* channel, std::ostream& out) {
            if (NULL == channel) {
                return;
            }

            out << "summary:" << std::endl <<
                "connection number: " << channel->conn_pool.size() << std::endl <<
                std::endl;

            out << "configure:" << std::endl <<
                "is_noblock: " << channel->conf.is_noblock << std::endl <<
                "is_nodelay: " << channel->conf.is_nodelay << std::endl <<
                "backlog: " << channel->conf.backlog << std::endl <<
                "keepalive: " << channel->conf.keepalive << std::endl <<
                "recv_buffer_limit_size(Bytes): " << channel->conf.recv_buffer_limit_size << std::endl <<
                "recv_buffer_max_size(Bytes): " << channel->conf.recv_buffer_max_size << std::endl <<
                "recv_buffer_static_max_number: " << channel->conf.recv_buffer_static << std::endl <<
                "send_buffer_limit_size(Bytes): " << channel->conf.send_buffer_limit_size << std::endl <<
                "send_buffer_max_size(Bytes): " << channel->conf.send_buffer_max_size << std::endl <<
                "send_buffer_static_max_number: " << channel->conf.send_buffer_static << std::endl <<
                std::endl;

            out << "all connections:" << std::endl;
            for (io_stream_channel::conn_pool_t::iterator iter = channel->conn_pool.begin();
                iter != channel->conn_pool.end(); ++ iter) {
                out << "\t" << iter->second->addr.address<< ":(status = "<< iter->second->status << ")" << std::endl;

                out << "\t\twrite_buffers.cost_number: " << iter->second->write_buffers.limit().cost_number_ << std::endl;
                out << "\t\twrite_buffers.cost_size: " << iter->second->write_buffers.limit().cost_size_ << std::endl;
                out << "\t\twrite_buffers.limit_number: " << iter->second->write_buffers.limit().limit_number_ << std::endl;
                out << "\t\twrite_buffers.limit_size: " << iter->second->write_buffers.limit().limit_size_ << std::endl;

                out << "\t\tread_buffers.cost_number: " << iter->second->read_buffers.limit().cost_number_ << std::endl;
                out << "\t\tread_buffers.cost_size: " << iter->second->read_buffers.limit().cost_size_ << std::endl;
                out << "\t\tread_buffers.limit_number: " << iter->second->read_buffers.limit().limit_number_ << std::endl;
                out << "\t\tread_buffers.limit_size: " << iter->second->read_buffers.limit().limit_size_ << std::endl;
            }
        }
    }
}
