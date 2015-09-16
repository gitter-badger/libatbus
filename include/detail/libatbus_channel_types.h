/**
 * libatbus_channel_types.h
 *
 *  Created on: 2014年8月13日
 *      Author: owent
 */


#pragma once

#ifndef LIBATBUS_CHANNEL_TYPES_H_
#define LIBATBUS_CHANNEL_TYPES_H_

#include <stdint.h>
#include <cstddef>
#include <utility>
#include <ostream>
#include <string>

#include "libatbus_config.h"
#include "libatbus_adapter_libuv.h"

#if defined(__ANDROID__)
#elif defined(__APPLE__)
    #if __dest_os == __mac_os_x
        #include <sys/ipc.h>
        #include <sys/shm.h>
        
        #define ATBUS_CHANNEL_SHM 1
    #endif
#elif defined(__unix__)
    #include <sys/ipc.h>
    #include <sys/shm.h>
    
    #define ATBUS_CHANNEL_SHM 1
#else
    #include <Windows.h>
    typedef long key_t;
    
    #define ATBUS_CHANNEL_SHM 1
#endif

namespace atbus {
    namespace channel {
        // utility functions
        typedef struct {
            std::string         address;        // 主机完整地址，比如：ipv4://127.0.0.1:8123 或 unix:///tmp/atbut.sock
            std::string         scheme;         // 协议名称，比如：ipv4 或 unix
            std::string         host;           // 主机地址，比如：127.0.0.1 或 /tmp/atbut.sock
            uint16_t            port;           // 端口。（仅网络连接有效）
        } channel_address_t;

        // memory channel
        struct mem_channel;
        struct mem_conf;

        #ifdef ATBUS_CHANNEL_SHM
        // shared memory channel
        struct shm_channel;
        struct shm_conf;
        #endif

        // stream channel(tcp,pipe(unix socket) and etc. udp is not a stream)
        // 以下不是POD类型，所以不得不暴露出来
        struct io_stream_connection {
            channel_address_t   addr;
            adapter::stream_t*  fds;            // 流设备
            adapter::fd_t       fd;             // 原始设备描述符/HANDLE
            int                 status;         // 状态

            // 数据区域
            detail::buffer_manager write_buffers;     // 写数据缓冲区(两种Buffer管理方式，一种动态，一种静态)
            detail::buffer_manager read_buffers;      // 读数据缓冲区(两种Buffer管理方式，一种动态，一种静态)
        };

        struct io_stream_conf {
            bool is_noblock;
            bool is_nodelay;
            bool is_keepalive;

            size_t send_buffer_max_size;
            size_t send_buffer_limit_size;
            size_t recv_buffer_max_size;
            size_t recv_buffer_limit_size;
        };

        struct io_stream_channel {
            adapter::loop_t* ev_loop;
            bool is_loop_owner;

            io_stream_conf conf;

            typedef ATBUS_ADVANCE_TYPE_MAP(adapter::fd_t, std::shared_ptr<io_stream_connection> ) conn_pool_t;
            conn_pool_t conn_pool;

            // 回调函数
            io_stream_callback_t on_connected;
            io_stream_callback_t on_disconnected;
            io_stream_callback_t on_recv;

            // 统计信息
        };
        typedef void (*io_stream_callback_t)(io_stream_channel* channel, io_stream_connection* connection, int status, void*);
    }
}


#endif /* LIBATBUS_CHANNEL_EXPORT_H_ */
