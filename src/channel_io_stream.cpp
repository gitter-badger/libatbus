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
#include <vector>

#include "detail/std/smart_ptr.h"

#include "detail/libatbus_error.h"
#include "detail/libatbus_channel_export.h"
#include "detail/buffer.h"

#ifdef ATBUS_MACRO_ENABLE_STATIC_ASSERT
#include <type_traits>
#include <detail/libatbus_channel_types.h>
#include <uv-win.h>

#endif

namespace atbus {
    namespace channel {

#ifdef ATBUS_MACRO_ENABLE_STATIC_ASSERT
        static_assert(std::is_pod<io_stream_conf>::value, "io_stream_conf should be a pod type");
#endif

        static inline void io_stream_channel_callback(
            io_stream_callback_evt_t::mem_fn_t fn, io_stream_channel* channel,
            io_stream_connection* connection, int status,
            void* priv_data, size_t s
        ) {
            if (NULL != channel && NULL != channel->evt.callbacks[fn]) {
                channel->evt.callbacks[fn](channel, connection, status, priv_data, s);
            }

            if (NULL != connection && NULL != connection->evt.callbacks[fn]) {
                channel->evt.callbacks[fn](channel, connection, status, priv_data, s);
            }
        }

        void io_stream_init_configure(io_stream_conf* conf) {
            if (NULL == conf) {
                return;
            }

            conf->keepalive = 60;
            conf->is_noblock = true;
            conf->is_nodelay = true;
            conf->send_buffer_static = false;
            conf->recv_buffer_static = false;

            conf->send_buffer_max_size = 0;
            conf->send_buffer_limit_size = ATBUS_MACRO_MSG_LIMIT;

            conf->recv_buffer_max_size = 0;
            conf->recv_buffer_limit_size = ATBUS_MACRO_MSG_LIMIT;

            conf->confirm_timeout = ATBUS_MACRO_CONNECTION_CONFIRM_TIMEOUT;
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

            memset(channel->evt.callbacks, NULL, sizeof(channel->evt.callbacks));

            channel->error_code = 0;
            return EN_ATBUS_ERR_SUCCESS;
        }

        int io_stream_close(io_stream_channel* channel) {
            if (NULL == channel) {
                return EN_ATBUS_ERR_PARAMS;
            }

            // 释放所有连接
            {
                std::vector<adapter::fd_t> pending_release;
                pending_release.reserve(channel->conn_pool.size());
                for (io_stream_channel::conn_pool_t::iterator iter = channel->conn_pool.begin();
                     iter != channel->conn_pool.end(); ++iter) {
                    pending_release.push_back(iter->first);
                }

                for (size_t i = 0; i < pending_release.size(); ++i) {
                    io_stream_disconnect_fd(channel, pending_release[i], NULL);
                }
            }

            if (true == channel->is_loop_owner && NULL != channel->ev_loop) {
                uv_loop_close(channel->ev_loop);
                free(channel->ev_loop);
            }

            channel->ev_loop = NULL;
            return EN_ATBUS_ERR_SUCCESS;
        }


