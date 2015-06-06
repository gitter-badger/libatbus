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
        // memory channel
        struct mem_channel;
        struct mem_conf;

        extern int mem_attach(void* buf, size_t len, mem_channel** channel, const mem_conf* conf);
        extern int mem_init(void* buf, size_t len, mem_channel** channel, const mem_conf* conf);
        extern int mem_send(mem_channel* channel, const void* buf, size_t len);
        extern int mem_recv(mem_channel* channel, void* buf, size_t len, size_t* recv_size);
        extern std::pair<size_t, size_t> mem_last_action();
        extern void mem_show_channel(mem_channel* channel, std::ostream& out, bool need_node_status, size_t need_node_data);

        #ifdef ATBUS_CHANNEL_SHM
        // shared memory channel
        struct shm_channel;
        struct shm_conf;
        extern int shm_attach(key_t shm_key, size_t len, shm_channel** channel, const shm_conf* conf);
        extern int shm_init(key_t shm_key, size_t len, shm_channel** channel, const shm_conf* conf);
        extern int shm_close(key_t shm_key);
        extern int shm_send(shm_channel* channel, const void* buf, size_t len);
        extern int shm_recv(shm_channel* channel, void* buf, size_t len, size_t* recv_size);
        extern std::pair<size_t, size_t> shm_last_action();
        extern void shm_show_channel(shm_channel* channel, std::ostream& out, bool need_node_status, size_t need_node_data);
        #endif

        // tcp socket channel
        //struct net_tcp_channel;
        //struct net_tcp_conf {
        //    const char* address; // ipv4://*.*.*.*, ipv6://*::*:*:*:*, unix://<path>, dns://<domain name>
        //    uint16_t port;
        //    bool is_noblock;
        //    bool is_nodelay;
        //    bool is_keepalive;
        //    int linger_timeout;
        //    size_t buffer_max_size;
        //    size_t buffer_limit_size;

        //    net_tcp_conf() : 
        //        address(NULL), port(0), is_noblock(true), 
        //        is_nodelay(true), is_keepalive(true), linger_timeout(-1),
        //        buffer_max_size(2 * 65536), buffer_limit_size(65536){}
        //};

        //extern int net_tcp_attach(net_tcp_channel* server_channel, net_tcp_channel** channel, const net_tcp_conf* conf);
        //extern int net_tcp_init(net_tcp_channel** channel, const net_tcp_conf* conf);
        //extern int net_tcp_close(net_tcp_conf* channel);
        //extern int net_tcp_send(net_tcp_channel* channel, const void* buf, size_t len);
        //extern int net_tcp_recv(net_tcp_channel* channel, void* buf, size_t len, size_t* recv_size);
        //extern std::pair<size_t, size_t> net_tcp_last_action();
        //extern void net_tcp_show_channel(net_tcp_channel* channel, std::ostream& out, bool need_node_status, size_t need_node_data);
    }
}


#endif /* CHANNEL_EXPORT_H_ */
