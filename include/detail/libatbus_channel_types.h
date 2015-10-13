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

#include "std/smart_ptr.h"

#include "libatbus_config.h"
#include "libatbus_adapter_libuv.h"
#include "buffer.h"

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
            int                 port;           // 端口。（仅网络连接有效）
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
        struct io_stream_connection;
        struct io_stream_channel;
        typedef void(*io_stream_callback_t)(
            const io_stream_channel* channel,         // 事件触发的channel
            const io_stream_connection* connection,   // 事件触发的连接
            int status,                         // libuv传入的转态码
            void*,                              // 额外参数(不同事件不同含义)
            size_t s                            // 额外参数长度
        );

        struct io_stream_callback_evt_t {
            enum mem_fn_t {
                EN_FN_ACCEPTED = 0,
                EN_FN_CONNECTED, // 连接或listen成功
                EN_FN_DISCONNECTED,
                EN_FN_RECVED,
                EN_FN_WRITEN,
                MAX
            };
            // 回调函数
            io_stream_callback_t callbacks[MAX];
        } ;

        // 以下不是POD类型，所以不得不暴露出来
        struct io_stream_connection {
            channel_address_t                   addr;
            std::shared_ptr<adapter::stream_t>  handle;             // 流设备
            adapter::fd_t                       fd;                 // 文件描述符
            typedef union {
                uv_shutdown_t shutdown;
            } conn_req_t;
            conn_req_t req;

            typedef enum {
                EN_ST_CREATED = 0,
                EN_ST_CONNECTING,
                EN_ST_CONNECTED,
                EN_ST_CONFIRMED,
                EN_ST_DISCONNECTIED
            } status_t;
            status_t                            status;             // 状态
            io_stream_channel*                  channel;

            // 事件响应
            mutable io_stream_callback_evt_t            evt;

            // 数据区域
            detail::buffer_manager read_buffers;            // 读数据缓冲区(两种Buffer管理方式，一种动态，一种静态)
            /**
             * @brief 由于大多数数据包都比较小
             *        当数据包比较小时和动态直接放在动态int的数据包一起，这样可以减少内存拷贝次数
             */
            typedef struct {
                char buffer[ATBUS_MACRO_DATA_SMALL_SIZE];   // varint数据暂存区和小数据包存储区
                size_t len;                                 // varint数据暂存区和小数据包存储区已使用长度
            } read_head_t;
            read_head_t read_head;
            detail::buffer_manager write_buffers;           // 写数据缓冲区(两种Buffer管理方式，一种动态，一种静态)
        };

        struct io_stream_conf {
            time_t keepalive;

            bool is_noblock;
            bool is_nodelay;
            size_t send_buffer_static;
            size_t recv_buffer_static;
            size_t send_buffer_max_size;
            size_t send_buffer_limit_size;
            size_t recv_buffer_max_size;
            size_t recv_buffer_limit_size;

            time_t confirm_timeout;
            int backlog;     // backlog indicates the number of connections the kernel might queue
        };

        struct io_stream_channel {
            adapter::loop_t* ev_loop;
            bool is_loop_owner;

            io_stream_conf conf;

            typedef ATBUS_ADVANCE_TYPE_MAP(adapter::fd_t, std::shared_ptr<io_stream_connection> ) conn_pool_t;
            conn_pool_t conn_pool;

            // 事件响应
            mutable io_stream_callback_evt_t            evt;

            int error_code; // 记录外部的错误码
            // 统计信息
        };
    }
}


#endif /* LIBATBUS_CHANNEL_EXPORT_H_ */
