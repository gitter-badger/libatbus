/**
 * channel_export.h
 *
 *  Created on: 2014年8月13日
 *      Author: owent
 */


#pragma once

#ifndef CHANNEL_EXPORT_H_
#define CHANNEL_EXPORT_H_

#include <stdint.h>
#include <cstddef>
#include <utility>
#include <ostream>

#ifdef __unix__
#include <sys/ipc.h>
#include <sys/shm.h>
#else
#include <Windows.h>
typedef long key_t;
#endif

namespace atbus {
    namespace channel {

        struct mem_channel;
        struct mem_conf;

        extern int mem_attach(void* buf, size_t len, mem_channel** channel, const mem_conf* conf);
        extern int mem_init(void* buf, size_t len, mem_channel** channel, const mem_conf* conf);
        extern int mem_send(mem_channel* channel, const void* buf, size_t len);
        extern int mem_recv(mem_channel* channel, void* buf, size_t len, size_t* recv_size);
        extern std::pair<size_t, size_t> mem_last_action();
        extern void mem_show_channel(mem_channel* channel, std::ostream& out, bool need_node_status, size_t need_node_data);

        struct shm_channel;
        struct shm_conf;
        extern int shm_attach(key_t shm_key, size_t len, shm_channel** channel, const shm_conf* conf);
        extern int shm_init(key_t shm_key, size_t len, shm_channel** channel, const shm_conf* conf);
        extern int shm_close(key_t shm_key);
        extern int shm_send(shm_channel* channel, const void* buf, size_t len);
        extern int shm_recv(shm_channel* channel, void* buf, size_t len, size_t* recv_size);
        extern std::pair<size_t, size_t> shm_last_action();
        extern void shm_show_channel(shm_channel* channel, std::ostream& out, bool need_node_status, size_t need_node_data);
    }
}


#endif /* CHANNEL_EXPORT_H_ */