        static void io_stream_on_recv_alloc_fn(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
            io_stream_connection* conn_raw_ptr = reinterpret_cast<io_stream_connection*>(handle->data);
            assert(conn_raw_ptr);
            io_stream_channel* channel = conn_raw_ptr->channel;
            assert(channel);

            void* data = NULL;
            size_t sread = 0, swrite = 0;
            conn_raw_ptr->read_buffers.back(data, sread, swrite);

            // 正在读取vint时，指定缓冲区为head内存块
            if (NULL == data || 0 == swrite) {
                buf->len = sizeof(conn_raw_ptr->read_head.buffer) - conn_raw_ptr->read_head.len;

                if (0 == buf->len) {
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

            if (nread <= 0) {
                channel->error_code = nread;
                io_stream_channel_callback(
                    io_stream_callback_evt_t::EN_FN_RECVED,
                    channel,
                    conn_raw_ptr,
                    UV_EOF == nread? EN_ATBUS_ERR_EOF: EN_ATBUS_ERR_READ_FAILED,
                    NULL,
                    0
                );
                return;
            }

            void *data = NULL;
            size_t sread = 0, swrite = 0;
            conn_raw_ptr->read_buffers.back(data, sread, swrite);

            // head 阶段
            if (NULL == data || 0 == swrite) {
                assert(nread <= sizeof(conn_raw_ptr->read_head.buffer) - conn_raw_ptr->read_head.len);
                conn_raw_ptr->read_head.len += static_cast<size_t>(nread); // 写数据计数

                // 尝试解出所有的head数据
                char* buff_start = conn_raw_ptr->read_head.buffer;
                size_t buff_left_len = conn_raw_ptr->read_head.len;

                // 可能包含多条消息
                while (buff_left_len > 0) {
                    uint64_t msg_len = 0;
                    size_t vint_len = detail::fn::read_vint(msg_len, buff_start, buff_left_len);

                    // 剩余数据不足以解动态长度整数，直接中断退出
                    if (0 == vint_len) {
                        break;
                    }

                    // 如果读取vint成功，判定是否有小数据包。并对小数据包直接回调
                    if (buff_left_len >= vint_len + msg_len) {
                        channel->error_code = 0;
                        io_stream_channel_callback(
                            io_stream_callback_evt_t::EN_FN_RECVED,
                            channel,
                            conn_raw_ptr,
                            EN_ATBUS_ERR_SUCCESS,
                            buff_start + vint_len,
                            msg_len
                        );

                        buff_start += vint_len + msg_len;
                        buff_left_len -= vint_len + msg_len;
                    } else {
                        // 大数据包，使用缓冲区，并且剩余数据一定是在一个包内
                        if (EN_ATBUS_ERR_SUCCESS == conn_raw_ptr->read_buffers.push_back(data, msg_len)) {
                            memcpy(data, buff_start + vint_len, buff_left_len - vint_len);

                            buff_start += buff_left_len;
                            buff_left_len = 0; // 循环退出
                        } else {
                            // 追加大缓冲区失败，可能是到达缓冲区限制
                            // TODO 读缓冲区一般只有一个正在处理的数据包，如果发生创建失败则是数据错误或者这个包就是超出大小限制的
                            // TODO 这时候是否应该通知上层然后直接断开连接？
                            // 强制中断
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
                io_stream_channel_callback(
                    io_stream_callback_evt_t::EN_FN_RECVED,
                    channel,
                    conn_raw_ptr,
                    EN_ATBUS_ERR_SUCCESS,
                    data,
                    sread
                );

                // 回调并释放缓冲区
                conn_raw_ptr->read_buffers.pop_front(0, true);
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
            adapter::fd_t fd;
            io_stream_connection* conn_raw_ptr = reinterpret_cast<io_stream_connection*>(handle->data);
            assert(conn_raw_ptr);
            io_stream_channel* channel = conn_raw_ptr->channel;
            assert(channel);

            io_stream_channel::conn_pool_t::iterator iter = channel->conn_pool.end();
            if (0 == uv_fileno(handle, &fd)) {
                 iter = channel->conn_pool.find(fd);
            } else {
                // 出现错误，需要使用其他方式查找handle
                for (iter = channel->conn_pool.begin(); iter != channel->conn_pool.end(); ++ iter) {
                    if (reinterpret_cast<uv_handle_t*>(iter->second->handle.get()) == handle) {
                        break;
                    }
                }
            }

            if (iter != channel->conn_pool.end()) {
                iter->second->status = io_stream_connection::EN_ST_DISCONNECTIED;
                io_stream_channel_callback(io_stream_callback_evt_t::EN_FN_DISCONNECTED, channel, iter->second.get(), 0, NULL, 0);
                channel->conn_pool.erase(iter);
            }
        }

        static std::shared_ptr<io_stream_connection>
            io_stream_make_connection(io_stream_channel* channel, std::shared_ptr<adapter::stream_t> handle) {
            std::shared_ptr<io_stream_connection> ret;
            if (NULL == channel) {
                return ret;
            }

            adapter::fd_t fd;
            if (0 != uv_fileno(reinterpret_cast<const uv_handle_t*>(handle.get()), &fd)) {
                return ret;
            }

            ret = std::make_shared<io_stream_connection>();
            ret->handle = handle;
            memset(ret->evt.callbacks, NULL, sizeof(ret->evt.callbacks));
            ret->status = io_stream_connection::EN_ST_CREATED;


            ret->read_buffers.set_limit(channel->conf.recv_buffer_max_size, 0);
            if (channel->conf.recv_buffer_static) {
                ret->read_buffers.set_mode(channel->conf.recv_buffer_max_size, 0);
            }
            ret->read_head.len = 0;

            ret->write_buffers.set_limit(channel->conf.send_buffer_max_size, 0);
            if (channel->conf.send_buffer_static) {
                ret->write_buffers.set_mode(channel->conf.send_buffer_max_size, 0);
            }

            channel->conn_pool[fd] = ret;
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

            if( 0 == uv_is_closing(reinterpret_cast<adapter::handle_t*>(handle))) {
                uv_close(reinterpret_cast<adapter::handle_t*>(handle), NULL);
            }

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
            if (NULL == tcp_conn || 0 != uv_accept(req, recv_conn.get())) {
                return NULL;
            }

            conn = io_stream_make_connection(
                channel,
                recv_conn
            );

            if (!conn) {
                uv_close(reinterpret_cast<adapter::handle_t*>(recv_conn.get()), NULL);
                return NULL;
            }

            io_stream_tcp_setup(channel, tcp_conn);
            io_stream_tcp_init(channel, conn.get(), tcp_conn);
            return tcp_conn;
        }

        // ipv4 收到连接
        static void io_stream_tcp_connection_cb_ipv4(uv_stream_t* req, int status) {
            io_stream_connection* conn_raw_ptr = reinterpret_cast<io_stream_connection*>(req->data);
            assert(conn_raw_ptr);
            io_stream_channel* channel = conn_raw_ptr->channel;
            assert(channel);

            std::shared_ptr<adapter::stream_t> recv_conn;
            std::shared_ptr<io_stream_connection> conn;

            do {
                adapter::tcp_t* tcp_conn = io_stream_tcp_connection_common(conn, req, status);
                if (NULL == tcp_conn || !conn) {
                    break;
                }

                conn->status = io_stream_connection::EN_ST_CONNECTED;

                struct sockaddr_in sock_addr;
                int name_len = sizeof(sockaddr_in);
                uv_tcp_getpeername(tcp_conn, reinterpret_cast<sockaddr*>(&sock_addr), &name_len);

                char ip[17] = { 0 };
                uv_ip4_name(&sock_addr, ip, sizeof(ip));
                make_address("ipv4", ip, sock_addr.sin_port, conn->addr);
            } while (false);

            // 回调函数，如果发起连接接口调用成功一定要调用回调函数
            io_stream_channel_callback(io_stream_callback_evt_t::EN_FN_ACCEPTED, channel, conn.get(), status, NULL, 0);
        }

        // ipv6 收到连接
        static void io_stream_tcp_connection_cb_ipv6(uv_stream_t* req, int status) {
            io_stream_connection* conn_raw_ptr = reinterpret_cast<io_stream_connection*>(req->data);
            assert(conn_raw_ptr);
            io_stream_channel* channel = conn_raw_ptr->channel;
            assert(channel);

            std::shared_ptr<adapter::stream_t> recv_conn;
            std::shared_ptr<io_stream_connection> conn;

            do {
                adapter::tcp_t* tcp_conn = io_stream_tcp_connection_common(conn, req, status);
                if (NULL == tcp_conn || !conn) {
                    break;
                }

                conn->status = io_stream_connection::EN_ST_CONNECTED;

                struct sockaddr_in6 sock_addr;
                int name_len = sizeof(sockaddr_in6);
                uv_tcp_getpeername(tcp_conn, reinterpret_cast<sockaddr*>(&sock_addr), &name_len);

                char ip[40] = { 0 };
                uv_ip6_name(&sock_addr, ip, sizeof(ip));
                make_address("ipv6", ip, sock_addr.sin6_port, conn->addr);
            } while (false);

            // 回调函数，如果发起连接接口调用成功一定要调用回调函数
            io_stream_channel_callback(io_stream_callback_evt_t::EN_FN_ACCEPTED, channel, conn.get(), status, NULL, 0);
        }

        // pipe 收到连接
        static void io_stream_pipe_connection_cb(uv_stream_t* req, int status) {
            io_stream_connection* conn_raw_ptr = reinterpret_cast<io_stream_connection*>(req->data);
            assert(conn_raw_ptr);
            io_stream_channel* channel = conn_raw_ptr->channel;
            assert(channel);

            std::shared_ptr<io_stream_connection> conn;
            do {
                if (0 != status || NULL == channel) {
                    break;
                }

                std::shared_ptr<adapter::stream_t> recv_conn;
                adapter::pipe_t* pipe_conn = io_stream_make_stream_ptr<adapter::pipe_t>(recv_conn);
                if (NULL == pipe_conn || 0 != uv_accept(req, recv_conn.get())) {
                    break;
                }

                conn = io_stream_make_connection(
                    channel,
                    recv_conn
                );

                if (!conn) {
                    uv_close(reinterpret_cast<adapter::handle_t*>(recv_conn.get()), NULL);
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
            io_stream_channel_callback(io_stream_callback_evt_t::EN_FN_ACCEPTED, channel, conn.get(), status, NULL, 0);
        }

        // listen 接口传入域名时的回调异步数据
        struct io_stream_dns_async_data {
            io_stream_channel* channel;
            channel_address_t addr;
            io_stream_callback_t callback;
        };

        // listen 接口传入域名时的回调
        static void io_stream_dns_connection_cb(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
            io_stream_dns_async_data* async_data = NULL;
            int listen_res = 0;
            do {
                async_data = reinterpret_cast<io_stream_dns_async_data*>(req->data);
                assert(async_data);

                if (0 != status) {
                    break;
                }

                if (NULL == async_data) {
                    break;
                }

                if (NULL == res) {
                    break;
                }

                if(AF_INET == res->ai_family) {
                    sockaddr_in* res_c = reinterpret_cast<sockaddr_in*>(res);
                    char ip[17] = { 0 };
                    uv_ip4_name(res_c, ip, sizeof(ip));
                    make_address("ipv4", ip, async_data->addr.port, async_data->addr);
                    listen_res = io_stream_listen(async_data->channel, async_data->addr, async_data->callback);
                } else {
                    sockaddr_in6* res_c = reinterpret_cast<sockaddr_in6*>(res);
                    char ip[40] = { 0 };
                    uv_ip6_name(res_c, ip, sizeof(ip));
                    make_address("ipv6", ip, async_data->addr.port, async_data->addr);
                    listen_res = io_stream_listen(async_data->channel, async_data->addr, async_data->callback);
                }
            } while (false);

            // 接口调用不成功则要调用回调函数
            if (0 != listen_res && NULL != async_data->callback) {
                async_data->callback(async_data->channel, NULL, EN_ATBUS_ERR_DNS_GETADDR_FAILED, NULL, 0);
            }

            if (NULL != req) {
                delete req;
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
                        if (0 != uv_tcp_bind(handle, reinterpret_cast<const sockaddr*>(&sock_addr), 0)) {
                            ret = EN_ATBUS_ERR_SOCK_BIND_FAILED;
                            break;
                        }

                        if (0 != uv_listen(reinterpret_cast<adapter::stream_t*>(handle), channel->conf.backlog, io_stream_tcp_connection_cb_ipv4)) {
                            ret = EN_ATBUS_ERR_SOCK_LISTEN_FAILED;
                            break;
                        }

                    } else {
                        sockaddr_in6 sock_addr;
                        uv_ip6_addr(addr.host.c_str(), addr.port, &sock_addr);
                        if (0 != uv_tcp_bind(handle, reinterpret_cast<const sockaddr*>(&sock_addr), 0)) {
                            ret = EN_ATBUS_ERR_SOCK_BIND_FAILED;
                            break;
                        }

                        if (0 != uv_listen(reinterpret_cast<adapter::stream_t*>(&handle), channel->conf.backlog, io_stream_tcp_connection_cb_ipv6)) {
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
                    conn->evt.callbacks[io_stream_callback_evt_t::EN_FN_CONNECTED] = callback;
                    conn->status = io_stream_connection::EN_ST_CONNECTED;

					io_stream_tcp_init(channel, conn.get(), handle);
                    io_stream_channel_callback(io_stream_callback_evt_t::EN_FN_CONNECTED, channel, conn.get(), 0, NULL, 0);
                    return ret;
                } while (false);

                uv_close(reinterpret_cast<uv_handle_t*>(handle), NULL);
                return ret;
            } else if (0 == ATBUS_FUNC_STRNCASE_CMP("unix", addr.scheme.c_str(), 4)) {
                std::shared_ptr<adapter::stream_t> listen_conn;
                adapter::pipe_t* handle = io_stream_make_stream_ptr<adapter::pipe_t>(listen_conn);
                uv_pipe_init(ev_loop, handle, 1);
                int ret = EN_ATBUS_ERR_SUCCESS;
                do {
                    if (0 != uv_pipe_bind(handle, addr.host.c_str())) {
                        ret = EN_ATBUS_ERR_PIPE_BIND_FAILED;
                        break;
                    }

					io_stream_pipe_setup(channel, handle);
                    if (0 != uv_listen(reinterpret_cast<adapter::stream_t*>(handle), channel->conf.backlog, io_stream_pipe_connection_cb)) {
                        ret = EN_ATBUS_ERR_PIPE_LISTEN_FAILED;
                        break;
                    }

                    std::shared_ptr<io_stream_connection> conn = io_stream_make_connection(channel, listen_conn);
                    if (!conn) {
                        ret = EN_ATBUS_ERR_MALLOC;
                        break;
                    }

                    conn->addr = addr;
                    conn->evt.callbacks[io_stream_callback_evt_t::EN_FN_CONNECTED] = callback;
                    conn->status = io_stream_connection::EN_ST_CONNECTED;

					io_stream_pipe_init(channel, conn.get(), handle);
                    io_stream_channel_callback(io_stream_callback_evt_t::EN_FN_CONNECTED, channel, conn.get(), 0, NULL, 0);
                    return ret;
                } while (false);

                uv_close(reinterpret_cast<uv_handle_t*>(handle), NULL);
                return ret;
            } else if (0 == ATBUS_FUNC_STRNCASE_CMP("dns", addr.scheme.c_str(), 3)) {
                uv_getaddrinfo_t* req = new uv_getaddrinfo_t();
                if (NULL == req) {
                    return EN_ATBUS_ERR_MALLOC;
                }

                io_stream_dns_async_data* async_data = new io_stream_dns_async_data();
                if (NULL == async_data) {
					delete req;
                    return EN_ATBUS_ERR_MALLOC;
                }
                async_data->channel = channel;
                async_data->addr = addr;
                async_data->callback = callback;

                req->data = async_data;
                if(0 != uv_getaddrinfo(ev_loop, req, io_stream_dns_connection_cb, addr.host.c_str(), NULL, NULL)) {
                    delete req;
                    delete async_data;
                    return EN_ATBUS_ERR_DNS_GETADDR_FAILED;
                }

                return EN_ATBUS_ERR_SUCCESS;
            }

            return EN_ATBUS_ERR_SCHEME;
        }

        static struct io_stream_connect_async_data {
            uv_connect_t req;
            channel_address_t addr;
            io_stream_channel* channel;
            io_stream_callback_t callback;
            std::shared_ptr<adapter::stream_t> stream;
            bool pipe;
        };

        static void io_stream_all_connected_cb(uv_connect_t* req, int status) {
            io_stream_connect_async_data* async_data = reinterpret_cast<io_stream_connect_async_data*>(req->data);
            assert(async_data);
            assert(async_data->channel);

            std::shared_ptr<io_stream_connection> conn;
            do {
                conn = io_stream_make_connection(async_data->channel, async_data->stream);
                if (!conn) {
                    status = EN_ATBUS_ERR_MALLOC;
                    break;
                }
                conn->addr = async_data->addr;

                if (async_data->pipe) {
                    io_stream_pipe_init(async_data->channel, conn.get(), reinterpret_cast<adapter::pipe_t*>(req->handle));
                } else {
                    io_stream_tcp_init(async_data->channel, conn.get(), reinterpret_cast<adapter::tcp_t*>(req->handle));
                }

                conn->evt.callbacks[io_stream_callback_evt_t::EN_FN_CONNECTED] = async_data->callback;
                conn->status = io_stream_connection::EN_ST_CONNECTED;
            } while(false);

            if (0 == status) {
                io_stream_channel_callback(io_stream_callback_evt_t::EN_FN_CONNECTED, async_data->channel, conn.get(), status, NULL, 0);
            } else {
                if(conn) {
                    conn->status = io_stream_connection::EN_ST_CREATED;
                }

                io_stream_channel_callback(io_stream_callback_evt_t::EN_FN_CONNECTED, async_data->channel, conn.get(), status, NULL, 0);

                // 释放连接
                io_stream_disconnect(async_data->channel, conn.get(), NULL);
            }

            delete async_data;
        }

        // listen 接口传入域名时的回调
        static void io_stream_dns_connect_cb(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
            io_stream_dns_async_data* async_data = NULL;
            int listen_res = 0;
            do {
                async_data = reinterpret_cast<io_stream_dns_async_data*>(req->data);
                assert(async_data);

                if (0 != status) {
                    break;
                }

                if (NULL == async_data) {
                    break;
                }

                if (NULL == res) {
                    break;
                }

                if (AF_INET == res->ai_family) {
                    sockaddr_in* res_c = reinterpret_cast<sockaddr_in*>(res);
                    char ip[17] = { 0 };
                    uv_ip4_name(res_c, ip, sizeof(ip));
                    make_address("ipv4", ip, async_data->addr.port, async_data->addr);
                    listen_res = io_stream_connect(async_data->channel, async_data->addr, async_data->callback);
                } else {
                    sockaddr_in6* res_c = reinterpret_cast<sockaddr_in6*>(res);
                    char ip[40] = { 0 };
                    uv_ip6_name(res_c, ip, sizeof(ip));
                    make_address("ipv6", ip, async_data->addr.port, async_data->addr);
                    listen_res = io_stream_connect(async_data->channel, async_data->addr, async_data->callback);
                }
            } while (false);

            // 接口调用不成功则要调用回调函数
            if (0 != listen_res && NULL != async_data->callback) {
                async_data->callback(async_data->channel, NULL, EN_ATBUS_ERR_DNS_GETADDR_FAILED, NULL, 0);
            }

            if (NULL != req) {
                delete req;
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

                    sockaddr_in sock_addr;
                    sockaddr_in6 sock_addr6;
                    const sockaddr* sock_addr_ptr = NULL;

                    if ('4' == addr.scheme[3]) {
                        uv_ip4_addr(addr.host.c_str(), addr.port, &sock_addr);
                        sock_addr_ptr = reinterpret_cast<sockaddr*>(&sock_addr);
                    } else {
                        uv_ip6_addr(addr.host.c_str(), addr.port, &sock_addr6);
                        sock_addr_ptr = reinterpret_cast<sockaddr*>(&sock_addr6);
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

                uv_close(reinterpret_cast<uv_handle_t*>(handle), NULL);
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
				
				uv_close(reinterpret_cast<uv_handle_t*>(handle), NULL);
				return ret;
				
            } else if (0 == ATBUS_FUNC_STRNCASE_CMP("dns", addr.scheme.c_str(), 3)) {
                uv_getaddrinfo_t* req = new uv_getaddrinfo_t();
                if (NULL == req) {
                    return EN_ATBUS_ERR_MALLOC;
                }

                io_stream_dns_async_data* async_data = new io_stream_dns_async_data();
                if (NULL == async_data) {
					delete req;
                    return EN_ATBUS_ERR_MALLOC;
                }
                async_data->channel = channel;
                async_data->addr = addr;
                async_data->callback = callback;

                req->data = async_data;
                if (0 != uv_getaddrinfo(ev_loop, req, io_stream_dns_connect_cb, addr.host.c_str(), NULL, NULL)) {
                    delete req;
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

            connection->evt.callbacks[io_stream_callback_evt_t::EN_FN_DISCONNECTED] = callback;

            uv_close( reinterpret_cast<adapter::handle_t*>(connection->handle.get()), NULL);
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
            while(req != data) {
                connection->write_buffers.front(data, nread, nwrite);
                if (NULL == data || 0 == nwrite) {
                    break;
                }

                // nwrite = uv_write_t的大小+vint的大小+数据区长度
                char* buff_start = reinterpret_cast<char*>(data);
                buff_start += sizeof(uv_write_t);
                uint64_t out;
                size_t vint_len = detail::fn::read_vint(out, buff_start, nwrite - sizeof(uv_write_t));

                assert(out == nwrite - vint_len - sizeof(uv_write_t));

                io_stream_channel_callback(
                    io_stream_callback_evt_t::EN_FN_WRITEN,
                    connection->channel,
                    connection,
                    EN_ATBUS_ERR_NODE_TIMEOUT,
                    buff_start + vint_len,
                    vint_len
                );

                // 消除缓存
                connection->write_buffers.pop_front(nwrite, true);
            }
            // libuv内部维护了一个发送队列，所以不需要再启动发送流程
        }

        int io_stream_send(io_stream_connection* connection, const void* buf, size_t len) {
            if (NULL == connection) {
                return EN_ATBUS_ERR_PARAMS;
            }

            char vint[16];
            size_t vint_len = detail::fn::write_vint(len, vint, sizeof(vint));
            // 计算需要的内存块大小（uv_write_t的大小+vint的大小+len）
            size_t total_buffer_size = sizeof(uv_write_t) + vint_len + len;

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

            buff_start += sizeof(uv_write_t);
            memcpy(buff_start, vint, vint_len);

            memcpy(buff_start + vint_len, buf, len);

            // 调用写出函数，bufs[]会在libuv内部复制
            uv_buf_t bufs[1] = { uv_buf_init(buff_start, vint_len + len) };
            res = uv_write(req, connection->handle.get(), bufs, 1, io_stream_on_written_fn);
            if (0 != res) {
                connection->channel->error_code = res;
                connection->write_buffers.pop_back(total_buffer_size, false);
                return EN_ATBUS_ERR_WRITE_FAILED;
            }

            // libuv调用失败时，直接返回底层错误。因为libuv内部也维护了一个发送队列，所以不会受到TCP发送窗口的限制
            return EN_ATBUS_ERR_SUCCESS;
        }
    }
}
