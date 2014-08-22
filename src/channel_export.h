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

        struct shm_channel;
        struct shm_conf;
    }
}


#endif /* CHANNEL_EXPORT_H_ */
