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

namespace atbus {
    namespace channel {
        io_stream_conf* io_stream_malloc_configure() {
            io_stream_conf* conf = reinterpret_cast<io_stream_conf*>(malloc(sizeof(io_stream_conf)));

            conf->is_noblock = true;
            conf->is_nodelay = true;
            conf->is_keepalive = true;

            conf->send_buffer_max_size = 0;
            conf->send_buffer_limit_size = ATBUS_MACRO_MSG_LIMIT;

            conf->recv_buffer_max_size = 0;
            conf->recv_buffer_limit_size = ATBUS_MACRO_MSG_LIMIT;
        }

        void io_stream_free_configure(io_stream_conf* conf) {
            free(conf);
        }

        void io_stream_set_configure_msg_limit(io_stream_conf* conf, size_t recv_size, size_t send_size) {
            conf->send_buffer_limit_size = send_size;
            conf->recv_buffer_limit_size = recv_size;
        }

        void io_stream_set_configure_buffer_limit(io_stream_conf* conf, size_t recv_size, size_t send_size) {
            conf->send_buffer_max_size = send_size;
            conf->recv_buffer_max_size = recv_size;
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
                io_stream_conf* default_conf = io_stream_malloc_configure();
                if (NULL == default_conf) {
                    return EN_ATBUS_ERR_MALLOC;
                }

                int ret = io_stream_init(channel, ev_loop, default_conf);
                io_stream_free_configure(default_conf);
                return ret;
            }

            channel->conf = *conf;
            channel->ev_loop = ev_loop;
            channel->is_loop_owner = false;
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
                    pending_release.push_back(iter->second->fd);
                }

                for (size_t i = 0; i < pending_release.size(); ++i) {
                    io_stream_disconnect_fd(channel, pending_release[i]);
                }
            }

            if (true == channel->is_loop_owner && NULL != channel->ev_loop) {
                uv_loop_close(channel->ev_loop);
                free(channel->ev_loop);
            }

            channel->ev_loop = NULL;
            return EN_ATBUS_ERR_SUCCESS;
        }

        int io_stream_listen(adapter::loop_t* ev_loop, io_stream_channel* channel, const char* addr) {
            if (NULL == ev_loop || NULL == channel) {
                return EN_ATBUS_ERR_PARAMS;
            }

            channel_address_t addr_info;
            if(false == make_address(addr, addr_info)) {
                return EN_ATBUS_ERR_PARAMS;
            }


            return EN_ATBUS_ERR_SUCCESS;
        }

        int io_stream_connect(adapter::loop_t* ev_loop, io_stream_channel* channel, const char* addr);
        int io_stream_disconnect(io_stream_channel* channel, io_stream_connection* connection) {
            if (NULL == channel || NULL == connection) {
                return EN_ATBUS_ERR_PARAMS;
            }

            return io_stream_disconnect_fd(channel, connection->fd);
        }
    }
}
