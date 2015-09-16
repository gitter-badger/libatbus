/**
 * libatbus_channel_export.h
 *
 *  Created on: 2014年8月13日
 *      Author: owent
 */


#pragma once

#ifndef LIBATBUS_CHANNEL_EXPORT_H_
#define LIBATBUS_CHANNEL_EXPORT_H_

#include <stdint.h>
#include <cstddef>
#include <utility>
#include <ostream>
#include <string>

#include "libatbus_config.h"
#include "libatbus_adapter_libuv.h"

#include "libatbus_channel_types.h"

namespace atbus {
    namespace channel {
        // utility functions
        bool make_address(const char* in, channel_address_t& addr);

        // memory channel
        extern int mem_attach(void* buf, size_t len, mem_channel** channel, const mem_conf* conf);
        extern int mem_init(void* buf, size_t len, mem_channel** channel, const mem_conf* conf);
        extern int mem_send(mem_channel* channel, const void* buf, size_t len);
        extern int mem_recv(mem_channel* channel, void* buf, size_t len, size_t* recv_size);
        extern std::pair<size_t, size_t> mem_last_action();
        extern void mem_show_channel(mem_channel* channel, std::ostream& out, bool need_node_status, size_t need_node_data);

        #ifdef ATBUS_CHANNEL_SHM
        // shared memory channel
        extern int shm_attach(key_t shm_key, size_t len, shm_channel** channel, const shm_conf* conf);
        extern int shm_init(key_t shm_key, size_t len, shm_channel** channel, const shm_conf* conf);
        extern int shm_close(key_t shm_key);
        extern int shm_send(shm_channel* channel, const void* buf, size_t len);
        extern int shm_recv(shm_channel* channel, void* buf, size_t len, size_t* recv_size);
        extern std::pair<size_t, size_t> shm_last_action();
        extern void shm_show_channel(shm_channel* channel, std::ostream& out, bool need_node_status, size_t need_node_data);
        #endif

        // stream channel(tcp,pipe(unix socket) and etc. udp is not a stream)
        typedef void (*io_stream_callback_t)(io_stream_channel* channel, io_stream_connection* connection, int status, void*);

        extern io_stream_conf* io_stream_malloc_configure();
        extern void io_stream_free_configure(io_stream_conf* conf);
        extern void io_stream_set_configure_msg_limit(io_stream_conf* conf, size_t recv_size, size_t send_size);
        extern void io_stream_set_configure_buffer_limit(io_stream_conf* conf, size_t recv_size, size_t send_size);

        extern int io_stream_init(io_stream_channel* channel, adapter::loop_t* ev_loop, const io_stream_conf* conf);
        extern int io_stream_close(io_stream_channel* channel);

        extern int io_stream_listen(io_stream_channel* channel, const char* addr);
        extern int io_stream_connect(io_stream_channel* channel, io_stream_connection* connection);
        extern int io_stream_disconnect(io_stream_channel* channel, io_stream_connection* connection);
        extern int io_stream_disconnect_fd(io_stream_channel* channel, adapter::fd_t fd);
        extern int io_stream_send(io_stream_channel* channel, const void* buf, size_t len);
        extern std::pair<size_t, size_t> io_stream_last_action();
        extern void io_stream_show_channel(io_stream_channel* channel, std::ostream& out, bool need_node_status, size_t need_node_data);
    }
}


#endif /* LIBATBUS_CHANNEL_EXPORT_H_ */
